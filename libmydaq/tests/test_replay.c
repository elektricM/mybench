/* Replay tests: the library performs the same operation NI-DAQmx performed in a capture; the
 * mock checks every command byte for byte against the capture, answers with the captured
 * responses/samples, and the results must equal DAQmx's printed values bit-exactly. */
#include "test.h"

#include "mock/mydaq_mock.h"
#include "platform.h"

#include <stdlib.h>

typedef struct replay {
    mydaq_context *ctx;
    mydaq_mock *mock;
    mydaq_device *dev;
} replay;

/* 1: open the mock without asynchronous OUT streams (the library's synchronous AO writer). */
static int g_sync_out;
/* AO range of the timed AO replays (ao_timed). */
static mydaq_ao_range g_ao_range;

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
    mydaq_mock_set_sync_out(r->mock, g_sync_out);
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

static void test_audio_in_lr(void)
{
    const mydaq_ai_channel_config c[2] = {{MYDAQ_AUDIO_IN_LEFT, MYDAQ_AI_RANGE_2V},
                                          {MYDAQ_AUDIO_IN_RIGHT, MYDAQ_AI_RANGE_2V}};
    ai_timed("audioLR-finite-48k-n480.mock", c, 2, 48000.0, MYDAQ_FINITE, 480, 1);
}

static void test_audio_in_l(void)
{
    const mydaq_ai_channel_config c[1] = {{MYDAQ_AUDIO_IN_LEFT, MYDAQ_AI_RANGE_2V}};
    ai_timed("audioL-finite-48k-n480-ac.mock", c, 1, 48000.0, MYDAQ_FINITE, 480, 1);
}

/* Timed AO replay: writes DAQmx's waveform ("=" lines 0.., one per channel), starts, then
 * finite: waits for completion; continuous: runs `run_ms` (streaming: keeps appending the
 * waveform in quarter blocks) and stops. The mock compares every command and the OUT stream. */
static void ao_timed(const char *script, const mydaq_ao_channel *ch, size_t nch, double rate,
                     mydaq_sample_mode mode, int regenerate, size_t n, int run_ms)
{
    replay r;
    mydaq_ao_config cfg;
    mydaq_ao_task *task = NULL;
    double *wave = malloc(sizeof *wave * n * nch);
    size_t c;
    int rc;
    memset(&cfg, 0, sizeof cfg);
    for (c = 0; c < nch; ++c) {
        cfg.channels[c] = ch[c];
        cfg.ranges[c] = g_ao_range;
    }
    cfg.num_channels = nch;
    cfg.sample_rate = rate;
    cfg.mode = mode;
    cfg.regenerate = regenerate;
    cfg.buffer_samples = 4 * n;
    if (!wave) {
        test_fail(__FILE__, __LINE__, "no memory");
        return;
    }
    if (!replay_open(&r, script)) {
        test_fail(__FILE__, __LINE__, "replay of %s could not start", script);
        replay_close(&r);
        free(wave);
        return;
    }
    for (c = 0; c < nch; ++c) {
        if (mydaq_mock_script_values(r.mock, c, wave + c * n, n) != n) {
            test_fail(__FILE__, __LINE__, "no waveform for channel %zu in %s", c, script);
            goto done;
        }
    }
    rc = mydaq_ao_task_create(r.dev, &cfg, &task);
    if (rc == MYDAQ_OK)
        rc = mydaq_ao_task_write(task, wave, n, MYDAQ_GROUP_BY_CHANNEL, 1000);
    if (rc == MYDAQ_OK)
        rc = mydaq_ao_task_start(task);
    if (rc != MYDAQ_OK) {
        test_fail(__FILE__, __LINE__, "AO create/write/start: %s", mydaq_error_name(rc));
        goto done;
    }
    if (mode == MYDAQ_FINITE) {
        rc = mydaq_ao_task_wait_done(task, 5000);
        if (rc != MYDAQ_OK) {
            test_fail(__FILE__, __LINE__, "wait_done: %s", mydaq_error_name(rc));
            goto done;
        }
        if (mydaq_mock_ao_bytes(r.mock) != mydaq_mock_ao_expected(r.mock) ||
            mydaq_mock_ao_extra(r.mock) != 0) {
            test_fail(__FILE__, __LINE__, "AO stream: sent %llu bytes, capture %llu",
                      (unsigned long long)mydaq_mock_ao_bytes(r.mock),
                      (unsigned long long)mydaq_mock_ao_expected(r.mock));
            goto done;
        }
    } else {
        const uint64_t end = md_now_ms() + (uint64_t)run_ms;
        while (md_now_ms() < end) {
            if (!regenerate) {   /* keep streaming the waveform, a quarter at a time */
                size_t q;
                for (q = 0; q < 4; ++q) {
                    double *block = malloc(sizeof *block * (n / 4) * nch);
                    size_t k;
                    for (c = 0; c < nch && block; ++c)
                        for (k = 0; k < n / 4; ++k)
                            block[c * (n / 4) + k] = wave[c * n + q * (n / 4) + k];
                    rc = block ? mydaq_ao_task_write(task, block, n / 4, MYDAQ_GROUP_BY_CHANNEL, 1000)
                               : MYDAQ_ERR_NO_MEM;
                    free(block);
                    if (rc != MYDAQ_OK) {
                        test_fail(__FILE__, __LINE__, "stream write: %s", mydaq_error_name(rc));
                        goto done;
                    }
                }
            } else {
                md_sleep_ms(5);
            }
        }
        if (mydaq_mock_ao_bytes(r.mock) < mydaq_mock_ao_expected(r.mock) / 4) {
            test_fail(__FILE__, __LINE__, "AO stream: only %llu bytes sent",
                      (unsigned long long)mydaq_mock_ao_bytes(r.mock));
            goto done;
        }
        rc = mydaq_ao_task_stop(task);
        if (rc != MYDAQ_OK) {
            test_fail(__FILE__, __LINE__, "stop: %s", mydaq_error_name(rc));
            goto done;
        }
    }
done:
    mydaq_ao_task_destroy(task);
    if (!test_failed && !replay_close(&r))
        test_fail(__FILE__, __LINE__, "replay of %s incomplete", script);
    else if (test_failed)
        replay_close(&r);
    free(wave);
}

static const mydaq_ao_channel ao0[1] = {MYDAQ_AO0};
static const mydaq_ao_channel ao1[1] = {MYDAQ_AO1};
static const mydaq_ao_channel ao01[2] = {MYDAQ_AO0, MYDAQ_AO1};
static const mydaq_ao_channel audio_lr[2] = {MYDAQ_AUDIO_OUT_LEFT, MYDAQ_AUDIO_OUT_RIGHT};

static void test_ao0_finite(void) { ao_timed("ao0-finite-1k-n100-ramp.mock", ao0, 1, 1000.0, MYDAQ_FINITE, 0, 100, 0); }
static void test_ao1_finite(void) { ao_timed("ao1-finite-1k-n100-ramp.mock", ao1, 1, 1000.0, MYDAQ_FINITE, 0, 100, 0); }
static void test_ao01_finite(void) { ao_timed("ao01-finite-1k-n100-ramp.mock", ao01, 2, 1000.0, MYDAQ_FINITE, 0, 100, 0); }
static void test_ao0_finite_10k(void) { ao_timed("ao0-finite-10k-n1000-sine.mock", ao0, 1, 10e3, MYDAQ_FINITE, 0, 1000, 0); }
static void test_ao0_finite_200k(void) { ao_timed("ao0-finite-200k-n2000-sine.mock", ao0, 1, 200e3, MYDAQ_FINITE, 0, 2000, 0); }
static void test_audio_out_finite(void) { ao_timed("audiooutLR-finite-48k-n480-sine.mock", audio_lr, 2, 48000.0, MYDAQ_FINITE, 0, 480, 0); }
static void test_ao0_regen(void) { ao_timed("ao0-cont-1k-n100-regen.mock", ao0, 1, 1000.0, MYDAQ_CONTINUOUS, 1, 100, 100); }
static void test_ao01_regen(void) { ao_timed("ao01-cont-10k-n1000-regen.mock", ao01, 2, 10e3, MYDAQ_CONTINUOUS, 1, 1000, 100); }
static void test_ao0_stream(void) { ao_timed("ao0-cont-10k-n1000-stream.mock", ao0, 1, 10e3, MYDAQ_CONTINUOUS, 0, 1000, 100); }
static void test_ao01_stream(void) { ao_timed("ao01-cont-10k-n1000-stream.mock", ao01, 2, 10e3, MYDAQ_CONTINUOUS, 0, 1000, 100); }

/* The same AO replays through the synchronous writer (transports without stream_open_out): both
 * paths must send the captured byte stream. */
static void sync_out(void (*test)(void))
{
    g_sync_out = 1;
    test();
    g_sync_out = 0;
}
static void test_ao0_finite_sync(void) { sync_out(test_ao0_finite); }
static void test_ao01_finite_sync(void) { sync_out(test_ao01_finite); }
static void test_ao0_finite_200k_sync(void) { sync_out(test_ao0_finite_200k); }
static void test_audio_out_finite_sync(void) { sync_out(test_audio_out_finite); }
static void test_ao01_regen_sync(void) { sync_out(test_ao01_regen); }
static void test_ao01_stream_sync(void) { sync_out(test_ao01_stream); }

/* ---- counter / FREQOUT ---- */

static void test_ctr_count_edges(void)
{
    replay r;
    mydaq_ctr_config cfg;
    mydaq_ctr_task *t = NULL;
    uint32_t count = 99;
    memset(&cfg, 0, sizeof cfg);
    cfg.kind = MYDAQ_CTR_COUNT_EDGES;
    REPLAY_OPEN(r, "ctr-countedges-pfi0-1s.mock");
    REPLAY_STEP(r, mydaq_ctr_task_create(r.dev, &cfg, &t));
    REPLAY_STEP(r, mydaq_ctr_task_start(t));
    REPLAY_STEP(r, mydaq_ctr_task_read_count(t, &count, 10015));
    REPLAY_CHECK(r, count == 0);   /* nothing connected to DIO0 */
    REPLAY_STEP(r, mydaq_ctr_task_stop(t));
    mydaq_ctr_task_destroy(t);
    CHECK(replay_close(&r));
}

static void test_ctr_frequency_timeout(void)
{
    /* nothing on DIO1: the device answers the read with its timeout status after 10 s */
    replay r;
    mydaq_ctr_config cfg;
    mydaq_ctr_task *t = NULL;
    double f = 0;
    memset(&cfg, 0, sizeof cfg);
    cfg.kind = MYDAQ_CTR_FREQUENCY;
    REPLAY_OPEN(r, "ctr-cifreq-pfi1-timeout.mock");
    REPLAY_STEP(r, mydaq_ctr_task_create(r.dev, &cfg, &t));
    REPLAY_STEP(r, mydaq_ctr_task_start(t));
    REPLAY_CHECK(r, mydaq_ctr_task_read(t, &f, 10015) == MYDAQ_ERR_TIMEOUT);
    REPLAY_STEP(r, mydaq_ctr_task_stop(t));
    mydaq_ctr_task_destroy(t);
    CHECK(replay_close(&r));
}

/* Counter inputs on the internal FREQOUT loop (ctrloop-*: FREQOUT 10 kHz started after the
 * counter, stopped before the read) or on the open terminals / timebases (protocol.md 10.5). */
static void ctr_loop(const char *script, const mydaq_ctr_config *cfg, uint32_t expect)
{
    replay r;
    mydaq_ctr_task *t = NULL;
    uint32_t count = 0;
    double hz = 0;
    REPLAY_OPEN(r, script);
    REPLAY_STEP(r, mydaq_ctr_task_create(r.dev, cfg, &t));
    REPLAY_STEP(r, mydaq_ctr_task_start(t));
    REPLAY_STEP(r, mydaq_freqout_start(r.dev, 10e3, &hz));
    REPLAY_STEP(r, mydaq_freqout_stop(r.dev));
    REPLAY_STEP(r, mydaq_ctr_task_read_count(t, &count, 10015));
    REPLAY_CHECK(r, count == expect);
    REPLAY_STEP(r, mydaq_ctr_task_stop(t));
    mydaq_ctr_task_destroy(t);
    CHECK(replay_close(&r));
}

static mydaq_ctr_config edges_on(mydaq_ctr_input input)
{
    mydaq_ctr_config c;
    memset(&c, 0, sizeof c);
    c.kind = MYDAQ_CTR_COUNT_EDGES;
    c.input = input;
    return c;
}

static void test_ctrloop_rising(void)
{
    const mydaq_ctr_config c = edges_on(MYDAQ_CTR_INPUT_FREQOUT);
    ctr_loop("ctrloop-freqout-10k.mock", &c, 10567);
}

static void test_ctrloop_falling(void)
{
    mydaq_ctr_config c = edges_on(MYDAQ_CTR_INPUT_FREQOUT);
    c.falling = 1;
    ctr_loop("ctrloop-falling-10k.mock", &c, 0x2ea5);
}

static void test_ctrloop_initial_count(void)
{
    mydaq_ctr_config c = edges_on(MYDAQ_CTR_INPUT_FREQOUT);
    c.initial_count = 1000;
    ctr_loop("ctrloop-init1000-10k.mock", &c, 0x72eb);
}

static void test_ctrloop_reset_dio1(void)
{
    mydaq_ctr_config c = edges_on(MYDAQ_CTR_INPUT_FREQOUT);
    c.reset_on_dio1 = 1;
    ctr_loop("ctrloop-resetpfi1-10k.mock", &c, 0x374a);
}

static void ctr_timebase(const char *script, mydaq_ctr_input input, uint32_t expect)
{
    replay r;
    const mydaq_ctr_config c = edges_on(input);
    mydaq_ctr_task *t = NULL;
    uint32_t count = 0;
    REPLAY_OPEN(r, script);
    REPLAY_STEP(r, mydaq_ctr_task_create(r.dev, &c, &t));
    REPLAY_CHECK(r, mydaq_ctr_task_dio_lines(t) == 0);   /* no terminal */
    REPLAY_STEP(r, mydaq_ctr_task_start(t));
    REPLAY_STEP(r, mydaq_ctr_task_read_count(t, &count, 10015));
    REPLAY_CHECK(r, count == expect);
    REPLAY_STEP(r, mydaq_ctr_task_stop(t));
    mydaq_ctr_task_destroy(t);
    CHECK(replay_close(&r));
}

static void test_ctr_edges_20mhz(void) { ctr_timebase("ctr-countedges-20mhz-1s.mock", MYDAQ_CTR_INPUT_20MHZ, 0x0206cb74); }
static void test_ctr_edges_100khz(void) { ctr_timebase("ctr-countedges-100khz-1s.mock", MYDAQ_CTR_INPUT_100KHZ, 0x22e7b); }

/* Measurements with FREQOUT (10 kHz) started first and stopped last (ctr-*-freqout-10k,
 * ctr-*enc-*). */
static void ctr_measure(const char *script, const mydaq_ctr_config *c, uint32_t expect, uint32_t expect2)
{
    replay r;
    mydaq_ctr_task *t = NULL;
    uint32_t v = 0, v2 = 0;
    double hz = 0, value = 0;
    REPLAY_OPEN(r, script);
    REPLAY_STEP(r, mydaq_freqout_start(r.dev, 10e3, &hz));
    REPLAY_STEP(r, mydaq_ctr_task_create(r.dev, c, &t));
    REPLAY_STEP(r, mydaq_ctr_task_start(t));
    if (c->kind == MYDAQ_CTR_PULSE_TIME) {
        REPLAY_STEP(r, mydaq_ctr_task_read_pulse_time(t, &v, &v2, 10015));
        REPLAY_CHECK(r, v == expect && v2 == expect2);
    } else {
        REPLAY_STEP(r, mydaq_ctr_task_read(t, &value, 10015));
        REPLAY_CHECK(r, c->kind == MYDAQ_CTR_PULSE_WIDTH || c->kind == MYDAQ_CTR_TWO_EDGE_SEPARATION ||
                                c->kind == MYDAQ_CTR_PERIOD
                            ? value == expect / 1e8
                        : c->kind == MYDAQ_CTR_FREQUENCY ? value == 1e8 / expect
                                                         : value == (double)expect);
    }
    REPLAY_STEP(r, mydaq_ctr_task_stop(t));
    mydaq_ctr_task_destroy(t);
    REPLAY_STEP(r, mydaq_freqout_stop(r.dev));
    CHECK(replay_close(&r));
}

static mydaq_ctr_config measure(mydaq_ctr_kind kind, mydaq_ctr_input input)
{
    mydaq_ctr_config c;
    memset(&c, 0, sizeof c);
    c.kind = kind;
    c.input = input;
    return c;
}

static void test_ctr_pulse_width_falling(void)
{
    mydaq_ctr_config c = measure(MYDAQ_CTR_PULSE_WIDTH, MYDAQ_CTR_INPUT_FREQOUT);
    c.falling = 1;   /* the low time: 50 us at 10 kHz */
    ctr_measure("ctr-pw-falling-freqout-10k.mock", &c, 5000, 0);
}

static void test_ctr_two_edge(void)
{
    mydaq_ctr_config c = measure(MYDAQ_CTR_TWO_EDGE_SEPARATION, MYDAQ_CTR_INPUT_FREQOUT);
    c.second_falling = 1;   /* rising -> falling: 50 us at 10 kHz */
    ctr_measure("ctr-twoedge-freqout-10k.mock", &c, 5000, 0);
}

/* frequency / period of the internal loop (10000 ticks at 10 kHz) */
static void test_ctr_freq_loop(void)
{
    const mydaq_ctr_config c = measure(MYDAQ_CTR_FREQUENCY, MYDAQ_CTR_INPUT_FREQOUT);
    ctr_measure("ctr-freq-freqout-10k.mock", &c, 10000, 0);
}
static void test_ctr_period_loop(void)
{
    const mydaq_ctr_config c = measure(MYDAQ_CTR_PERIOD, MYDAQ_CTR_INPUT_FREQOUT);
    ctr_measure("ctr-period-freqout-10k.mock", &c, 10000, 0);
}

static void test_ctr_pulse_time(void)
{
    const mydaq_ctr_config c = measure(MYDAQ_CTR_PULSE_TIME, MYDAQ_CTR_INPUT_FREQOUT);
    ctr_measure("ctr-pulsetime-freqout-10k.mock", &c, 5000, 5000);
}

static void encoder(const char *script, mydaq_ctr_kind kind, mydaq_ctr_input input,
                    mydaq_ctr_decoding decoding, int z, uint32_t expect)
{
    mydaq_ctr_config c = measure(kind, input);
    c.decoding = decoding;
    c.z_index = z;
    ctr_measure(script, &c, expect, 0);
}

static void test_ctr_linenc_x4(void) { encoder("ctr-linenc-x4.mock", MYDAQ_CTR_LINEAR_ENCODER, MYDAQ_CTR_INPUT_TERMINAL, MYDAQ_CTR_X4, 0, 0); }
static void test_ctr_linenc_x1_z(void) { encoder("ctr-linenc-x1-z.mock", MYDAQ_CTR_LINEAR_ENCODER, MYDAQ_CTR_INPUT_TERMINAL, MYDAQ_CTR_X1, 1, 0); }
static void test_ctr_angenc_x2(void) { encoder("ctr-angenc-x2.mock", MYDAQ_CTR_ANGULAR_ENCODER, MYDAQ_CTR_INPUT_TERMINAL, MYDAQ_CTR_X2, 0, 0); }
static void test_ctr_angenc_2pulse(void) { encoder("ctr-angenc-2pulse-freqout-10k.mock", MYDAQ_CTR_ANGULAR_ENCODER, MYDAQ_CTR_INPUT_FREQOUT, MYDAQ_CTR_TWO_PULSE, 0, 0x2cb3); }

static void pulse_train_ex(const char *script, double f, double duty, mydaq_sample_mode mode,
                           uint64_t pulses, int idle_high, double delay)
{
    replay r;
    mydaq_ctr_config cfg;
    mydaq_ctr_task *t = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.kind = MYDAQ_CTR_PULSE_TRAIN;
    cfg.frequency = f;
    cfg.duty_cycle = duty;
    cfg.mode = mode;
    cfg.pulses = pulses;
    cfg.idle_high = idle_high;
    cfg.initial_delay = delay;
    REPLAY_OPEN(r, script);
    REPLAY_STEP(r, mydaq_ctr_task_create(r.dev, &cfg, &t));
    REPLAY_STEP(r, mydaq_ctr_task_start(t));
    if (mode == MYDAQ_FINITE)
        REPLAY_STEP(r, mydaq_ctr_task_wait_done(t, 5000));
    else
        REPLAY_STEP(r, mydaq_ctr_task_stop(t));
    mydaq_ctr_task_destroy(t);
    CHECK(replay_close(&r));
}

static void pulse_train(const char *script, double f, double duty, mydaq_sample_mode mode,
                        uint64_t pulses)
{
    pulse_train_ex(script, f, duty, mode, pulses, 0, 0.0);
}

static void test_co_delay(void) { pulse_train_ex("co-ptrain-1k-d0.25-delay10ms.mock", 1000.0, 0.25, MYDAQ_CONTINUOUS, 0, 0, 10e-3); }
static void test_co_idle_high(void) { pulse_train_ex("co-ptrain-1k-d0.25-idlehigh.mock", 1000.0, 0.25, MYDAQ_CONTINUOUS, 0, 1, 0.0); }
static void test_co_finite_delay(void) { pulse_train_ex("co-finite-10-1k-d0.25-delay5ms.mock", 1000.0, 0.25, MYDAQ_FINITE, 10, 0, 5e-3); }
/* live update of a running train: 1 kHz 50 % -> 2 kHz 25 % (ctr.1e + ctr.18(37500, 12500)) */
static void test_co_update(void)
{
    replay r;
    mydaq_ctr_config cfg;
    mydaq_ctr_task *t = NULL;
    double f = 0, d = 0;
    memset(&cfg, 0, sizeof cfg);
    cfg.kind = MYDAQ_CTR_PULSE_TRAIN;
    cfg.frequency = 1000.0;
    cfg.duty_cycle = 0.5;
    cfg.mode = MYDAQ_CONTINUOUS;
    REPLAY_OPEN(r, "co-update-1k-to-2k-d0.25.mock");
    REPLAY_STEP(r, mydaq_ctr_task_create(r.dev, &cfg, &t));
    REPLAY_CHECK(r, mydaq_ctr_task_update_pulse(t, 2000.0, 0.25) == MYDAQ_ERR_STATE);   /* not running */
    REPLAY_STEP(r, mydaq_ctr_task_start(t));
    REPLAY_STEP(r, mydaq_ctr_task_update_pulse(t, 2000.0, 0.25));
    REPLAY_STEP(r, mydaq_ctr_task_pulse(t, &f, &d));
    REPLAY_CHECK(r, f == 2000.0 && d == 0.25);
    REPLAY_STEP(r, mydaq_ctr_task_stop(t));
    mydaq_ctr_task_destroy(t);
    CHECK(replay_close(&r));
}
static void test_co_1k(void) { pulse_train("co-ptrain-1k-d0.5.mock", 1000.0, 0.5, MYDAQ_CONTINUOUS, 0); }
static void test_co_10k(void) { pulse_train("co-ptrain-10k-d0.25.mock", 10e3, 0.25, MYDAQ_CONTINUOUS, 0); }
static void test_co_finite(void) { pulse_train("co-finite-100-1k.mock", 1000.0, 0.5, MYDAQ_FINITE, 100); }

static void freqout(const char *script, double hz)
{
    replay r;
    double actual = 0;
    REPLAY_OPEN(r, script);
    REPLAY_STEP(r, mydaq_freqout_start(r.dev, hz, &actual));
    REPLAY_CHECK(r, actual == hz);
    REPLAY_STEP(r, mydaq_freqout_stop(r.dev));
    CHECK(replay_close(&r));
}

static void test_freqout_10k(void) { freqout("freqout-10k.mock", 10e3); }
static void test_freqout_100k(void) { freqout("freqout-100k.mock", 100e3); }
/* the other timebases and n = 16 (divider field 0) */
static void test_freqout_4M(void) { freqout("freqout-4M.mock", 4e6); }
static void test_freqout_2M(void) { freqout("freqout-2M.mock", 2e6); }
static void test_freqout_1M25(void) { freqout("freqout-1M25.mock", 1.25e6); }
static void test_freqout_1M(void) { freqout("freqout-1M.mock", 1e6); }
static void test_freqout_6250(void) { freqout("freqout-6250.mock", 6250.0); }

static void ao_static(const char *script, mydaq_ao_channel ch, double volts)
{
    replay r;
    REPLAY_OPEN(r, script);
    REPLAY_STEP(r, mydaq_ao_write(r.dev, ch, volts));
    CHECK(replay_close(&r));
}

static void test_ao0_p05(void) { ao_static("ao0-static-p0.5.mock", MYDAQ_AO0, 0.5); }
/* ±2 V range (the ±2 V calibration record: 0.5 V -> 7226 on ao0, -0.5 V -> -7532 on ao1) */
static void ao_static_2v(const char *script, mydaq_ao_channel ch, double volts)
{
    replay r;
    REPLAY_OPEN(r, script);
    REPLAY_STEP(r, mydaq_ao_write_range(r.dev, ch, volts, MYDAQ_AO_RANGE_2V));
    CHECK(replay_close(&r));
}
static void test_ao0_2v_p05(void) { ao_static_2v("ao0-r2-static-p0.5.mock", MYDAQ_AO0, 0.5); }
static void test_ao1_2v_m05(void) { ao_static_2v("ao1-r2-static-m0.5.mock", MYDAQ_AO1, -0.5); }
static void test_ao0_2v_finite(void)
{
    g_ao_range = MYDAQ_AO_RANGE_2V;
    ao_timed("ao0-r2-finite-1k-n100-ramp.mock", ao0, 1, 1000.0, MYDAQ_FINITE, 0, 100, 0);
    g_ao_range = MYDAQ_AO_RANGE_10V;
}
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
/* open leads (overload) on the high ranges: mode bytes 16..19 */
static void test_dmm_ohm_20k(void) { dmm_function("dmm-ohm-r20k-x2.mock", MYDAQ_DMM_RESISTANCE, 20e3, 2); }
static void test_dmm_ohm_200k(void) { dmm_function("dmm-ohm-r200k-x2.mock", MYDAQ_DMM_RESISTANCE, 200e3, 2); }
static void test_dmm_ohm_2M(void) { dmm_function("dmm-ohm-r2M-x2.mock", MYDAQ_DMM_RESISTANCE, 2e6, 2); }
static void test_dmm_ohm_20M(void) { dmm_function("dmm-ohm-r20M-x2.mock", MYDAQ_DMM_RESISTANCE, 20e6, 2); }
/* Diode (mode 20) and continuity (mode 21): only ELVISmx's own DMM reaches them (DAQmx has no
 * such channel); frames from its GUI captures, see the scripts' headers. */
/* DAQmx's DMM read with a 200 ms timeout and nothing to read: status -225202, no packet. */
static void test_dmm_timeout(void)
{
    replay r;
    mydaq_dmm_reading reading;
    REPLAY_OPEN(r, "dmm-dcv-timeout.mock");
    REPLAY_CHECK(r, mydaq_dmm_read_timeout(r.dev, MYDAQ_DMM_DC_VOLTS, 20.0, 0, 200, &reading) ==
                        MYDAQ_ERR_TIMEOUT);
    CHECK(replay_close(&r));
}
static void test_dmm_diode(void) { dmm_function("dmm-elvis-diode.mock", MYDAQ_DMM_DIODE, 2.0, 3); }
static void test_dmm_continuity(void) { dmm_function("dmm-elvis-cont.mock", MYDAQ_DMM_CONTINUITY, 200.0, 3); }

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
    {"replay.audio_in_lr", test_audio_in_lr},
    {"replay.audio_in_l", test_audio_in_l},
    {"replay.ao0_finite", test_ao0_finite},
    {"replay.ao1_finite", test_ao1_finite},
    {"replay.ao01_finite", test_ao01_finite},
    {"replay.ao0_finite_10k", test_ao0_finite_10k},
    {"replay.ao0_finite_200k", test_ao0_finite_200k},
    {"replay.audio_out_finite", test_audio_out_finite},
    {"replay.ao0_regen", test_ao0_regen},
    {"replay.ao01_regen", test_ao01_regen},
    {"replay.ao0_stream", test_ao0_stream},
    {"replay.ao01_stream", test_ao01_stream},
    {"replay.ao0_finite_sync", test_ao0_finite_sync},
    {"replay.ao01_finite_sync", test_ao01_finite_sync},
    {"replay.ao0_finite_200k_sync", test_ao0_finite_200k_sync},
    {"replay.audio_out_finite_sync", test_audio_out_finite_sync},
    {"replay.ao01_regen_sync", test_ao01_regen_sync},
    {"replay.ao01_stream_sync", test_ao01_stream_sync},
    {"replay.ctr_count_edges", test_ctr_count_edges},
    {"replay.ctr_frequency_timeout", test_ctr_frequency_timeout},
    {"replay.co_ptrain_1k", test_co_1k},
    {"replay.co_ptrain_10k_d0.25", test_co_10k},
    {"replay.co_finite", test_co_finite},
    {"replay.co_delay", test_co_delay},
    {"replay.co_idle_high", test_co_idle_high},
    {"replay.co_update", test_co_update},
    {"replay.co_finite_delay", test_co_finite_delay},
    {"replay.ctrloop_rising", test_ctrloop_rising},
    {"replay.ctrloop_falling", test_ctrloop_falling},
    {"replay.ctrloop_initial_count", test_ctrloop_initial_count},
    {"replay.ctrloop_reset_dio1", test_ctrloop_reset_dio1},
    {"replay.ctr_edges_20mhz", test_ctr_edges_20mhz},
    {"replay.ctr_edges_100khz", test_ctr_edges_100khz},
    {"replay.ctr_pulse_width_falling", test_ctr_pulse_width_falling},
    {"replay.ctr_pulse_time", test_ctr_pulse_time},
    {"replay.ctr_two_edge", test_ctr_two_edge},
    {"replay.ctr_freq_loop", test_ctr_freq_loop},
    {"replay.ctr_period_loop", test_ctr_period_loop},
    {"replay.ctr_linenc_x4", test_ctr_linenc_x4},
    {"replay.ctr_linenc_x1_z", test_ctr_linenc_x1_z},
    {"replay.ctr_angenc_x2", test_ctr_angenc_x2},
    {"replay.ctr_angenc_2pulse", test_ctr_angenc_2pulse},
    {"replay.freqout_10k", test_freqout_10k},
    {"replay.freqout_100k", test_freqout_100k},
    {"replay.freqout_4M", test_freqout_4M},
    {"replay.freqout_2M", test_freqout_2M},
    {"replay.freqout_1M25", test_freqout_1M25},
    {"replay.freqout_1M", test_freqout_1M},
    {"replay.freqout_6250", test_freqout_6250},
    {"replay.ao0_p0.5", test_ao0_p05},
    {"replay.ao0_2v_p0.5", test_ao0_2v_p05},
    {"replay.ao1_2v_m0.5", test_ao1_2v_m05},
    {"replay.ao0_2v_finite", test_ao0_2v_finite},
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
    {"replay.dmm_ohm_20k", test_dmm_ohm_20k},
    {"replay.dmm_ohm_200k", test_dmm_ohm_200k},
    {"replay.dmm_ohm_2M", test_dmm_ohm_2M},
    {"replay.dmm_ohm_20M", test_dmm_ohm_20M},
    {"replay.dmm_timeout", test_dmm_timeout},
    {"replay.dmm_diode", test_dmm_diode},
    {"replay.dmm_continuity", test_dmm_continuity},
    {"replay.mismatch_detected", test_mismatch_detected},
    {NULL, NULL},
};
