/* Hardware-timed analog input (docs/protocol.md section 6).
 *
 * Data path: the device streams little-endian i16 samples, interleaved by scan, on bulk IN 0x84
 * (stream 4; another stream when an AO task already owns 4, see md_stream_alloc).
 * The transport's stream callback (event thread) appends them to a byte ring; readers consume
 * whole scans. Interrupt IN 0x82 carries event frames (done, error, FIFO overflow), watched while
 * the task runs. Finite and continuous acquisitions use the same commands; continuous ones set
 * the sample count to 0, give the stream unlimited credit and report the host read position with
 * one-way frames (docs/driver-notes.md 3.3).
 *
 * Locks: op_lock serialises start/stop/finish/destroy (it is held while talking to the device and
 * while closing streams); lock guards the ring and the state and is the only lock the stream
 * callbacks take. Order: op_lock -> dev cmd_lock; op_lock -> lock. Streams are never closed while
 * `lock` is held (the callbacks take it). */
#include "internal.h"
#include "proto.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum task_state {
    TASK_IDLE = 0,       /* not started, stopped, or read to the end */
    TASK_RUNNING = 1,    /* acquiring */
    TASK_COMPLETE = 2    /* finite: every sample has arrived (hardware cleanup may be pending) */
};

#define MAX_FINITE_BYTES (256u * 1024u * 1024u)
#define MAX_RING_BYTES (64u * 1024u * 1024u)
#define MIN_RING_SCANS 8192u
#define CREDIT_UNLIMITED 0x7fffffffu
#define TRANSFER_MIN 512u
#define TRANSFER_MAX 32768u      /* NI-DAQmx reads the stream in 32 KiB transfers */
#define TRANSFER_COUNT 8u
#define TRANSFER_TARGET_S 0.02   /* aim for a completion every ~20 ms */

struct mydaq_ai_task {
    mydaq_device *dev;
    mydaq_ai_config cfg;
    uint32_t divisor;
    uint32_t convert_ticks;
    double rate;
    double skew;
    size_t scan_bytes;
    size_t transfer_size;
    uint8_t stream;          /* while hw_active: stream number (bulk IN 0x80 | stream), 0 = none */

    md_mutex op_lock;
    int hw_active;           /* device configured and started; cleanup pending (op_lock) */
    void *data_stream;       /* (op_lock) */
    int events_on;           /* registered with the event dispatcher (op_lock) */

    md_mutex lock;
    md_cond cond;
    uint8_t *ring;
    size_t ring_cap, ring_head, ring_count;   /* bytes; head = oldest byte */
    int state;
    int error;               /* sticky until the next start */
    uint64_t total_bytes;    /* finite: bytes of the whole acquisition; 0 = continuous */
    uint32_t host_buffer;    /* bytes, as announced to the device (in_fifo configure) */
    uint64_t consumed_bytes; /* continuous: handed to the application since start */
    uint64_t reported_bytes; /* continuous: last read position reported to the device */
    uint64_t received_bytes;
    uint64_t requested_bytes;   /* in flight */
    uint64_t read_scans;
    unsigned events;
    int32_t last_event_status;
};

/* ---- ring ------------------------------------------------------------------------------ */

static int ring_push(mydaq_ai_task *t, const uint8_t *data, size_t len)
{
    size_t tail, first;
    if (len > t->ring_cap - t->ring_count)
        return 0;
    tail = (t->ring_head + t->ring_count) % t->ring_cap;
    first = t->ring_cap - tail < len ? t->ring_cap - tail : len;
    memcpy(t->ring + tail, data, first);
    memcpy(t->ring, data + first, len - first);
    t->ring_count += len;
    return 1;
}

static int16_t ring_sample(const mydaq_ai_task *t, size_t byte_index)
{
    const size_t i = (t->ring_head + byte_index) % t->ring_cap;
    const size_t j = (i + 1) % t->ring_cap;
    return (int16_t)(uint16_t)(t->ring[i] | t->ring[j] << 8);
}

static void ring_drop(mydaq_ai_task *t, size_t len)
{
    t->ring_head = (t->ring_head + len) % t->ring_cap;
    t->ring_count -= len;
}

/* ---- stream callbacks (event thread) --------------------------------------------------- */

/* Length of the next transfer. Requests are multiples of the 512-byte packet size (a smaller
 * last request could not hold a full packet: LIBUSB_ERROR_OVERFLOW); a finite acquisition only
 * requests what is still expected, rounded up (the device ends it with a short packet). */
static size_t next_request(mydaq_ai_task *t)
{
    size_t n = t->transfer_size;
    if (t->total_bytes) {
        const uint64_t outstanding = t->received_bytes + t->requested_bytes;
        uint64_t left;
        if (outstanding >= t->total_bytes)
            return 0;
        left = (t->total_bytes - outstanding + TRANSFER_MIN - 1) / TRANSFER_MIN * TRANSFER_MIN;
        if (left < n)
            n = (size_t)left;
    }
    t->requested_bytes += n;
    return n;
}

static size_t on_data(void *user, const uint8_t *data, size_t len, size_t requested, int status)
{
    mydaq_ai_task *t = user;
    size_t next = 0;
    md_mutex_lock(&t->lock);
    if (!data) {
        next = t->state == TASK_RUNNING ? next_request(t) : 0;
        md_mutex_unlock(&t->lock);
        return next;
    }
    t->requested_bytes -= requested <= t->requested_bytes ? requested : t->requested_bytes;
    if (t->state != TASK_RUNNING) {
        md_mutex_unlock(&t->lock);
        return 0;
    }
    if (status != MYDAQ_OK) {
        if (!t->error)
            t->error = status;
    } else if (!ring_push(t, data, len)) {
        if (!t->error)
            t->error = MYDAQ_ERR_OVERFLOW;
    } else {
        t->received_bytes += len;
        if (t->total_bytes && t->received_bytes >= t->total_bytes)
            t->state = TASK_COMPLETE;
        else if (!t->error)
            next = next_request(t);
    }
    md_cond_broadcast(&t->cond);
    md_mutex_unlock(&t->lock);
    return next;
}

/* Events (docs/protocol.md 6.5), dispatched by events.c. */
#define EV_TIMING_CLASS 0x0550u
#define EV_FIFO_CLASS 0x0551u

static void on_event(void *user, const md_event *ev)
{
    mydaq_ai_task *t = user;
    int err = MYDAQ_OK, applied = 0;

    if (ev->transport_status == MYDAQ_ERR_NO_DEVICE)
        err = MYDAQ_ERR_NO_DEVICE;
    else if (ev->transport_status != MYDAQ_OK)
        return;
    else if (ev->object_class == EV_FIFO_CLASS && ev->id == 2)
        err = MYDAQ_ERR_OVERFLOW;   /* device FIFO overflow: the stream was not read in time */
    else if (ev->object_class == EV_TIMING_CLASS && ev->id == 7)
        err = MYDAQ_ERR_OVERFLOW;   /* stopped because of an error: follows the overflow event */
    else if ((ev->object_class == EV_FIFO_CLASS && ev->id == 3) ||
             (ev->object_class == EV_TIMING_CLASS && (ev->id == 3 || ev->id == 8)))
        err = MYDAQ_ERR_DEVICE;     /* FIFO / timing engine error */
    else if (ev->object_class == EV_TIMING_CLASS && ev->id == 5 && t->total_bytes == 0)
        err = MYDAQ_ERR_DEVICE;     /* "stopped": an error only if we did not stop it */
    else if (ev->object_class != EV_TIMING_CLASS && ev->object_class != EV_FIFO_CLASS)
        return;                     /* another task's event */

    md_mutex_lock(&t->lock);
    ++t->events;
    t->last_event_status = ev->status;
    if (err && !t->error && t->state == TASK_RUNNING) {
        t->error = err;
        applied = 1;
        md_cond_broadcast(&t->cond);
    }
    md_mutex_unlock(&t->lock);
    if (applied)
        MD_WARN(t->dev->ctx, "AI event %u on %04x: status %ld (%s)", (unsigned)ev->id,
                (unsigned)ev->object_class, (long)ev->status, mydaq_error_name(err));
}

/* ---- device sequences ------------------------------------------------------------------ */

static int configure_and_arm(mydaq_ai_task *t)
{
    mydaq_device *dev = t->dev;
    const int finite = t->total_bytes != 0;
    /* in_timing.configure: mode 3, convert ticks, clock source 4 (internal), divisor, samples
     * per channel (0 = continuous), 0, then u16 1 and 25 flag bytes, which differ between finite
     * and continuous in bytes 16, 20 and 22 (captures ai0*-finite-*, ai01-r10-cont-10k-n5000) */
    const uint32_t timing[13] = {3, t->convert_ticks, 4, t->divisor,
                                 finite ? (uint32_t)t->cfg.samples_per_channel : 0u, 0,
                                 0x00010000u, 1, 0, 1,
                                 finite ? 0x100u : 0u, finite ? 0u : 0x100u, finite ? 0x01000000u : 0u};
    /* in_fifo.configure: host buffer bytes, stream number */
    const uint32_t datapath[6] = {0, 0, t->host_buffer, 0, 0, (uint32_t)t->stream << 24};
    /* stream credit: exact bytes (finite) or unlimited */
    const uint32_t engine[1] = {finite ? (uint32_t)t->total_bytes : CREDIT_UNLIMITED};
    const uint32_t endpoint_in[1] = {0x01100000u};   /* endpoint.configure(1 = IN, 0x10) */
    const uint32_t endpoint = MD_OBJ_STREAM_BASE | t->stream;
    int rc;

    rc = md_ai_send_scan_list(dev, t->cfg.channels, t->cfg.num_channels);
    if (rc == MYDAQ_OK)
        rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_RESET, endpoint, endpoint_in, 1, MD_TIMEOUT_CMD, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_WRITE, "05010001 00000006 3212f413 00000006 00000000",
                        MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_WRITE, MD_OBJ_AI_TIMING, timing, 13,
                          MD_TIMEOUT_CMD, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_RESET, MD_OBJ_AI_DATAPATH, datapath, 6,
                          MD_TIMEOUT_CMD, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_WRITE, endpoint, NULL, 0, MD_TIMEOUT_CMD, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_words(dev, MD_CH_STREAM, MD_OP_RESET, MD_OBJ_STREAM_ENGINE_BASE | t->stream,
                          engine, 1, MD_TIMEOUT_CMD, NULL);
    return rc;
}

static int start_timing(mydaq_ai_task *t)
{
    mydaq_device *dev = t->dev;
    int rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_COMMIT, "05500002", MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_START, "05500002 00000000", MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_QUERY, "05500002", MD_TIMEOUT_CMD, 0, NULL);
    return rc;
}

/* The release sequence NI sends after a finite acquisition; also used to abort one. */
static int release_hw(mydaq_ai_task *t)
{
    mydaq_device *dev = t->dev;
    int rc, first = MYDAQ_OK;
    size_t i;
    static const struct {
        uint8_t op;
        uint32_t object;   /* 0 = our stream's endpoint object */
        unsigned flags;
    } seq[] = {
        {MD_OP_COMMIT, 0, 0},                    /* stop the stream */
        {MD_OP_STATUS, MD_OBJ_AI_TIMING, 0},
        {MD_OP_STOP, MD_OBJ_AI_TIMING, 0},
        {MD_OP_STATUS, MD_OBJ_AI_FRONTEND, 0},
        {MD_OP_RELEASE, MD_OBJ_AI_TIMING, 0},
        {MD_OP_START, 0, MD_CMD_ANY_STATUS},     /* flush: answers 0xfffc9033, also for NI */
    };
    for (i = 0; i < sizeof seq / sizeof seq[0]; ++i) {
        const uint32_t object = seq[i].object ? seq[i].object : MD_OBJ_STREAM_BASE | t->stream;
        md_buf b;
        md_buf_init(&b);
        md_put_u32(&b, object);
        rc = md_cmd(dev, MD_CH_OBJECT, seq[i].op, &b, MD_TIMEOUT_CMD, seq[i].flags, NULL);
        if (rc == MYDAQ_ERR_NO_DEVICE)
            return rc;
        if (rc != MYDAQ_OK && first == MYDAQ_OK)
            first = rc;   /* keep going: release as much as possible */
    }
    return first;
}

static void close_streams(mydaq_ai_task *t)
{
    mydaq_device *dev = t->dev;
    if (t->data_stream) {
        dev->ops->stream_close(dev->impl, t->data_stream);
        md_pump_release(dev);
    }
    if (t->events_on)
        md_events_remove(dev, on_event, t);
    t->data_stream = NULL;
    t->events_on = 0;
}

/* Ends the acquisition on the device and host side. op_lock held. */
static int finish_hw(mydaq_ai_task *t)
{
    int rc = MYDAQ_OK, was_complete;
    md_mutex_lock(&t->lock);
    was_complete = t->state == TASK_COMPLETE;
    if (t->state == TASK_RUNNING)
        t->state = TASK_IDLE;   /* callbacks retire their transfers from now on */
    md_cond_broadcast(&t->cond);
    md_mutex_unlock(&t->lock);

    if (t->hw_active) {
        md_cmd_lock(t->dev);
        rc = release_hw(t);
        md_cmd_unlock(t->dev);
        t->hw_active = 0;
    }
    close_streams(t);
    if (t->stream) {
        if (!was_complete && md_is_connected(t->dev)) {
            /* Aborted early: samples the device had already queued must not leak into the next
             * acquisition. */
            md_drain(t->dev, (uint8_t)(0x80u | t->stream), MYDAQ_EP_BULK, 64);
        }
        md_stream_free(t->dev, t->stream);
        t->stream = 0;
    }
    return rc;
}

/* stream[4].read_pos (class 0x0002 method 9, one-way on slot 7): continuous acquisitions tell
 * the device how far the host buffer is free, 0x7fffffff - R with R = consumed bytes rounded
 * down to whole host buffers, plus one buffer. NI sends it once at start and as the
 * application consumes buffers; the device keeps streaming regardless ([H], driver-notes 3.3). */
static void report_read_position(mydaq_ai_task *t, uint32_t value)
{
    md_buf b;
    md_buf_init(&b);
    md_put_u32(&b, MD_OBJ_STREAM_ENGINE_BASE | t->stream);
    md_put_u32(&b, value);
    md_send_oneway(t->dev, MD_CH_STREAM, MD_OP_WRITE, &b);
}

/* Next report due for `consumed` bytes (0 if none); called with the task lock held. */
static uint32_t due_report(mydaq_ai_task *t)
{
    const uint64_t r = t->consumed_bytes / t->host_buffer * t->host_buffer + t->host_buffer;
    if (t->total_bytes != 0 || r <= t->reported_bytes || r >= CREDIT_UNLIMITED)
        return 0;
    t->reported_bytes = r;
    return CREDIT_UNLIMITED - (uint32_t)r;
}

/* ---- public API ------------------------------------------------------------------------ */

static size_t choose_transfer_size(double rate, size_t scan_bytes)
{
    const double per_second = rate * (double)scan_bytes;
    double target = ceil(per_second * TRANSFER_TARGET_S / TRANSFER_MIN) * TRANSFER_MIN;
    if (target < TRANSFER_MIN)
        target = TRANSFER_MIN;
    if (target > TRANSFER_MAX)
        target = TRANSFER_MAX;
    return (size_t)target;
}

int mydaq_ai_task_create(mydaq_device *dev, const mydaq_ai_config *config, mydaq_ai_task **out)
{
    mydaq_ai_task *t;
    uint64_t total;
    int rc;

    if (!out)
        return MYDAQ_ERR_INVALID_ARG;
    *out = NULL;
    if (!dev || !config)
        return MYDAQ_ERR_INVALID_ARG;
    rc = md_ai_validate_channels(config->channels, config->num_channels, 1);
    if (rc != MYDAQ_OK)
        return rc;
    if (!(config->sample_rate > 0.0) || config->sample_rate > MYDAQ_AI_MAX_RATE * (1.0 + 1e-9))
        return MYDAQ_ERR_INVALID_ARG;
    if (config->mode != MYDAQ_FINITE && config->mode != MYDAQ_CONTINUOUS)
        return MYDAQ_ERR_INVALID_ARG;
    if (config->mode == MYDAQ_FINITE) {
        if (config->samples_per_channel == 0 || config->samples_per_channel > 0xffffffffu)
            return MYDAQ_ERR_INVALID_ARG;
        total = config->samples_per_channel * 2u * config->num_channels;
        if (total > MAX_FINITE_BYTES)
            return MYDAQ_ERR_INVALID_ARG;
    } else {
        total = 0;
    }

    t = calloc(1, sizeof *t);
    if (!t)
        return MYDAQ_ERR_NO_MEM;
    t->dev = dev;
    t->cfg = *config;
    t->scan_bytes = 2u * config->num_channels;
    t->divisor = md_rate_divisor(config->sample_rate);
    t->rate = md_divisor_rate(t->divisor);
    t->convert_ticks = md_ai_convert_ticks(config->num_channels, t->divisor);
    t->skew = config->num_channels > 1 ? (double)t->convert_ticks / MYDAQ_TIMEBASE_HZ : 0.0;
    t->total_bytes = total;
    t->transfer_size = choose_transfer_size(t->rate, t->scan_bytes);
    if (total) {
        t->ring_cap = (size_t)total;   /* a finite acquisition is kept whole */
    } else {
        /* Continuous. The buffer announced to the device (and the unit of the read-position
         * reports) follows NI-DAQmx: buffer_samples if given, else the sample hint, else 1 s of
         * data (at least MIN_RING_SCANS). The host ring is that size when buffer_samples is
         * explicit; otherwise it gets a margin of at least 2 s of data so a reader that asks for
         * a whole buffer at a time does not overflow while it is being woken up. */
        uint64_t scans = config->buffer_samples ? config->buffer_samples : config->samples_per_channel;
        uint64_t ring;
        if (scans == 0) {
            /* the device sends the stream in bursts of this size: ~100 ms of data */
            scans = (uint64_t)(t->rate * 0.1);
            if (scans < 16)
                scans = 16;
        }
        if (scans * t->scan_bytes > MAX_RING_BYTES)
            scans = MAX_RING_BYTES / t->scan_bytes;
        ring = scans;
        if (!config->buffer_samples) {
            if ((uint64_t)(2.0 * t->rate) > ring)
                ring = (uint64_t)(2.0 * t->rate);
            if (ring < MIN_RING_SCANS)
                ring = MIN_RING_SCANS;
            if (ring * t->scan_bytes > MAX_RING_BYTES)
                ring = MAX_RING_BYTES / t->scan_bytes;
        }
        t->host_buffer = (uint32_t)(scans * t->scan_bytes);
        t->ring_cap = (size_t)ring * t->scan_bytes;
        /* one transfer per burst (each burst ends with a short packet), at most 32 KiB */
        t->transfer_size = (t->host_buffer + TRANSFER_MIN - 1) / TRANSFER_MIN * TRANSFER_MIN;
        if (t->transfer_size > TRANSFER_MAX)
            t->transfer_size = TRANSFER_MAX;
        /* a transfer must never be able to fill more than half the host ring */
        if (t->transfer_size > t->ring_cap / 2)
            t->transfer_size = t->ring_cap / 2 / TRANSFER_MIN * TRANSFER_MIN;
        if (t->transfer_size < TRANSFER_MIN)
            t->transfer_size = TRANSFER_MIN;
    }
    if (total)
        t->host_buffer = (uint32_t)t->ring_cap;
    t->ring = malloc(t->ring_cap);
    if (!t->ring || md_mutex_init(&t->op_lock) || md_mutex_init(&t->lock) || md_cond_init(&t->cond)) {
        free(t->ring);
        free(t);
        return MYDAQ_ERR_NO_MEM;
    }

    md_mutex_lock(&dev->state_lock);
    if (!dev->connected)
        rc = MYDAQ_ERR_NO_DEVICE;
    else if (dev->ai_task)
        rc = MYDAQ_ERR_BUSY;
    else
        dev->ai_task = t;
    md_mutex_unlock(&dev->state_lock);
    if (rc != MYDAQ_OK) {
        md_cond_destroy(&t->cond);
        md_mutex_destroy(&t->lock);
        md_mutex_destroy(&t->op_lock);
        free(t->ring);
        free(t);
        return rc;
    }
    MD_DBG(dev->ctx, "AI task: %zu channel(s), %.6g S/s (divisor %u, convert %u ticks), %llu samples",
           config->num_channels, t->rate, (unsigned)t->divisor, (unsigned)t->convert_ticks,
           (unsigned long long)config->samples_per_channel);
    *out = t;
    return MYDAQ_OK;
}

int mydaq_ai_task_start(mydaq_ai_task *t)
{
    mydaq_device *dev;
    int rc;
    if (!t)
        return MYDAQ_ERR_INVALID_ARG;
    dev = t->dev;
    md_mutex_lock(&t->op_lock);
    if (t->hw_active || t->data_stream || t->events_on)
        finish_hw(t);
    if (!md_is_connected(dev)) {
        md_mutex_unlock(&t->op_lock);
        return MYDAQ_ERR_NO_DEVICE;
    }

    md_mutex_lock(&t->lock);
    t->ring_head = t->ring_count = 0;
    t->received_bytes = t->requested_bytes = t->read_scans = 0;
    t->consumed_bytes = t->reported_bytes = 0;
    t->error = MYDAQ_OK;
    t->events = 0;
    t->last_event_status = 0;
    t->state = TASK_RUNNING;
    md_mutex_unlock(&t->lock);

    rc = md_stream_alloc(dev, &t->stream);
    if (rc == MYDAQ_OK) {
        md_cmd_lock(dev);
        t->hw_active = 1;
        rc = configure_and_arm(t);
        md_cmd_unlock(dev);
    }
    if (rc == MYDAQ_OK) {
        rc = md_events_add(dev, on_event, t);
        t->events_on = rc == MYDAQ_OK;
    }
    if (rc == MYDAQ_OK)
        rc = md_pump_acquire(dev);
    if (rc == MYDAQ_OK) {
        rc = dev->ops->stream_open(dev->impl, (uint8_t)(0x80u | t->stream), MYDAQ_EP_BULK, t->transfer_size,
                                   TRANSFER_COUNT, on_data, t, &t->data_stream);
        if (!t->data_stream)
            md_pump_release(dev);
    }
    if (rc == MYDAQ_OK) {
        md_cmd_lock(dev);
        rc = start_timing(t);
        md_cmd_unlock(dev);
    }
    if (rc == MYDAQ_OK && t->total_bytes == 0) {
        uint32_t value;
        md_mutex_lock(&t->lock);
        value = due_report(t);
        md_mutex_unlock(&t->lock);
        if (value)
            report_read_position(t, value);
    }
    if (rc != MYDAQ_OK) {
        md_log(dev->ctx, rc == MYDAQ_ERR_NO_DEVICE ? MYDAQ_LOG_DEBUG : MYDAQ_LOG_ERROR,
               "AI start failed: %s", mydaq_strerror(rc));
        finish_hw(t);
        md_mutex_lock(&t->lock);
        t->state = TASK_IDLE;
        md_mutex_unlock(&t->lock);
    }
    md_mutex_unlock(&t->op_lock);
    return rc;
}

int mydaq_ai_task_stop(mydaq_ai_task *t)
{
    int rc = MYDAQ_OK;
    if (!t)
        return MYDAQ_ERR_INVALID_ARG;
    md_mutex_lock(&t->op_lock);
    if (t->hw_active || t->data_stream || t->events_on)
        rc = finish_hw(t);
    md_mutex_lock(&t->lock);
    t->state = TASK_IDLE;
    t->ring_head = t->ring_count = 0;   /* unread samples are discarded */
    t->read_scans = 0;
    md_cond_broadcast(&t->cond);
    md_mutex_unlock(&t->lock);
    md_mutex_unlock(&t->op_lock);
    return rc == MYDAQ_ERR_NO_DEVICE ? MYDAQ_OK : rc;   /* stopping an unplugged device is fine */
}

int md_ai_task_abort(mydaq_ai_task *t) { return mydaq_ai_task_stop(t); }

void mydaq_ai_task_destroy(mydaq_ai_task *t)
{
    mydaq_device *dev;
    if (!t)
        return;
    dev = t->dev;
    mydaq_ai_task_stop(t);
    md_mutex_lock(&dev->state_lock);
    if (dev->ai_task == t)
        dev->ai_task = NULL;
    md_mutex_unlock(&dev->state_lock);
    md_cond_destroy(&t->cond);
    md_mutex_destroy(&t->lock);
    md_mutex_destroy(&t->op_lock);
    free(t->ring);
    free(t);
}

void md_ai_task_force_destroy(mydaq_ai_task *t) { mydaq_ai_task_destroy(t); }

int mydaq_ai_task_is_running(const mydaq_ai_task *task)
{
    mydaq_ai_task *t = (mydaq_ai_task *)(uintptr_t)task;
    int running;
    if (!t)
        return 0;
    md_mutex_lock(&t->lock);
    running = t->state == TASK_RUNNING && !t->error;
    md_mutex_unlock(&t->lock);
    return running;
}

size_t mydaq_ai_task_available(const mydaq_ai_task *task)
{
    mydaq_ai_task *t = (mydaq_ai_task *)(uintptr_t)task;
    size_t n;
    if (!t)
        return 0;
    md_mutex_lock(&t->lock);
    n = t->ring_count / t->scan_bytes;
    md_mutex_unlock(&t->lock);
    return n;
}

uint64_t mydaq_ai_task_remaining(const mydaq_ai_task *task)
{
    mydaq_ai_task *t = (mydaq_ai_task *)(uintptr_t)task;
    uint64_t n;
    if (!t || t->cfg.mode != MYDAQ_FINITE)
        return 0;
    md_mutex_lock(&t->lock);
    n = t->cfg.samples_per_channel - t->read_scans;
    md_mutex_unlock(&t->lock);
    return n;
}

double mydaq_ai_task_rate(const mydaq_ai_task *t) { return t ? t->rate : 0.0; }
double mydaq_ai_task_channel_skew(const mydaq_ai_task *t) { return t ? t->skew : 0.0; }

/* Waits for n scans and copies them out; returns with the task lock released. */
static int read_scans(mydaq_ai_task *t, size_t n, int timeout_ms, int16_t *codes, double *volts,
                      mydaq_layout layout)
{
    const size_t nch = t->cfg.num_channels;
    const uint64_t deadline = timeout_ms >= 0 ? md_now_ms() + (uint64_t)timeout_ms : 0;
    int rc = MYDAQ_OK, finished = 0;
    uint32_t report_value = 0;
    size_t s, c;

    md_mutex_lock(&t->lock);
    if (t->cfg.mode == MYDAQ_FINITE && t->read_scans + n > t->cfg.samples_per_channel) {
        md_mutex_unlock(&t->lock);
        return MYDAQ_ERR_INVALID_ARG;
    }
    for (;;) {
        if (t->error) {
            rc = t->error;
            break;
        }
        if (t->ring_count / t->scan_bytes >= n)
            break;
        if (t->state != TASK_RUNNING) {
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
    if (rc == MYDAQ_OK) {
        for (s = 0; s < n; ++s) {
            for (c = 0; c < nch; ++c) {
                const int16_t code = ring_sample(t, (s * nch + c) * 2u);
                if (codes)
                    codes[s * nch + c] = code;
                if (volts) {
                    const size_t at = layout == MYDAQ_GROUP_BY_SCAN ? s * nch + c : c * n + s;
                    volts[at] = mydaq_ai_code_to_volts(&t->dev->cal, t->cfg.channels[c].range, code);
                }
            }
        }
        ring_drop(t, n * t->scan_bytes);
        t->read_scans += n;
        finished = t->cfg.mode == MYDAQ_FINITE && t->state == TASK_COMPLETE &&
                   t->read_scans == t->cfg.samples_per_channel;
        if (t->total_bytes == 0) {
            t->consumed_bytes += n * t->scan_bytes;
            report_value = due_report(t);
        }
    }
    md_mutex_unlock(&t->lock);

    if (report_value)
        report_read_position(t, report_value);

    if (finished) {
        md_mutex_lock(&t->op_lock);
        if (t->hw_active || t->data_stream || t->events_on)
            finish_hw(t);
        md_mutex_lock(&t->lock);
        t->state = TASK_IDLE;
        md_mutex_unlock(&t->lock);
        md_mutex_unlock(&t->op_lock);
    }
    if (rc == MYDAQ_ERR_NO_DEVICE)
        md_note_error(t->dev, rc);
    return rc;
}

int mydaq_ai_task_read(mydaq_ai_task *t, double *volts, size_t samples_per_channel,
                       mydaq_layout layout, int timeout_ms)
{
    if (!t || (!volts && samples_per_channel > 0) ||
        (layout != MYDAQ_GROUP_BY_CHANNEL && layout != MYDAQ_GROUP_BY_SCAN))
        return MYDAQ_ERR_INVALID_ARG;
    if (samples_per_channel == 0)
        return MYDAQ_OK;
    return read_scans(t, samples_per_channel, timeout_ms, NULL, volts, layout);
}

int mydaq_ai_task_read_raw(mydaq_ai_task *t, int16_t *codes, size_t samples_per_channel,
                           int timeout_ms)
{
    if (!t || (!codes && samples_per_channel > 0))
        return MYDAQ_ERR_INVALID_ARG;
    if (samples_per_channel == 0)
        return MYDAQ_OK;
    return read_scans(t, samples_per_channel, timeout_ms, codes, NULL, MYDAQ_GROUP_BY_SCAN);
}
