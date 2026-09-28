/* Counter ctr0 (edge counting on DIO0, pulse train on DIO3) and FREQOUT (DIO4)
 * (docs/protocol.md section 10; PFI n = DIO n).
 *
 * The ctr0 object (0x0c41) is configured through a fixed skeleton of calls whose arguments were
 * decoded differentially from NI's captures; each task kind below replays its captured frames,
 * with the task parameters (pulse low/high times in 100 MHz ticks, pulse count) substituted.
 * Only the task kinds that were captured are offered; the others return NOT_SUPPORTED. */
#include "internal.h"
#include "proto.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define OBJ_CTR0 0x0c410000u
#define OBJ_FREQOUT 0x0c400000u
#define CTR_CLASS 0x0c41u
#define CTR_TIMEBASE_HZ 100e6
#define CTR_MIN_TICKS 2u
#define FREQOUT_TIMEBASE_HZ 100e3   /* the only FREQOUT timebase captured so far */
#define FREQOUT_MAX_DIVIDER 16u

#define DIO_CTR_SOURCE 0x01u   /* DIO0 = PFI0: count-edges input */
#define DIO_CTR_GATE 0x02u     /* DIO1 = PFI1: frequency/period input (gate) */
#define DIO_CTR_OUTPUT 0x08u   /* DIO3 = PFI3: pulse-train output */
#define DIO_FREQOUT 0x10u      /* DIO4 = PFI4 */

struct mydaq_ctr_task {
    mydaq_device *dev;
    mydaq_ctr_config cfg;
    uint32_t low, high;       /* pulse train, ticks */
    uint8_t dio_lines;

    md_mutex op_lock;
    int hw_active;            /* (op_lock) */
    int events_on;            /* (op_lock) */

    md_mutex lock;
    md_cond cond;
    int running;
    int done;                 /* finite pulse train complete */
    int error;
};

/* ---- helpers ----------------------------------------------------------------------------- */

/* route.connect(u32[] {id, pair}, u32[] {value, pair_value}) */
static int route2(mydaq_device *dev, uint32_t id, uint32_t pair, uint32_t value, uint32_t pair_value)
{
    const uint32_t w[6] = {0x0a, id, pair, 0x0a, value, pair_value};
    return md_cmd_words(dev, MD_CH_OBJECT, MD_OP_WRITE, MD_OBJ_PROPERTIES, w, 6, MD_TIMEOUT_CMD, NULL);
}

static int ctr_hex(mydaq_device *dev, uint8_t op, const char *args)
{
    md_buf b;
    md_buf_init(&b);
    md_put_u32(&b, OBJ_CTR0);
    md_put_hex(&b, args);
    return md_cmd(dev, MD_CH_OBJECT, op, &b, MD_TIMEOUT_CMD, 0, NULL);
}

/* 0c41.09: 9 x u32, 2 x u16 (0), 4 x u8 */
static int ctr_configure(mydaq_device *dev, const uint32_t u[9], const char *bytes4)
{
    md_buf b;
    size_t i;
    md_buf_init(&b);
    md_put_u32(&b, OBJ_CTR0);
    for (i = 0; i < 9; ++i)
        md_put_u32(&b, u[i]);
    md_put_u16(&b, 0);
    md_put_u16(&b, 0);
    md_put_hex(&b, bytes4);
    return md_cmd(dev, MD_CH_OBJECT, MD_OP_WRITE, &b, MD_TIMEOUT_CMD, 0, NULL);
}

/* 0c41.14 start: u32, u32, u16 0, 4 x u8 */
static int ctr_start(mydaq_device *dev, uint32_t a, uint32_t b4, const char *bytes4)
{
    md_buf b;
    md_buf_init(&b);
    md_put_u32(&b, OBJ_CTR0);
    md_put_u32(&b, a);
    md_put_u32(&b, b4);
    md_put_u16(&b, 0);
    md_put_hex(&b, bytes4);
    return md_cmd(dev, MD_CH_OBJECT, 0x14, &b, MD_TIMEOUT_CMD, 0, NULL);
}

int mydaq_ctr_coerce_pulse(double frequency, double duty, uint32_t *low_ticks, uint32_t *high_ticks,
                           double *actual_frequency, double *actual_duty)
{
    double period, high, low;
    if (!(frequency > 0.0) || !(duty > 0.0 && duty < 1.0))
        return MYDAQ_ERR_INVALID_ARG;
    period = floor(CTR_TIMEBASE_HZ / frequency + 0.5);
    if (period < 2.0 * CTR_MIN_TICKS)
        period = 2.0 * CTR_MIN_TICKS;
    if (period > 2.0 * 4294967295.0)
        return MYDAQ_ERR_INVALID_ARG;
    high = floor(period * duty + 0.5);
    if (high < CTR_MIN_TICKS)
        high = CTR_MIN_TICKS;
    if (high > period - CTR_MIN_TICKS)
        high = period - CTR_MIN_TICKS;
    low = period - high;
    if (low > 4294967295.0 || high > 4294967295.0)
        return MYDAQ_ERR_INVALID_ARG;
    if (low_ticks)
        *low_ticks = (uint32_t)low;
    if (high_ticks)
        *high_ticks = (uint32_t)high;
    if (actual_frequency)
        *actual_frequency = CTR_TIMEBASE_HZ / period;
    if (actual_duty)
        *actual_duty = high / period;
    return MYDAQ_OK;
}

/* ---- events ------------------------------------------------------------------------------ */

static void on_event(void *user, const md_event *ev)
{
    mydaq_ctr_task *t = user;
    md_mutex_lock(&t->lock);
    if (ev->transport_status == MYDAQ_ERR_NO_DEVICE) {
        if (!t->error)
            t->error = MYDAQ_ERR_NO_DEVICE;
    } else if (ev->transport_status == MYDAQ_OK && ev->object_class == CTR_CLASS && ev->id == 9) {
        t->done = 1;   /* finite pulse train complete */
        t->running = 0;
    }
    md_cond_broadcast(&t->cond);
    md_mutex_unlock(&t->lock);
}

/* ---- device sequences -------------------------------------------------------------------- */

/* Frequency / period, one-counter method: counts the 100 MHz timebase between rising edges of
 * the gate (DIO1). Frames from ctr-cifreq-pfi1-timeout; period differs only in the application
 * code (2 instead of 3; driver-notes 7.3, M). */
static int program_period(mydaq_ctr_task *t)
{
    mydaq_device *dev = t->dev;
    const int period = t->cfg.kind == MYDAQ_CTR_PERIOD;
    const uint32_t u[9] = {1, 0, 1, 0, 0, 0, 0, 0, 0};
    int rc = route2(dev, 0xb21bf404u, 0xf937a9a3u, 0, 0);                 /* PFI1: input */
    if (rc == MYDAQ_OK)
        rc = route2(dev, 0x097d2c54u, 0x11f9be17u, 0x02, 0);              /* gate = PFI1 */
    if (rc == MYDAQ_OK)
        rc = route2(dev, 0x587c6efeu, 0x37cf14ddu, 0x1e, 0);              /* source = 100 MHz */
    if (rc == MYDAQ_OK)
        rc = route2(dev, 0xc7c20613u, 0x88fbba44u, 0x2f, 0);
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_RESET, "");
    if (rc == MYDAQ_OK)
        rc = ctr_configure(dev, u, period ? "00020100" : "00030100");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_COMMIT, "00000000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_START, "0000000000000001000000000000000300000002000000000001000000000000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_QUERY, "00010000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_STATUS, "00020000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_STOP, "");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_RELEASE, "000000000000000300000000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x10, "00000000000100000001000000000000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x11, "000000010000000000000000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x12, "000000000000000000000000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x13, "");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x12, "000000000000000000000000");
    if (rc == MYDAQ_OK)
        rc = ctr_start(dev, 1, 0, period ? "02000000" : "03000000");
    return rc;
}

static int program(mydaq_ctr_task *t)
{
    mydaq_device *dev = t->dev;
    const mydaq_ctr_config *c = &t->cfg;
    const int edges = c->kind == MYDAQ_CTR_COUNT_EDGES;
    const int finite = !edges && c->mode == MYDAQ_FINITE;
    const uint32_t two_n = finite ? (uint32_t)(2u * c->pulses) : 0u;
    int rc = MYDAQ_OK;

    if (edges) {
        rc = route2(dev, 0xc51cc492u, 0x8e309935u, 0, 0);                   /* count edges */
        if (rc == MYDAQ_OK)
            rc = route2(dev, 0x587c6efeu, 0x37cf14ddu, 0x01, 0);            /* source = PFI0 */
    } else {
        rc = route2(dev, 0x587c6efeu, 0x37cf14ddu, 0x1e, 0);                /* 100 MHz timebase */
    }
    if (rc == MYDAQ_OK)
        rc = route2(dev, 0xc7c20613u, 0x88fbba44u, 0x2f, 0);                /* gate/aux default */
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_RESET, "");
    if (rc == MYDAQ_OK) {
        if (edges) {
            const uint32_t u[9] = {0};
            rc = ctr_configure(dev, u, "00010001");
        } else if (finite) {
            /* U6..U8 were all 50000 at 1 kHz / 50 %: taken as (initial low, low, high) [H] */
            const uint32_t u[9] = {2, 2, 0, two_n, 0, 0, t->low, t->low, t->high};
            rc = ctr_configure(dev, u, "000b0001");
        } else {
            const uint32_t u[9] = {2, 0, t->low, t->high, 0, 0, 0, 0, 0};
            rc = ctr_configure(dev, u, "01090001");
        }
    }
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_COMMIT, "00000000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_START,
                     edges    ? "0000000000000000000000000000000200000000000000000001000000000000"
                     : finite ? "0001000000010000000100010002000000000000000000000000000000000002"
                              : "0001000000010000000100000000000000000000000000000000000000000000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_QUERY, edges ? "00010000" : "00000000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_STATUS, "00020000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_STOP, "");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, MD_OP_RELEASE, finite ? "000000000000000300000007" : "000000000000000300000000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x10, edges || finite ? "00000000000000000000000000000000"
                                                : "00010000000000010001000000000000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x11, "000000010000000000000000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x12, finite ? "000000000000000001000000" : "000000000000000000000000");
    if (rc == MYDAQ_OK && !edges)
        rc = route2(dev, 0x5c159528u, 0x1739c88fu, 0x0d, 1);                /* output -> PFI3 */
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x13, "");                                           /* arm */
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x12, finite ? "000000000000000001000000" : "000000000000000000000000");
    if (rc == MYDAQ_OK) {
        if (edges)
            rc = ctr_start(dev, 0, 0, "01010000");
        else if (finite)
            rc = ctr_start(dev, 0, two_n, "0b010000");
        else
            rc = ctr_start(dev, t->low, t->high, "09010001");
    }
    return rc;
}

static int release_hw(mydaq_ctr_task *t)
{
    int rc = ctr_hex(t->dev, 0x15, "");
    if (rc != MYDAQ_ERR_NO_DEVICE) {
        const int rc2 = ctr_hex(t->dev, 0x16, "");
        const int rc3 = ctr_hex(t->dev, 0x17, "");
        if (rc == MYDAQ_OK)
            rc = rc2 != MYDAQ_OK ? rc2 : rc3;
    }
    return rc;
}

static int finish(mydaq_ctr_task *t)
{
    int rc = MYDAQ_OK;
    if (t->hw_active) {
        md_cmd_lock(t->dev);
        rc = release_hw(t);
        md_cmd_unlock(t->dev);
        t->hw_active = 0;
    }
    if (t->events_on) {
        md_events_remove(t->dev, on_event, t);
        t->events_on = 0;
    }
    md_mutex_lock(&t->lock);
    t->running = 0;
    md_cond_broadcast(&t->cond);
    md_mutex_unlock(&t->lock);
    return rc;
}

/* ---- public API: ctr0 -------------------------------------------------------------------- */

int mydaq_ctr_task_create(mydaq_device *dev, const mydaq_ctr_config *config, mydaq_ctr_task **out)
{
    mydaq_ctr_task *t;
    int rc = MYDAQ_OK;
    uint32_t low = 0, high = 0;
    uint8_t lines;
    if (!out)
        return MYDAQ_ERR_INVALID_ARG;
    *out = NULL;
    if (!dev || !config)
        return MYDAQ_ERR_INVALID_ARG;
    if (config->kind == MYDAQ_CTR_COUNT_EDGES) {
        lines = DIO_CTR_SOURCE;
    } else if (config->kind == MYDAQ_CTR_FREQUENCY || config->kind == MYDAQ_CTR_PERIOD) {
        lines = DIO_CTR_GATE;
    } else if (config->kind == MYDAQ_CTR_PULSE_TRAIN) {
        rc = mydaq_ctr_coerce_pulse(config->frequency, config->duty_cycle, &low, &high, NULL, NULL);
        if (rc != MYDAQ_OK)
            return rc;
        if (config->mode == MYDAQ_FINITE && (config->pulses == 0 || config->pulses > 0x7fffffffu))
            return MYDAQ_ERR_INVALID_ARG;
        if (config->mode != MYDAQ_FINITE && config->mode != MYDAQ_CONTINUOUS)
            return MYDAQ_ERR_INVALID_ARG;
        lines = DIO_CTR_OUTPUT;
    } else {
        return MYDAQ_ERR_NOT_SUPPORTED;   /* other measurements: not decoded yet */
    }
    t = calloc(1, sizeof *t);
    if (!t)
        return MYDAQ_ERR_NO_MEM;
    t->dev = dev;
    t->cfg = *config;
    t->low = low;
    t->high = high;
    t->dio_lines = lines;
    md_mutex_init(&t->op_lock);
    md_mutex_init(&t->lock);
    md_cond_init(&t->cond);

    md_mutex_lock(&dev->state_lock);
    if (!dev->connected)
        rc = MYDAQ_ERR_NO_DEVICE;
    else if (dev->ctr_task || (dev->dio_reserved & lines) || (dev->dio_direction & lines))
        rc = MYDAQ_ERR_BUSY;   /* ctr0 in use, or the line is taken / driven by DIO */
    else {
        dev->ctr_task = t;
        dev->dio_reserved |= lines;
    }
    md_mutex_unlock(&dev->state_lock);
    if (rc != MYDAQ_OK) {
        md_cond_destroy(&t->cond);
        md_mutex_destroy(&t->lock);
        md_mutex_destroy(&t->op_lock);
        free(t);
        return rc;
    }
    *out = t;
    return MYDAQ_OK;
}

int mydaq_ctr_task_start(mydaq_ctr_task *t)
{
    int rc;
    if (!t)
        return MYDAQ_ERR_INVALID_ARG;
    md_mutex_lock(&t->op_lock);
    if (t->hw_active || t->events_on)
        finish(t);
    md_mutex_lock(&t->lock);
    t->done = 0;
    t->error = MYDAQ_OK;
    t->running = 1;
    md_mutex_unlock(&t->lock);
    rc = md_events_add(t->dev, on_event, t);
    t->events_on = rc == MYDAQ_OK;
    if (rc == MYDAQ_OK) {
        md_cmd_lock(t->dev);
        t->hw_active = 1;
        rc = t->cfg.kind == MYDAQ_CTR_FREQUENCY || t->cfg.kind == MYDAQ_CTR_PERIOD ? program_period(t)
                                                                                    : program(t);
        md_cmd_unlock(t->dev);
    }
    if (rc != MYDAQ_OK)
        finish(t);
    md_mutex_unlock(&t->op_lock);
    return rc;
}

int mydaq_ctr_task_stop(mydaq_ctr_task *t)
{
    int rc = MYDAQ_OK;
    if (!t)
        return MYDAQ_ERR_INVALID_ARG;
    md_mutex_lock(&t->op_lock);
    if (t->hw_active || t->events_on)
        rc = finish(t);
    md_mutex_unlock(&t->op_lock);
    return rc == MYDAQ_ERR_NO_DEVICE ? MYDAQ_OK : rc;
}

void mydaq_ctr_task_destroy(mydaq_ctr_task *t)
{
    mydaq_device *dev;
    if (!t)
        return;
    dev = t->dev;
    mydaq_ctr_task_stop(t);
    md_mutex_lock(&dev->state_lock);
    if (dev->ctr_task == t) {
        dev->ctr_task = NULL;
        dev->dio_reserved &= (uint8_t)~t->dio_lines;
    }
    md_mutex_unlock(&dev->state_lock);
    md_cond_destroy(&t->cond);
    md_mutex_destroy(&t->lock);
    md_mutex_destroy(&t->op_lock);
    free(t);
}

int mydaq_ctr_task_is_running(const mydaq_ctr_task *task)
{
    mydaq_ctr_task *t = (mydaq_ctr_task *)(uintptr_t)task;
    int r;
    if (!t)
        return 0;
    md_mutex_lock(&t->lock);
    r = t->running && !t->error;
    md_mutex_unlock(&t->lock);
    return r;
}

int mydaq_ctr_task_read_count(mydaq_ctr_task *t, uint32_t *count, int timeout_ms)
{
    uint32_t args[5];
    const uint8_t *p;
    md_resp r;
    md_buf b;
    int rc;
    if (!t || !count || timeout_ms < 0)
        return MYDAQ_ERR_INVALID_ARG;
    if (t->cfg.kind == MYDAQ_CTR_PULSE_TRAIN)
        return MYDAQ_ERR_STATE;
    if (!mydaq_ctr_task_is_running(t))
        return MYDAQ_ERR_STATE;
    /* 0c41.1d(u32 1, u32 1, u32 timeout ms, u32 1, u16 1, u8 1, u8 0) -> u32[] {count};
     * NI passes 10015 ms (its 10 s default + 15) */
    args[0] = 1;
    args[1] = 1;
    args[2] = (uint32_t)timeout_ms;
    args[3] = 1;
    md_buf_init(&b);
    md_put_u32(&b, OBJ_CTR0);
    md_put_u32(&b, args[0]);
    md_put_u32(&b, args[1]);
    md_put_u32(&b, args[2]);
    md_put_u32(&b, args[3]);
    md_put_u16(&b, 1);
    md_put_u8(&b, t->cfg.kind == MYDAQ_CTR_COUNT_EDGES ? 1u : t->cfg.kind == MYDAQ_CTR_PERIOD ? 2u : 3u);
    md_put_u8(&b, 0);   /* application code as the last argument */
    md_cmd_lock(t->dev);
    rc = md_cmd(t->dev, MD_CH_OBJECT, 0x1d, &b, (unsigned)timeout_ms + MD_TIMEOUT_CMD,
                MD_CMD_ANY_STATUS, &r);
    md_cmd_unlock(t->dev);
    if (rc != MYDAQ_OK)
        return rc;
    if (r.kind == MD_KIND_DATA && r.status == -200474)
        return MYDAQ_ERR_TIMEOUT;   /* no value within the timeout (expected without a signal) */
    if (r.kind == MD_KIND_DATA && r.status != 0) {
        MD_ERR(t->dev->ctx, "counter read: device status %ld", (long)r.status);
        return MYDAQ_ERR_DEVICE;
    }
    if (r.payload_len < 8 || md_get_u32(r.payload) != 6)
        return MYDAQ_ERR_PROTOCOL;
    p = r.payload + 4;
    *count = md_get_u32(p);
    return MYDAQ_OK;
}

int mydaq_ctr_task_wait_done(mydaq_ctr_task *t, int timeout_ms)
{
    const uint64_t deadline = timeout_ms >= 0 ? md_now_ms() + (uint64_t)timeout_ms : 0;
    int rc = MYDAQ_OK;
    if (!t)
        return MYDAQ_ERR_INVALID_ARG;
    if (t->cfg.kind != MYDAQ_CTR_PULSE_TRAIN || t->cfg.mode != MYDAQ_FINITE)
        return MYDAQ_ERR_STATE;
    md_mutex_lock(&t->lock);
    for (;;) {
        if (t->error) {
            rc = t->error;
            break;
        }
        if (t->done)
            break;
        if (!t->running) {
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
    if (rc == MYDAQ_OK) {   /* release ctr0 like NI does after the done event */
        md_mutex_lock(&t->op_lock);
        if (t->hw_active || t->events_on)
            rc = finish(t);
        md_mutex_unlock(&t->op_lock);
    }
    return rc;
}

int mydaq_ctr_task_read(mydaq_ctr_task *t, double *value, int timeout_ms)
{
    uint32_t n = 0;
    int rc;
    if (!value)
        return MYDAQ_ERR_INVALID_ARG;
    rc = mydaq_ctr_task_read_count(t, &n, timeout_ms);
    if (rc != MYDAQ_OK)
        return rc;
    if (t->cfg.kind == MYDAQ_CTR_COUNT_EDGES)
        *value = (double)n;
    else if (n == 0)
        return MYDAQ_ERR_TIMEOUT;   /* no complete period yet */
    else if (t->cfg.kind == MYDAQ_CTR_PERIOD)
        *value = (double)n / CTR_TIMEBASE_HZ;
    else
        *value = CTR_TIMEBASE_HZ / (double)n;
    return MYDAQ_OK;
}

uint8_t mydaq_ctr_task_dio_lines(const mydaq_ctr_task *t) { return t ? t->dio_lines : 0; }

int mydaq_ctr_task_pulse(const mydaq_ctr_task *t, double *frequency, double *duty_cycle)
{
    double period;
    if (!t || t->cfg.kind != MYDAQ_CTR_PULSE_TRAIN)
        return MYDAQ_ERR_INVALID_ARG;
    period = (double)t->low + (double)t->high;
    if (frequency)
        *frequency = CTR_TIMEBASE_HZ / period;
    if (duty_cycle)
        *duty_cycle = (double)t->high / period;
    return MYDAQ_OK;
}

/* ---- FREQOUT ----------------------------------------------------------------------------- */

static uint16_t freqout_divider(double hz)
{
    unsigned n, best = 1;
    double err = INFINITY;
    for (n = 1; n <= FREQOUT_MAX_DIVIDER; ++n) {
        const double e = fabs(FREQOUT_TIMEBASE_HZ / n - hz);
        if (e < err) {
            err = e;
            best = n;
        }
    }
    return (uint16_t)best;
}

double mydaq_freqout_coerce(double hz)
{
    return FREQOUT_TIMEBASE_HZ / freqout_divider(hz);
}

int mydaq_freqout_start(mydaq_device *dev, double hz, double *actual)
{
    md_buf b;
    uint16_t div;
    int rc = MYDAQ_OK;
    if (!dev || !(hz > 0.0))
        return MYDAQ_ERR_INVALID_ARG;
    div = freqout_divider(hz);
    md_mutex_lock(&dev->state_lock);
    if (!dev->connected)
        rc = MYDAQ_ERR_NO_DEVICE;
    else if (dev->freqout_running)
        rc = MYDAQ_ERR_BUSY;
    else if ((dev->dio_reserved | dev->dio_direction) & DIO_FREQOUT)
        rc = MYDAQ_ERR_BUSY;
    else {
        dev->freqout_running = 1;
        dev->dio_reserved |= DIO_FREQOUT;
    }
    md_mutex_unlock(&dev->state_lock);
    if (rc != MYDAQ_OK)
        return rc;

    md_cmd_lock(dev);
    rc = route2(dev, 0xc271008bu, 0x895d5d2cu, 0x0f, 1);   /* FREQOUT -> PFI4 */
    if (rc == MYDAQ_OK) {
        md_buf_init(&b);   /* configure: u16 divider, u8 1 (100 kHz timebase), u8 0 */
        md_put_u32(&b, OBJ_FREQOUT);
        md_put_u16(&b, div);
        md_put_u8(&b, 1);
        md_put_u8(&b, 0);
        rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_RESET, &b, MD_TIMEOUT_CMD, 0, NULL);
    }
    if (rc == MYDAQ_OK)
        rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_WRITE, OBJ_FREQOUT, NULL, 0, MD_TIMEOUT_CMD, NULL);
    md_cmd_unlock(dev);
    if (rc != MYDAQ_OK) {
        md_mutex_lock(&dev->state_lock);
        dev->freqout_running = 0;
        dev->dio_reserved &= (uint8_t)~DIO_FREQOUT;
        md_mutex_unlock(&dev->state_lock);
        return rc;
    }
    if (actual)
        *actual = FREQOUT_TIMEBASE_HZ / div;
    return MYDAQ_OK;
}

int mydaq_freqout_stop(mydaq_device *dev)
{
    int running, rc = MYDAQ_OK;
    if (!dev)
        return MYDAQ_ERR_INVALID_ARG;
    md_mutex_lock(&dev->state_lock);
    running = dev->freqout_running;
    md_mutex_unlock(&dev->state_lock);
    if (!running)
        return MYDAQ_OK;
    md_cmd_lock(dev);
    rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_COMMIT, OBJ_FREQOUT, NULL, 0, MD_TIMEOUT_CMD, NULL);
    if (rc != MYDAQ_ERR_NO_DEVICE) {
        const int rc2 = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_START, OBJ_FREQOUT, NULL, 0,
                                     MD_TIMEOUT_CMD, NULL);
        if (rc == MYDAQ_OK)
            rc = rc2;
    }
    md_cmd_unlock(dev);
    md_mutex_lock(&dev->state_lock);
    dev->freqout_running = 0;
    dev->dio_reserved &= (uint8_t)~DIO_FREQOUT;
    md_mutex_unlock(&dev->state_lock);
    return rc == MYDAQ_ERR_NO_DEVICE ? MYDAQ_OK : rc;
}

int mydaq_freqout_running(const mydaq_device *cdev)
{
    mydaq_device *dev = (mydaq_device *)(uintptr_t)cdev;
    int r;
    if (!dev)
        return 0;
    md_mutex_lock(&dev->state_lock);
    r = dev->freqout_running;
    md_mutex_unlock(&dev->state_lock);
    return r;
}
