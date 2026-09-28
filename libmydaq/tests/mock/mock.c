/* Mock transport: capture replay and myDAQ emulator (see mydaq_mock.h). */
#include "mydaq_mock.h"

#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STREAMS 4
#define MAX_PENDING 16
#define EP_CMD_OUT 0x03u
#define EP_CMD_IN 0x81u
#define EP_EVENT 0x82u
#define EP_AI 0x84u
#define EP_AO_OUT 0x04u

/* ---- small containers ------------------------------------------------------------------ */

typedef struct bytes {
    uint8_t *p;
    size_t len, cap;
} bytes;

static int bytes_append(bytes *b, const uint8_t *p, size_t n)
{
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        uint8_t *q;
        while (cap < b->len + n)
            cap *= 2;
        q = realloc(b->p, cap);
        if (!q)
            return 0;
        b->p = q;
        b->cap = cap;
    }
    if (n)
        memcpy(b->p + b->len, p, n);
    b->len += n;
    return 1;
}

static void bytes_consume(bytes *b, size_t n)
{
    if (n > b->len)
        n = b->len;
    memmove(b->p, b->p + n, b->len - n);
    b->len -= n;
}

/* FIFO of frames */
typedef struct frame_queue {
    bytes *items;
    size_t count, cap;
} frame_queue;

static int queue_push(frame_queue *q, const uint8_t *p, size_t n)
{
    bytes item = {NULL, 0, 0};
    if (q->count == q->cap) {
        const size_t cap = q->cap ? q->cap * 2 : 16;
        bytes *items = realloc(q->items, cap * sizeof *items);
        if (!items)
            return 0;
        q->items = items;
        q->cap = cap;
    }
    if (!bytes_append(&item, p, n))
        return 0;
    q->items[q->count++] = item;
    return 1;
}

/* Pops the oldest frame into buf (truncated to cap); returns its length or -1 if empty. */
static long queue_pop(frame_queue *q, uint8_t *buf, size_t cap)
{
    size_t n;
    if (q->count == 0)
        return -1;
    n = q->items[0].len < cap ? q->items[0].len : cap;
    memcpy(buf, q->items[0].p, n);
    free(q->items[0].p);
    memmove(q->items, q->items + 1, (q->count - 1) * sizeof *q->items);
    --q->count;
    return (long)n;
}

static void queue_clear(frame_queue *q)
{
    while (q->count)
        free(q->items[--q->count].p);
    free(q->items);
    q->items = NULL;
    q->cap = 0;
}

/* ---- mock state ------------------------------------------------------------------------ */

typedef struct entry {
    char kind;   /* '>' '<' 'e' 'd' '=' */
    uint8_t *data;
    size_t len;
    double *values;
    size_t nvalues;
} entry;

typedef struct mock_stream {
    int used;
    int closing;
    int in_callback;
    uint8_t ep;
    mydaq_stream_cb cb;
    void *user;
    size_t xfer_size;
    size_t pending[MAX_PENDING];   /* requested lengths, oldest first */
    unsigned npending;
    int unplug_reported;
} mock_stream;

struct mydaq_mock {
    md_mutex lock;
    md_cond cond;
    int replay;
    int unplugged;
    unsigned commands;
    char serial[MYDAQ_SERIAL_MAX];
    char location[MYDAQ_LOCATION_MAX];
    uint8_t address;
    char error[512];

    /* replay script */
    entry *entries;
    size_t nentries, pos;

    /* endpoint queues */
    frame_queue responses;   /* 0x81 */
    frame_queue events;      /* 0x82 */
    bytes ai_data;           /* 0x84 */

    mock_stream streams[MAX_STREAMS];

    /* emulator */
    uint8_t calmem[MYDAQ_CALMEM_SIZE];
    mydaq_mock_ai_fn ai_fn;
    void *ai_user;
    int realtime;
    uint16_t scan[2];
    size_t nscan;
    uint32_t timing_mode, divisor, samples;
    int ai_streaming;
    uint64_t ai_t0_us, ai_sent_scans, ai_index;
    unsigned hwverify_busy;     /* hwverify answers -225270 this many more times */
    /* AO (bulk OUT 0x04) */
    bytes ao_expected;          /* replay: the captured OUT stream */
    uint64_t ao_offset;         /* bytes received */
    uint64_t ao_extra;          /* replay: bytes beyond the captured stream */
    unsigned ao_nch;
    uint8_t ao_chans[4];
    uint32_t ao_divisor, ao_n;
    int ao_started, ao_done_sent;
    uint64_t ao_t0_us, ao_written, ao_output;   /* bytes accepted / scans output */
    int16_t ao_last[4];         /* last code output per DAC channel */
    bytes ao_capture;           /* emulator: everything received (first 4 MiB) */
    /* counter / FREQOUT */
    uint32_t ctr_count;
    int ctr_started;
    int freqout_divider, freqout_running;
    unsigned oneway_frames;     /* one-way frames received (stream read-position reports) */
    unsigned oneway_extra;      /* replay: one-way frames not in the script */
    uint32_t last_read_pos;
    int16_t ao_code[4];
    uint8_t dio_dir, dio_latch, dio_inputs;
    int dmm_mode;
    char dmm_packet[12];
};

/* Records the first failure: what happened, the frame sent and the frame expected (if any). */
static void set_error(mydaq_mock *m, const char *what, const uint8_t *sent, size_t sent_len,
                      const uint8_t *want, size_t want_len)
{
    char hs[200] = "", hw[200] = "";
    size_t i;
    if (m->error[0])
        return;
    for (i = 0; i < sent_len && 2 * i + 2 < sizeof hs; ++i)
        snprintf(hs + 2 * i, 3, "%02x", sent[i]);
    for (i = 0; i < want_len && 2 * i + 2 < sizeof hw; ++i)
        snprintf(hw + 2 * i, 3, "%02x", want[i]);
    snprintf(m->error, sizeof m->error, "%s\n  sent     %s\n  expected %s", what, hs,
             want ? hw : "-");
}

static mydaq_mock *mock_alloc(void)
{
    mydaq_mock *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    md_mutex_init(&m->lock);
    md_cond_init(&m->cond);
    snprintf(m->serial, sizeof m->serial, "036CD1C6");
    snprintf(m->location, sizeof m->location, "mock");
    m->address = 1;
    memcpy(m->dmm_packet, "10000;808\r\n", 11);
    m->dmm_mode = -1;
    return m;
}

void mydaq_mock_free(mydaq_mock *m)
{
    size_t i;
    if (!m)
        return;
    for (i = 0; i < m->nentries; ++i) {
        free(m->entries[i].data);
        free(m->entries[i].values);
    }
    free(m->entries);
    queue_clear(&m->responses);
    queue_clear(&m->events);
    free(m->ai_data.p);
    free(m->ao_expected.p);
    free(m->ao_capture.p);
    md_cond_destroy(&m->cond);
    md_mutex_destroy(&m->lock);
    free(m);
}

/* ---- script loading -------------------------------------------------------------------- */

static int parse_hex(const char *s, bytes *out)
{
    int hi = -1;
    for (; *s && *s != '\n' && *s != '\r'; ++s) {
        int v;
        const char c = *s;
        if (c >= '0' && c <= '9')
            v = c - '0';
        else if (c >= 'a' && c <= 'f')
            v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            v = c - 'A' + 10;
        else if (c == ' ')
            continue;
        else
            return 0;
        if (hi < 0) {
            hi = v;
        } else {
            const uint8_t byte = (uint8_t)(hi << 4 | v);
            if (!bytes_append(out, &byte, 1))
                return 0;
            hi = -1;
        }
    }
    return hi < 0;
}

static int parse_values(const char *s, entry *e)
{
    size_t cap = 16;
    e->values = malloc(cap * sizeof *e->values);
    if (!e->values)
        return 0;
    for (;;) {
        char *end;
        const double v = strtod(s, &end);
        if (end == s)
            break;
        if (e->nvalues == cap) {
            double *q = realloc(e->values, 2 * cap * sizeof *q);
            if (!q)
                return 0;
            e->values = q;
            cap *= 2;
        }
        e->values[e->nvalues++] = v;
        s = end;
    }
    return 1;
}

mydaq_mock *mydaq_mock_new_replay(const char *path)
{
    FILE *f = fopen(path, "r");
    mydaq_mock *m;
    size_t cap = 0;
    const size_t line_cap = 4u << 20;   /* "=" lines hold thousands of values */
    char *line = malloc(line_cap);
    if (!f || !line) {
        if (f)
            fclose(f);
        free(line);
        return NULL;
    }
    m = mock_alloc();
    if (!m) {
        fclose(f);
        free(line);
        return NULL;
    }
    m->replay = 1;
    while (fgets(line, (int)line_cap, f)) {
        entry e;
        memset(&e, 0, sizeof e);
        if (line[0] == '#' || line[0] == '@' || line[0] == '\n' || line[0] == '\0')
            continue;
        if (line[1] != ' ')
            goto bad;
        e.kind = line[0];
        if (e.kind == '=') {
            if (!parse_values(line + 2, &e))
                goto bad;
        } else if (e.kind == 'o') {   /* host OUT stream: one byte string, compared as a whole */
            if (!parse_hex(line + 2, &m->ao_expected))
                goto bad;
            continue;
        } else if (e.kind == '>' || e.kind == '<' || e.kind == 'e' || e.kind == 'd') {
            bytes b = {NULL, 0, 0};
            if (!parse_hex(line + 2, &b)) {
                free(b.p);
                goto bad;
            }
            e.data = b.p;
            e.len = b.len;
        } else {
            goto bad;
        }
        if (m->nentries == cap) {
            entry *q;
            cap = cap ? cap * 2 : 64;
            q = realloc(m->entries, cap * sizeof *q);
            if (!q) {
                free(e.data);
                free(e.values);
                goto bad;
            }
            m->entries = q;
        }
        m->entries[m->nentries++] = e;
    }
    fclose(f);
    free(line);
    return m;
bad:
    fclose(f);
    free(line);
    mydaq_mock_free(m);
    return NULL;
}

mydaq_mock *mydaq_mock_new_emulator(const uint8_t *calmem, size_t len)
{
    mydaq_mock *m;
    if (!calmem || len != MYDAQ_CALMEM_SIZE)
        return NULL;
    m = mock_alloc();
    if (m)
        memcpy(m->calmem, calmem, len);
    return m;
}

mydaq_mock *mydaq_mock_new_emulator_file(const char *path)
{
    uint8_t buf[MYDAQ_CALMEM_SIZE];
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f)
        return NULL;
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    return n == sizeof buf ? mydaq_mock_new_emulator(buf, n) : NULL;
}

/* ---- replay ---------------------------------------------------------------------------- */

/* Queues the device-to-host entries that follow the command at `pos`. */
static void replay_queue_following(mydaq_mock *m)
{
    while (m->pos < m->nentries && m->entries[m->pos].kind != '>') {
        const entry *e = &m->entries[m->pos];
        if (e->kind == '<')
            queue_push(&m->responses, e->data, e->len);
        else if (e->kind == 'e')
            queue_push(&m->events, e->data, e->len);
        else if (e->kind == 'd')
            bytes_append(&m->ai_data, e->data, e->len);
        ++m->pos;
    }
}

/* Stream read-position reports (one-way, method 09 on the stream object): their cadence depends
 * on the host's read timing. Other one-way frames (the AO output credit) are compared strictly. */
static int is_oneway(const uint8_t *frame, size_t len)
{
    return len >= 8 && frame[6] == 0 && frame[7] == 0x09;
}

/* One-way frames (stream read-position reports) depend on the host's read timing, so they are
 * matched loosely: a report the library sends is consumed if it is next in the script and
 * otherwise accepted; reports in the script the library did not send are skipped. */
static int replay_write(mydaq_mock *m, const uint8_t *data, size_t len)
{
    const entry *e;
    replay_queue_following(m);
    if (is_oneway(data, len)) {
        if (m->pos < m->nentries && m->entries[m->pos].len == len &&
            memcmp(m->entries[m->pos].data, data, len) == 0) {
            ++m->pos;
            replay_queue_following(m);
        } else {
            ++m->oneway_extra;
        }
        ++m->oneway_frames;
        return MYDAQ_OK;
    }
    while (m->pos < m->nentries && is_oneway(m->entries[m->pos].data, m->entries[m->pos].len)) {
        ++m->pos;   /* a report the library did not send */
        replay_queue_following(m);
    }
    if (m->pos >= m->nentries) {
        set_error(m, "unexpected command after the end of the script", data, len, NULL, 0);
        return MYDAQ_ERR_USB;
    }
    e = &m->entries[m->pos];
    if (e->len != len || memcmp(e->data, data, len) != 0) {
        char what[80];
        snprintf(what, sizeof what, "command #%u differs from the capture:", m->commands);
        set_error(m, what, data, len, e->data, e->len);
        return MYDAQ_ERR_USB;
    }
    ++m->pos;
    replay_queue_following(m);
    return MYDAQ_OK;
}

int mydaq_mock_replay_finished(const mydaq_mock *m)
{
    mydaq_mock *mm = (mydaq_mock *)(uintptr_t)m;
    size_t i;
    int done = 1;
    md_mutex_lock(&mm->lock);
    for (i = mm->pos; i < mm->nentries; ++i)
        if (mm->entries[i].kind == '>' && !is_oneway(mm->entries[i].data, mm->entries[i].len))
            done = 0;
    if (mm->responses.count)
        done = 0;
    md_mutex_unlock(&mm->lock);
    return done;
}

const char *mydaq_mock_error(const mydaq_mock *m) { return m->error[0] ? m->error : NULL; }

size_t mydaq_mock_script_values(const mydaq_mock *m, size_t n, double *values, size_t max)
{
    size_t i, k = 0, c;
    for (i = 0; i < m->nentries; ++i) {
        if (m->entries[i].kind != '=')
            continue;
        if (k++ == n) {
            c = m->entries[i].nvalues < max ? m->entries[i].nvalues : max;
            memcpy(values, m->entries[i].values, c * sizeof *values);
            return c;
        }
    }
    return 0;
}

/* ---- emulator -------------------------------------------------------------------------- */

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Queues a response: kind (+status for kind 6) + payload. */
static void respond(mydaq_mock *m, uint16_t channel, uint32_t kind, int32_t status,
                    const uint8_t *payload, size_t n)
{
    uint8_t f[1024];
    size_t len = 12 + (kind == 6 ? 4 : 0) + n, total;
    total = (len + 3u) & ~(size_t)3u;
    if (total > sizeof f)
        return;
    memset(f, 0, total);
    f[0] = (uint8_t)(channel >> 8);
    f[1] = (uint8_t)channel;
    f[2] = (uint8_t)(total >> 8);
    f[3] = (uint8_t)total;
    f[4] = (uint8_t)((total - 4) >> 8);
    f[5] = (uint8_t)(total - 4);
    f[6] = 1;
    f[7] = 0;
    put32(f + 8, kind);
    if (kind == 6) {
        put32(f + 12, (uint32_t)status);
        memcpy(f + 16, payload, n);
    } else {
        memcpy(f + 12, payload, n);
    }
    queue_push(&m->responses, f, total);
}

static void ack(mydaq_mock *m, uint16_t ch) { respond(m, ch, 2, 0, NULL, 0); }

static void ack_words(mydaq_mock *m, uint16_t ch, const uint32_t *w, size_t n)
{
    uint8_t p[64];
    size_t i;
    for (i = 0; i < n; ++i)
        put32(p + 4 * i, w[i]);
    respond(m, ch, 2, 0, p, 4 * n);
}

/* kind 6 with a length-prefixed blob at payload offset `pre` (pre zero bytes before it) */
static void respond_blob(mydaq_mock *m, uint16_t ch, size_t pre, const uint8_t *data, size_t n)
{
    uint8_t p[1024];
    if (pre + 2 + n > sizeof p)
        return;
    memset(p, 0, pre);
    p[pre] = (uint8_t)((n + 2) >> 8);
    p[pre + 1] = (uint8_t)(n + 2);
    memcpy(p + pre + 2, data, n);
    respond(m, ch, 6, 0, p, pre + 2 + n);
}

static int16_t default_ai(void *user, int channel, int range_code, uint64_t index)
{
    (void)user;
    (void)range_code;
    return (int16_t)((channel == 0 ? -8113 : 3000) + (int)(index % 5) - 2);
}

static int16_t ai_code(mydaq_mock *m, size_t slot, uint64_t index)
{
    const uint16_t e = m->scan[slot];
    return (m->ai_fn ? m->ai_fn : default_ai)(m->ai_user, e & 1, (e >> 9) & 3, index);
}

static void push_event(mydaq_mock *m, uint16_t cls, uint16_t id, int32_t status)
{
    uint8_t ev[64];
    memset(ev, 0, sizeof ev);
    ev[2] = 0;
    ev[3] = 0x40;
    ev[5] = 0x3c;
    ev[6] = (uint8_t)(id >> 8);
    ev[7] = (uint8_t)id;
    ev[8] = (uint8_t)(cls >> 8);
    ev[9] = (uint8_t)cls;
    ev[11] = 2;
    put32(ev + 12, (uint32_t)status);
    queue_push(&m->events, ev, sizeof ev);
}

static void emulate(mydaq_mock *m, const uint8_t *f, size_t len)
{
    const uint16_t ch = be16(f);
    const uint8_t op = f[7];
    const uint8_t *b = f + 8;
    const size_t blen = len - 8;
    const uint32_t obj = blen >= 4 ? be32(b) : 0;
    const uint16_t cls = (uint16_t)(obj >> 16);

    if (f[6] == 0) {   /* one-way: stream read-position report, no reply */
        ++m->oneway_frames;
        if (blen >= 8)
            m->last_read_pos = be32(b + 4);
        return;
    }
    if (ch == 0 && op == 0x00) {
        const uint32_t w[4] = {0x0200a000u, 0x1980c12du, 0x1980c12du, 0x00020000u};
        ack_words(m, ch, w, 4);
    } else if (ch == 0 && op == 0x02) {
        const uint32_t w[3] = {0xe0000007u, 0x00001c00u, 0x04000000u};
        ack_words(m, ch, w, 3);
    } else if (ch == 0 && op == 0x0d) {
        const uint32_t w[1] = {0x02000000u};
        ack_words(m, ch, w, 1);
    } else if (op == 0x13) {
        static const uint32_t caps[] = {0x1e, 0x800, 0x800, 0x400, 0x400, 0x400, 0x200, 0x200,
                                        0x00100004u, 0x00050006u, 0x00070008u, 0x0009000au,
                                        0x00100000u};
        uint8_t p[96];
        size_t i;
        memset(p, 0, sizeof p);
        for (i = 0; i < sizeof caps / sizeof caps[0]; ++i)
            put32(p + 4 * i, caps[i]);
        respond(m, ch, 6, 0, p, sizeof p);
    } else if (op == 0x08 && obj == 0x05060000u && m->hwverify_busy > 0) {
        --m->hwverify_busy;
        respond(m, ch, 6, -225270, NULL, 0);   /* device still starting up */
    } else if (op == 0x08 && obj == 0x05070000u && blen >= 12) {
        const uint32_t off = be32(b + 4), n = be32(b + 8);
        if (off + n <= MYDAQ_CALMEM_SIZE)
            respond_blob(m, ch, 0, m->calmem + off, n);
        else
            respond(m, ch, 6, -1, NULL, 0);
    } else if (cls == 0x0512 && op == 0x0a && blen >= 8) {
        const size_t n = (be16(b + 4) - 2u) / 2u;
        size_t i;
        m->nscan = n > 2 ? 2 : n;
        for (i = 0; i < m->nscan; ++i)
            m->scan[i] = be16(b + 6 + 2 * i);
        ack(m, ch);
    } else if (cls == 0x0512 && (op == 0x0c || op == 0x0d)) {
        const uint32_t w[1] = {(uint32_t)m->nscan};
        ack_words(m, ch, w, 1);
    } else if (obj == 0x05500002u && op == 0x09 && blen >= 24) {
        m->timing_mode = be32(b + 12);
        m->divisor = be32(b + 16);
        m->samples = be32(b + 20);
        ack(m, ch);
    } else if (obj == 0x05500002u && op == 0x0b) {
        if (m->timing_mode == 4) {   /* internal sample clock: finite (N > 0) or continuous */
            m->ai_streaming = 1;
            m->ai_t0_us = md_now_us();
            m->ai_sent_scans = 0;
        }
        ack(m, ch);
    } else if (obj == 0x05500002u && op == 0x0d) {
        const uint32_t w[1] = {m->timing_mode == 4 && !m->ai_streaming ? 0x00010000u : 0};
        uint8_t p[4];
        put32(p, w[0]);
        respond(m, ch, 6, 0, p, 4);
    } else if (obj == 0x05500002u && op == 0x0e) {
        m->ai_streaming = 0;
        ack(m, ch);
    } else if (obj == 0x02fc0004u && op == 0x0a) {
        m->ai_streaming = 0;   /* stop stream */
        ack(m, ch);
    } else if (obj == 0x02fc0004u && op == 0x0b) {
        respond(m, ch, 6, (int32_t)0xfffc9033u, NULL, 0);
    } else if (obj == 0x05130002u && op == 0x0b && blen >= 8 && be32(b + 4) == 1) {
        uint8_t s[4];
        size_t i;
        for (i = 0; i < m->nscan; ++i) {
            const int16_t c = ai_code(m, i, m->ai_index);
            s[2 * i] = (uint8_t)((uint16_t)c >> 8);
            s[2 * i + 1] = (uint8_t)c;
        }
        ++m->ai_index;
        respond_blob(m, ch, 2, s, 2 * m->nscan);
    } else if (cls == 0x0520 && op == 0x09 && blen >= 16) {
        const uint16_t chan = be16(b + 14);
        if (chan < 4)
            m->ao_code[chan] = (int16_t)be16(b + 10);
        ack(m, ch);
    } else if (obj == 0x05300001u && op == 0x0a && blen >= 9) {
        const uint8_t mask = (uint8_t)be32(b + 4);
        if (b[8])
            m->dio_dir |= mask;
        else
            m->dio_dir &= (uint8_t)~mask;
        ack(m, ch);
    } else if (obj == 0x05300001u && op == 0x09 && blen >= 12) {
        const uint8_t mask = (uint8_t)be32(b + 4), value = (uint8_t)be32(b + 8);
        m->dio_latch = (uint8_t)((m->dio_latch & ~mask) | (value & mask));
        ack(m, ch);
    } else if (obj == 0x05300001u && op == 0x08) {
        const uint8_t levels =
            (uint8_t)((m->dio_latch & m->dio_dir) | (m->dio_inputs & (uint8_t)~m->dio_dir));
        const uint8_t p[4] = {0, 0, 0xff, levels};
        respond(m, ch, 2, 0, p, 4);
    } else if (obj == 0x05600000u && op == 0x0a && blen >= 24) {   /* out_timing.configure */
        m->ao_n = be32(b + 4) == 1 ? be32(b + 8) : 0;
        m->ao_divisor = be32(b + 16);
        m->ao_started = 0;
        m->ao_done_sent = 0;
        m->ao_written = m->ao_output = 0;
        ack(m, ch);
    } else if (obj == 0x05610000u && op == 0x09 && blen >= 6) {    /* out_fifo.configure */
        const unsigned n = be16(b + 4) - 2u;
        unsigned i;
        m->ao_nch = n > 4 ? 4 : n;
        for (i = 0; i < m->ao_nch; ++i)
            m->ao_chans[i] = b[6 + i];
        ack(m, ch);
    } else if (obj == 0x05600000u && op == 0x0c) {                  /* out_timing.start */
        m->ao_started = 1;
        m->ao_t0_us = md_now_us();
        ack(m, ch);
    } else if (obj == 0x05600000u && op == 0x0d) {                  /* out_timing stop */
        m->ao_started = 0;
        ack(m, ch);
    } else if (obj == 0x05600000u && op == 0x13) {
        const uint32_t w[3] = {0, 0, 0x01000000u};
        ack_words(m, ch, w, 3);
    } else if (obj == 0x05600000u && op == 0x17) {
        const uint32_t w[1] = {m->ao_n ? 1u : 0u};
        ack_words(m, ch, w, 1);
    } else if (obj == 0x0c410000u && op == 0x1d) {                  /* ctr0 read */
        uint8_t p[8];
        put32(p, 6);
        put32(p + 4, m->ctr_count);
        respond(m, ch, 6, 0, p, 8);
    } else if (obj == 0x0c410000u && op == 0x14 && blen >= 15) {    /* ctr0 start */
        m->ctr_started = 1;
        if (b[14] == 0x0b)   /* finite pulse train: done at once */
            push_event(m, 0x0c41, 9, 0);
        ack(m, ch);
    } else if (obj == 0x0c410000u && op == 0x15) {
        m->ctr_started = 0;
        ack(m, ch);
    } else if (obj == 0x0c400000u && op == 0x08 && blen >= 6) {
        m->freqout_divider = be16(b + 4);
        ack(m, ch);
    } else if (obj == 0x0c400000u && op == 0x09) {
        m->freqout_running = 1;
        ack(m, ch);
    } else if (obj == 0x0c400000u && op == 0x0a) {
        m->freqout_running = 0;
        ack(m, ch);
    } else if (obj == 0x0c900000u && op == 0x09 && blen >= 5) {
        m->dmm_mode = b[4];
        ack(m, ch);
    } else if (obj == 0x0c900000u && op == 0x0a) {
        respond_blob(m, ch, 0, (const uint8_t *)m->dmm_packet, 11);
    } else {
        ack(m, ch);
    }
}

/* Streams generated samples into ai_data (called under the lock). */
static void emulator_generate(mydaq_mock *m)
{
    uint64_t due, total;
    if (!m->ai_streaming || m->nscan == 0)
        return;
    /* finite: N scans; continuous (N = 0): unbounded, kept 64 KiB ahead when not paced */
    total = m->samples ? m->samples : UINT64_MAX;
    due = m->samples ? total : m->ai_sent_scans + (m->ai_data.len < 65536 ? 16384 / m->nscan : 0);
    if (m->realtime && m->divisor) {
        const double elapsed = (double)(md_now_us() - m->ai_t0_us) / 1e6;
        const double n = elapsed * 100e6 / (double)m->divisor;
        due = n < (double)total ? (uint64_t)n : total;
    }
    while (m->ai_sent_scans < due) {
        uint8_t s[4];
        size_t i;
        for (i = 0; i < m->nscan; ++i) {
            const uint16_t c = (uint16_t)ai_code(m, i, m->ai_sent_scans);
            s[2 * i] = (uint8_t)c;   /* little-endian on the stream */
            s[2 * i + 1] = (uint8_t)(c >> 8);
        }
        bytes_append(&m->ai_data, s, 2 * m->nscan);
        ++m->ai_sent_scans;
    }
    if (m->ai_sent_scans >= total) {
        m->ai_streaming = 0;
        push_event(m, 0x0550, 5, 0);   /* finite acquisition done */
    }
}

/* ---- AO stream (bulk OUT 0x04) ------------------------------------------------------------ */

#define AO_FIFO_BYTES 20000u   /* the device accepts ~20 KB ahead of the output */

/* Advances the emulated output; lock held. */
static void ao_update(mydaq_mock *m)
{
    uint64_t due, have;
    const size_t scan = m->ao_nch ? 2u * m->ao_nch : 2u;
    if (!m->ao_started)
        return;
    have = m->ao_written / scan;
    due = have;
    if (m->realtime && m->ao_divisor) {
        const double n = (double)(md_now_us() - m->ao_t0_us) / 1e6 * 100e6 / (double)m->ao_divisor;
        due = n < (double)have ? (uint64_t)n : have;
    }
    if (due > m->ao_output)
        m->ao_output = due;
    if (m->ao_n && !m->ao_done_sent && m->ao_output >= m->ao_n) {
        m->ao_done_sent = 1;
        push_event(m, 0x0560, 1, 0);   /* finite generation complete */
    }
}

static int ao_write(mydaq_mock *m, const uint8_t *data, size_t len, size_t *actual,
                    unsigned timeout_ms)
{
    size_t accept = len;
    md_mutex_lock(&m->lock);
    if (m->unplugged) {
        md_mutex_unlock(&m->lock);
        return MYDAQ_ERR_NO_DEVICE;
    }
    if (m->replay) {
        /* compare with the captured stream; extra data (regeneration, timing) is accepted */
        const uint64_t exp_len = m->ao_expected.len;
        const size_t n = m->ao_offset >= exp_len ? 0 : (size_t)(exp_len - m->ao_offset) < len
                                                          ? (size_t)(exp_len - m->ao_offset)
                                                          : len;
        if (n && memcmp(m->ao_expected.p + m->ao_offset, data, n) != 0) {
            char what[96];
            snprintf(what, sizeof what, "AO stream differs from the capture at byte %llu:",
                     (unsigned long long)m->ao_offset);
            set_error(m, what, data, n < 32 ? n : 32, m->ao_expected.p + m->ao_offset, n < 32 ? n : 32);
            md_mutex_unlock(&m->lock);
            return MYDAQ_ERR_USB;
        }
        m->ao_offset += len;
        if (len > n) {
            m->ao_extra += len - n;
            md_mutex_unlock(&m->lock);
            md_sleep_ms(1);   /* throttle an endless regeneration */
            if (actual)
                *actual = len;
            return MYDAQ_OK;
        }
    } else {
        const size_t scan = m->ao_nch ? 2u * m->ao_nch : 2u;
        uint64_t fifo;
        ao_update(m);
        fifo = m->ao_written - m->ao_output * scan;
        if (!m->ao_started && m->realtime == 0 && fifo >= AO_FIFO_BYTES)
            fifo = AO_FIFO_BYTES;
        accept = fifo >= AO_FIFO_BYTES ? 0 : AO_FIFO_BYTES - (size_t)fifo;
        if (accept > len)
            accept = len;
        if (accept >= scan) {
            const size_t last = (accept / scan - 1) * scan;
            unsigned i;
            for (i = 0; i < m->ao_nch; ++i)
                m->ao_last[m->ao_chans[i] & 3] =
                    (int16_t)(uint16_t)(data[last + 2 * i] | data[last + 2 * i + 1] << 8);
        }
        if (m->ao_capture.len < (4u << 20))
            bytes_append(&m->ao_capture, data, accept);
        m->ao_written += accept;
        m->ao_offset += accept;
        ao_update(m);
    }
    md_mutex_unlock(&m->lock);
    if (actual)
        *actual = accept;
    if (accept < len) {
        md_sleep_ms(timeout_ms < 2 ? timeout_ms : 2);
        return MYDAQ_ERR_TIMEOUT;
    }
    return MYDAQ_OK;
}

/* ---- transport ops --------------------------------------------------------------------- */

static int mock_write(void *impl, uint8_t ep, const uint8_t *data, size_t len, size_t *actual,
                      unsigned timeout_ms)
{
    mydaq_mock *m = impl;
    int rc = MYDAQ_OK;
    if (actual)
        *actual = 0;
    if (ep == EP_AO_OUT)
        return ao_write(m, data, len, actual, timeout_ms);
    md_mutex_lock(&m->lock);
    if (m->unplugged) {
        rc = MYDAQ_ERR_NO_DEVICE;
    } else if (ep != EP_CMD_OUT || len < 12 || len % 4 || be16(data + 2) != len ||
               be16(data + 4) != len - 4 || data[6] > 1) {
        set_error(m, "malformed command frame", data, len < 64 ? len : 64, NULL, 0);
        rc = MYDAQ_ERR_USB;
    } else {
        ++m->commands;
        if (m->replay)
            rc = replay_write(m, data, len);
        else
            emulate(m, data, len);
        if (rc == MYDAQ_OK && actual)
            *actual = len;
    }
    md_mutex_unlock(&m->lock);
    return rc;
}

static int mock_read(void *impl, uint8_t ep, mydaq_endpoint_type type, uint8_t *buf, size_t cap,
                     size_t *actual, unsigned timeout_ms)
{
    mydaq_mock *m = impl;
    int rc = MYDAQ_ERR_TIMEOUT;
    long n;
    (void)type;
    (void)timeout_ms;
    *actual = 0;
    md_mutex_lock(&m->lock);
    if (m->unplugged) {
        rc = MYDAQ_ERR_NO_DEVICE;
    } else if (ep == EP_CMD_IN) {
        n = queue_pop(&m->responses, buf, cap);
        if (n >= 0) {
            *actual = (size_t)n;
            rc = MYDAQ_OK;
        }
    } else if (ep == EP_EVENT) {
        n = queue_pop(&m->events, buf, cap);
        if (n >= 0) {
            *actual = (size_t)n;
            rc = MYDAQ_OK;
        }
    } else if (ep == EP_AI) {
        emulator_generate(m);
        if (m->ai_data.len) {
            *actual = m->ai_data.len < cap ? m->ai_data.len : cap;
            memcpy(buf, m->ai_data.p, *actual);
            bytes_consume(&m->ai_data, *actual);
            rc = MYDAQ_OK;
        }
    }
    md_mutex_unlock(&m->lock);
    return rc;
}

static int mock_stream_open(void *impl, uint8_t ep, mydaq_endpoint_type type, size_t transfer_size,
                            unsigned num_transfers, mydaq_stream_cb cb, void *user, void **out)
{
    mydaq_mock *m = impl;
    mock_stream *s = NULL;
    unsigned i;
    (void)type;
    *out = NULL;
    md_mutex_lock(&m->lock);
    if (m->unplugged) {
        md_mutex_unlock(&m->lock);
        return MYDAQ_ERR_NO_DEVICE;
    }
    for (i = 0; i < MAX_STREAMS; ++i) {
        if (!m->streams[i].used) {
            s = &m->streams[i];
            break;
        }
    }
    if (!s) {
        md_mutex_unlock(&m->lock);
        return MYDAQ_ERR_NO_MEM;
    }
    memset(s, 0, sizeof *s);
    s->used = 1;
    s->ep = ep;
    s->cb = cb;
    s->user = user;
    s->xfer_size = transfer_size;
    md_mutex_unlock(&m->lock);

    /* initial lengths (no lock held: the callback takes the library's locks) */
    for (i = 0; i < num_transfers && i < MAX_PENDING; ++i) {
        size_t n = cb(user, NULL, 0, 0, MYDAQ_OK);
        if (n == 0)
            break;
        md_mutex_lock(&m->lock);
        s->pending[s->npending++] = n < transfer_size ? n : transfer_size;
        md_mutex_unlock(&m->lock);
    }
    *out = s;
    return MYDAQ_OK;
}

static void mock_stream_close(void *impl, void *stream)
{
    mydaq_mock *m = impl;
    mock_stream *s = stream;
    if (!s)
        return;
    md_mutex_lock(&m->lock);
    s->closing = 1;
    while (s->in_callback)
        md_cond_wait(&m->cond, &m->lock);
    s->used = 0;
    s->npending = 0;
    md_mutex_unlock(&m->lock);
}

/* Completes at most one transfer per stream. Returns the number of completions. */
static int deliver(mydaq_mock *m)
{
    int done = 0;
    unsigned i;
    for (i = 0; i < MAX_STREAMS; ++i) {
        mock_stream *s = &m->streams[i];
        uint8_t buf[65536];
        size_t n = 0, requested, next;
        int status = MYDAQ_OK;
        md_mutex_lock(&m->lock);
        if (!s->used || s->closing || s->npending == 0) {
            md_mutex_unlock(&m->lock);
            continue;
        }
        requested = s->pending[0];
        if (m->unplugged) {
            status = MYDAQ_ERR_NO_DEVICE;
        } else if (s->ep == EP_AI) {
            emulator_generate(m);
            n = m->ai_data.len < requested ? m->ai_data.len : requested;
            if (n > sizeof buf)
                n = sizeof buf;
            /* Like USB: a transfer completes when full, or short at the end of the data (the
             * replayed transfers are the device's own). */
            if (n == 0 || (n < requested && !m->replay && m->ai_streaming)) {
                md_mutex_unlock(&m->lock);
                continue;
            }
            memcpy(buf, m->ai_data.p, n);
            bytes_consume(&m->ai_data, n);
        } else if (s->ep == EP_EVENT) {
            const long got = queue_pop(&m->events, buf, requested < sizeof buf ? requested : sizeof buf);
            if (got < 0) {
                md_mutex_unlock(&m->lock);
                continue;
            }
            n = (size_t)got;
        } else {
            md_mutex_unlock(&m->lock);
            continue;
        }
        memmove(s->pending, s->pending + 1, (s->npending - 1) * sizeof s->pending[0]);
        --s->npending;
        s->in_callback = 1;
        md_mutex_unlock(&m->lock);

        next = s->cb(s->user, buf, n, requested, status);

        md_mutex_lock(&m->lock);
        s->in_callback = 0;
        if (next > 0 && !s->closing && s->npending < MAX_PENDING)
            s->pending[s->npending++] = next < s->xfer_size ? next : s->xfer_size;
        md_cond_broadcast(&m->cond);
        md_mutex_unlock(&m->lock);
        ++done;
    }
    return done;
}

static int mock_handle_events(void *impl, unsigned timeout_ms)
{
    mydaq_mock *m = impl;
    md_mutex_lock(&m->lock);
    if (!m->replay)
        ao_update(m);
    md_mutex_unlock(&m->lock);
    if (deliver(m) == 0)
        md_sleep_ms(timeout_ms < 1 ? timeout_ms : 1);
    return MYDAQ_OK;
}

static void mock_close(void *impl) { (void)impl; }

static const mydaq_transport_ops ops = {
    mock_write, mock_read, mock_stream_open, mock_stream_close, mock_handle_events, mock_close,
};

const mydaq_transport_ops *mydaq_mock_ops(void) { return &ops; }

int mydaq_mock_open(mydaq_context *ctx, mydaq_mock *m, unsigned flags, mydaq_device **dev)
{
    mydaq_transport_info info;
    memset(&info, 0, sizeof info);
    md_mutex_lock(&m->lock);
    memcpy(info.serial, m->serial, sizeof info.serial);
    memcpy(info.location, m->location, sizeof info.location);
    info.bus = 1;
    info.address = m->address;
    md_mutex_unlock(&m->lock);
    return mydaq_open_transport(ctx, &ops, m, &info, flags, dev);
}

void mydaq_mock_set_address(mydaq_mock *m, uint8_t address)
{
    md_mutex_lock(&m->lock);
    m->address = address;
    md_mutex_unlock(&m->lock);
}

void mydaq_mock_set_serial(mydaq_mock *m, const char *serial, const char *location)
{
    md_mutex_lock(&m->lock);
    snprintf(m->serial, sizeof m->serial, "%s", serial ? serial : "");
    snprintf(m->location, sizeof m->location, "%s", location ? location : "");
    md_mutex_unlock(&m->lock);
}

/* ---- emulator controls ----------------------------------------------------------------- */

void mydaq_mock_set_ai_source(mydaq_mock *m, mydaq_mock_ai_fn fn, void *user)
{
    md_mutex_lock(&m->lock);
    m->ai_fn = fn;
    m->ai_user = user;
    md_mutex_unlock(&m->lock);
}

void mydaq_mock_set_realtime(mydaq_mock *m, int realtime)
{
    md_mutex_lock(&m->lock);
    m->realtime = realtime;
    md_mutex_unlock(&m->lock);
}

void mydaq_mock_set_dmm_packet(mydaq_mock *m, const char *packet)
{
    md_mutex_lock(&m->lock);
    memcpy(m->dmm_packet, packet, 11);
    md_mutex_unlock(&m->lock);
}

void mydaq_mock_set_dio_inputs(mydaq_mock *m, uint8_t levels)
{
    md_mutex_lock(&m->lock);
    m->dio_inputs = levels;
    md_mutex_unlock(&m->lock);
}

void mydaq_mock_set_hwverify_busy(mydaq_mock *m, unsigned count)
{
    md_mutex_lock(&m->lock);
    m->hwverify_busy = count;
    md_mutex_unlock(&m->lock);
}

void mydaq_mock_unplug(mydaq_mock *m)
{
    md_mutex_lock(&m->lock);
    m->unplugged = 1;
    md_mutex_unlock(&m->lock);
}

void mydaq_mock_inject_event(mydaq_mock *m, uint16_t object_class, uint16_t event_id, int32_t status)
{
    md_mutex_lock(&m->lock);
    push_event(m, object_class, event_id, status);
    md_mutex_unlock(&m->lock);
}

#define GETTER(type, name, expr)                                                                   \
    type name(const mydaq_mock *cm)                                                                \
    {                                                                                              \
        mydaq_mock *m = (mydaq_mock *)(uintptr_t)cm;                                               \
        type v;                                                                                    \
        md_mutex_lock(&m->lock);                                                                   \
        v = (expr);                                                                                \
        md_mutex_unlock(&m->lock);                                                                 \
        return v;                                                                                  \
    }

int16_t mydaq_mock_ao_code(const mydaq_mock *cm, int channel)
{
    mydaq_mock *m = (mydaq_mock *)(uintptr_t)cm;
    int16_t v;
    md_mutex_lock(&m->lock);
    v = channel >= 0 && channel < 4 ? m->ao_code[channel] : 0;
    md_mutex_unlock(&m->lock);
    return v;
}

GETTER(uint8_t, mydaq_mock_dio_direction, m->dio_dir)
GETTER(uint8_t, mydaq_mock_dio_latch, m->dio_latch)
GETTER(int, mydaq_mock_dmm_mode, m->dmm_mode)
GETTER(unsigned, mydaq_mock_command_count, m->commands)
GETTER(int, mydaq_mock_ai_streaming, m->ai_streaming)
GETTER(unsigned, mydaq_mock_oneway_frames, m->oneway_frames)
GETTER(unsigned, mydaq_mock_oneway_extra, m->oneway_extra)
GETTER(uint32_t, mydaq_mock_last_read_pos, m->last_read_pos)
GETTER(uint64_t, mydaq_mock_ao_bytes, m->ao_offset)
GETTER(uint64_t, mydaq_mock_ao_extra, m->ao_extra)
GETTER(uint64_t, mydaq_mock_ao_expected, (uint64_t)m->ao_expected.len)
GETTER(uint64_t, mydaq_mock_ao_output_scans, m->ao_output)
GETTER(int, mydaq_mock_ctr_started, m->ctr_started)
GETTER(int, mydaq_mock_freqout_divider, m->freqout_running ? m->freqout_divider : 0)

int16_t mydaq_mock_ao_last(const mydaq_mock *cm, int dac)
{
    mydaq_mock *m = (mydaq_mock *)(uintptr_t)cm;
    int16_t v;
    md_mutex_lock(&m->lock);
    v = m->ao_last[dac & 3];
    md_mutex_unlock(&m->lock);
    return v;
}

void mydaq_mock_set_counter(mydaq_mock *m, uint32_t count)
{
    md_mutex_lock(&m->lock);
    m->ctr_count = count;
    md_mutex_unlock(&m->lock);
}
