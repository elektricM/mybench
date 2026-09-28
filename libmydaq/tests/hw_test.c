/* Hardware test for a real myDAQ (ctest label "hw"; run through tools/devlock.sh).
 *
 * Nothing is wired to the device: inputs float, so AI checks are statistical. AO stays within
 * ±1 V and ends at 0 V; DIO ends as inputs. Prints one line per check; exit 0 = all passed. */
#include "libmydaq/mydaq.h"

#include "platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define EXPECT(cond, ...)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL %s: ", #cond);                                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

#define EXPECT_OK(expr)                                                                            \
    do {                                                                                           \
        const int rc_ = (expr);                                                                    \
        EXPECT(rc_ == MYDAQ_OK, "%s", mydaq_error_name(rc_));                                      \
    } while (0)

static void stats(const double *v, size_t n, size_t stride, double *mean, double *sd)
{
    double s = 0, q = 0;
    size_t i;
    for (i = 0; i < n; ++i)
        s += v[i * stride];
    *mean = s / (double)n;
    for (i = 0; i < n; ++i)
        q += (v[i * stride] - *mean) * (v[i * stride] - *mean);
    *sd = n > 1 ? sqrt(q / (double)(n - 1)) : 0;
}

static void check_info(mydaq_device *dev)
{
    mydaq_info info;
    const mydaq_calibration *cal = mydaq_get_calibration(dev);
    uint8_t mem[MYDAQ_CALMEM_SIZE], ref[MYDAQ_CALMEM_SIZE];
    FILE *f;
    EXPECT_OK(mydaq_get_info(dev, &info));
    printf("info: %s %s at %s, init %.2f s, identity %08x\n", info.product, info.serial,
           info.location, info.init_seconds, info.identity[0]);
    EXPECT(strlen(info.serial) == 8, "serial '%s'", info.serial);
    EXPECT(cal->checksums_ok && cal->backup_matches, "calibration CRC/backup");
    mydaq_get_calibration_memory(dev, mem, sizeof mem);
    /* same unit as the reference dump? then the memory must be identical */
    f = fopen(MYDAQ_TEST_DATA_DIR "/calmem-036CD1C6.bin", "rb");
    if (f && strcmp(info.serial, "036CD1C6") == 0) {
        EXPECT(fread(ref, 1, sizeof ref, f) == sizeof ref && memcmp(ref, mem, sizeof mem) == 0,
               "calibration memory differs from the reference dump");
        printf("calibration memory: identical to tests/data/calmem-036CD1C6.bin\n");
    }
    if (f)
        fclose(f);
}

static void check_ai_on_demand(mydaq_device *dev)
{
    static const mydaq_ai_channel_config chans[4] = {
        {MYDAQ_AI0, MYDAQ_AI_RANGE_10V}, {MYDAQ_AI1, MYDAQ_AI_RANGE_10V},
        {MYDAQ_AI0, MYDAQ_AI_RANGE_2V},  {MYDAQ_AI1, MYDAQ_AI_RANGE_2V}};
    const mydaq_ai_channel_config pair[2] = {{MYDAQ_AI0, MYDAQ_AI_RANGE_10V},
                                             {MYDAQ_AI1, MYDAQ_AI_RANGE_10V}};
    double v[10], two[2], mean, sd;
    int c, k;
    uint64_t t0;
    for (c = 0; c < 4; ++c) {
        t0 = md_now_us();
        for (k = 0; k < 10; ++k)
            EXPECT_OK(mydaq_ai_read_scan(dev, &chans[c], 1, &v[k]));
        stats(v, 10, 1, &mean, &sd);
        printf("AI on demand ai%d ±%s V: mean %.6f V sd %.3f mV, %.1f ms/read\n", (int)chans[c].channel,
               chans[c].range == MYDAQ_AI_RANGE_10V ? "10" : "2", mean, sd * 1e3,
               (double)(md_now_us() - t0) / 1e4);
        EXPECT(fabs(mean) < (chans[c].range == MYDAQ_AI_RANGE_10V ? 10.6 : 2.3), "out of range");
    }
    EXPECT_OK(mydaq_ai_read_scan(dev, pair, 2, two));
    printf("AI on demand ai0+ai1: %.6f V, %.6f V\n", two[0], two[1]);
}

static int finite(mydaq_device *dev, size_t nch, double rate, size_t n, double *out, double *seconds)
{
    mydaq_ai_config cfg;
    mydaq_ai_task *t = NULL;
    uint64_t t0;
    int rc;
    memset(&cfg, 0, sizeof cfg);
    cfg.channels[0].channel = MYDAQ_AI0;
    cfg.channels[0].range = MYDAQ_AI_RANGE_10V;
    cfg.channels[1].channel = MYDAQ_AI1;
    cfg.channels[1].range = MYDAQ_AI_RANGE_10V;
    cfg.num_channels = nch;
    cfg.sample_rate = rate;
    cfg.mode = MYDAQ_FINITE;
    cfg.samples_per_channel = n;
    rc = mydaq_ai_task_create(dev, &cfg, &t);
    if (rc != MYDAQ_OK)
        return rc;
    t0 = md_now_us();
    rc = mydaq_ai_task_start(t);
    if (rc == MYDAQ_OK)
        rc = mydaq_ai_task_read(t, out, n, MYDAQ_GROUP_BY_CHANNEL,
                                (int)(3000 + 1000.0 * (double)n / rate));
    *seconds = (double)(md_now_us() - t0) / 1e6;
    mydaq_ai_task_destroy(t);
    return rc;
}

static void check_ai_finite(mydaq_device *dev)
{
    static const struct {
        size_t nch;
        double rate;
        size_t n;
    } runs[] = {{1, 1000.0, 100}, {1, 1000.0, 1000}, {2, 10e3, 2000}, {1, 200e3, 40000},
                {2, 200e3, 40000}};
    static double buf[2 * 40000];
    size_t r, c;
    for (r = 0; r < sizeof runs / sizeof runs[0]; ++r) {
        double secs = 0, mean, sd;
        const int rc = finite(dev, runs[r].nch, runs[r].rate, runs[r].n, buf, &secs);
        EXPECT(rc == MYDAQ_OK, "finite %zu ch %.0f S/s x %zu: %s", runs[r].nch, runs[r].rate,
               runs[r].n, mydaq_error_name(rc));
        if (rc != MYDAQ_OK)
            continue;
        printf("AI finite %zu ch %.0f S/s x %zu: %.3f s (acquisition %.3f s)", runs[r].nch,
               runs[r].rate, runs[r].n, secs, (double)runs[r].n / runs[r].rate);
        for (c = 0; c < runs[r].nch; ++c) {
            stats(buf + c * runs[r].n, runs[r].n, 1, &mean, &sd);
            printf(" | ai%zu mean %.6f V sd %.3f mV", c, mean, sd * 1e3);
            EXPECT(fabs(mean) < 10.6, "mean out of range");
        }
        printf("\n");
        EXPECT(secs >= 0.9 * (double)runs[r].n / runs[r].rate, "finished faster than the clock");
    }
}

/* Continuous acquisition: `blocks` reads of `block` samples per channel; reports overflow. */
static void continuous(mydaq_device *dev, size_t nch, double rate, size_t block, int blocks)
{
    mydaq_ai_config cfg;
    mydaq_ai_task *t = NULL;
    double *v = malloc(sizeof *v * block * nch), first_ms = 0, mean, sd;
    uint64_t t0;
    int k, rc = MYDAQ_OK;
    memset(&cfg, 0, sizeof cfg);
    cfg.channels[0].channel = MYDAQ_AI0;
    cfg.channels[0].range = MYDAQ_AI_RANGE_10V;
    cfg.channels[1].channel = MYDAQ_AI1;
    cfg.channels[1].range = MYDAQ_AI_RANGE_10V;
    cfg.num_channels = nch;
    cfg.sample_rate = rate;
    cfg.mode = MYDAQ_CONTINUOUS;
    cfg.samples_per_channel = block;
    if (!v)
        return;
    EXPECT_OK(mydaq_ai_task_create(dev, &cfg, &t));
    if (!t) {
        free(v);
        return;
    }
    t0 = md_now_us();
    EXPECT_OK(mydaq_ai_task_start(t));
    for (k = 0; k < blocks && rc == MYDAQ_OK; ++k) {
        rc = mydaq_ai_task_read(t, v, block, MYDAQ_GROUP_BY_CHANNEL,
                                (int)(3000 + 1000.0 * (double)block / rate));
        if (k == 0)
            first_ms = (double)(md_now_us() - t0) / 1e3;
    }
    stats(v, block, 1, &mean, &sd);
    printf("AI continuous %zu ch %.0f S/s, %d x %zu: %s after %.3f s (stream time %.3f s), first block "
           "after %.1f ms (block = %.1f ms), ai0 mean %.6f V sd %.3f mV\n",
           nch, rate, k, block, mydaq_error_name(rc), (double)(md_now_us() - t0) / 1e6,
           (double)k * (double)block / rate, first_ms, 1000.0 * (double)block / rate, mean, sd * 1e3);
    EXPECT(rc == MYDAQ_OK, "continuous %zu ch %.0f S/s: %s", nch, rate, mydaq_error_name(rc));
    EXPECT_OK(mydaq_ai_task_stop(t));
    mydaq_ai_task_destroy(t);
    free(v);
}

static void check_ai_continuous(mydaq_device *dev)
{
    continuous(dev, 1, 1000.0, 50, 10);       /* low rate: latency of small blocks */
    continuous(dev, 2, 10e3, 5000, 4);        /* as captures/ai01-r10-cont-10k-n5000 */
    continuous(dev, 2, 100e3, 10000, 30);     /* 3 s */
    continuous(dev, 2, 200e3, 20000, 30);     /* 3 s at the maximum rate */
}

static void check_ai_abort(mydaq_device *dev)
{
    mydaq_ai_config cfg;
    mydaq_ai_task *t = NULL;
    double v[200], secs, one;
    const mydaq_ai_channel_config c = {MYDAQ_AI0, MYDAQ_AI_RANGE_10V};
    int rc;
    memset(&cfg, 0, sizeof cfg);
    cfg.channels[0] = c;
    cfg.num_channels = 1;
    cfg.sample_rate = 1000.0;
    cfg.mode = MYDAQ_FINITE;
    cfg.samples_per_channel = 5000;
    EXPECT_OK(mydaq_ai_task_create(dev, &cfg, &t));
    if (!t)
        return;
    EXPECT_OK(mydaq_ai_task_start(t));
    rc = mydaq_ai_task_read(t, v, 200, MYDAQ_GROUP_BY_SCAN, 3000);
    printf("AI abort: first 200 of 5000 samples at 1 kS/s: %s, available now %zu\n",
           mydaq_error_name(rc), mydaq_ai_task_available(t));
    EXPECT(rc == MYDAQ_OK, "partial read");
    EXPECT_OK(mydaq_ai_task_stop(t));
    mydaq_ai_task_destroy(t);
    /* the device must be usable right after an aborted acquisition */
    EXPECT_OK(mydaq_ai_read_scan(dev, &c, 1, &one));
    rc = finite(dev, 1, 1000.0, 100, v, &secs);
    EXPECT(rc == MYDAQ_OK, "finite after abort: %s", mydaq_error_name(rc));
    printf("AI after abort: on-demand %.6f V, finite x100 %s\n", one, mydaq_error_name(rc));
}

static void check_ao(mydaq_device *dev)
{
    const mydaq_calibration *cal = mydaq_get_calibration(dev);
    static const struct {
        mydaq_ao_channel ch;
        double v;
    } w[] = {{MYDAQ_AO0, 0.5}, {MYDAQ_AO0, -0.5}, {MYDAQ_AO1, 1.0}, {MYDAQ_AO1, -1.0},
             {MYDAQ_AO0, 0.0}, {MYDAQ_AO1, 0.0}};
    size_t i;
    for (i = 0; i < sizeof w / sizeof w[0]; ++i) {
        int16_t code = 0;
        mydaq_ao_volts_to_code(cal, w[i].ch, w[i].v, &code);
        EXPECT_OK(mydaq_ao_write(dev, w[i].ch, w[i].v));
        printf("AO ao%d %+.1f V (code %d) accepted\n", (int)w[i].ch, w[i].v, code);
        md_sleep_ms(50);
    }
}

static void check_dio(mydaq_device *dev)
{
    static const uint8_t patterns[] = {0x55, 0xaa, 0x0f, 0xf0};
    uint8_t v = 0;
    size_t i;
    EXPECT_OK(mydaq_dio_set_direction(dev, 0xff, 0));
    EXPECT_OK(mydaq_dio_read(dev, &v));
    printf("DIO inputs: 0x%02x\n", v);
    EXPECT_OK(mydaq_dio_write(dev, 0x00, 0xff));
    EXPECT_OK(mydaq_dio_set_direction(dev, 0xff, 1));
    for (i = 0; i < sizeof patterns; ++i) {
        EXPECT_OK(mydaq_dio_write(dev, patterns[i], 0xff));
        EXPECT_OK(mydaq_dio_read(dev, &v));
        printf("DIO wrote 0x%02x read back 0x%02x\n", patterns[i], v);
        EXPECT(v == patterns[i], "readback");
    }
    EXPECT_OK(mydaq_dio_write(dev, 0x00, 0xff));
    EXPECT_OK(mydaq_dio_set_direction(dev, 0xff, 0));
    EXPECT_OK(mydaq_dio_read(dev, &v));
    printf("DIO back to inputs: 0x%02x\n", v);
}

static void check_dmm(mydaq_device *dev)
{
    static const double ranges[] = {60.0, 20.0, 2.0, 0.2};
    size_t i;
    for (i = 0; i < 4; ++i) {
        mydaq_dmm_reading r;
        const uint64_t t0 = md_now_us();
        const int rc = mydaq_dmm_read(dev, MYDAQ_DMM_DC_VOLTS, ranges[i], &r);
        EXPECT(rc == MYDAQ_OK, "dmm %g V: %s", ranges[i], mydaq_error_name(rc));
        if (rc == MYDAQ_OK) {
            printf("DMM DC V %g V: packet \"%.9s\" -> %.17g V (%.2f s)%s\n", ranges[i],
                   (const char *)r.packet.raw, r.value, (double)(md_now_us() - t0) / 1e6,
                   r.overload ? " OVERLOAD" : "");
            EXPECT(fabs(r.value) < 0.5, "open DMM input reads %g V", r.value);
        }
    }
    {
        mydaq_dmm_reading r;
        const uint64_t t0 = md_now_us();
        const int rc = mydaq_dmm_read(dev, MYDAQ_DMM_AC_VOLTS, 2.0, &r);
        EXPECT(rc == MYDAQ_OK, "dmm AC V 2 V: %s", mydaq_error_name(rc));
        if (rc == MYDAQ_OK)
            printf("DMM AC V 2 V: packet \"%.9s\" -> %.17g V rms (%.2f s)\n", (const char *)r.packet.raw,
                   r.value, (double)(md_now_us() - t0) / 1e6);
    }
}

/* Every supported range of current and resistance, one reading each (~2 s per reading; opt-in:
 * MYDAQ_HW_ONLY=dmmall). Open leads: currents read ~0, resistance ranges overload. */
static void check_dmm_all(mydaq_device *dev)
{
    static const mydaq_dmm_function fns[] = {MYDAQ_DMM_DC_CURRENT, MYDAQ_DMM_AC_CURRENT,
                                             MYDAQ_DMM_RESISTANCE};
    size_t f, i, n;
    for (f = 0; f < 3; ++f) {
        const mydaq_dmm_range_info *r;
        mydaq_dmm_ranges(fns[f], &r, &n);
        for (i = 0; i < n; ++i) {
            mydaq_dmm_reading rd;
            const int rc = mydaq_dmm_read(dev, fns[f], r[i].full_scale, &rd);
            EXPECT(rc == MYDAQ_OK, "dmm function %d %s: %s", (int)fns[f], r[i].label, mydaq_error_name(rc));
            if (rc == MYDAQ_OK)
                printf("DMM function %d %s: packet \"%.9s\" -> %.17g%s\n", (int)fns[f], r[i].label,
                       (const char *)rd.packet.raw, rd.value,
                       rd.overload ? (rd.overload > 0 ? " +OVERLOAD" : " -OVERLOAD") : "");
        }
    }
}

int main(void)
{
    mydaq_context *ctx = NULL;
    mydaq_device *dev = NULL;
    mydaq_device_desc list[4];
    size_t n = 0;
    int rc;
    const char *only = getenv("MYDAQ_HW_ONLY");   /* e.g. "ai,dio" to shorten the hold */

    rc = mydaq_context_new(&ctx);
    if (rc != MYDAQ_OK) {
        printf("libusb: %s\n", mydaq_strerror(rc));
        return 1;
    }
    mydaq_context_set_log(ctx, getenv("MYDAQ_HW_TRACE") ? MYDAQ_LOG_TRACE : MYDAQ_LOG_INFO, NULL, NULL);
    rc = mydaq_enumerate(ctx, list, 4, &n, 0);
    printf("enumerate: %s, %zu device(s)%s%s\n", mydaq_error_name(rc), n, n ? " at " : "",
           n ? list[0].location : "");
    if (n == 0) {
        mydaq_context_free(ctx);
        return 1;
    }
    rc = mydaq_open(ctx, NULL, 0, &dev);
    if (rc != MYDAQ_OK) {
        printf("open: %s\n", mydaq_strerror(rc));
        mydaq_context_free(ctx);
        return 1;
    }
    check_info(dev);
    if (!only || strstr(only, "ai")) {
        check_ai_on_demand(dev);
        check_ai_finite(dev);
        check_ai_continuous(dev);
        check_ai_abort(dev);
    }
    if (!only || strstr(only, "ao"))
        check_ao(dev);
    if (!only || strstr(only, "dio"))
        check_dio(dev);
    if (!only || (strstr(only, "dmm") && !strstr(only, "dmmall")))
        check_dmm(dev);
    if (only && strstr(only, "dmmall"))
        check_dmm_all(dev);
    if (!only || strstr(only, "reset")) {
        const uint64_t t0 = md_now_us();
        EXPECT_OK(mydaq_reset(dev));
        printf("reset: %.2f s\n", (double)(md_now_us() - t0) / 1e6);
    }
    /* leave the bench as found: outputs at 0 V, DIO as inputs */
    mydaq_ao_write(dev, MYDAQ_AO0, 0.0);
    mydaq_ao_write(dev, MYDAQ_AO1, 0.0);
    mydaq_dio_set_direction(dev, 0xff, 0);
    mydaq_close(dev);
    mydaq_context_free(ctx);
    printf("%s (%d failure(s))\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
