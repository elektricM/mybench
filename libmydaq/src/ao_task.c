/* Hardware-timed analog output: finite, continuous with regeneration, continuous streaming;
 * AO0/AO1 (±10 V) and the audio outputs (±2 V) (docs/protocol.md 7.2).
 *
 * Data path: little-endian i16 DAC codes, interleaved in channel order, go to bulk OUT 0x04 (stream
 * 4; another stream when an AI task already owns 4, see md_stream_alloc). The
 * device buffers ~18-20 KB ahead of the output and NAKs while its FIFO is full, which paces the
 * host. A writer thread per running task feeds it with synchronous bulk writes (bounded
 * timeouts, partial writes accounted). Regeneration is host-side (NI re-sends the buffer too).
 * Streaming data comes from a host ring the application appends to; when the ring runs dry for
 * longer than the device can cover, the task reports MYDAQ_ERR_UNDERFLOW.
 *
 * Locks: op_lock serialises start/stop/destroy; lock guards the buffers and state (the writer
 * thread and the event listener take it). Order: op_lock -> dev cmd_lock; op_lock -> lock. */
#include "internal.h"
#include "proto.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

#define AO_PREFILL 16384u          /* bytes written before start (device FIFO holds ~20 KB) */
#define AO_CHUNK 8192u             /* bytes per bulk write */
#define AO_WRITE_TIMEOUT_MS 100u
#define AO_MAX_FINITE_BYTES (256u * 1024u * 1024u)
#define AO_MIN_RING_SCANS 4096u
#define AO_CREDIT_UNLIMITED 0x7fffffffu
#define AO_CREDIT_TOPUP 0x40000000u   /* continuous: add 1 GiB when less than 256 MiB remain [H] */
#define AO_CREDIT_LOW 0x10000000u
#define AO_INFINITE 0xfffffffeu

#define EV_OUT_TIMING 0x0560u
#define EV_OUT_FIFO 0x0561u
#define EV_POWER 0x0c91u

/* out_timing.configure flag bytes (22), finite and continuous (differ at index 11) */
static const char ao_flags_finite[] = "00010000000101000000010000000101000000000000";
static const char ao_flags_cont[] = "00010000000101000000010100000101000000000000";

enum ao_state { AO_IDLE = 0, AO_RUNNING = 1, AO_DONE = 2 };

struct mydaq_ao_task {
    mydaq_device *dev;
    mydaq_ao_config cfg;
    uint32_t divisor;
    double rate;
    size_t scan_bytes;
    int audio;
    uint8_t stream;           /* while hw_active: stream number (bulk OUT endpoint), 0 = none (op_lock) */

    md_mutex op_lock;
    int hw_active;            /* configured on the device; stop sequence pending (op_lock) */
    int events_on;            /* (op_lock) */
    int thread_running;       /* (op_lock) */
    md_thread thread;
    void *out_stream;         /* asynchronous OUT stream (transports with stream_open_out) (op_lock) */
    int pump_on;              /* (op_lock) */
    size_t xfer_size;         /* asynchronous OUT: bytes per transfer */

    md_mutex lock;
    md_cond cond;
    int state;
    int error;                /* sticky until the next start */
    int stop_writer;
    /* finite / regenerate: the whole buffer */
    uint8_t *buf;
    size_t buf_len, buf_cap;
    uint64_t buf_pos;         /* bytes sent (regenerate: modulo buf_len) */
    /* streaming: ring */
    uint8_t *ring;
    size_t ring_cap, ring_head, ring_count;
    /* accounting */
    uint64_t sent_bytes;      /* accepted by the device */
    uint64_t queued_bytes;    /* asynchronous OUT: handed to the transport (>= sent_bytes) */
    uint64_t credit_bytes;
    uint64_t start_us;
    int started;              /* out_timing.start sent */
};

/* ---- conversion ------------------------------------------------------------------------ */

static int ao_code(const mydaq_calibration *cal, mydaq_ao_channel ch, double volts, int16_t *code)
{
    const int audio = ch == MYDAQ_AUDIO_OUT_LEFT || ch == MYDAQ_AUDIO_OUT_RIGHT;
    const double limit = audio ? 2.0 : 10.0;
    const mydaq_cal_pair *p;
    double c;
    if (!(volts >= -limit && volts <= limit))
        return MYDAQ_ERR_INVALID_ARG;
    p = audio ? &cal->ao[ch][2] : &cal->ao[ch][0];   /* audio: ±2 V record; AO: ±10 V record */
    c = nearbyint((double)p->offset + (double)p->gain * volts);
    if (c > 32767.0)
        c = 32767.0;
    if (c < -32768.0)
        c = -32768.0;
    *code = (int16_t)c;
    return MYDAQ_OK;
}

int mydaq_ao_task_volts_to_code(const mydaq_calibration *cal, mydaq_ao_channel channel,
                                double volts, int16_t *code)
{
    if (!cal || !code || (unsigned)channel > 3u)
        return MYDAQ_ERR_INVALID_ARG;
    return ao_code(cal, channel, volts, code);
}

/* ---- ring (streaming) -------------------------------------------------------------------- */

static void ring_put(mydaq_ao_task *t, const uint8_t *p, size_t n)
{
    const size_t tail = (t->ring_head + t->ring_count) % t->ring_cap;
    const size_t first = t->ring_cap - tail < n ? t->ring_cap - tail : n;
    memcpy(t->ring + tail, p, first);
    memcpy(t->ring, p + first, n - first);
    t->ring_count += n;
}

static size_t ring_peek(const mydaq_ao_task *t, uint8_t *out, size_t n)
{
    size_t first;
    if (n > t->ring_count)
        n = t->ring_count;
    first = t->ring_cap - t->ring_head < n ? t->ring_cap - t->ring_head : n;
    memcpy(out, t->ring + t->ring_head, first);
    memcpy(out + first, t->ring, n - first);
    return n;
}

static void ring_drop(mydaq_ao_task *t, size_t n)
{
    t->ring_head = (t->ring_head + n) % t->ring_cap;
    t->ring_count -= n;
}

/* ---- events ------------------------------------------------------------------------------ */

static void on_event(void *user, const md_event *ev)
{
    mydaq_ao_task *t = user;
    int err = MYDAQ_OK, done = 0;
    if (ev->transport_status == MYDAQ_ERR_NO_DEVICE)
        err = MYDAQ_ERR_NO_DEVICE;
    else if (ev->transport_status != MYDAQ_OK)
        return;
    else if (ev->object_class == EV_OUT_TIMING && ev->id == 1)
        done = 1;                                     /* finite generation complete */
    else if (ev->object_class == EV_OUT_FIFO && t->cfg.mode == MYDAQ_CONTINUOUS)
        err = MYDAQ_ERR_UNDERFLOW;                    /* output FIFO ran dry [H] */
    else if (ev->object_class == EV_POWER && ev->status < 0)
        err = MYDAQ_ERR_DEVICE;                       /* power-supply fault: abort output */
    else
        return;

    md_mutex_lock(&t->lock);
    if (t->state == AO_RUNNING) {
        if (done && t->cfg.mode == MYDAQ_FINITE)
            t->state = AO_DONE;
        else if (err && !t->error)
            t->error = err;
        md_cond_broadcast(&t->cond);
    }
    md_mutex_unlock(&t->lock);
    if (err)
        MD_WARN(t->dev->ctx, "AO event %u on %04x: status %ld (%s)", (unsigned)ev->id,
                (unsigned)ev->object_class, (long)ev->status, mydaq_error_name(err));
}

/* ---- writer ------------------------------------------------------------------------------ */

/* Next chunk to send; lock held. Returns its length (0: nothing now). *finished: no more data. */
static size_t next_chunk(mydaq_ao_task *t, uint8_t *out, size_t cap, int *finished)
{
    *finished = 0;
    if (t->cfg.mode == MYDAQ_FINITE) {
        const size_t left = t->buf_len - (size_t)t->buf_pos;
        const size_t n = left < cap ? left : cap;
        *finished = left == 0;
        memcpy(out, t->buf + t->buf_pos, n);
        return n;
    }
    if (t->cfg.regenerate) {
        const size_t off = (size_t)(t->buf_pos % t->buf_len);
        const size_t left = t->buf_len - off;
        const size_t n = left < cap ? left : cap;
        memcpy(out, t->buf + off, n);
        return n;
    }
    return ring_peek(t, out, cap);
}

static void consume(mydaq_ao_task *t, size_t n)
{
    t->sent_bytes += n;
    if (t->cfg.mode == MYDAQ_FINITE || t->cfg.regenerate)
        t->buf_pos += n;
    else
        ring_drop(t, n);
    md_cond_broadcast(&t->cond);
}

/* Streaming ran dry: has the device already output more than we sent? (lock held) */
static int underflowed(mydaq_ao_task *t)
{
    double elapsed, consumed, margin;
    if (!t->started)
        return 0;
    elapsed = (double)(md_now_us() - t->start_us) / 1e6;
    consumed = elapsed * t->rate;
    margin = t->rate * 0.002 + consumed * 2e-4 + 2.0;   /* 2 ms + clock tolerance */
    return consumed > (double)(t->sent_bytes / t->scan_bytes) + margin;
}

static int send_credit(mydaq_ao_task *t, uint32_t bytes)
{
    md_buf b;
    md_buf_init(&b);
    md_put_u32(&b, MD_OBJ_STREAM_ENGINE_BASE | t->stream);
    md_put_u32(&b, bytes);
    return md_send_oneway(t->dev, MD_CH_STREAM, MD_OP_RESET, &b);   /* output credit, one-way */
}

/* Writes one chunk; lock NOT held. Returns 1 to continue, 0 when the writer should stop. */
static int write_once(mydaq_ao_task *t, uint8_t *chunk, size_t cap, uint32_t timeout_ms)
{
    size_t n, actual = 0;
    int finished, rc;

    md_mutex_lock(&t->lock);
    if (t->stop_writer || t->error) {
        md_mutex_unlock(&t->lock);
        return 0;
    }
    n = next_chunk(t, chunk, cap, &finished);
    if (finished) {
        md_mutex_unlock(&t->lock);
        return 0;
    }
    if (n == 0) {   /* streaming, ring empty */
        if (underflowed(t)) {
            t->error = MYDAQ_ERR_UNDERFLOW;
            md_cond_broadcast(&t->cond);
            md_mutex_unlock(&t->lock);
            MD_WARN(t->dev->ctx, "AO underflow: the application did not write data in time");
            return 0;
        }
        md_cond_timedwait(&t->cond, &t->lock, 2);
        md_mutex_unlock(&t->lock);
        return 1;
    }
    if (t->cfg.mode == MYDAQ_CONTINUOUS &&
        t->credit_bytes - t->sent_bytes < AO_CREDIT_LOW + n) {
        t->credit_bytes += AO_CREDIT_TOPUP;
        md_mutex_unlock(&t->lock);
        send_credit(t, AO_CREDIT_TOPUP);
    } else {
        md_mutex_unlock(&t->lock);
    }

    rc = t->dev->ops->write(t->dev->impl, t->stream, chunk, n, &actual, timeout_ms);
    md_mutex_lock(&t->lock);
    if (actual)
        consume(t, actual);
    if (rc != MYDAQ_OK && rc != MYDAQ_ERR_TIMEOUT && !t->error) {
        t->error = rc;
        md_cond_broadcast(&t->cond);
    }
    md_mutex_unlock(&t->lock);
    if (rc == MYDAQ_ERR_NO_DEVICE)
        md_note_error(t->dev, rc);
    return rc == MYDAQ_OK || rc == MYDAQ_ERR_TIMEOUT;
}

static void writer_main(void *arg)
{
    mydaq_ao_task *t = arg;
    uint8_t *chunk = malloc(AO_CHUNK);
    if (!chunk) {
        md_mutex_lock(&t->lock);
        t->error = MYDAQ_ERR_NO_MEM;
        md_mutex_unlock(&t->lock);
        return;
    }
    while (write_once(t, chunk, AO_CHUNK, AO_WRITE_TIMEOUT_MS)) {
    }
    free(chunk);
}

/* ---- asynchronous OUT stream --------------------------------------------------------------
 * With a transport that queues OUT transfers (stream_open_out), up to AO_XFERS transfers wait on
 * the endpoint behind the device's ~20 KB FIFO, so a late thread does not starve the output
 * (with synchronous writes the FIFO alone, ~24 ms at 2 x 200 kS/s, is the margin). Buffers are
 * filled on the event thread when a transfer completes and, for streaming, from write() when
 * new data arrives after the ring ran dry (stream_kick). A monitor thread handles what cannot
 * run in a transfer callback: credit top-ups (a command) and host-side underflow detection. */

#define AO_XFERS 4u
#define AO_XFER_MAX 16384u
#define AO_XFER_TARGET_S 0.025

/* Copies the next bytes to send into out (lock held) and advances the queue position. */
static size_t take_chunk(mydaq_ao_task *t, uint8_t *out, size_t cap)
{
    size_t n = 0;
    cap -= cap % t->scan_bytes;
    if (t->cfg.mode == MYDAQ_FINITE) {
        const size_t left = t->buf_len - (size_t)t->buf_pos;
        n = left < cap ? left : cap;
        memcpy(out, t->buf + t->buf_pos, n);
        t->buf_pos += n;
    } else if (t->cfg.regenerate) {
        while (n < cap) {   /* whole transfers, wrapping around a short buffer */
            const size_t off = (size_t)(t->buf_pos % t->buf_len);
            const size_t left = t->buf_len - off;
            const size_t k = left < cap - n ? left : cap - n;
            memcpy(out + n, t->buf + off, k);
            t->buf_pos += k;
            n += k;
        }
    } else {
        n = ring_peek(t, out, cap);
        ring_drop(t, n);
    }
    t->queued_bytes += n;
    if (n)
        md_cond_broadcast(&t->cond);   /* ring space for a blocked writer */
    return n;
}

static size_t out_fill(void *user, uint8_t *buf, size_t cap, size_t done, int status)
{
    mydaq_ao_task *t = user;
    size_t n = 0;
    int err = MYDAQ_OK;
    md_mutex_lock(&t->lock);
    if (done) {
        t->sent_bytes += done;
        md_cond_broadcast(&t->cond);
    }
    if (status != MYDAQ_OK && !t->error) {
        t->error = err = status;
        md_cond_broadcast(&t->cond);
    }
    if (!t->stop_writer && !t->error && t->state == AO_RUNNING)
        n = take_chunk(t, buf, cap);
    md_mutex_unlock(&t->lock);
    if (err == MYDAQ_ERR_NO_DEVICE)
        md_note_error(t->dev, err);
    return n;
}

static void monitor_main(void *arg)
{
    mydaq_ao_task *t = arg;
    md_mutex_lock(&t->lock);
    while (!t->stop_writer && !t->error) {
        uint32_t topup = 0;
        if (t->cfg.mode == MYDAQ_CONTINUOUS &&
            t->credit_bytes - t->queued_bytes < AO_CREDIT_LOW + 4u * AO_XFER_MAX) {
            t->credit_bytes += AO_CREDIT_TOPUP;
            topup = AO_CREDIT_TOPUP;
        }
        if (!t->cfg.regenerate && t->cfg.mode == MYDAQ_CONTINUOUS && t->ring_count == 0 &&
            t->queued_bytes == t->sent_bytes && underflowed(t)) {
            t->error = MYDAQ_ERR_UNDERFLOW;
            md_cond_broadcast(&t->cond);
            md_mutex_unlock(&t->lock);
            MD_WARN(t->dev->ctx, "AO underflow: the application did not write data in time");
            md_mutex_lock(&t->lock);
            break;
        }
        if (topup) {
            md_mutex_unlock(&t->lock);
            send_credit(t, topup);
            md_mutex_lock(&t->lock);
            continue;
        }
        md_cond_timedwait(&t->cond, &t->lock, 20);
    }
    md_mutex_unlock(&t->lock);
}

static size_t choose_xfer_size(const mydaq_ao_task *t)
{
    double target = ceil(t->rate * (double)t->scan_bytes * AO_XFER_TARGET_S / 512.0) * 512.0;
    if (target < 512.0)
        target = 512.0;
    if (target > AO_XFER_MAX)
        target = AO_XFER_MAX;
    return (size_t)target;   /* a multiple of 512, hence of the scan size (2 or 4 bytes) */
}

/* Opens the OUT stream (which queues the first transfers) and waits until the device holds the
 * pre-fill, as NI writes the first buffer before starting the timing engine. op_lock held. */
static int async_prefill(mydaq_ao_task *t)
{
    mydaq_device *dev = t->dev;
    const uint64_t deadline = md_now_ms() + 1000;
    int rc = md_pump_acquire(dev);
    if (rc != MYDAQ_OK)
        return rc;
    t->pump_on = 1;
    rc = dev->ops->stream_open_out(dev->impl, t->stream, t->xfer_size, AO_XFERS, out_fill, t,
                                   &t->out_stream);
    if (rc != MYDAQ_OK)
        return rc;
    md_mutex_lock(&t->lock);
    for (;;) {
        const uint64_t now = md_now_ms();
        const uint64_t want = t->queued_bytes < AO_PREFILL ? t->queued_bytes : AO_PREFILL;
        if (t->error || t->sent_bytes >= want || now >= deadline)
            break;
        md_cond_timedwait(&t->cond, &t->lock, (uint32_t)(deadline - now));
    }
    rc = t->error;
    md_mutex_unlock(&t->lock);
    return rc;
}

static void async_close(mydaq_ao_task *t)
{
    if (t->out_stream) {
        t->dev->ops->stream_close(t->dev->impl, t->out_stream);
        t->out_stream = NULL;
    }
    if (t->pump_on) {
        md_pump_release(t->dev);
        t->pump_on = 0;
    }
}

/* ---- device sequences -------------------------------------------------------------------- */

static int configure(mydaq_ao_task *t)
{
    mydaq_device *dev = t->dev;
    const int finite = t->cfg.mode == MYDAQ_FINITE;
    const uint32_t n = finite ? (uint32_t)(t->buf_len / t->scan_bytes) : 0;
    md_buf b;
    size_t i;
    int rc = MYDAQ_OK;

    for (i = 0; i < t->cfg.num_channels && rc == MYDAQ_OK; ++i) {
        /* ao_dac.configure (timed): 1, 1, SEL, 0, 0, AUD (audio: 5, 1) */
        md_buf_init(&b);
        md_put_u16(&b, 0x0520);
        md_put_u16(&b, (uint16_t)t->cfg.channels[i]);
        md_put_u8(&b, 1);
        md_put_u8(&b, 1);
        md_put_u8(&b, t->audio ? 5u : 0u);
        md_put_u8(&b, 0);
        md_put_u8(&b, 0);
        md_put_u8(&b, t->audio ? 1u : 0u);
        rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_RESET, &b, MD_TIMEOUT_CMD, 0, NULL);
    }
    if (rc == MYDAQ_OK) {   /* out_timing.configure */
        md_buf_init(&b);
        md_put_u32(&b, 0x05600000u);
        md_put_u32(&b, finite ? 1u : AO_INFINITE);
        md_put_u32(&b, finite ? n : AO_INFINITE);
        md_put_u32(&b, 4);
        md_put_u32(&b, t->divisor);
        md_put_u32(&b, finite ? 0u : AO_INFINITE);
        md_put_hex(&b, finite ? ao_flags_finite : ao_flags_cont);
        rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_COMMIT, &b, MD_TIMEOUT_CMD, 0, NULL);
    }
    if (rc == MYDAQ_OK) {   /* out_fifo.configure(u8[] DAC list, 2, 0) on slot 2 */
        md_buf_init(&b);
        md_put_u32(&b, 0x05610000u);
        md_put_u16(&b, (uint16_t)(2u + t->cfg.num_channels));
        for (i = 0; i < t->cfg.num_channels; ++i)
            md_put_u8(&b, (uint8_t)t->cfg.channels[i]);
        md_put_u8(&b, 2);
        md_put_u8(&b, 0);
        rc = md_cmd(dev, 2, MD_OP_WRITE, &b, MD_TIMEOUT_CMD, 0, NULL);
    }
    if (rc == MYDAQ_OK) {   /* endpoint[stream].configure(0 = OUT, 0x10) */
        md_buf_init(&b);
        md_put_u32(&b, MD_OBJ_STREAM_BASE | t->stream);
        md_put_hex(&b, "0010");
        rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_RESET, &b, MD_TIMEOUT_CMD, 0, NULL);
    }
    if (rc == MYDAQ_OK) {   /* out_timing.m0b(stream, 0) */
        md_buf_init(&b);
        md_put_u32(&b, 0x05600000u);
        md_put_u8(&b, t->stream);
        md_put_u8(&b, 0);
        rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_START, &b, MD_TIMEOUT_CMD, 0, NULL);
    }
    if (rc == MYDAQ_OK)   /* power task status (the device then reports supply faults) */
        rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_WRITE, "0c920000", MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_RESET, "05610000 01", MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_WRITE, MD_OBJ_STREAM_BASE | t->stream, NULL, 0,
                          MD_TIMEOUT_CMD, NULL);
    return rc;
}

static int release_hw(mydaq_ao_task *t)
{
    mydaq_device *dev = t->dev;
    static const struct {
        uint8_t op;
        const char *body;
        unsigned flags;
        int finite_only;
    } seq[] = {
        {0x0d, "05600000", 0, 0},                    /* stop */
        {0x13, "05600000", 0, 0},                    /* status */
        {0x18, "05600000", 0, 1},
        {0x17, "05600000", 0, 0},
        {MD_OP_RESET, "05610000 00", 0, 0},          /* out_fifo disable */
        {MD_OP_COMMIT, "02fc0004", 0, 0},            /* endpoint stop */
        {MD_OP_STOP, "05600000", 0, 0},              /* disarm */
        {MD_OP_START, "02fc0004", MD_CMD_ANY_STATUS, 0},   /* flush: -225229 as for NI */
        {MD_OP_RELEASE, "05600000", 0, 0},           /* unconfigure */
    };
    const int finite = t->cfg.mode == MYDAQ_FINITE;
    int rc, first = MYDAQ_OK;
    size_t i;
    for (i = 0; i < sizeof seq / sizeof seq[0]; ++i) {
        md_buf b;
        if (seq[i].finite_only && !finite)
            continue;
        md_buf_init(&b);
        md_put_hex(&b, seq[i].body);
        if (b.len == 4 && md_get_u32(b.data) == MD_OBJ_STREAM4) {   /* the endpoint of our stream */
            md_buf_init(&b);
            md_put_u32(&b, MD_OBJ_STREAM_BASE | t->stream);
        }
        rc = md_cmd(dev, MD_CH_OBJECT, seq[i].op, &b, MD_TIMEOUT_CMD, seq[i].flags, NULL);
        if (rc == MYDAQ_ERR_NO_DEVICE)
            return rc;
        if (rc != MYDAQ_OK && first == MYDAQ_OK)
            first = rc;
    }
    for (i = 0; i < t->cfg.num_channels; ++i) {   /* ao_dac.update: outputs keep the last sample */
        md_buf b;
        md_buf_init(&b);
        md_put_u16(&b, 0x0520);
        md_put_u16(&b, (uint16_t)t->cfg.channels[i]);
        rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_COMMIT, &b, MD_TIMEOUT_CMD, 0, NULL);
        if (rc != MYDAQ_OK && first == MYDAQ_OK)
            first = rc;
    }
    return first;
}

/* Stops the writer and releases the device and events. op_lock held. */
static int finish(mydaq_ao_task *t)
{
    int rc = MYDAQ_OK;
    md_mutex_lock(&t->lock);
    t->stop_writer = 1;
    md_cond_broadcast(&t->cond);
    md_mutex_unlock(&t->lock);
    if (t->thread_running) {
        md_thread_join(t->thread);
        t->thread_running = 0;
    }
    async_close(t);   /* cancels queued transfers before the endpoint is stopped */
    if (t->hw_active) {
        md_cmd_lock(t->dev);
        rc = release_hw(t);
        md_cmd_unlock(t->dev);
        t->hw_active = 0;
    }
    if (t->stream) {
        md_stream_free(t->dev, t->stream);
        t->stream = 0;
    }
    if (t->events_on) {
        md_events_remove(t->dev, on_event, t);
        t->events_on = 0;
    }
    md_mutex_lock(&t->lock);
    t->state = AO_IDLE;
    t->started = 0;
    md_cond_broadcast(&t->cond);
    md_mutex_unlock(&t->lock);
    return rc;
}

/* ---- public API -------------------------------------------------------------------------- */

static int validate(const mydaq_ao_config *c, int *audio)
{
    size_t i, a = 0;
    if (c->num_channels < 1 || c->num_channels > 2)
        return MYDAQ_ERR_INVALID_ARG;
    for (i = 0; i < c->num_channels; ++i) {
        if ((unsigned)c->channels[i] > 3u)
            return MYDAQ_ERR_INVALID_ARG;
        a += c->channels[i] >= MYDAQ_AUDIO_OUT_LEFT;
    }
    if (c->num_channels == 2 && c->channels[0] == c->channels[1])
        return MYDAQ_ERR_INVALID_ARG;
    if (a && a != c->num_channels)
        return MYDAQ_ERR_INVALID_ARG;   /* audio and AO channels share the DACs */
    if (!(c->sample_rate > 0.0) || c->sample_rate > MYDAQ_AO_MAX_RATE * (1.0 + 1e-9))
        return MYDAQ_ERR_INVALID_ARG;
    if (c->mode != MYDAQ_FINITE && c->mode != MYDAQ_CONTINUOUS)
        return MYDAQ_ERR_INVALID_ARG;
    *audio = a != 0;
    return MYDAQ_OK;
}

int mydaq_ao_task_create(mydaq_device *dev, const mydaq_ao_config *config, mydaq_ao_task **out)
{
    mydaq_ao_task *t;
    int rc, audio = 0;
    if (!out)
        return MYDAQ_ERR_INVALID_ARG;
    *out = NULL;
    if (!dev || !config)
        return MYDAQ_ERR_INVALID_ARG;
    rc = validate(config, &audio);
    if (rc != MYDAQ_OK)
        return rc;
    t = calloc(1, sizeof *t);
    if (!t)
        return MYDAQ_ERR_NO_MEM;
    t->dev = dev;
    t->cfg = *config;
    t->audio = audio;
    t->scan_bytes = 2u * config->num_channels;
    t->divisor = md_rate_divisor(config->sample_rate);
    t->rate = md_divisor_rate(t->divisor);
    t->xfer_size = choose_xfer_size(t);
    if (config->mode == MYDAQ_CONTINUOUS && !config->regenerate) {
        size_t scans = config->buffer_samples ? config->buffer_samples : (size_t)t->rate;
        if (scans < AO_MIN_RING_SCANS)
            scans = AO_MIN_RING_SCANS;
        t->ring_cap = scans * t->scan_bytes;
        t->ring = malloc(t->ring_cap);
        if (!t->ring) {
            free(t);
            return MYDAQ_ERR_NO_MEM;
        }
    }
    md_mutex_init(&t->op_lock);
    md_mutex_init(&t->lock);
    md_cond_init(&t->cond);

    md_mutex_lock(&dev->state_lock);
    if (!dev->connected)
        rc = MYDAQ_ERR_NO_DEVICE;
    else if (dev->ao_task)
        rc = MYDAQ_ERR_BUSY;
    else
        dev->ao_task = t;
    md_mutex_unlock(&dev->state_lock);
    if (rc != MYDAQ_OK) {
        md_cond_destroy(&t->cond);
        md_mutex_destroy(&t->lock);
        md_mutex_destroy(&t->op_lock);
        free(t->ring);
        free(t);
        return rc;
    }
    MD_DBG(dev->ctx, "AO task: %zu channel(s)%s, %.6g S/s (divisor %u), %s", config->num_channels,
           audio ? " (audio)" : "", t->rate, (unsigned)t->divisor,
           config->mode == MYDAQ_FINITE ? "finite"
           : config->regenerate          ? "continuous, regenerated"
                                         : "continuous, streamed");
    *out = t;
    return MYDAQ_OK;
}

void mydaq_ao_task_destroy(mydaq_ao_task *t)
{
    mydaq_device *dev;
    if (!t)
        return;
    dev = t->dev;
    mydaq_ao_task_stop(t);
    md_mutex_lock(&dev->state_lock);
    if (dev->ao_task == t)
        dev->ao_task = NULL;
    md_mutex_unlock(&dev->state_lock);
    md_cond_destroy(&t->cond);
    md_mutex_destroy(&t->lock);
    md_mutex_destroy(&t->op_lock);
    free(t->buf);
    free(t->ring);
    free(t);
}

int mydaq_ao_task_write_raw(mydaq_ao_task *t, const int16_t *codes, size_t samples_per_channel,
                            int timeout_ms)
{
    const size_t n = samples_per_channel * (t ? t->scan_bytes : 0);
    const uint64_t deadline = timeout_ms >= 0 ? md_now_ms() + (uint64_t)timeout_ms : 0;
    uint8_t *bytes;
    size_t i;
    int rc = MYDAQ_OK, kick = 0;

    if (!t || (!codes && samples_per_channel))
        return MYDAQ_ERR_INVALID_ARG;
    if (samples_per_channel == 0)
        return MYDAQ_OK;
    bytes = malloc(n);
    if (!bytes)
        return MYDAQ_ERR_NO_MEM;
    for (i = 0; i < n / 2; ++i) {
        bytes[2 * i] = (uint8_t)((uint16_t)codes[i] & 0xffu);
        bytes[2 * i + 1] = (uint8_t)((uint16_t)codes[i] >> 8);
    }

    md_mutex_lock(&t->lock);
    if (t->cfg.mode == MYDAQ_FINITE || t->cfg.regenerate) {
        /* the buffer, replaced by each write; not while generating */
        if (t->state == AO_RUNNING) {
            rc = MYDAQ_ERR_STATE;
        } else if (n > AO_MAX_FINITE_BYTES) {
            rc = MYDAQ_ERR_INVALID_ARG;
        } else {
            if (n > t->buf_cap) {
                uint8_t *nb = realloc(t->buf, n);
                if (!nb)
                    rc = MYDAQ_ERR_NO_MEM;
                else {
                    t->buf = nb;
                    t->buf_cap = n;
                }
            }
            if (rc == MYDAQ_OK) {
                memcpy(t->buf, bytes, n);
                t->buf_len = n;
            }
        }
    } else if (n > t->ring_cap) {
        rc = MYDAQ_ERR_INVALID_ARG;   /* larger than the host buffer */
    } else {
        /* streaming: append, waiting for space */
        for (;;) {
            if (t->error) {
                rc = t->error;
                break;
            }
            if (t->ring_cap - t->ring_count >= n) {
                ring_put(t, bytes, n);
                md_cond_broadcast(&t->cond);
                kick = t->state == AO_RUNNING;
                break;
            }
            if (timeout_ms < 0) {
                md_cond_wait(&t->cond, &t->lock);
            } else {
                const uint64_t now = md_now_ms();
                if (now >= deadline) {
                    rc = MYDAQ_ERR_TIMEOUT;
                    break;
                }
                md_cond_timedwait(&t->cond, &t->lock, (uint32_t)(deadline - now));
            }
        }
    }
    md_mutex_unlock(&t->lock);
    free(bytes);
    if (kick) {   /* queued OUT transfers may have gone idle while the ring was empty */
        md_mutex_lock(&t->op_lock);
        if (t->out_stream)
            t->dev->ops->stream_kick(t->dev->impl, t->out_stream);
        md_mutex_unlock(&t->op_lock);
    }
    return rc;
}

int mydaq_ao_task_write(mydaq_ao_task *t, const double *volts, size_t samples_per_channel,
                        mydaq_layout layout, int timeout_ms)
{
    int16_t *codes;
    size_t s, c, nch;
    int rc = MYDAQ_OK;
    if (!t || (!volts && samples_per_channel) ||
        (layout != MYDAQ_GROUP_BY_CHANNEL && layout != MYDAQ_GROUP_BY_SCAN))
        return MYDAQ_ERR_INVALID_ARG;
    if (samples_per_channel == 0)
        return MYDAQ_OK;
    nch = t->cfg.num_channels;
    codes = malloc(sizeof *codes * samples_per_channel * nch);
    if (!codes)
        return MYDAQ_ERR_NO_MEM;
    for (s = 0; s < samples_per_channel && rc == MYDAQ_OK; ++s)
        for (c = 0; c < nch && rc == MYDAQ_OK; ++c) {
            const double v = layout == MYDAQ_GROUP_BY_SCAN ? volts[s * nch + c]
                                                           : volts[c * samples_per_channel + s];
            rc = ao_code(&t->dev->cal, t->cfg.channels[c], v, &codes[s * nch + c]);
        }
    if (rc == MYDAQ_OK)
        rc = mydaq_ao_task_write_raw(t, codes, samples_per_channel, timeout_ms);
    free(codes);
    return rc;
}

int mydaq_ao_task_start(mydaq_ao_task *t)
{
    mydaq_device *dev;
    uint8_t *chunk = NULL;
    int rc = MYDAQ_OK, async;
    if (!t)
        return MYDAQ_ERR_INVALID_ARG;
    dev = t->dev;
    async = dev->ops->stream_open_out != NULL && dev->ops->stream_kick != NULL;
    md_mutex_lock(&t->op_lock);
    if (t->hw_active || t->thread_running || t->events_on || t->out_stream || t->pump_on)
        finish(t);
    if (!md_is_connected(dev)) {
        md_mutex_unlock(&t->op_lock);
        return MYDAQ_ERR_NO_DEVICE;
    }
    md_mutex_lock(&t->lock);
    if ((t->cfg.mode == MYDAQ_FINITE || t->cfg.regenerate) && t->buf_len == 0)
        rc = MYDAQ_ERR_STATE;   /* nothing to generate: write the buffer first */
    else if (!t->cfg.regenerate && t->cfg.mode == MYDAQ_CONTINUOUS && t->ring_count == 0)
        rc = MYDAQ_ERR_STATE;   /* streaming: pre-fill before start */
    t->buf_pos = 0;
    t->sent_bytes = t->queued_bytes = 0;
    t->error = MYDAQ_OK;
    t->stop_writer = 0;
    t->started = 0;
    t->credit_bytes = t->cfg.mode == MYDAQ_FINITE ? t->buf_len : AO_CREDIT_UNLIMITED;
    t->state = rc == MYDAQ_OK ? AO_RUNNING : AO_IDLE;
    md_mutex_unlock(&t->lock);
    if (rc != MYDAQ_OK) {
        md_mutex_unlock(&t->op_lock);
        return rc;
    }

    rc = md_stream_alloc(dev, &t->stream);
    if (rc == MYDAQ_OK) {
        md_cmd_lock(dev);
        t->hw_active = 1;
        rc = configure(t);
        md_cmd_unlock(dev);
    }
    if (rc == MYDAQ_OK)
        rc = send_credit(t, (uint32_t)t->credit_bytes);
    if (rc == MYDAQ_OK) {
        rc = md_events_add(dev, on_event, t);
        t->events_on = rc == MYDAQ_OK;
    }
    /* prefill the device FIFO (NI writes the first buffer before starting) */
    if (rc == MYDAQ_OK && async) {
        rc = async_prefill(t);
    } else if (rc == MYDAQ_OK) {
        chunk = malloc(AO_CHUNK);
        if (!chunk)
            rc = MYDAQ_ERR_NO_MEM;
    }
    while (rc == MYDAQ_OK && !async) {
        uint64_t sent;
        md_mutex_lock(&t->lock);
        sent = t->sent_bytes;
        md_mutex_unlock(&t->lock);
        if (sent >= AO_PREFILL)
            break;
        if (!write_once(t, chunk, AO_PREFILL - sent < AO_CHUNK ? (size_t)(AO_PREFILL - sent) : AO_CHUNK,
                        1000)) {
            md_mutex_lock(&t->lock);
            rc = t->error;
            md_mutex_unlock(&t->lock);
            break;   /* finite buffer fully sent, or an error */
        }
        md_mutex_lock(&t->lock);
        if (!t->cfg.regenerate && t->cfg.mode == MYDAQ_CONTINUOUS && t->ring_count == 0) {
            md_mutex_unlock(&t->lock);
            break;   /* streaming: everything written so far is out */
        }
        if (t->sent_bytes == sent) {   /* the device took nothing: its FIFO is full */
            md_mutex_unlock(&t->lock);
            break;
        }
        md_mutex_unlock(&t->lock);
    }
    free(chunk);
    if (rc == MYDAQ_OK) {
        md_cmd_lock(dev);
        rc = md_cmd_hex(dev, MD_CH_OBJECT, 0x0c, "05600000 01", MD_TIMEOUT_CMD, 0, NULL);
        md_cmd_unlock(dev);
    }
    if (rc == MYDAQ_OK) {
        md_mutex_lock(&t->lock);
        t->started = 1;
        t->start_us = md_now_us();
        md_mutex_unlock(&t->lock);
        if (md_thread_start(&t->thread, async ? monitor_main : writer_main, t) != 0)
            rc = MYDAQ_ERR_NO_MEM;
        else
            t->thread_running = 1;
    }
    if (rc != MYDAQ_OK) {
        md_log(dev->ctx, rc == MYDAQ_ERR_NO_DEVICE ? MYDAQ_LOG_DEBUG : MYDAQ_LOG_ERROR,
               "AO start failed: %s", mydaq_strerror(rc));
        finish(t);
    }
    md_mutex_unlock(&t->op_lock);
    return rc;
}

int mydaq_ao_task_stop(mydaq_ao_task *t)
{
    int rc = MYDAQ_OK;
    if (!t)
        return MYDAQ_ERR_INVALID_ARG;
    md_mutex_lock(&t->op_lock);
    if (t->hw_active || t->thread_running || t->events_on || t->out_stream || t->pump_on)
        rc = finish(t);
    md_mutex_lock(&t->lock);
    t->ring_head = t->ring_count = 0;   /* unsent streaming data is discarded */
    md_mutex_unlock(&t->lock);
    md_mutex_unlock(&t->op_lock);
    return rc == MYDAQ_ERR_NO_DEVICE ? MYDAQ_OK : rc;
}

int mydaq_ao_task_wait_done(mydaq_ao_task *t, int timeout_ms)
{
    const uint64_t deadline = timeout_ms >= 0 ? md_now_ms() + (uint64_t)timeout_ms : 0;
    int rc = MYDAQ_OK, done = 0;
    if (!t)
        return MYDAQ_ERR_INVALID_ARG;
    if (t->cfg.mode != MYDAQ_FINITE)
        return MYDAQ_ERR_STATE;
    md_mutex_lock(&t->lock);
    for (;;) {
        if (t->error) {
            rc = t->error;
            break;
        }
        if (t->state == AO_DONE) {
            done = 1;
            break;
        }
        if (t->state != AO_RUNNING) {
            rc = MYDAQ_ERR_STATE;
            break;
        }
        if (timeout_ms < 0) {
            md_cond_wait(&t->cond, &t->lock);
        } else {
            const uint64_t now = md_now_ms();
            if (now >= deadline) {
                rc = MYDAQ_ERR_TIMEOUT;
                break;
            }
            md_cond_timedwait(&t->cond, &t->lock, (uint32_t)(deadline - now));
        }
    }
    md_mutex_unlock(&t->lock);
    if (done) {   /* release the hardware now; the outputs keep the last sample */
        md_mutex_lock(&t->op_lock);
        if (t->hw_active || t->thread_running || t->events_on || t->out_stream || t->pump_on)
            rc = finish(t);
        md_mutex_unlock(&t->op_lock);
        if (rc == MYDAQ_OK) {
            md_mutex_lock(&t->lock);
            t->state = AO_DONE;
            md_mutex_unlock(&t->lock);
        }
    }
    return rc;
}

int mydaq_ao_task_is_running(const mydaq_ao_task *task)
{
    mydaq_ao_task *t = (mydaq_ao_task *)(uintptr_t)task;
    int running;
    if (!t)
        return 0;
    md_mutex_lock(&t->lock);
    running = t->state == AO_RUNNING && !t->error;
    md_mutex_unlock(&t->lock);
    return running;
}

size_t mydaq_ao_task_space_available(const mydaq_ao_task *task)
{
    mydaq_ao_task *t = (mydaq_ao_task *)(uintptr_t)task;
    size_t n = 0;
    if (!t || !t->ring)
        return 0;
    md_mutex_lock(&t->lock);
    n = (t->ring_cap - t->ring_count) / t->scan_bytes;
    md_mutex_unlock(&t->lock);
    return n;
}

int mydaq_ao_task_error(const mydaq_ao_task *task)
{
    mydaq_ao_task *t = (mydaq_ao_task *)(uintptr_t)task;
    int e;
    if (!t)
        return MYDAQ_ERR_INVALID_ARG;
    md_mutex_lock(&t->lock);
    e = t->error;
    md_mutex_unlock(&t->lock);
    return e;
}

double mydaq_ao_task_rate(const mydaq_ao_task *t) { return t ? t->rate : 0.0; }

uint64_t mydaq_ao_task_samples_sent(const mydaq_ao_task *task)
{
    mydaq_ao_task *t = (mydaq_ao_task *)(uintptr_t)task;
    uint64_t n;
    if (!t)
        return 0;
    md_mutex_lock(&t->lock);
    n = t->sent_bytes / t->scan_bytes;
    md_mutex_unlock(&t->lock);
    return n;
}

void md_ao_task_force_destroy(mydaq_ao_task *t) { mydaq_ao_task_destroy(t); }
int md_ao_task_abort(mydaq_ao_task *t) { return mydaq_ao_task_stop(t); }

int md_ao_channel_in_task(mydaq_device *dev, mydaq_ao_channel ch)
{
    int busy = 0;
    size_t i;
    md_mutex_lock(&dev->state_lock);
    if (dev->ao_task)
        for (i = 0; i < dev->ao_task->cfg.num_channels; ++i)
            busy |= dev->ao_task->cfg.channels[i] == ch;
    md_mutex_unlock(&dev->state_lock);
    return busy;
}
