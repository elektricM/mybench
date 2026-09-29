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
#define DIO_CTR_GATE 0x02u     /* DIO1 = PFI1: frequency/period input (gate), count reset, Z */
#define DIO_CTR_AUX 0x04u      /* DIO2 = PFI2: encoder B */
#define DIO_CTR_OUTPUT 0x08u   /* DIO3 = PFI3: pulse-train output */
#define DIO_FREQOUT 0x10u      /* DIO4 = PFI4 */

struct mydaq_ctr_task {
    mydaq_device *dev;
    mydaq_ctr_config cfg;
    uint32_t low, high;       /* pulse train, ticks */
    uint32_t delay;           /* pulse train: initial delay, ticks (2 = none, as DAQmx) */
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

/* ---- device sequences --------------------------------------------------------------------
 * Every ctr0 task is the same skeleton (protocol.md 10.1): routes, reset, configure(U, B), 0a,
 * 0b(16 u16), 0c, 0d(2, idle level), 0e, 0f(6 u16), 10(8 u16), 11, 12, [output route], 13, 12,
 * arm. The task kinds differ only in the values below, taken from the captures named in
 * protocol.md 10.2 / 10.5 (byte-exact replays in tests/test_replay.c). */

/* route ids (protocol.md 10.3 / 10.5) */
#define R_PFI0_IN 0xc51cc492u, 0x8e309935u
#define R_PFI1_IN 0xb21bf404u, 0xf937a9a3u
#define R_PFI2_IN 0x2b12a5beu, 0x603ef819u
#define R_SOURCE 0x587c6efeu, 0x37cf14ddu
#define R_GATE 0x097d2c54u, 0x11f9be17u
#define R_AUX 0xc7c20613u, 0x88fbba44u
#define R_PW 0x77c75630u, 0x5d392cd4u
#define R_LOOP 0x5e3c3848u          /* single id: the internal FREQOUT loop (value 0x24) */
#define R_ENC_A 0x3da647a3u
#define R_ENC_B 0xa4af1619u
#define R_ENC_Z 0xb7c38e4fu
#define SRC_100MHZ 0x1eu

typedef struct ctr_prog {
    uint32_t u[9];
    uint8_t b[4];
    uint16_t m0b[16];
    uint16_t m0c;
    uint16_t idle;            /* 0d second value: 1 = idle high */
    uint16_t m0f[6];
    uint16_t m10[8];
    int finite;               /* 12: finite pulse train flag; 0f[5] = 7 */
    int output;               /* pulse train: route ctr0 out -> PFI3 before arming */
    uint32_t arm_a, arm_b;
    uint8_t arm[4];
} ctr_prog;

static int route1(mydaq_device *dev, uint32_t id, uint32_t value)
{
    const uint32_t w[4] = {6, id, 6, value};
    return md_cmd_words(dev, MD_CH_OBJECT, MD_OP_WRITE, MD_OBJ_PROPERTIES, w, 4, MD_TIMEOUT_CMD, NULL);
}

static int ctr_u16s(mydaq_device *dev, uint8_t op, const uint16_t *v, size_t n)
{
    md_buf b;
    size_t i;
    md_buf_init(&b);
    md_put_u32(&b, OBJ_CTR0);
    for (i = 0; i < n; ++i)
        md_put_u16(&b, v[i]);
    return md_cmd(dev, MD_CH_OBJECT, op, &b, MD_TIMEOUT_CMD, 0, NULL);
}

/* The skeleton after the task's routes. */
static int run_program(mydaq_device *dev, const ctr_prog *p)
{
    const char *m12 = p->finite ? "000000000000000001000000" : "000000000000000000000000";
    const uint16_t m0c[2] = {p->m0c, 0};
    const uint16_t m0d[2] = {2, p->idle};
    md_buf b;
    size_t i;
    int rc = ctr_hex(dev, MD_OP_RESET, "");
    if (rc == MYDAQ_OK) {
        md_buf_init(&b);
        md_put_u32(&b, OBJ_CTR0);
        for (i = 0; i < 9; ++i)
            md_put_u32(&b, p->u[i]);
        md_put_u16(&b, 0);
        md_put_u16(&b, 0);
        for (i = 0; i < 4; ++i)
            md_put_u8(&b, p->b[i]);
        rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_WRITE, &b, MD_TIMEOUT_CMD, 0, NULL);
    }
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x0a, "00000000");
    if (rc == MYDAQ_OK)
        rc = ctr_u16s(dev, 0x0b, p->m0b, 16);
    if (rc == MYDAQ_OK)
        rc = ctr_u16s(dev, 0x0c, m0c, 2);
    if (rc == MYDAQ_OK)
        rc = ctr_u16s(dev, 0x0d, m0d, 2);
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x0e, "");
    if (rc == MYDAQ_OK)
        rc = ctr_u16s(dev, 0x0f, p->m0f, 6);
    if (rc == MYDAQ_OK)
        rc = ctr_u16s(dev, 0x10, p->m10, 8);
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x11, "000000010000000000000000");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x12, m12);
    if (rc == MYDAQ_OK && p->output)
        rc = route2(dev, 0x5c159528u, 0x1739c88fu, 0x0d, 1);                /* output -> PFI3 */
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x13, "");
    if (rc == MYDAQ_OK)
        rc = ctr_hex(dev, 0x12, m12);
    if (rc == MYDAQ_OK) {
        md_buf_init(&b);
        md_put_u32(&b, OBJ_CTR0);
        md_put_u32(&b, p->arm_a);
        md_put_u32(&b, p->arm_b);
        md_put_u16(&b, 0);
        for (i = 0; i < 4; ++i)
            md_put_u8(&b, p->arm[i]);
        rc = md_cmd(dev, MD_CH_OBJECT, 0x14, &b, MD_TIMEOUT_CMD, 0, NULL);
    }
    return rc;
}

/* Application code of an input task (configure B1, arm, read). */
static uint8_t app_code(mydaq_ctr_kind kind)
{
    switch (kind) {
    case MYDAQ_CTR_COUNT_EDGES: return 1;
    case MYDAQ_CTR_PERIOD: return 2;
    case MYDAQ_CTR_FREQUENCY: return 3;
    case MYDAQ_CTR_PULSE_WIDTH: return 4;
    case MYDAQ_CTR_LINEAR_ENCODER: return 7;
    case MYDAQ_CTR_ANGULAR_ENCODER: return 0x0e;
    case MYDAQ_CTR_PULSE_TIME: return 0x0f;
    default: return 0;
    }
}

/* Gate input (the measured signal) of frequency, period, pulse width and pulse time. */
static int route_gate(mydaq_device *dev, const mydaq_ctr_config *c, int pulse_width)
{
    const uint32_t pol = c->falling ? 1u : 0u;
    int rc = MYDAQ_OK;
    if (c->input == MYDAQ_CTR_INPUT_FREQOUT) {   /* ctr-pw-falling / ctr-pulsetime -freqout-10k */
        if (pulse_width)
            rc = route2(dev, R_PW, 0, 0);
        if (rc == MYDAQ_OK)
            rc = route1(dev, R_LOOP, 0x24);
        if (rc == MYDAQ_OK)
            rc = route2(dev, R_GATE, 0x29, pol);
    } else {                                   /* ctr-cifreq-pfi1-timeout */
        rc = route2(dev, R_PFI1_IN, 0, 0);
        if (rc == MYDAQ_OK && pulse_width)
            rc = route2(dev, R_PW, 0, 0);
        if (rc == MYDAQ_OK)
            rc = route2(dev, R_GATE, 0x02, pol);
    }
    if (rc == MYDAQ_OK)
        rc = route2(dev, R_SOURCE, SRC_100MHZ, 0);
    return rc;
}

static int program(mydaq_ctr_task *t)
{
    mydaq_device *dev = t->dev;
    const mydaq_ctr_config *c = &t->cfg;
    const uint8_t app = app_code(c->kind);
    ctr_prog p;
    int rc = MYDAQ_OK;

    memset(&p, 0, sizeof p);
    p.m0c = 1;
    p.m0f[3] = 3;
    p.m0b[12] = 1;
    switch (c->kind) {
    case MYDAQ_CTR_COUNT_EDGES: {   /* ctr-countedges-*, ctrloop-* */
        static const uint32_t source[] = {0x01, 0x2b, 0x1c, 0x12};
        if (c->reset_on_dio1) {     /* ctrloop-resetpfi1-10k */
            rc = route2(dev, R_PFI1_IN, 0, 0);
            if (rc == MYDAQ_OK)
                rc = route2(dev, R_GATE, 0x02, 0);
            p.m0b[3] = 1;
            p.m0b[7] = 3;
            p.m0b[9] = 2;
        } else {
            p.m0b[7] = 2;
        }
        if (rc == MYDAQ_OK && c->input == MYDAQ_CTR_INPUT_TERMINAL)
            rc = route2(dev, R_PFI0_IN, 0, 0);
        if (rc == MYDAQ_OK && c->input == MYDAQ_CTR_INPUT_FREQOUT)
            rc = route1(dev, R_LOOP, 0x24);
        if (rc == MYDAQ_OK)
            rc = route2(dev, R_SOURCE, source[c->input], c->falling ? 1u : 0u);
        p.u[0] = c->initial_count;
        p.b[1] = 1;
        p.b[3] = 1;
        p.arm[0] = 1;
        p.arm[1] = 1;
        break;
    }
    case MYDAQ_CTR_FREQUENCY:
    case MYDAQ_CTR_PERIOD:          /* ctr-cifreq-pfi1-timeout (period: app code 2) */
        rc = route_gate(dev, c, 0);
        p.u[0] = 1;
        p.u[2] = 1;
        p.b[1] = app;
        p.b[2] = 1;
        p.m0b[3] = 1;
        p.m0b[7] = 3;
        p.m0b[9] = 2;
        p.m10[2] = p.m10[4] = 1;
        p.arm_a = 1;
        p.arm[0] = app;
        break;
    case MYDAQ_CTR_PULSE_WIDTH:     /* ctr-pw-falling-freqout-10k */
        rc = route_gate(dev, c, 1);
        p.b[1] = app;
        p.b[3] = 1;
        p.m0b[3] = 1;
        p.m0b[7] = 2;
        p.m0b[9] = 1;
        p.m10[2] = p.m10[4] = 1;
        p.arm[0] = app;
        p.arm[1] = 1;
        break;
    case MYDAQ_CTR_PULSE_TIME:      /* ctr-pulsetime-freqout-10k */
        rc = route_gate(dev, c, 0);
        p.u[0] = 1;
        p.u[2] = 1;
        p.b[1] = app;
        p.b[2] = 1;
        p.b[3] = 1;
        p.m0b[3] = 1;
        p.m0b[7] = 3;
        p.m0b[8] = 1;
        p.m0b[9] = 3;
        p.m10[2] = p.m10[4] = 1;
        p.arm_a = 1;
        p.arm[0] = app;
        p.arm[1] = 1;
        break;
    case MYDAQ_CTR_LINEAR_ENCODER:
    case MYDAQ_CTR_ANGULAR_ENCODER: /* ctr-linenc-x4 / -x1-z, ctr-angenc-x2 / -2pulse-freqout-10k */
        if (c->input == MYDAQ_CTR_INPUT_FREQOUT) {
            rc = route1(dev, R_LOOP, 0x24);
            if (rc == MYDAQ_OK)
                rc = route1(dev, R_ENC_A, 0x2e);
        } else {
            rc = route2(dev, R_PFI0_IN, 0, 0);
            if (rc == MYDAQ_OK)
                rc = route1(dev, R_ENC_A, 0x01);
        }
        if (rc == MYDAQ_OK)
            rc = route2(dev, R_PFI2_IN, 0, 0);
        if (rc == MYDAQ_OK)
            rc = route1(dev, R_ENC_B, 0x03);
        if (rc == MYDAQ_OK && c->z_index) {
            rc = route2(dev, R_PFI1_IN, 0, 0);
            if (rc == MYDAQ_OK)
                rc = route1(dev, R_ENC_Z, 0x02);
        }
        p.u[0] = 1;
        p.u[2] = 1;
        p.b[1] = app;
        p.b[2] = 2;
        p.b[3] = 1;
        p.m0b[7] = 3;
        p.m0c = 2;
        p.m0f[4] = c->z_index ? 1u : 0u;
        p.m0f[5] = (uint16_t)(c->decoding ? c->decoding : MYDAQ_CTR_X4);
        p.arm_a = 1;
        p.arm[0] = app;
        p.arm[1] = 1;
        break;
    case MYDAQ_CTR_PULSE_TRAIN: {   /* co-ptrain-*, co-finite-* (protocol.md 10.5) */
        const uint32_t active = c->idle_high ? t->low : t->high;
        const uint32_t idle = c->idle_high ? t->high : t->low;
        const int delayed = t->delay > 2u;
        rc = route2(dev, R_SOURCE, SRC_100MHZ, 0);
        p.u[0] = t->delay;
        p.b[1] = c->mode == MYDAQ_FINITE ? 0x0b : 0x09;
        p.b[3] = 1;
        p.m0b[0] = p.m0b[2] = p.m0b[4] = 1;
        p.m0b[12] = 0;
        p.m0c = 0;
        p.idle = c->idle_high ? 1u : 0u;
        p.output = 1;
        p.arm[0] = p.b[1];
        p.arm[1] = 1;
        if (c->mode == MYDAQ_FINITE) {
            const uint32_t two_n = (uint32_t)(2u * c->pulses);
            p.u[1] = 2;
            p.u[3] = two_n;
            p.u[6] = active;   /* (active, idle, active): co-finite-10-1k-d0.25-delay5ms */
            p.u[7] = idle;
            p.u[8] = active;
            p.m0b[5] = 1;
            p.m0b[6] = 2;
            p.m0b[15] = 2;
            p.m0f[5] = 7;
            p.finite = 1;
            p.arm_b = two_n;
        } else {
            p.u[2] = idle;     /* time in the idle state first: co-ptrain-1k-d0.25-idlehigh */
            p.u[3] = active;
            p.b[0] = delayed ? 0 : 1;   /* co-ptrain-1k-d0.25-delay10ms */
            p.m10[0] = p.m10[3] = p.m10[4] = 1;
            p.arm_a = idle;
            p.arm_b = active;
            p.arm[3] = delayed ? 0 : 1;
        }
        break;
    }
    default:
        return MYDAQ_ERR_NOT_SUPPORTED;
    }
    if (rc == MYDAQ_OK)
        rc = route2(dev, R_AUX, 0x2f, 0);                                    /* aux: none */
    if (rc == MYDAQ_OK)
        rc = run_program(dev, &p);
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
    uint32_t low = 0, high = 0, delay_ticks = CTR_MIN_TICKS;
    uint8_t lines;
    if (!out)
        return MYDAQ_ERR_INVALID_ARG;
    *out = NULL;
    if (!dev || !config)
        return MYDAQ_ERR_INVALID_ARG;
    if ((unsigned)config->input > MYDAQ_CTR_INPUT_100KHZ ||
        (config->input >= MYDAQ_CTR_INPUT_20MHZ && config->kind != MYDAQ_CTR_COUNT_EDGES))
        return MYDAQ_ERR_INVALID_ARG;
    switch (config->kind) {
    case MYDAQ_CTR_COUNT_EDGES:
        lines = config->input == MYDAQ_CTR_INPUT_TERMINAL ? DIO_CTR_SOURCE : 0;
        if (config->reset_on_dio1)
            lines |= DIO_CTR_GATE;
        break;
    case MYDAQ_CTR_FREQUENCY:
    case MYDAQ_CTR_PERIOD:
    case MYDAQ_CTR_PULSE_WIDTH:
    case MYDAQ_CTR_PULSE_TIME:
        lines = config->input == MYDAQ_CTR_INPUT_FREQOUT ? 0 : DIO_CTR_GATE;
        break;
    case MYDAQ_CTR_LINEAR_ENCODER:
    case MYDAQ_CTR_ANGULAR_ENCODER:
        if (config->decoding && (config->decoding < MYDAQ_CTR_X1 || config->decoding > MYDAQ_CTR_TWO_PULSE))
            return MYDAQ_ERR_INVALID_ARG;
        lines = (uint8_t)((config->input == MYDAQ_CTR_INPUT_FREQOUT ? 0 : DIO_CTR_SOURCE) | DIO_CTR_AUX |
                          (config->z_index ? DIO_CTR_GATE : 0));
        break;
    case MYDAQ_CTR_PULSE_TRAIN: {
        double delay;
        rc = mydaq_ctr_coerce_pulse(config->frequency, config->duty_cycle, &low, &high, NULL, NULL);
        if (rc != MYDAQ_OK)
            return rc;
        if (config->mode == MYDAQ_FINITE && (config->pulses == 0 || config->pulses > 0x7fffffffu))
            return MYDAQ_ERR_INVALID_ARG;
        if (config->mode != MYDAQ_FINITE && config->mode != MYDAQ_CONTINUOUS)
            return MYDAQ_ERR_INVALID_ARG;
        delay = floor(config->initial_delay * CTR_TIMEBASE_HZ + 0.5);
        if (!(delay >= 0.0) || delay > 4294967295.0)
            return MYDAQ_ERR_INVALID_ARG;
        delay_ticks = delay < CTR_MIN_TICKS ? CTR_MIN_TICKS : (uint32_t)delay;
        lines = DIO_CTR_OUTPUT;
        break;
    }
    default:
        return MYDAQ_ERR_NOT_SUPPORTED;   /* two-edge separation: not decoded yet */
    }
    t = calloc(1, sizeof *t);
    if (!t)
        return MYDAQ_ERR_NO_MEM;
    t->dev = dev;
    t->cfg = *config;
    t->low = low;
    t->high = high;
    t->delay = delay_ticks;
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
        rc = program(t);
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

/* 0c41.1d(u32 1, u32 N, u32 timeout ms, u32 1, u16 1, u8 app, u8 0) -> u32[] {N values};
 * NI passes 10015 ms (its 10 s default + 15); pulse time asks for 2 values. */
static int read_values(mydaq_ctr_task *t, uint32_t *values, uint32_t n, int timeout_ms)
{
    md_resp r;
    md_buf b;
    uint32_t i;
    int rc;
    if (!t || !values || timeout_ms < 0)
        return MYDAQ_ERR_INVALID_ARG;
    if (t->cfg.kind == MYDAQ_CTR_PULSE_TRAIN)
        return MYDAQ_ERR_STATE;
    if (!mydaq_ctr_task_is_running(t))
        return MYDAQ_ERR_STATE;
    md_buf_init(&b);
    md_put_u32(&b, OBJ_CTR0);
    md_put_u32(&b, 1);
    md_put_u32(&b, n);
    md_put_u32(&b, (uint32_t)timeout_ms);
    md_put_u32(&b, 1);
    md_put_u16(&b, 1);
    md_put_u8(&b, app_code(t->cfg.kind));
    md_put_u8(&b, 0);
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
    if (r.payload_len < 4 + 4u * n || md_get_u32(r.payload) != 2u + 4u * n)
        return MYDAQ_ERR_PROTOCOL;
    for (i = 0; i < n; ++i)
        values[i] = md_get_u32(r.payload + 4 + 4 * i);
    return MYDAQ_OK;
}

int mydaq_ctr_task_read_count(mydaq_ctr_task *t, uint32_t *count, int timeout_ms)
{
    uint32_t v[2];
    int rc;
    if (!count)
        return MYDAQ_ERR_INVALID_ARG;
    rc = read_values(t, v, t && t->cfg.kind == MYDAQ_CTR_PULSE_TIME ? 2u : 1u, timeout_ms);
    if (rc == MYDAQ_OK)
        *count = v[0];
    return rc;
}

int mydaq_ctr_task_read_pulse_time(mydaq_ctr_task *t, uint32_t *high, uint32_t *low, int timeout_ms)
{
    uint32_t v[2];
    int rc;
    if (!t || !high || !low)
        return MYDAQ_ERR_INVALID_ARG;
    if (t->cfg.kind != MYDAQ_CTR_PULSE_TIME)
        return MYDAQ_ERR_STATE;
    rc = read_values(t, v, 2, timeout_ms);
    if (rc == MYDAQ_OK && (v[0] == 0 || v[1] == 0))
        rc = MYDAQ_ERR_TIMEOUT;   /* no complete period yet */
    if (rc == MYDAQ_OK) {
        *high = v[0];
        *low = v[1];
    }
    return rc;
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
    uint32_t n = 0, low = 0;
    int rc;
    if (!t || !value)
        return MYDAQ_ERR_INVALID_ARG;
    if (t->cfg.kind == MYDAQ_CTR_PULSE_TIME) {
        rc = mydaq_ctr_task_read_pulse_time(t, &n, &low, timeout_ms);
        if (rc == MYDAQ_OK)
            *value = ((double)n + (double)low) / CTR_TIMEBASE_HZ;
        return rc;
    }
    rc = mydaq_ctr_task_read_count(t, &n, timeout_ms);
    if (rc != MYDAQ_OK)
        return rc;
    switch (t->cfg.kind) {
    case MYDAQ_CTR_COUNT_EDGES:
        *value = (double)n;
        return MYDAQ_OK;
    case MYDAQ_CTR_LINEAR_ENCODER:
    case MYDAQ_CTR_ANGULAR_ENCODER:
        *value = (double)(int32_t)n;   /* signed position counts */
        return MYDAQ_OK;
    default:
        break;
    }
    if (n == 0)
        return MYDAQ_ERR_TIMEOUT;   /* no complete period / pulse yet */
    *value = t->cfg.kind == MYDAQ_CTR_FREQUENCY ? CTR_TIMEBASE_HZ / (double)n : (double)n / CTR_TIMEBASE_HZ;
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
