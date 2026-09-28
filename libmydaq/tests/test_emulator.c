/* Emulator tests: API semantics, error paths, threading and fault handling against the
 * behavioural model of the myDAQ in the mock transport. */
#include "test.h"

#include "mock/mydaq_mock.h"
#include "platform.h"

#include <math.h>
#include <stdlib.h>

typedef struct emu {
    mydaq_context *ctx;
    mydaq_mock *mock;
    mydaq_device *dev;
} emu;

static int emu_open(emu *e)
{
    memset(e, 0, sizeof *e);
    if (mydaq_context_new_no_usb(&e->ctx) != MYDAQ_OK)
        return 0;
    mydaq_context_set_log(e->ctx, MYDAQ_LOG_NONE, NULL, NULL);
    e->mock = mydaq_mock_new_emulator_file(test_data("calmem-036CD1C6.bin"));
    return e->mock && mydaq_mock_open(e->ctx, e->mock, 0, &e->dev) == MYDAQ_OK;
}

static void emu_close(emu *e)
{
    mydaq_close(e->dev);
    mydaq_mock_free(e->mock);
    mydaq_context_free(e->ctx);
}

#define EMU_OPEN(e)                                                                                \
    do {                                                                                           \
        if (!emu_open(&(e))) {                                                                     \
            test_fail(__FILE__, __LINE__, "emulator open failed");                                 \
            emu_close(&(e));                                                                       \
            return;                                                                                \
        }                                                                                          \
    } while (0)

/* Checks that tear the fixture down on failure. */
#define E_CHECK(e, cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            test_fail(__FILE__, __LINE__, "%s", #cond);                                            \
            emu_close(&(e));                                                                       \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define E_RC(e, expr, want)                                                                        \
    do {                                                                                           \
        const int rc_ = (expr);                                                                    \
        if (rc_ != (want)) {                                                                       \
            test_fail(__FILE__, __LINE__, "%s returned %s, expected %s", #expr,                    \
                      mydaq_error_name(rc_), mydaq_error_name(want));                              \
            emu_close(&(e));                                                                       \
            return;                                                                                \
        }                                                                                          \
    } while (0)

static mydaq_ai_config finite_config(size_t nch, double rate, uint64_t n)
{
    mydaq_ai_config c;
    memset(&c, 0, sizeof c);
    c.channels[0].channel = MYDAQ_AI0;
    c.channels[0].range = MYDAQ_AI_RANGE_10V;
    c.channels[1].channel = MYDAQ_AI1;
    c.channels[1].range = MYDAQ_AI_RANGE_2V;
    c.num_channels = nch;
    c.sample_rate = rate;
    c.mode = MYDAQ_FINITE;
    c.samples_per_channel = n;
    return c;
}

/* Sample source with a recognisable pattern: channel 0 counts up, channel 1 counts down. */
static int16_t ramp(void *user, int channel, int range_code, uint64_t index)
{
    (void)user;
    (void)range_code;
    return (int16_t)(channel == 0 ? (int)(index % 30000) : -(int)(index % 30000));
}

static void test_open_info(void)
{
    emu e;
    mydaq_info info;
    EMU_OPEN(e);
    E_RC(e, mydaq_get_info(e.dev, &info), MYDAQ_OK);
    E_CHECK(e, strcmp(info.serial, "036CD1C6") == 0);
    E_CHECK(e, strcmp(info.product, "NI myDAQ") == 0);
    E_CHECK(e, mydaq_is_connected(e.dev));
    E_CHECK(e, mydaq_get_calibration(e.dev)->checksums_ok);
    E_CHECK(e, mydaq_last_device_status(e.dev) == 0);
    emu_close(&e);
}

static void test_ai_on_demand(void)
{
    emu e;
    const mydaq_ai_channel_config c[2] = {{MYDAQ_AI0, MYDAQ_AI_RANGE_10V},
                                          {MYDAQ_AI1, MYDAQ_AI_RANGE_2V}};
    int16_t codes[2];
    double v[2];
    const mydaq_calibration *cal;
    EMU_OPEN(e);
    cal = mydaq_get_calibration(e.dev);
    E_RC(e, mydaq_ai_read_scan_raw(e.dev, c, 2, codes), MYDAQ_OK);
    E_CHECK(e, codes[0] == -8113 - 2 && codes[1] == 3000 - 2);
    E_RC(e, mydaq_ai_read_scan(e.dev, c, 2, v), MYDAQ_OK);
    E_CHECK(e, v[0] == mydaq_ai_code_to_volts(cal, MYDAQ_AI_RANGE_10V, -8113 - 1));
    E_CHECK(e, v[1] == mydaq_ai_code_to_volts(cal, MYDAQ_AI_RANGE_2V, 3000 - 1));
    E_RC(e, mydaq_ai_read_scan(e.dev, c, 0, v), MYDAQ_ERR_INVALID_ARG);
    emu_close(&e);
}

static void test_ai_finite_layouts(void)
{
    emu e;
    mydaq_ai_config cfg = finite_config(2, 50e3, 4000);
    mydaq_ai_task *t = NULL;
    const mydaq_calibration *cal;
    static double by_channel[8000], by_scan[8000];
    static int16_t raw[8000];
    size_t i;
    EMU_OPEN(e);
    cal = mydaq_get_calibration(e.dev);
    mydaq_mock_set_ai_source(e.mock, ramp, NULL);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_OK);
    E_CHECK(e, mydaq_ai_task_rate(t) == 50e3);
    /* 1250 ticks (+10 us settling) would not fit twice in a 20 us scan: spread evenly */
    E_CHECK(e, fabs(mydaq_ai_task_channel_skew(t) - 10e-6) < 1e-12);

    E_RC(e, mydaq_ai_task_start(t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_read(t, by_channel, 1000, MYDAQ_GROUP_BY_CHANNEL, 2000), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_read(t, by_scan, 1000, MYDAQ_GROUP_BY_SCAN, 2000), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_read_raw(t, raw, 2000, 2000), MYDAQ_OK);
    E_CHECK(e, mydaq_ai_task_remaining(t) == 0);
    for (i = 0; i < 1000; ++i) {
        E_CHECK(e, by_channel[i] == mydaq_ai_code_to_volts(cal, MYDAQ_AI_RANGE_10V, (int16_t)i));
        E_CHECK(e, by_channel[1000 + i] ==
                       mydaq_ai_code_to_volts(cal, MYDAQ_AI_RANGE_2V, (int16_t) - (int)i));
        E_CHECK(e, by_scan[2 * i] ==
                       mydaq_ai_code_to_volts(cal, MYDAQ_AI_RANGE_10V, (int16_t)(1000 + i)));
        E_CHECK(e, by_scan[2 * i + 1] ==
                       mydaq_ai_code_to_volts(cal, MYDAQ_AI_RANGE_2V, (int16_t) - (int)(1000 + i)));
    }
    for (i = 0; i < 2000; ++i) {
        E_CHECK(e, raw[2 * i] == (int16_t)(2000 + i));
        E_CHECK(e, raw[2 * i + 1] == (int16_t) - (int)(2000 + i));
    }
    /* nothing left: reading more is an argument error */
    E_RC(e, mydaq_ai_task_read_raw(t, raw, 1, 0), MYDAQ_ERR_INVALID_ARG);
    E_CHECK(e, !mydaq_mock_ai_streaming(e.mock));

    /* a second acquisition on the same task */
    E_RC(e, mydaq_ai_task_start(t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_read_raw(t, raw, 4000, 2000), MYDAQ_OK);
    E_CHECK(e, raw[0] == 0 && raw[7999] == -3999);
    mydaq_ai_task_destroy(t);
    emu_close(&e);
}

static void test_ai_realtime_and_timeout(void)
{
    emu e;
    mydaq_ai_config cfg = finite_config(1, 2000.0, 400);   /* 0.2 s */
    mydaq_ai_task *t = NULL;
    static double v[400];
    uint64_t t0;
    EMU_OPEN(e);
    mydaq_mock_set_realtime(e.mock, 1);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_start(t), MYDAQ_OK);
    E_CHECK(e, mydaq_ai_task_is_running(t));
    /* not all there yet: times out without consuming */
    E_RC(e, mydaq_ai_task_read(t, v, 400, MYDAQ_GROUP_BY_SCAN, 20), MYDAQ_ERR_TIMEOUT);
    E_CHECK(e, mydaq_ai_task_remaining(t) == 400);
    t0 = md_now_ms();
    E_RC(e, mydaq_ai_task_read(t, v, 400, MYDAQ_GROUP_BY_SCAN, 3000), MYDAQ_OK);
    E_CHECK(e, md_now_ms() - t0 >= 100);   /* paced by the sample clock */
    E_CHECK(e, !mydaq_ai_task_is_running(t));
    /* not started: reading fails */
    E_RC(e, mydaq_ai_task_stop(t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_read(t, v, 1, MYDAQ_GROUP_BY_SCAN, 10), MYDAQ_ERR_STATE);
    mydaq_ai_task_destroy(t);
    emu_close(&e);
}

typedef struct stopper {
    mydaq_ai_task *task;
    uint32_t delay_ms;
} stopper;

static void stop_later(void *arg)
{
    stopper *s = arg;
    md_sleep_ms(s->delay_ms);
    mydaq_ai_task_stop(s->task);
}

static void test_ai_stop_wakes_reader(void)
{
    emu e;
    mydaq_ai_config cfg = finite_config(1, 1000.0, 5000);   /* 5 s */
    mydaq_ai_task *t = NULL;
    static double v[5000];
    stopper s;
    md_thread th;
    uint64_t t0;
    EMU_OPEN(e);
    mydaq_mock_set_realtime(e.mock, 1);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_start(t), MYDAQ_OK);
    s.task = t;
    s.delay_ms = 100;
    E_CHECK(e, md_thread_start(&th, stop_later, &s) == 0);
    t0 = md_now_ms();
    E_RC(e, mydaq_ai_task_read(t, v, 5000, MYDAQ_GROUP_BY_SCAN, -1), MYDAQ_ERR_STATE);
    md_thread_join(th);
    E_CHECK(e, md_now_ms() - t0 < 2000);
    E_CHECK(e, !mydaq_mock_ai_streaming(e.mock));
    mydaq_ai_task_destroy(t);
    emu_close(&e);
}

static void test_ai_reservation(void)
{
    emu e;
    mydaq_ai_config cfg = finite_config(1, 1000.0, 10);
    mydaq_ai_task *t = NULL, *t2 = NULL;
    const mydaq_ai_channel_config c = {MYDAQ_AI0, MYDAQ_AI_RANGE_10V};
    double v;
    EMU_OPEN(e);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t2), MYDAQ_ERR_BUSY);
    E_RC(e, mydaq_ai_read_scan(e.dev, &c, 1, &v), MYDAQ_ERR_BUSY);
    mydaq_ai_task_destroy(t);
    E_RC(e, mydaq_ai_read_scan(e.dev, &c, 1, &v), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t2), MYDAQ_OK);
    mydaq_ai_task_destroy(t2);
    emu_close(&e);
}

static void test_ai_invalid_config(void)
{
    emu e;
    mydaq_ai_task *t = NULL;
    mydaq_ai_config cfg;
    EMU_OPEN(e);
    cfg = finite_config(1, 0.0, 10);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_ERR_INVALID_ARG);
    cfg = finite_config(1, 250e3, 10);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_ERR_INVALID_ARG);
    cfg = finite_config(1, 1000.0, 0);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_ERR_INVALID_ARG);
    cfg = finite_config(3, 1000.0, 10);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_ERR_INVALID_ARG);
    cfg = finite_config(1, 1000.0, 10);
    cfg.mode = (mydaq_sample_mode)5;
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_ERR_INVALID_ARG);
    cfg = finite_config(1, 1000.0, 10);
    cfg.channels[0].channel = MYDAQ_AUDIO_IN_LEFT;
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_ERR_NOT_SUPPORTED);
    E_CHECK(e, t == NULL);
    emu_close(&e);
}

static void test_ai_unplug(void)
{
    emu e;
    mydaq_ai_config cfg = finite_config(1, 2000.0, 2000);
    mydaq_ai_task *t = NULL;
    static double v[2000];
    uint8_t levels;
    EMU_OPEN(e);
    mydaq_mock_set_realtime(e.mock, 1);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_start(t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_read(t, v, 100, MYDAQ_GROUP_BY_SCAN, 2000), MYDAQ_OK);
    mydaq_mock_unplug(e.mock);
    E_RC(e, mydaq_ai_task_read(t, v, 1900, MYDAQ_GROUP_BY_SCAN, 3000), MYDAQ_ERR_NO_DEVICE);
    E_CHECK(e, !mydaq_is_connected(e.dev));
    E_RC(e, mydaq_dio_read(e.dev, &levels), MYDAQ_ERR_NO_DEVICE);
    E_RC(e, mydaq_ai_task_stop(t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_start(t), MYDAQ_ERR_NO_DEVICE);
    mydaq_ai_task_destroy(t);
    emu_close(&e);
}

static void test_ai_overflow_event(void)
{
    emu e;
    mydaq_ai_config cfg = finite_config(1, 1000.0, 2000);
    mydaq_ai_task *t = NULL;
    static double v[2000];
    EMU_OPEN(e);
    mydaq_mock_set_realtime(e.mock, 1);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_start(t), MYDAQ_OK);
    mydaq_mock_inject_event(e.mock, 0x0551, 2, 0);   /* device FIFO overflow */
    E_RC(e, mydaq_ai_task_read(t, v, 2000, MYDAQ_GROUP_BY_SCAN, 3000), MYDAQ_ERR_OVERFLOW);
    E_RC(e, mydaq_ai_task_stop(t), MYDAQ_OK);
    mydaq_ai_task_destroy(t);
    emu_close(&e);
}

static void test_ai_continuous(void)
{
    emu e;
    mydaq_ai_config cfg = finite_config(2, 20e3, 1000);
    mydaq_ai_task *t = NULL;
    static int16_t raw[2 * 1000];
    uint64_t expect = 0;
    int k;
    size_t i;
    EMU_OPEN(e);
    cfg.mode = MYDAQ_CONTINUOUS;
    cfg.buffer_samples = 4000;     /* host buffer 4000 scans = 16000 bytes; reads of 1000 */
    mydaq_mock_set_ai_source(e.mock, ramp, NULL);
    mydaq_mock_set_realtime(e.mock, 1);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_start(t), MYDAQ_OK);
    E_CHECK(e, mydaq_mock_last_read_pos(e.mock) == 0x7fffffffu - 16000u);   /* sent at start */
    for (k = 0; k < 10; ++k) {   /* 0.5 s, 10 host buffers: gapless */
        E_RC(e, mydaq_ai_task_read_raw(t, raw, 1000, 2000), MYDAQ_OK);
        for (i = 0; i < 1000; ++i, ++expect) {
            E_CHECK(e, raw[2 * i] == (int16_t)(expect % 30000));
            E_CHECK(e, raw[2 * i + 1] == (int16_t) - (int)(expect % 30000));
        }
    }
    E_CHECK(e, mydaq_ai_task_is_running(t));
    E_CHECK(e, mydaq_ai_task_remaining(t) == 0);
    /* start + one per host buffer consumed (40000 bytes = 2.5 buffers) */
    E_CHECK(e, mydaq_mock_oneway_frames(e.mock) == 3);
    E_CHECK(e, mydaq_mock_last_read_pos(e.mock) == 0x7fffffffu - 3u * 16000u);
    E_RC(e, mydaq_ai_task_stop(t), MYDAQ_OK);
    E_CHECK(e, !mydaq_mock_ai_streaming(e.mock));
    /* restart continues to work */
    E_RC(e, mydaq_ai_task_start(t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_read_raw(t, raw, 1000, 2000), MYDAQ_OK);
    E_CHECK(e, raw[0] == 0);
    mydaq_ai_task_destroy(t);
    emu_close(&e);
}

static void test_ai_host_overflow(void)
{
    /* the application does not read: the host buffer overflows */
    emu e;
    mydaq_ai_config cfg = finite_config(1, 200e3, 1000);
    mydaq_ai_task *t = NULL;
    int16_t raw[16];
    EMU_OPEN(e);
    cfg.mode = MYDAQ_CONTINUOUS;
    cfg.buffer_samples = 1000;   /* exact host buffer */
    mydaq_mock_set_realtime(e.mock, 1);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_start(t), MYDAQ_OK);
    md_sleep_ms(100);   /* 20000 samples into a 1000-sample buffer */
    E_RC(e, mydaq_ai_task_read_raw(t, raw, 16, 1000), MYDAQ_ERR_OVERFLOW);
    E_RC(e, mydaq_ai_task_stop(t), MYDAQ_OK);
    mydaq_ai_task_destroy(t);
    emu_close(&e);
}

static void test_close_with_task(void)
{
    emu e;
    mydaq_ai_config cfg = finite_config(2, 1000.0, 5000);
    mydaq_ai_task *t = NULL;
    EMU_OPEN(e);
    mydaq_mock_set_realtime(e.mock, 1);
    E_RC(e, mydaq_ai_task_create(e.dev, &cfg, &t), MYDAQ_OK);
    E_RC(e, mydaq_ai_task_start(t), MYDAQ_OK);
    md_sleep_ms(20);
    emu_close(&e);   /* destroys the running task */
}

static void test_ao(void)
{
    emu e;
    EMU_OPEN(e);
    E_RC(e, mydaq_ao_write(e.dev, MYDAQ_AO1, 0.5), MYDAQ_OK);
    E_CHECK(e, mydaq_mock_ao_code(e.mock, 1) == 1253);
    E_RC(e, mydaq_ao_write(e.dev, MYDAQ_AO0, 0.0), MYDAQ_OK);
    E_CHECK(e, mydaq_mock_ao_code(e.mock, 0) == -167);
    E_RC(e, mydaq_ao_write_raw(e.dev, MYDAQ_AO0, 1234), MYDAQ_OK);
    E_CHECK(e, mydaq_mock_ao_code(e.mock, 0) == 1234);
    E_RC(e, mydaq_ao_write(e.dev, MYDAQ_AO0, 11.0), MYDAQ_ERR_INVALID_ARG);
    E_RC(e, mydaq_ao_write(e.dev, MYDAQ_AUDIO_OUT_LEFT, 0.0), MYDAQ_ERR_NOT_SUPPORTED);
    E_RC(e, mydaq_ao_write(e.dev, (mydaq_ao_channel)9, 0.0), MYDAQ_ERR_INVALID_ARG);
    emu_close(&e);
}

static void test_dio(void)
{
    emu e;
    uint8_t v, dir;
    EMU_OPEN(e);
    mydaq_mock_set_dio_inputs(e.mock, 0xa5);
    E_RC(e, mydaq_dio_read(e.dev, &v), MYDAQ_OK);
    E_CHECK(e, v == 0xa5);
    E_RC(e, mydaq_dio_write(e.dev, 0x0f, 0x0f), MYDAQ_OK);
    E_RC(e, mydaq_dio_set_direction(e.dev, 0x0f, 1), MYDAQ_OK);
    E_RC(e, mydaq_dio_get_direction(e.dev, &dir), MYDAQ_OK);
    E_CHECK(e, dir == 0x0f && mydaq_mock_dio_direction(e.mock) == 0x0f);
    E_RC(e, mydaq_dio_read(e.dev, &v), MYDAQ_OK);
    E_CHECK(e, v == 0xaf);
    E_RC(e, mydaq_dio_write(e.dev, 0x00, 0x03), MYDAQ_OK);
    E_RC(e, mydaq_dio_read(e.dev, &v), MYDAQ_OK);
    E_CHECK(e, v == 0xac);
    E_RC(e, mydaq_dio_set_direction(e.dev, 0xff, 0), MYDAQ_OK);
    E_RC(e, mydaq_dio_get_direction(e.dev, &dir), MYDAQ_OK);
    E_CHECK(e, dir == 0);
    emu_close(&e);
}

static void test_dmm(void)
{
    emu e;
    mydaq_dmm_reading r;
    EMU_OPEN(e);
    mydaq_mock_set_dmm_packet(e.mock, "40162;<08\r\n");
    E_RC(e, mydaq_dmm_read(e.dev, MYDAQ_DMM_DC_VOLTS, 0.2, &r), MYDAQ_OK);
    E_CHECK(e, r.value == -0.018572956927664929);
    E_CHECK(e, mydaq_mock_dmm_mode(e.mock) == 3);
    mydaq_mock_set_dmm_packet(e.mock, "20000;909\r\n");   /* overload */
    E_RC(e, mydaq_dmm_read(e.dev, MYDAQ_DMM_DC_VOLTS, 60.0, &r), MYDAQ_OK);
    E_CHECK(e, r.overload == 1 && r.value == 60.0);
    E_RC(e, mydaq_dmm_read(e.dev, MYDAQ_DMM_DC_VOLTS, 5.0, &r), MYDAQ_ERR_INVALID_ARG);
    E_RC(e, mydaq_dmm_read(e.dev, MYDAQ_DMM_DIODE, 2.0, &r), MYDAQ_ERR_NOT_SUPPORTED);
    mydaq_mock_set_dmm_packet(e.mock, "41705;804\r\n");   /* captures/dmm-acv-r0.2-x2 */
    E_RC(e, mydaq_dmm_read(e.dev, MYDAQ_DMM_AC_VOLTS, 0.2, &r), MYDAQ_OK);
    E_CHECK(e, r.value == 0.21825235477124808 && mydaq_mock_dmm_mode(e.mock) == 7);
    mydaq_mock_set_dmm_packet(e.mock, "1000x;808\r\n");
    E_RC(e, mydaq_dmm_read(e.dev, MYDAQ_DMM_DC_VOLTS, 20.0, &r), MYDAQ_ERR_PROTOCOL);
    emu_close(&e);
}

static void test_reset(void)
{
    emu e;
    EMU_OPEN(e);
    E_RC(e, mydaq_ao_write(e.dev, MYDAQ_AO0, 0.5), MYDAQ_OK);
    E_RC(e, mydaq_dio_set_direction(e.dev, 0xff, 1), MYDAQ_OK);
    E_RC(e, mydaq_reset(e.dev), MYDAQ_OK);
    E_CHECK(e, mydaq_mock_ao_code(e.mock, 0) == -167 && mydaq_mock_ao_code(e.mock, 1) == -220);
    E_CHECK(e, mydaq_mock_dio_direction(e.mock) == 0);
    emu_close(&e);
}

/* Concurrent callers are serialised on the command channel without corrupting each other. */
typedef struct hammer {
    mydaq_device *dev;
    int kind;
    int errors;
} hammer;

static void hammer_main(void *arg)
{
    hammer *h = arg;
    int i;
    for (i = 0; i < 60; ++i) {
        int rc;
        if (h->kind == 0) {
            const mydaq_ai_channel_config c = {MYDAQ_AI0, MYDAQ_AI_RANGE_10V};
            int16_t code;
            rc = mydaq_ai_read_scan_raw(h->dev, &c, 1, &code);
            if (rc == MYDAQ_OK && (code < -8115 || code > -8111))
                rc = MYDAQ_ERR_PROTOCOL;
        } else if (h->kind == 1) {
            uint8_t v;
            rc = mydaq_dio_read(h->dev, &v);
        } else {
            rc = mydaq_ao_write(h->dev, MYDAQ_AO1, 0.25);
        }
        if (rc != MYDAQ_OK)
            ++h->errors;
    }
}

static void test_concurrent_calls(void)
{
    emu e;
    hammer h[3];
    md_thread th[3];
    int i;
    EMU_OPEN(e);
    for (i = 0; i < 3; ++i) {
        h[i].dev = e.dev;
        h[i].kind = i;
        h[i].errors = 0;
        E_CHECK(e, md_thread_start(&th[i], hammer_main, &h[i]) == 0);
    }
    for (i = 0; i < 3; ++i)
        md_thread_join(th[i]);
    E_CHECK(e, h[0].errors == 0 && h[1].errors == 0 && h[2].errors == 0);
    emu_close(&e);
}

static void test_no_init_open(void)
{
    mydaq_context *ctx = NULL;
    mydaq_mock *m = mydaq_mock_new_emulator_file(test_data("calmem-036CD1C6.bin"));
    mydaq_device *dev = NULL;
    unsigned full, fast;
    CHECK(m);
    CHECK_RC(mydaq_context_new_no_usb(&ctx));
    mydaq_context_set_log(ctx, MYDAQ_LOG_NONE, NULL, NULL);
    CHECK_RC(mydaq_mock_open(ctx, m, 0, &dev));
    full = mydaq_mock_command_count(m);
    mydaq_close(dev);
    CHECK_RC(mydaq_mock_open(ctx, m, MYDAQ_OPEN_NO_INIT, &dev));
    fast = mydaq_mock_command_count(m) - full;
    mydaq_close(dev);
    mydaq_mock_free(m);
    mydaq_context_free(ctx);
    CHECK_INT(full, 24);
    CHECK_INT(fast, 8);
}

static void test_hwverify_retry(void)
{
    mydaq_context *ctx = NULL;
    mydaq_mock *m = mydaq_mock_new_emulator_file(test_data("calmem-036CD1C6.bin"));
    mydaq_device *dev = NULL;
    mydaq_info info;
    int rc;
    CHECK(m);
    CHECK_RC(mydaq_context_new_no_usb(&ctx));
    mydaq_context_set_log(ctx, MYDAQ_LOG_NONE, NULL, NULL);
    mydaq_mock_set_hwverify_busy(m, 4);   /* ~200 ms of "still starting" */
    rc = mydaq_mock_open(ctx, m, 0, &dev);
    if (rc == MYDAQ_OK)
        mydaq_get_info(dev, &info);
    mydaq_close(dev);
    CHECK_INT(mydaq_mock_command_count(m), 24 + 4);
    mydaq_mock_free(m);
    mydaq_context_free(ctx);
    CHECK_RC(rc);
    CHECK(info.init_seconds >= 0.15);
}

static void test_open_unplugged(void)
{
    mydaq_context *ctx = NULL;
    mydaq_mock *m = mydaq_mock_new_emulator_file(test_data("calmem-036CD1C6.bin"));
    mydaq_device *dev = (mydaq_device *)1;
    CHECK(m);
    CHECK_RC(mydaq_context_new_no_usb(&ctx));
    mydaq_context_set_log(ctx, MYDAQ_LOG_NONE, NULL, NULL);
    mydaq_mock_unplug(m);
    CHECK_ERR(mydaq_mock_open(ctx, m, 0, &dev), MYDAQ_ERR_NO_DEVICE);
    CHECK(dev == NULL);
    mydaq_mock_free(m);
    mydaq_context_free(ctx);
}

static void count_frames(void *user, mydaq_log_level level, const char *message)
{
    int *n = user;
    if (level == MYDAQ_LOG_TRACE && strncmp(message, ">> ", 3) == 0)
        ++*n;
}

static void test_log_callback(void)
{
    mydaq_context *ctx = NULL;
    mydaq_mock *m = mydaq_mock_new_emulator_file(test_data("calmem-036CD1C6.bin"));
    mydaq_device *dev = NULL;
    int frames = 0;
    CHECK(m);
    CHECK_RC(mydaq_context_new_no_usb(&ctx));
    mydaq_context_set_log(ctx, MYDAQ_LOG_TRACE, count_frames, &frames);
    CHECK_RC(mydaq_mock_open(ctx, m, 0, &dev));
    mydaq_close(dev);
    mydaq_mock_free(m);
    mydaq_context_free(ctx);
    CHECK_INT(frames, 24);   /* every init command was traced */
}

const test_case emulator_tests[] = {
    {"emu.open_info", test_open_info},
    {"emu.ai_on_demand", test_ai_on_demand},
    {"emu.ai_finite_layouts", test_ai_finite_layouts},
    {"emu.ai_realtime_timeout", test_ai_realtime_and_timeout},
    {"emu.ai_stop_wakes_reader", test_ai_stop_wakes_reader},
    {"emu.ai_reservation", test_ai_reservation},
    {"emu.ai_invalid_config", test_ai_invalid_config},
    {"emu.ai_unplug", test_ai_unplug},
    {"emu.ai_overflow_event", test_ai_overflow_event},
    {"emu.ai_continuous", test_ai_continuous},
    {"emu.ai_host_overflow", test_ai_host_overflow},
    {"emu.close_with_task", test_close_with_task},
    {"emu.ao", test_ao},
    {"emu.dio", test_dio},
    {"emu.dmm", test_dmm},
    {"emu.reset", test_reset},
    {"emu.concurrent_calls", test_concurrent_calls},
    {"emu.no_init_open", test_no_init_open},
    {"emu.log_callback", test_log_callback},
    {"emu.hwverify_retry", test_hwverify_retry},
    {"emu.open_unplugged", test_open_unplugged},
    {NULL, NULL},
};
