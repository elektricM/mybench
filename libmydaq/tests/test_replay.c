/* Replay tests: the library performs the same operation NI-DAQmx performed in a capture; the
 * mock checks every command byte for byte against the capture, answers with the captured
 * responses/samples, and the results must equal DAQmx's printed values bit-exactly. */
#include "test.h"

#include "mock/mydaq_mock.h"

#include <stdlib.h>

typedef struct replay {
    mydaq_context *ctx;
    mydaq_mock *mock;
    mydaq_device *dev;
} replay;

static int replay_open(replay *r, const char *script)
{
    int rc;
    memset(r, 0, sizeof *r);
    if (mydaq_context_new_no_usb(&r->ctx) != MYDAQ_OK)
        return 0;
    mydaq_context_set_log(r->ctx, MYDAQ_LOG_NONE, NULL, NULL);
    r->mock = mydaq_mock_new_replay(test_data(script));
    if (!r->mock) {
        fprintf(stderr, "    cannot load %s\n", script);
        return 0;
    }
    rc = mydaq_mock_open(r->ctx, r->mock, 0, &r->dev);
    if (rc != MYDAQ_OK) {
        fprintf(stderr, "    open: %s; %s\n", mydaq_error_name(rc),
                mydaq_mock_error(r->mock) ? mydaq_mock_error(r->mock) : "");
        return 0;
    }
    return 1;
}

/* Closes and reports whether the whole script was followed. */
static int replay_close(replay *r)
{
    int ok;
    const char *err;
    mydaq_close(r->dev);
    err = r->mock ? mydaq_mock_error(r->mock) : "no mock";
    ok = r->mock && !err && mydaq_mock_replay_finished(r->mock);
    if (err)
        fprintf(stderr, "    %s\n", err);
    else if (r->mock && !ok)
        fprintf(stderr, "    script not finished: commands of the capture were not sent\n");
    mydaq_mock_free(r->mock);
    mydaq_context_free(r->ctx);
    return ok;
}

#define REPLAY_OPEN(r, script)                                                                     \
    do {                                                                                           \
        if (!replay_open(&(r), (script))) {                                                        \
            test_fail(__FILE__, __LINE__, "replay of %s could not start", (script));               \
            replay_close(&(r));                                                                    \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define REPLAY_STEP(r, expr)                                                                       \
    do {                                                                                           \
        const int rc_ = (expr);                                                                    \
        if (rc_ != MYDAQ_OK) {                                                                     \
            test_fail(__FILE__, __LINE__, "%s returned %s", #expr, mydaq_error_name(rc_));        \
            replay_close(&(r));                                                                    \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define REPLAY_CHECK(r, cond)                                                                      \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            test_fail(__FILE__, __LINE__, "%s", #cond);                                            \
            replay_close(&(r));                                                                    \
            return;                                                                                \
        }                                                                                          \
    } while (0)

static void test_init(void)
{
    replay r;
    mydaq_info info;
    const mydaq_calibration *cal;
    REPLAY_OPEN(r, "idle-a.mock");
    REPLAY_STEP(r, mydaq_get_info(r.dev, &info));
    REPLAY_CHECK(r, info.identity[0] == 0x0200a000u && info.identity[1] == 0x1980c12du);
    REPLAY_CHECK(r, info.core_info[0] == 0xe0000007u);
    REPLAY_CHECK(r, info.stream_buffer_sizes[0] == 0x800 && info.stream_buffer_sizes[6] == 0x200);
    REPLAY_CHECK(r, info.serial_number == 0x036CD1C6u && strcmp(info.serial, "036CD1C6") == 0);
    cal = mydaq_get_calibration(r.dev);
    REPLAY_CHECK(r, cal && cal->checksums_ok && cal->backup_matches);
    CHECK(replay_close(&r));
}

/* On-demand AI: `reps` reads of the channels, compared with the DAQmx values. */
static void ai_static(const char *script, const mydaq_ai_channel_config *ch, size_t n, int reps)
{
    replay r;
    int k;
    REPLAY_OPEN(r, script);
    for (k = 0; k < reps; ++k) {
        double v[2], want[2];
        size_t i;
        REPLAY_STEP(r, mydaq_ai_read_scan(r.dev, ch, n, v));
        REPLAY_CHECK(r, mydaq_mock_script_values(r.mock, (size_t)k, want, 2) == n);
        for (i = 0; i < n; ++i) {
            if (v[i] != want[i]) {
                test_fail(__FILE__, __LINE__, "rep %d ch %zu: %.17g, DAQmx %.17g", k, i, v[i], want[i]);
                replay_close(&r);
                return;
            }
        }
    }
    CHECK(replay_close(&r));
}

static void test_ai0_r10(void)
{
    const mydaq_ai_channel_config c = {MYDAQ_AI0, MYDAQ_AI_RANGE_10V};
    ai_static("ai0-r10-static-x3.mock", &c, 1, 3);
}

static void test_ai0_r2(void)
{
    const mydaq_ai_channel_config c = {MYDAQ_AI0, MYDAQ_AI_RANGE_2V};
    ai_static("ai0-r2-static-x3.mock", &c, 1, 3);
}

static void test_ai1_r2(void)
{
    const mydaq_ai_channel_config c = {MYDAQ_AI1, MYDAQ_AI_RANGE_2V};
    ai_static("ai1-r2-static-x3.mock", &c, 1, 3);
}

static void test_ai01_r10(void)
{
    const mydaq_ai_channel_config c[2] = {{MYDAQ_AI0, MYDAQ_AI_RANGE_10V},
                                          {MYDAQ_AI1, MYDAQ_AI_RANGE_10V}};
    ai_static("ai01-r10-static-x2.mock", c, 2, 2);
}

static void test_ai_finite(void)
{
    replay r;
    mydaq_ai_config cfg;
    mydaq_ai_task *task = NULL;
    double v[100], want[100];
    size_t i;
    memset(&cfg, 0, sizeof cfg);
    cfg.channels[0].channel = MYDAQ_AI0;
    cfg.channels[0].range = MYDAQ_AI_RANGE_10V;
    cfg.num_channels = 1;
    cfg.sample_rate = 1000.0;
    cfg.mode = MYDAQ_FINITE;
    cfg.samples_per_channel = 100;
    REPLAY_OPEN(r, "ai0-r10-finite-1k-n100.mock");
    REPLAY_STEP(r, mydaq_ai_task_create(r.dev, &cfg, &task));
    REPLAY_CHECK(r, mydaq_ai_task_rate(task) == 1000.0);
    REPLAY_STEP(r, mydaq_ai_task_start(task));
    REPLAY_STEP(r, mydaq_ai_task_read(task, v, 100, MYDAQ_GROUP_BY_CHANNEL, 5000));
    REPLAY_CHECK(r, !mydaq_ai_task_is_running(task));
    REPLAY_CHECK(r, mydaq_mock_script_values(r.mock, 0, want, 100) == 100);
    for (i = 0; i < 100; ++i) {
        if (v[i] != want[i]) {
            test_fail(__FILE__, __LINE__, "sample %zu: %.17g, DAQmx %.17g", i, v[i], want[i]);
            mydaq_ai_task_destroy(task);
            replay_close(&r);
            return;
        }
    }
    mydaq_ai_task_destroy(task);
    CHECK(replay_close(&r));
}

/* Timed acquisition replay: `reads` reads of n samples per channel; the script's "=" lines hold
 * DAQmx's values per read and channel (ch0 line, ch1 line, ...). */
static void ai_timed(const char *script, const mydaq_ai_channel_config *ch, size_t nch,
                     double rate, mydaq_sample_mode mode, size_t n, int reads)
{
    replay r;
    mydaq_ai_config cfg;
    mydaq_ai_task *task = NULL;
    double *v = malloc(sizeof *v * n * nch), *want = malloc(sizeof *want * n);
    int k;
    size_t c, i;
    memset(&cfg, 0, sizeof cfg);
    for (c = 0; c < nch; ++c)
        cfg.channels[c] = ch[c];
    cfg.num_channels = nch;
    cfg.sample_rate = rate;
    cfg.mode = mode;
    cfg.samples_per_channel = n;
    if (!v || !want) {
        free(v);
        free(want);
        test_fail(__FILE__, __LINE__, "no memory");
        return;
    }
    if (!replay_open(&r, script)) {
        test_fail(__FILE__, __LINE__, "replay of %s could not start", script);
        replay_close(&r);
        free(v);
        free(want);
        return;
    }
    if (mydaq_ai_task_create(r.dev, &cfg, &task) != MYDAQ_OK || mydaq_ai_task_start(task) != MYDAQ_OK) {
        test_fail(__FILE__, __LINE__, "task create/start failed");
        goto done;
    }
    for (k = 0; k < reads; ++k) {
        const int rc = mydaq_ai_task_read(task, v, n, MYDAQ_GROUP_BY_CHANNEL, 5000);
        if (rc != MYDAQ_OK) {
            test_fail(__FILE__, __LINE__, "read %d: %s", k, mydaq_error_name(rc));
            goto done;
        }
        for (c = 0; c < nch; ++c) {
            if (mydaq_mock_script_values(r.mock, (size_t)k * nch + c, want, n) != n) {
                test_fail(__FILE__, __LINE__, "script has no values for read %d ch %zu", k, c);
                goto done;
            }
            for (i = 0; i < n; ++i) {
                if (v[c * n + i] != want[i]) {
                    test_fail(__FILE__, __LINE__, "read %d ch %zu sample %zu: %.17g, DAQmx %.17g", k,
                              c, i, v[c * n + i], want[i]);
                    goto done;
                }
            }
        }
    }
    mydaq_ai_task_stop(task);
done:
    mydaq_ai_task_destroy(task);
    if (!test_failed && !replay_close(&r))
        test_fail(__FILE__, __LINE__, "replay of %s incomplete", script);
    else if (test_failed)
        replay_close(&r);
    free(v);
    free(want);
}

static const mydaq_ai_channel_config ai01_r10[2] = {{MYDAQ_AI0, MYDAQ_AI_RANGE_10V},
                                                    {MYDAQ_AI1, MYDAQ_AI_RANGE_10V}};

static void test_ai01_finite(void)
{
    ai_timed("ai01-r10-finite-1k-n100.mock", ai01_r10, 2, 1000.0, MYDAQ_FINITE, 100, 1);
}

static void test_ai10_finite(void)
{
    /* channel order ai1, ai0: the scan list and the stream follow it */
    const mydaq_ai_channel_config c[2] = {{MYDAQ_AI1, MYDAQ_AI_RANGE_10V}, {MYDAQ_AI0, MYDAQ_AI_RANGE_10V}};
    ai_timed("ai10-r10-finite-1k-n100.mock", c, 2, 1000.0, MYDAQ_FINITE, 100, 1);
}

static void test_ai01_mixed_ranges(void)
{
    const mydaq_ai_channel_config c[2] = {{MYDAQ_AI0, MYDAQ_AI_RANGE_10V}, {MYDAQ_AI1, MYDAQ_AI_RANGE_2V}};
    ai_timed("ai01-r10r2-finite-1k-n100.mock", c, 2, 1000.0, MYDAQ_FINITE, 100, 1);
}

static void test_ai01_finite_200k(void)
{
    /* convert clock at the maximum rate: min(1250 ticks, divisor / channels) = 250 */
    ai_timed("ai01-r10-finite-200k-n2000.mock", ai01_r10, 2, 200e3, MYDAQ_FINITE, 2000, 1);
}

static void test_ai01_continuous(void)
{
    /* three reads of 5000 samples per channel at 10 kS/s, then stop */
    ai_timed("ai01-r10-cont-10k-n5000.mock", ai01_r10, 2, 10e3, MYDAQ_CONTINUOUS, 5000, 3);
}

static void ao_static(const char *script, mydaq_ao_channel ch, double volts)
{
    replay r;
    REPLAY_OPEN(r, script);
    REPLAY_STEP(r, mydaq_ao_write(r.dev, ch, volts));
    CHECK(replay_close(&r));
}

static void test_ao0_p05(void) { ao_static("ao0-static-p0.5.mock", MYDAQ_AO0, 0.5); }
static void test_ao0_m05(void) { ao_static("ao0-static-m0.5.mock", MYDAQ_AO0, -0.5); }
static void test_ao1_p05(void) { ao_static("ao1-static-p0.5.mock", MYDAQ_AO1, 0.5); }

static void test_di_read(void)
{
    replay r;
    uint8_t levels = 0xaa;
    REPLAY_OPEN(r, "di-port0-read.mock");
    REPLAY_STEP(r, mydaq_dio_set_direction(r.dev, 0xff, 0));
    REPLAY_STEP(r, mydaq_dio_read(r.dev, &levels));
    REPLAY_CHECK(r, levels == 0x00);   /* open inputs, as DAQmx read */
    CHECK(replay_close(&r));
}

static void test_do_write(void)
{
    /* DAQmx's DO sequence: direction (no lines), read, preset 0, lines to output, drive value */
    replay r;
    uint8_t levels, dir;
    REPLAY_OPEN(r, "do-port0-write-0x55.mock");
    REPLAY_STEP(r, mydaq_dio_set_direction(r.dev, 0x00, 0));
    REPLAY_STEP(r, mydaq_dio_read(r.dev, &levels));
    REPLAY_STEP(r, mydaq_dio_write(r.dev, 0x00, 0xff));
    REPLAY_STEP(r, mydaq_dio_set_direction(r.dev, 0xff, 1));
    REPLAY_STEP(r, mydaq_dio_write(r.dev, 0x55, 0xff));
    REPLAY_STEP(r, mydaq_dio_get_direction(r.dev, &dir));
    REPLAY_CHECK(r, dir == 0xff);
    CHECK(replay_close(&r));
}

/* DAQmx's DO sequence on a subset of lines: direction (none), read, preset 0, output, drive. */
static int do_lines(replay *r, uint8_t mask, uint8_t value)
{
    uint8_t levels;
    return mydaq_dio_set_direction(r->dev, 0x00, 0) == MYDAQ_OK &&
           mydaq_dio_read(r->dev, &levels) == MYDAQ_OK &&
           mydaq_dio_write(r->dev, 0x00, mask) == MYDAQ_OK &&
           mydaq_dio_set_direction(r->dev, mask, 1) == MYDAQ_OK &&
           mydaq_dio_write(r->dev, value, mask) == MYDAQ_OK;
}

static void test_do_lines_0_3(void)
{
    replay r;
    REPLAY_OPEN(r, "do-line0-3-write-0x5.mock");
    REPLAY_CHECK(r, do_lines(&r, 0x0f, 0x05));
    CHECK(replay_close(&r));
}

static void test_di_lines_4_7(void)
{
    replay r;
    uint8_t levels = 0xff;
    REPLAY_OPEN(r, "di-line4-7-read.mock");
    REPLAY_STEP(r, mydaq_dio_set_direction(r.dev, 0xf0, 0));
    REPLAY_STEP(r, mydaq_dio_read(r.dev, &levels));
    REPLAY_CHECK(r, (levels & 0xf0) == 0x00);
    CHECK(replay_close(&r));
}

static void test_do_then_di(void)
{
    replay r;
    uint8_t levels = 0xff;
    REPLAY_OPEN(r, "do-line0-3-writeread-0xa.mock");
    REPLAY_CHECK(r, do_lines(&r, 0x0f, 0x0a));
    REPLAY_STEP(r, mydaq_dio_set_direction(r.dev, 0xff, 0));
    REPLAY_STEP(r, mydaq_dio_read(r.dev, &levels));
    CHECK(replay_close(&r));
}

static void dmm_function(const char *script, mydaq_dmm_function f, double range, int reps);
static void dmm_dcv(const char *script, double range, int reps)
{
    dmm_function(script, MYDAQ_DMM_DC_VOLTS, range, reps);
}

static void dmm_function(const char *script, mydaq_dmm_function f, double range, int reps)
{
    replay r;
    int k;
    REPLAY_OPEN(r, script);
    for (k = 0; k < reps; ++k) {
        mydaq_dmm_reading reading;
        double want;
        REPLAY_STEP(r, mydaq_dmm_read(r.dev, f, range, &reading));
        REPLAY_CHECK(r, mydaq_mock_script_values(r.mock, (size_t)k, &want, 1) == 1);
        if (want != want) {   /* DAQmx returns NaN for an overload (leads open on Ohm ranges) */
            REPLAY_CHECK(r, reading.overload != 0);
            continue;
        }
        if (reading.value != want) {
            test_fail(__FILE__, __LINE__, "rep %d: %.17g, DAQmx %.17g", k, reading.value, want);
            replay_close(&r);
            return;
        }
    }
    CHECK(replay_close(&r));
}

static void test_dmm_20(void) { dmm_dcv("dmm-dcv-r20.mock", 20.0, 2); }
static void test_dmm_2(void) { dmm_dcv("dmm-dcv-r2-x2.mock", 2.0, 2); }
static void test_dmm_02(void) { dmm_dcv("dmm-dcv-r0.2-x2.mock", 0.2, 2); }
static void test_dmm_60(void) { dmm_dcv("dmm-dcv-r60-x2.mock", 60.0, 2); }

static void test_dmm_acv_20(void) { dmm_function("dmm-acv-r20-x2.mock", MYDAQ_DMM_AC_VOLTS, 20.0, 2); }
static void test_dmm_acv_2(void) { dmm_function("dmm-acv-r2-x2.mock", MYDAQ_DMM_AC_VOLTS, 2.0, 2); }
static void test_dmm_acv_02(void) { dmm_function("dmm-acv-r0.2-x2.mock", MYDAQ_DMM_AC_VOLTS, 0.2, 2); }

static void test_dmm_dca_20m(void) { dmm_function("dmm-dca-r0.02-x2.mock", MYDAQ_DMM_DC_CURRENT, 0.02, 2); }
static void test_dmm_dca_200m(void) { dmm_function("dmm-dca-r0.2-x2.mock", MYDAQ_DMM_DC_CURRENT, 0.2, 2); }
static void test_dmm_dca_1(void) { dmm_function("dmm-dca-r1-x2.mock", MYDAQ_DMM_DC_CURRENT, 1.0, 2); }
static void test_dmm_aca_20m(void) { dmm_function("dmm-aca-r0.02-x2.mock", MYDAQ_DMM_AC_CURRENT, 0.02, 2); }
static void test_dmm_aca_200m(void) { dmm_function("dmm-aca-r0.2-x2.mock", MYDAQ_DMM_AC_CURRENT, 0.2, 2); }
static void test_dmm_aca_1(void) { dmm_function("dmm-aca-r1-x2.mock", MYDAQ_DMM_AC_CURRENT, 1.0, 2); }
static void test_dmm_ohm_200(void) { dmm_function("dmm-ohm-r200-x2.mock", MYDAQ_DMM_RESISTANCE, 200.0, 2); }
static void test_dmm_ohm_2k(void) { dmm_function("dmm-ohm-r2k-x2.mock", MYDAQ_DMM_RESISTANCE, 2e3, 2); }

static void test_mismatch_detected(void)
{
    /* The mock must catch a command that differs from the capture. */
    replay r;
    const mydaq_ai_channel_config c = {MYDAQ_AI0, MYDAQ_AI_RANGE_2V};   /* capture used ±10 V */
    double v;
    REPLAY_OPEN(r, "ai0-r10-static-x3.mock");
    REPLAY_CHECK(r, mydaq_ai_read_scan(r.dev, &c, 1, &v) != MYDAQ_OK);
    REPLAY_CHECK(r, mydaq_mock_error(r.mock) != NULL);
    mydaq_close(r.dev);
    mydaq_mock_free(r.mock);
    mydaq_context_free(r.ctx);
}

const test_case replay_tests[] = {
    {"replay.init", test_init},
    {"replay.ai0_r10_static", test_ai0_r10},
    {"replay.ai0_r2_static", test_ai0_r2},
    {"replay.ai1_r2_static", test_ai1_r2},
    {"replay.ai01_r10_static", test_ai01_r10},
    {"replay.ai0_finite", test_ai_finite},
    {"replay.ai01_finite", test_ai01_finite},
    {"replay.ai10_finite", test_ai10_finite},
    {"replay.ai01_mixed_ranges", test_ai01_mixed_ranges},
    {"replay.ai01_finite_200k", test_ai01_finite_200k},
    {"replay.ai01_continuous", test_ai01_continuous},
    {"replay.ao0_p0.5", test_ao0_p05},
    {"replay.ao0_m0.5", test_ao0_m05},
    {"replay.ao1_p0.5", test_ao1_p05},
    {"replay.di_read", test_di_read},
    {"replay.do_write", test_do_write},
    {"replay.do_lines_0_3", test_do_lines_0_3},
    {"replay.di_lines_4_7", test_di_lines_4_7},
    {"replay.do_then_di", test_do_then_di},
    {"replay.dmm_dcv_20", test_dmm_20},
    {"replay.dmm_dcv_2", test_dmm_2},
    {"replay.dmm_dcv_0.2", test_dmm_02},
    {"replay.dmm_dcv_60", test_dmm_60},
    {"replay.dmm_acv_20", test_dmm_acv_20},
    {"replay.dmm_acv_2", test_dmm_acv_2},
    {"replay.dmm_acv_0.2", test_dmm_acv_02},
    {"replay.dmm_dca_0.02", test_dmm_dca_20m},
    {"replay.dmm_dca_0.2", test_dmm_dca_200m},
    {"replay.dmm_dca_1", test_dmm_dca_1},
    {"replay.dmm_aca_0.02", test_dmm_aca_20m},
    {"replay.dmm_aca_0.2", test_dmm_aca_200m},
    {"replay.dmm_aca_1", test_dmm_aca_1},
    {"replay.dmm_ohm_200", test_dmm_ohm_200},
    {"replay.dmm_ohm_2k", test_dmm_ohm_2k},
    {"replay.mismatch_detected", test_mismatch_detected},
    {NULL, NULL},
};
