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

/* MYDAQ_HW_ONLY: comma-separated group names; a group runs when it is listed exactly. */
static int selected(const char *only, const char *group)
{
    const size_t n = strlen(group);
    const char *p = only;
    if (!only)
        return 1;
    while ((p = strstr(p, group)) != NULL) {
        if ((p == only || p[-1] == ',') && (p[n] == ',' || p[n] == '\0'))
            return 1;
        p += n;
    }
    return 0;
}

/* After the lock script takes the myDAQ back from the Windows VM, the device can refuse the AI
 * timing engine (status -201381) for a few seconds while NI's driver on the other side was still
 * using it. Wait until an on-demand read works (at most 15 s) so that transient state is not
 * reported as a failure of the checks; the wait is printed. */
static void wait_ready(mydaq_device *dev)
{
    const mydaq_ai_channel_config ch = {MYDAQ_AI0, MYDAQ_AI_RANGE_10V};
    const uint64_t t0 = md_now_ms();
    double v;
    int rc;
    while ((rc = mydaq_ai_read_scan(dev, &ch, 1, &v)) != MYDAQ_OK && md_now_ms() < t0 + 15000)
        md_sleep_ms(250);
    printf("ready: first on-demand read after %.2f s (%s, device status %ld)\n",
           (double)(md_now_ms() - t0) / 1000.0, mydaq_error_name(rc),
           (long)mydaq_last_device_status(dev));
    EXPECT(rc == MYDAQ_OK, "device not ready after 15 s");
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
        int ok = 1;
        t0 = md_now_us();
        for (k = 0; k < 10; ++k) {
            const int rc = mydaq_ai_read_scan(dev, &chans[c], 1, &v[k]);
            EXPECT(rc == MYDAQ_OK, "%s", mydaq_error_name(rc));
            ok &= rc == MYDAQ_OK;
        }
        if (!ok)
            continue;   /* no statistics over failed reads */
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

/* Jumpers declared in MYDAQ_JUMPERS (comma-separated: ao0-ai0, dio4-dio1). */
static int jumper(const char *name)
{
    const char *j = getenv("MYDAQ_JUMPERS");
    return j && strstr(j, name) != NULL;
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
    /* ±2 V range (protocol.md 7.1/7.2): static on both outputs, then a timed finite ramp; with
     * the AO0 -> AI0 jumper the level is measured. Outputs back to 0 V. */
    {
        mydaq_ao_config o;
        mydaq_ao_task *t = NULL;
        static double ramp[100];
        int16_t code = 0;
        mydaq_ao_task_volts_to_code_range(cal, MYDAQ_AO0, 0.5, MYDAQ_AO_RANGE_2V, &code);
        EXPECT_OK(mydaq_ao_write_range(dev, MYDAQ_AO0, 0.5, MYDAQ_AO_RANGE_2V));
        printf("AO ao0 +0.5 V in the ±2 V range (code %d) accepted\n", code);
        EXPECT(code == 7226, "±2 V code of ao0 0.5 V");
        EXPECT_OK(mydaq_ao_write_range(dev, MYDAQ_AO1, -0.5, MYDAQ_AO_RANGE_2V));
        EXPECT(mydaq_ao_write_range(dev, MYDAQ_AO1, 2.5, MYDAQ_AO_RANGE_2V) == MYDAQ_ERR_INVALID_ARG, "±2 V limit");
        if (jumper("ao0-ai0")) {
            const mydaq_ai_channel_config ai = {MYDAQ_AI0, MYDAQ_AI_RANGE_2V};
            double v = 0;
            md_sleep_ms(20);
            EXPECT_OK(mydaq_ai_read_scan(dev, &ai, 1, &v));
            printf("  jumper AO0->AI0: %.4f V (expect 0.5)\n", v);
            EXPECT(fabs(v - 0.5) < 0.01, "±2 V AO0 level on AI0");
        }
        for (i = 0; i < 100; ++i)
            ramp[i] = -0.5 + (double)i / 100.0;
        memset(&o, 0, sizeof o);
        o.channels[0] = MYDAQ_AO0;
        o.num_channels = 1;
        o.sample_rate = 1000.0;
        o.mode = MYDAQ_FINITE;
        o.ranges[0] = MYDAQ_AO_RANGE_2V;
        EXPECT_OK(mydaq_ao_task_create(dev, &o, &t));
        if (t) {
            int rc;
            EXPECT_OK(mydaq_ao_task_write(t, ramp, 100, MYDAQ_GROUP_BY_CHANNEL, 1000));
            EXPECT_OK(mydaq_ao_task_start(t));
            rc = mydaq_ao_task_wait_done(t, 2000);
            printf("AO finite ramp in the ±2 V range, 1 kS/s x 100: %s\n", mydaq_error_name(rc));
            EXPECT(rc == MYDAQ_OK, "±2 V finite AO");
            mydaq_ao_task_destroy(t);
        }
        EXPECT_OK(mydaq_ao_write(dev, MYDAQ_AO0, 0.0));
        EXPECT_OK(mydaq_ao_write(dev, MYDAQ_AO1, 0.0));
    }
}

static void check_dio(mydaq_device *dev)
{
    static const uint8_t patterns[] = {0x55, 0xaa, 0x0f, 0xf0};
    /* With DIO4 wired to DIO1, never drive both as outputs (they would fight): leave them out
     * of the output patterns and test them as a loopback instead (DIO4 out, DIO1 in). */
    const int loop41 = jumper("dio4-dio1");
    const uint8_t mask = loop41 ? (uint8_t)~0x12u : 0xffu;
    uint8_t v = 0;
    size_t i;
    EXPECT_OK(mydaq_dio_set_direction(dev, 0xff, 0));
    EXPECT_OK(mydaq_dio_read(dev, &v));
    printf("DIO inputs: 0x%02x\n", v);
    EXPECT_OK(mydaq_dio_write(dev, 0x00, mask));
    EXPECT_OK(mydaq_dio_set_direction(dev, mask, 1));
    for (i = 0; i < sizeof patterns; ++i) {
        EXPECT_OK(mydaq_dio_write(dev, patterns[i], mask));
        EXPECT_OK(mydaq_dio_read(dev, &v));
        printf("DIO wrote 0x%02x (mask 0x%02x) read back 0x%02x\n", patterns[i] & mask, mask, v & mask);
        EXPECT((v & mask) == (patterns[i] & mask), "readback");
    }
    EXPECT_OK(mydaq_dio_write(dev, 0x00, mask));
    EXPECT_OK(mydaq_dio_set_direction(dev, 0xff, 0));
    if (loop41) {   /* DIO4 output -> DIO1 input, both levels */
        static const uint8_t levels[] = {0x10, 0x00, 0x10, 0x00};
        EXPECT_OK(mydaq_dio_set_direction(dev, 0x10, 1));
        for (i = 0; i < sizeof levels; ++i) {
            EXPECT_OK(mydaq_dio_write(dev, levels[i], 0x10));
            md_sleep_ms(2);
            EXPECT_OK(mydaq_dio_read(dev, &v));
            printf("jumper DIO4->DIO1: DIO4 = %d, DIO1 reads %d\n", levels[i] ? 1 : 0, (v >> 1) & 1);
            EXPECT(((v >> 1) & 1) == (levels[i] ? 1 : 0), "DIO1 follows DIO4");
        }
        EXPECT_OK(mydaq_dio_set_direction(dev, 0xff, 0));
    }
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
    /* ELVISmx cadence (protocol.md 9.4): set the mode once, then every read returns the meter's
     * next packet: every 0.5 s for DC V, ~94 ms for diode. And the device-side read timeout:
     * right after set_mode the meter sends nothing for > 200 ms (dmm-dcv-timeout). */
    {
        static const struct {
            mydaq_dmm_function f;
            double range, lo, hi;
            const char *name;
        } c[] = {{MYDAQ_DMM_DC_VOLTS, 20.0, 0.40, 0.60, "DC V 20 V"},
                 {MYDAQ_DMM_DIODE, 2.0, 0.07, 0.13, "diode"}};
        mydaq_dmm_reading r;
        size_t k;
        int rc = mydaq_dmm_read_timeout(dev, MYDAQ_DMM_DC_VOLTS, 2.0, 0, 200, &r);
        printf("DMM read 200 ms right after set_mode: %s (expect MYDAQ_ERR_TIMEOUT)\n", mydaq_error_name(rc));
        EXPECT(rc == MYDAQ_ERR_TIMEOUT, "DMM device-side timeout");
        for (k = 0; k < 2; ++k) {
            uint64_t t0 = 0;
            int j;
            rc = mydaq_dmm_read_ex(dev, c[k].f, c[k].range, MYDAQ_DMM_KEEP_MODE, &r);   /* settles */
            for (j = 0; j < 6 && rc == MYDAQ_OK; ++j) {
                if (j == 1)
                    t0 = md_now_us();
                rc = mydaq_dmm_read_ex(dev, c[k].f, c[k].range, MYDAQ_DMM_KEEP_MODE, &r);
            }
            if (rc == MYDAQ_OK) {
                const double dt = (double)(md_now_us() - t0) / 1e6 / 5.0;
                printf("DMM %s, mode kept: a reading every %.3f s (\"%.9s\")\n", c[k].name, dt,
                       (const char *)r.packet.raw);
                EXPECT(dt > c[k].lo && dt < c[k].hi, "DMM %s cadence %.3f s", c[k].name, dt);
            }
            EXPECT(rc == MYDAQ_OK, "DMM %s kept-mode reads: %s", c[k].name, mydaq_error_name(rc));
        }
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

/* Timed AO (nothing wired: exercises the protocol; AO stays within ±0.5 V and ends at 0 V). */
static void check_ao_timed(mydaq_device *dev)
{
    static double wave[2 * 2000], zero[2] = {0, 0};
    mydaq_ao_config cfg;
    mydaq_ao_task *t = NULL;
    size_t i;
    int rc;
    uint64_t end, start_ms;
    for (i = 0; i < 2000; ++i) {
        wave[i] = 0.5 * sin(6.283185307179586 * (double)i / 200.0);
        wave[2000 + i] = -wave[i];
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.channels[0] = MYDAQ_AO0;
    cfg.channels[1] = MYDAQ_AO1;
    cfg.num_channels = 2;
    cfg.sample_rate = 20e3;
    cfg.mode = MYDAQ_FINITE;
    EXPECT_OK(mydaq_ao_task_create(dev, &cfg, &t));
    EXPECT_OK(mydaq_ao_task_write(t, wave, 2000, MYDAQ_GROUP_BY_CHANNEL, 1000));
    EXPECT_OK(mydaq_ao_task_start(t));
    rc = mydaq_ao_task_wait_done(t, 3000);
    printf("AO finite 2 ch 20 kS/s x 2000: %s\n", mydaq_error_name(rc));
    EXPECT(rc == MYDAQ_OK, "finite AO");
    mydaq_ao_task_destroy(t);

    cfg.mode = MYDAQ_CONTINUOUS;
    cfg.regenerate = 1;
    EXPECT_OK(mydaq_ao_task_create(dev, &cfg, &t));
    EXPECT_OK(mydaq_ao_task_write(t, wave, 2000, MYDAQ_GROUP_BY_CHANNEL, 1000));
    EXPECT_OK(mydaq_ao_task_start(t));
    md_sleep_ms(1000);
    printf("AO regenerate 2 ch 20 kS/s, 1 s: %llu samples sent, %s\n",
           (unsigned long long)mydaq_ao_task_samples_sent(t), mydaq_error_name(mydaq_ao_task_error(t)));
    EXPECT(mydaq_ao_task_error(t) == MYDAQ_OK && mydaq_ao_task_samples_sent(t) >= 20000, "regenerate");
    EXPECT_OK(mydaq_ao_task_stop(t));
    mydaq_ao_task_destroy(t);

    cfg.regenerate = 0;   /* streaming at 2 x 100 kS/s for 2 s, 20 ms blocks */
    cfg.sample_rate = 100e3;
    cfg.buffer_samples = 50000;
    EXPECT_OK(mydaq_ao_task_create(dev, &cfg, &t));
    EXPECT_OK(mydaq_ao_task_write(t, wave, 2000, MYDAQ_GROUP_BY_CHANNEL, 1000));
    start_ms = md_now_ms();
    EXPECT_OK(mydaq_ao_task_start(t));
    start_ms = md_now_ms() - start_ms;
    end = md_now_ms() + 2000;
    rc = MYDAQ_OK;
    while (rc == MYDAQ_OK && md_now_ms() < end)
        rc = mydaq_ao_task_write(t, wave, 2000, MYDAQ_GROUP_BY_CHANNEL, 1000);
    printf("AO stream 2 ch 100 kS/s, 2 s: %s, %llu samples sent, start took %llu ms\n", mydaq_error_name(rc),
           (unsigned long long)mydaq_ao_task_samples_sent(t), (unsigned long long)start_ms);
    EXPECT(rc == MYDAQ_OK, "AO streaming");
    EXPECT(start_ms < 300, "AO start latency (pre-fill must not wait for its deadline)");
    EXPECT_OK(mydaq_ao_task_stop(t));
    mydaq_ao_task_destroy(t);

    cfg.channels[0] = MYDAQ_AUDIO_OUT_LEFT;
    cfg.channels[1] = MYDAQ_AUDIO_OUT_RIGHT;
    cfg.mode = MYDAQ_FINITE;
    cfg.sample_rate = 48000.0;
    EXPECT_OK(mydaq_ao_task_create(dev, &cfg, &t));
    EXPECT_OK(mydaq_ao_task_write(t, wave, 2000, MYDAQ_GROUP_BY_CHANNEL, 1000));
    EXPECT_OK(mydaq_ao_task_start(t));
    rc = mydaq_ao_task_wait_done(t, 3000);
    printf("audio out finite 48 kS/s x 2000: %s\n", mydaq_error_name(rc));
    EXPECT(rc == MYDAQ_OK, "audio out");
    EXPECT_OK(mydaq_ao_task_write(t, zero, 1, MYDAQ_GROUP_BY_SCAN, 0));   /* leave audio at 0 */
    EXPECT_OK(mydaq_ao_task_start(t));
    mydaq_ao_task_wait_done(t, 1000);
    mydaq_ao_task_destroy(t);
    EXPECT_OK(mydaq_ao_write(dev, MYDAQ_AO0, 0.0));
    EXPECT_OK(mydaq_ao_write(dev, MYDAQ_AO1, 0.0));
}

static void check_audio_in(mydaq_device *dev)
{
    static double v[2 * 4800];
    mydaq_ai_config cfg;
    mydaq_ai_task *t = NULL;
    double mean, sd;
    int rc;
    memset(&cfg, 0, sizeof cfg);
    cfg.channels[0].channel = MYDAQ_AUDIO_IN_LEFT;
    cfg.channels[1].channel = MYDAQ_AUDIO_IN_RIGHT;
    cfg.channels[0].range = cfg.channels[1].range = MYDAQ_AI_RANGE_2V;
    cfg.num_channels = 2;
    cfg.sample_rate = 48000.0;
    cfg.mode = MYDAQ_FINITE;
    cfg.samples_per_channel = 4800;
    EXPECT_OK(mydaq_ai_task_create(dev, &cfg, &t));
    EXPECT_OK(mydaq_ai_task_start(t));
    rc = mydaq_ai_task_read(t, v, 4800, MYDAQ_GROUP_BY_CHANNEL, 3000);
    stats(v, 4800, 1, &mean, &sd);
    printf("audio in L+R 48 kS/s x 4800: %s, L mean %.6f V sd %.3f mV\n", mydaq_error_name(rc), mean, sd * 1e3);
    EXPECT(rc == MYDAQ_OK && fabs(mean) < 0.1, "audio in (AC coupled, open: ~0 V)");
    mydaq_ai_task_destroy(t);
}

/* Counter and FREQOUT, observed through the DIO read-back of their lines. */
/* Stage-3 counter modes on the internal FREQOUT loop (no wiring; protocol.md 10.5). FREQOUT runs
 * at 10 kHz = 100 MHz / 10000 exactly, so every measurement has an exact expected value. */
static int ctr_loop_task(mydaq_device *dev, mydaq_ctr_config *cfg, mydaq_ctr_task **t)
{
    int rc = mydaq_ctr_task_create(dev, cfg, t);
    if (rc == MYDAQ_OK)
        rc = mydaq_ctr_task_start(*t);
    return rc;
}

static void check_counter_loop(mydaq_device *dev)
{
    mydaq_ctr_config cfg;
    mydaq_ctr_task *t = NULL;
    double f = 0, v = 0;
    uint32_t c1 = 0, c2 = 0, hi = 0, lo = 0;
    uint64_t t1, t2;
    int k, rc;
    static const struct {
        mydaq_ctr_input input;
        const char *name;
        double hz;
    } bases[] = {{MYDAQ_CTR_INPUT_FREQOUT, "FREQOUT 10 kHz", 10e3},
                 {MYDAQ_CTR_INPUT_20MHZ, "20 MHz timebase", 20e6},
                 {MYDAQ_CTR_INPUT_100KHZ, "100 kHz timebase", 100e3}};

    EXPECT_OK(mydaq_freqout_start(dev, 10e3, &f));
    /* count edges: rate of the loop / timebases, rising and falling, initial count */
    for (k = 0; k < 4; ++k) {
        const int b = k < 3 ? k : 0;
        memset(&cfg, 0, sizeof cfg);
        cfg.kind = MYDAQ_CTR_COUNT_EDGES;
        cfg.input = bases[b].input;
        cfg.falling = k == 3;
        cfg.initial_count = k == 0 ? 1000u : 0u;
        EXPECT_OK(ctr_loop_task(dev, &cfg, &t));
        md_sleep_ms(50);
        EXPECT_OK(mydaq_ctr_task_read_count(t, &c1, 1000));
        t1 = md_now_us();
        md_sleep_ms(500);
        EXPECT_OK(mydaq_ctr_task_read_count(t, &c2, 1000));
        t2 = md_now_us();
        v = (double)(uint32_t)(c2 - c1) / ((double)(t2 - t1) / 1e6);
        printf("counter: edges of %s%s%s: %u, then %.0f /s (expect %.0f)\n", bases[b].name,
               cfg.falling ? " (falling)" : "", k == 0 ? " from 1000" : "", c1, v, bases[b].hz);
        EXPECT(fabs(v / bases[b].hz - 1.0) < 0.03, "edge rate");   /* read latency ~ms */
        if (k == 0)
            EXPECT(c1 >= 1000, "initial count");
        mydaq_ctr_task_destroy(t);
        t = NULL;
    }
    /* frequency / period / pulse width / pulse time of the loop */
    for (k = 0; k < 6; ++k) {
        static const mydaq_ctr_kind kinds[6] = {MYDAQ_CTR_FREQUENCY, MYDAQ_CTR_FREQUENCY, MYDAQ_CTR_PERIOD,
                                                MYDAQ_CTR_PULSE_WIDTH, MYDAQ_CTR_PULSE_WIDTH, MYDAQ_CTR_PULSE_TIME};
        static const double expect[6] = {10e3, 10e3, 1e-4, 50e-6, 50e-6, 1e-4};
        static const char *names[6] = {"frequency", "frequency (falling)", "period", "pulse width high",
                                       "pulse width low", "pulse time"};
        memset(&cfg, 0, sizeof cfg);
        cfg.kind = kinds[k];
        cfg.input = MYDAQ_CTR_INPUT_FREQOUT;
        cfg.falling = k == 1 || k == 4;
        EXPECT_OK(ctr_loop_task(dev, &cfg, &t));
        if (kinds[k] == MYDAQ_CTR_PULSE_TIME) {
            rc = mydaq_ctr_task_read_pulse_time(t, &hi, &lo, 2000);
            v = ((double)hi + (double)lo) / 1e8;
            printf("counter: %s of the loop: %s high %u low %u ticks\n", names[k], mydaq_error_name(rc), hi, lo);
            EXPECT(rc == MYDAQ_OK && hi == 5000 && lo == 5000, "pulse time");
        } else {
            rc = mydaq_ctr_task_read(t, &v, 2000);
            printf("counter: %s of the loop: %s %.9g (expect %g)\n", names[k], mydaq_error_name(rc), v, expect[k]);
        }
        EXPECT(rc == MYDAQ_OK && fabs(v / expect[k] - 1.0) < 1e-3, "loop measurement");
        mydaq_ctr_task_destroy(t);
        t = NULL;
    }
    /* two-edge separation on the loop: rising -> falling and falling -> rising = half a period */
    for (k = 0; k < 2; ++k) {
        memset(&cfg, 0, sizeof cfg);
        cfg.kind = MYDAQ_CTR_TWO_EDGE_SEPARATION;
        cfg.input = MYDAQ_CTR_INPUT_FREQOUT;
        cfg.falling = k;
        cfg.second_falling = !k;
        EXPECT_OK(ctr_loop_task(dev, &cfg, &t));
        rc = mydaq_ctr_task_read(t, &v, 2000);
        printf("counter: two-edge %s of the loop: %s %.9g s (expect 5e-05)\n",
               k ? "falling -> rising" : "rising -> falling", mydaq_error_name(rc), v);
        EXPECT(rc == MYDAQ_OK && fabs(v / 50e-6 - 1.0) < 1e-3, "two-edge separation");
        mydaq_ctr_task_destroy(t);
        t = NULL;
    }
    /* two-pulse encoder: A = the loop counts up */
    memset(&cfg, 0, sizeof cfg);
    cfg.kind = MYDAQ_CTR_ANGULAR_ENCODER;
    cfg.input = MYDAQ_CTR_INPUT_FREQOUT;
    cfg.decoding = MYDAQ_CTR_TWO_PULSE;
    EXPECT_OK(ctr_loop_task(dev, &cfg, &t));
    md_sleep_ms(300);
    EXPECT_OK(mydaq_ctr_task_read(t, &v, 1000));
    printf("counter: two-pulse encoder on the loop after 0.3 s: %.0f counts (~3000)\n", v);
    EXPECT(v > 2000 && v < 5000, "encoder counts");
    mydaq_ctr_task_destroy(t);
    t = NULL;
    /* Why the myDAQ has no external count direction (DAQmx refuses it; MYBENCH lists it as
     * unsupported): an encoder decoder does not count with a static level on B. X1 with
     * A = the loop and B = DIO2 open (the DIO inputs read 0) counted 0 edges on 2026-09-29. */
    {
        uint8_t lv = 0;
        int32_t pos;
        EXPECT_OK(mydaq_dio_read(dev, &lv));
        memset(&cfg, 0, sizeof cfg);
        cfg.kind = MYDAQ_CTR_LINEAR_ENCODER;
        cfg.input = MYDAQ_CTR_INPUT_FREQOUT;
        cfg.decoding = MYDAQ_CTR_X1;
        EXPECT_OK(ctr_loop_task(dev, &cfg, &t));
        md_sleep_ms(300);
        EXPECT_OK(mydaq_ctr_task_read_count(t, &c1, 1000));
        pos = (int32_t)c1;
        printf("counter: X1 encoder, A = the loop, B = DIO2 open (level %d), 0.3 s: %ld counts (a "
               "quadrature decoder: ~0)\n", (lv >> 2) & 1, (long)pos);
        EXPECT(labs((long)pos) < 10, "X1 does not count with a static B");
        mydaq_ctr_task_destroy(t);
        t = NULL;
    }
    EXPECT_OK(mydaq_freqout_stop(dev));

    /* FREQOUT on every timebase, counted on the internal loop */
    {
        static const double fo[] = {20e6, 10e6, 4e6, 2e6, 1.25e6, 1e6, 625e3, 100e3, 6250.0};
        for (k = 0; k < (int)(sizeof fo / sizeof fo[0]); ++k) {
            double actual = 0;
            EXPECT_OK(mydaq_freqout_start(dev, fo[k], &actual));
            memset(&cfg, 0, sizeof cfg);
            cfg.kind = MYDAQ_CTR_COUNT_EDGES;
            cfg.input = MYDAQ_CTR_INPUT_FREQOUT;
            EXPECT_OK(ctr_loop_task(dev, &cfg, &t));
            md_sleep_ms(20);
            EXPECT_OK(mydaq_ctr_task_read_count(t, &c1, 1000));
            t1 = md_now_us();
            md_sleep_ms(300);
            EXPECT_OK(mydaq_ctr_task_read_count(t, &c2, 1000));
            t2 = md_now_us();
            v = (double)(uint32_t)(c2 - c1) / ((double)(t2 - t1) / 1e6);
            printf("FREQOUT %.9g Hz (asked %.9g): counted %.0f /s\n", actual, fo[k], v);
            EXPECT(actual == fo[k] && fabs(v / fo[k] - 1.0) < 0.02, "FREQOUT timebase");
            mydaq_ctr_task_destroy(t);
            t = NULL;
            EXPECT_OK(mydaq_freqout_stop(dev));
        }
    }

    /* pulse trains: initial delay (timing) and idle level (read on DIO3 during the delay) */
    for (k = 0; k < 2; ++k) {
        uint8_t lv = 0;
        memset(&cfg, 0, sizeof cfg);
        cfg.kind = MYDAQ_CTR_PULSE_TRAIN;
        cfg.frequency = 1000.0;
        cfg.duty_cycle = 0.25;
        cfg.mode = MYDAQ_FINITE;
        cfg.pulses = 10;
        cfg.idle_high = k;
        cfg.initial_delay = 0.3;
        t1 = md_now_us();
        EXPECT_OK(ctr_loop_task(dev, &cfg, &t));
        md_sleep_ms(100);
        EXPECT_OK(mydaq_dio_read(dev, &lv));
        rc = mydaq_ctr_task_wait_done(t, 3000);
        t2 = md_now_us();
        printf("counter: 10 pulses at 1 kHz, idle %s, 0.3 s delay: %s after %.3f s (expect ~0.31), DIO3 during the delay %d\n",
               k ? "high" : "low", mydaq_error_name(rc), (double)(t2 - t1) / 1e6, (lv >> 3) & 1);
        EXPECT(rc == MYDAQ_OK && (double)(t2 - t1) / 1e6 > 0.3 && (double)(t2 - t1) / 1e6 < 0.6, "initial delay");
        EXPECT(((lv >> 3) & 1) == k, "idle level");
        mydaq_ctr_task_destroy(t);
        t = NULL;
    }
}

static void check_counter(mydaq_device *dev)
{
    mydaq_ctr_config cfg;
    mydaq_ctr_task *t = NULL;
    uint32_t count = 99;
    double f = 0;
    int k, seen[2] = {0, 0}, rc;
    uint8_t v;

    memset(&cfg, 0, sizeof cfg);
    cfg.kind = MYDAQ_CTR_COUNT_EDGES;
    EXPECT_OK(mydaq_ctr_task_create(dev, &cfg, &t));
    EXPECT_OK(mydaq_ctr_task_start(t));
    md_sleep_ms(200);
    EXPECT_OK(mydaq_ctr_task_read_count(t, &count, 1000));
    printf("counter: count edges on open DIO0 after 0.2 s: %u\n", count);
    mydaq_ctr_task_destroy(t);

    cfg.kind = MYDAQ_CTR_FREQUENCY;   /* unverified: nothing on DIO1 -> timeout */
    EXPECT_OK(mydaq_ctr_task_create(dev, &cfg, &t));
    EXPECT_OK(mydaq_ctr_task_start(t));
    rc = mydaq_ctr_task_read(t, &f, 500);
    printf("counter: frequency on open DIO1: %s (expected MYDAQ_ERR_TIMEOUT)\n", mydaq_error_name(rc));
    EXPECT(rc == MYDAQ_ERR_TIMEOUT, "frequency timeout");
    mydaq_ctr_task_destroy(t);

    /* live update of a running train (ctr.1e + ctr.18): 10 Hz -> 4 Hz without stopping; the
     * edge rate on DIO3, sampled with DIO reads, follows */
    {
        static const double hz[2] = {10.0, 4.0};
        int j;
        memset(&cfg, 0, sizeof cfg);
        cfg.kind = MYDAQ_CTR_PULSE_TRAIN;
        cfg.frequency = hz[0];
        cfg.duty_cycle = 0.5;
        cfg.mode = MYDAQ_CONTINUOUS;
        EXPECT_OK(mydaq_ctr_task_create(dev, &cfg, &t));
        EXPECT_OK(mydaq_ctr_task_start(t));
        for (j = 0; j < 2; ++j) {
            int edges = 0, last = -1;
            uint64_t end;
            if (j == 1)
                EXPECT_OK(mydaq_ctr_task_update_pulse(t, hz[1], 0.5));
            md_sleep_ms(300);   /* let the new period start */
            end = md_now_ms() + 1500;
            while (md_now_ms() < end) {
                if (mydaq_dio_read(dev, &v) == MYDAQ_OK) {
                    const int level = (v >> 3) & 1;
                    edges += last >= 0 && level != last;
                    last = level;
                }
            }
            printf("counter: pulse train %s %.0f Hz: %d edges on DIO3 in 1.5 s (expect ~%.0f)\n",
                   j ? "updated live to" : "at", hz[j], edges, 3.0 * hz[j]);
            EXPECT(fabs(edges - 3.0 * hz[j]) <= 2.0 + 0.1 * 3.0 * hz[j], "pulse train edge rate");
            EXPECT(mydaq_ctr_task_is_running(t), "train keeps running");
        }
        mydaq_ctr_task_destroy(t);
        memset(&cfg, 0, sizeof cfg);
    }

    cfg.kind = MYDAQ_CTR_PULSE_TRAIN;   /* 5 Hz on DIO3: sample the line */
    cfg.frequency = 5.0;
    cfg.duty_cycle = 0.5;
    cfg.mode = MYDAQ_CONTINUOUS;
    EXPECT_OK(mydaq_ctr_task_create(dev, &cfg, &t));
    EXPECT_OK(mydaq_ctr_task_start(t));
    for (k = 0; k < 20; ++k) {
        if (mydaq_dio_read(dev, &v) == MYDAQ_OK)
            seen[(v >> 3) & 1] = 1;
        md_sleep_ms(30);
    }
    printf("counter: 5 Hz pulse train, DIO3 read back low %s / high %s\n", seen[0] ? "yes" : "no",
           seen[1] ? "yes" : "no");
    EXPECT(seen[0] && seen[1], "pulse train toggles DIO3");
    mydaq_ctr_task_destroy(t);

    cfg.mode = MYDAQ_FINITE;
    cfg.frequency = 1000.0;
    cfg.pulses = 100;
    EXPECT_OK(mydaq_ctr_task_create(dev, &cfg, &t));
    EXPECT_OK(mydaq_ctr_task_start(t));
    rc = mydaq_ctr_task_wait_done(t, 2000);
    printf("counter: finite 100 pulses at 1 kHz: %s\n", mydaq_error_name(rc));
    EXPECT(rc == MYDAQ_OK, "finite pulse train");
    mydaq_ctr_task_destroy(t);

    seen[0] = seen[1] = 0;
    EXPECT_OK(mydaq_freqout_start(dev, 6250.0, &f));
    for (k = 0; k < 40; ++k)
        if (mydaq_dio_read(dev, &v) == MYDAQ_OK)
            seen[(v >> 4) & 1] = 1;
    printf("FREQOUT %.0f Hz: DIO4 read back low %s / high %s\n", f, seen[0] ? "yes" : "no",
           seen[1] ? "yes" : "no");
    EXPECT(seen[0] && seen[1], "FREQOUT toggles DIO4");
    EXPECT_OK(mydaq_freqout_stop(dev));

    /* MYDAQ_JUMPERS=dio4-dio1: FREQOUT (DIO4) wired to the counter gate (DIO1). The one-counter
     * frequency and period readings must match FREQOUT's exact 100 kHz / n. */
    if (jumper("dio4-dio1")) {
        static const double hz[3] = {100e3, 10e3, 6250.0};
        for (k = 0; k < 3; ++k) {
            double fo = 0, fm = 0, pm = 0;
            EXPECT_OK(mydaq_freqout_start(dev, hz[k], &fo));
            cfg.kind = MYDAQ_CTR_FREQUENCY;
            EXPECT_OK(mydaq_ctr_task_create(dev, &cfg, &t));
            EXPECT_OK(mydaq_ctr_task_start(t));
            md_sleep_ms(50);
            rc = mydaq_ctr_task_read(t, &fm, 2000);
            mydaq_ctr_task_destroy(t);
            cfg.kind = MYDAQ_CTR_PERIOD;
            EXPECT_OK(mydaq_ctr_task_create(dev, &cfg, &t));
            EXPECT_OK(mydaq_ctr_task_start(t));
            md_sleep_ms(50);
            EXPECT_OK(mydaq_ctr_task_read(t, &pm, 2000));
            mydaq_ctr_task_destroy(t);
            EXPECT_OK(mydaq_freqout_stop(dev));
            printf("jumper DIO4->DIO1: FREQOUT %.1f Hz -> frequency %s %.3f Hz, period %.9f s\n", fo,
                   mydaq_error_name(rc), fm, pm);
            EXPECT(rc == MYDAQ_OK && fabs(fm - fo) < fo * 1e-3, "counter frequency = FREQOUT");
            EXPECT(fabs(pm * fo - 1.0) < 1e-3, "counter period = 1 / FREQOUT");
        }
    }
}

static void check_dmm_unverified(mydaq_device *dev)
{
    static const struct {
        mydaq_dmm_function f;
        double range;
        const char *name;
    } m[] = {{MYDAQ_DMM_DIODE, 2.0, "diode"}, {MYDAQ_DMM_CONTINUITY, 200.0, "continuity"}};
    size_t i;
    for (i = 0; i < 2; ++i) {
        mydaq_dmm_reading r;
        const int rc = mydaq_dmm_read(dev, m[i].f, m[i].range, &r);
        printf("DMM %s (mode byte unverified): %s packet \"%.9s\" -> %.9g%s\n", m[i].name,
               mydaq_error_name(rc), (const char *)r.packet.raw, r.value, r.overload ? " OVERLOAD" : "");
        EXPECT(rc == MYDAQ_OK, "dmm %s", m[i].name);
    }
}

/* AI and AO continuous at the same time (they need two streams, see md_stream_alloc), in both
 * start orders, at the maximum rates: 2 x 200 kS/s in, 2 x 200 kS/s streamed out. Then each
 * task must survive the other one stopping. With MYDAQ_JUMPERS containing "ao0-ai0" (AO0 wired
 * to AI0+, AI0- to AGND) the generated sine is also measured on AI0. */
static void ai_ao_pass(mydaq_device *dev, int ao_first, double ai_rate, double ao_rate, int regen)
{
    enum { BLOCK = 4000 };
    static double in[2 * BLOCK], out[2 * BLOCK];
    mydaq_ai_config a;
    mydaq_ao_config o;
    mydaq_ai_task *ai = NULL;
    mydaq_ao_task *ao = NULL;
    const uint64_t seconds = 3;
    uint64_t t0, got = 0, sent0, sent1, deadline;
    int rc = MYDAQ_OK, rco = MYDAQ_OK, k;
    size_t i;
    double peak = 0;
    const int loop0 = jumper("ao0-ai0");

    for (i = 0; i < BLOCK; ++i) {   /* 0.5 V sine, 40 periods per block; AO1 inverted */
        out[i] = 0.5 * sin(6.283185307179586 * 40.0 * (double)i / BLOCK);
        out[BLOCK + i] = -out[i];
    }
    memset(&a, 0, sizeof a);
    a.channels[0].channel = MYDAQ_AI0;
    a.channels[1].channel = MYDAQ_AI1;
    a.channels[0].range = a.channels[1].range = MYDAQ_AI_RANGE_10V;
    a.num_channels = 2;
    a.sample_rate = ai_rate;
    a.mode = MYDAQ_CONTINUOUS;
    memset(&o, 0, sizeof o);
    o.channels[0] = MYDAQ_AO0;
    o.channels[1] = MYDAQ_AO1;
    o.num_channels = 2;
    o.sample_rate = ao_rate;
    o.mode = MYDAQ_CONTINUOUS;
    o.regenerate = regen;
    o.buffer_samples = (size_t)(ao_rate / 4);
    EXPECT_OK(mydaq_ai_task_create(dev, &a, &ai));
    EXPECT_OK(mydaq_ao_task_create(dev, &o, &ao));
    if (!ai || !ao) {
        mydaq_ai_task_destroy(ai);
        mydaq_ao_task_destroy(ao);
        return;
    }
    EXPECT_OK(mydaq_ao_task_write(ao, out, BLOCK, MYDAQ_GROUP_BY_CHANNEL, 1000));
    while (!regen && mydaq_ao_task_space_available(ao) >= BLOCK)   /* streaming: fill the buffer */
        EXPECT_OK(mydaq_ao_task_write(ao, out, BLOCK, MYDAQ_GROUP_BY_CHANNEL, 0));
    for (k = 0; k < 2; ++k) {
        if ((k == 0) == (ao_first != 0))
            rco = mydaq_ao_task_start(ao);
        else
            rc = mydaq_ai_task_start(ai);
    }
    EXPECT(rc == MYDAQ_OK && rco == MYDAQ_OK, "start: AI %s, AO %s", mydaq_error_name(rc),
           mydaq_error_name(rco));
    t0 = md_now_ms();
    sent0 = mydaq_ao_task_samples_sent(ao);
    while (rc == MYDAQ_OK && md_now_ms() < t0 + seconds * 1000) {
        if (!regen)
            while (mydaq_ao_task_space_available(ao) >= BLOCK && rco == MYDAQ_OK)
                rco = mydaq_ao_task_write(ao, out, BLOCK, MYDAQ_GROUP_BY_CHANNEL, 0);
        rc = mydaq_ai_task_read(ai, in, BLOCK / 4, MYDAQ_GROUP_BY_CHANNEL, 2000);
        if (rc == MYDAQ_OK)
            got += BLOCK / 4;
        if (loop0 && rc == MYDAQ_OK)
            for (i = 0; i < BLOCK / 4; ++i)
                peak = fabs(in[i]) > peak ? fabs(in[i]) : peak;
    }
    sent1 = mydaq_ao_task_samples_sent(ao);
    {
        const double el = (double)(md_now_ms() - t0) / 1000.0;
        printf("AI 2x%.0fk + AO 2x%.0fk %s, %s first, %.1f s: AI %s %.0f S/s/ch, AO %s %.0f S/s/ch\n",
               ai_rate / 1e3, ao_rate / 1e3, regen ? "regenerated" : "streamed",
               ao_first ? "AO" : "AI", el, mydaq_error_name(rc), (double)got / el,
               mydaq_error_name(mydaq_ao_task_error(ao)), (double)(sent1 - sent0) / el);
        EXPECT(rc == MYDAQ_OK && (double)got > 0.97 * ai_rate * el, "AI while AO runs");
        EXPECT(mydaq_ao_task_error(ao) == MYDAQ_OK && rco == MYDAQ_OK &&
                   (double)(sent1 - sent0) > 0.95 * ao_rate * el,
               "AO while AI runs");
    }
    if (loop0) {
        printf("  jumper AO0->AI0: peak %.3f V (expect 0.5)\n", peak);
        EXPECT(fabs(peak - 0.5) < 0.03, "AO0 sine on AI0");
    }
    /* stop the first-started task; the other one keeps going */
    if (ao_first) {
        EXPECT_OK(mydaq_ao_task_stop(ao));
        deadline = md_now_ms() + 500;
        rc = MYDAQ_OK;
        while (rc == MYDAQ_OK && md_now_ms() < deadline)
            rc = mydaq_ai_task_read(ai, in, BLOCK / 4, MYDAQ_GROUP_BY_CHANNEL, 2000);
        printf("  AI after AO stop: %s\n", mydaq_error_name(rc));
        EXPECT(rc == MYDAQ_OK, "AI survives AO stop");
    } else {
        EXPECT_OK(mydaq_ai_task_stop(ai));
        sent0 = mydaq_ao_task_samples_sent(ao);
        deadline = md_now_ms() + 500;
        while (md_now_ms() < deadline && rco == MYDAQ_OK) {
            if (!regen && mydaq_ao_task_space_available(ao) >= BLOCK)
                rco = mydaq_ao_task_write(ao, out, BLOCK, MYDAQ_GROUP_BY_CHANNEL, 0);
            md_sleep_ms(5);
        }
        sent1 = mydaq_ao_task_samples_sent(ao);
        printf("  AO after AI stop: %s, %.0f S/s/ch\n", mydaq_error_name(mydaq_ao_task_error(ao)),
               (double)(sent1 - sent0) / 0.5);
        EXPECT(mydaq_ao_task_error(ao) == MYDAQ_OK && rco == MYDAQ_OK &&
                   (double)(sent1 - sent0) > 0.8 * ao_rate * 0.5,
               "AO survives AI stop");
    }
    mydaq_ai_task_destroy(ai);
    mydaq_ao_task_destroy(ao);
}

static void check_ai_ao_concurrent(mydaq_device *dev)
{
    ai_ao_pass(dev, 1, 200e3, 100e3, 0);
    ai_ao_pass(dev, 0, 200e3, 100e3, 0);
    /* AO streamed at 2 x 200 kS/s next to AI at 2 x 200 kS/s: the queued asynchronous OUT
     * transfers (4 x 16 KiB, ~80 ms at 800 KB/s) plus the device FIFO give the writer its slack. */
    ai_ao_pass(dev, 1, 200e3, 200e3, 0);
    ai_ao_pass(dev, 0, 200e3, 200e3, 0);
    ai_ao_pass(dev, 1, 100e3, 10e3, 1);
    ai_ao_pass(dev, 0, 100e3, 10e3, 1);
    EXPECT_OK(mydaq_ao_write(dev, MYDAQ_AO0, 0.0));
    EXPECT_OK(mydaq_ao_write(dev, MYDAQ_AO1, 0.0));
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
    wait_ready(dev);
    if (selected(only, "ai")) {
        check_ai_on_demand(dev);
        check_ai_finite(dev);
        check_ai_continuous(dev);
        check_ai_abort(dev);
    }
    if (selected(only, "ao"))
        check_ao(dev);
    if (selected(only, "aotimed"))
        check_ao_timed(dev);
    if (selected(only, "concurrent"))
        check_ai_ao_concurrent(dev);
    if (selected(only, "audio"))
        check_audio_in(dev);
    if (selected(only, "ctr"))
        check_counter(dev);
    if (selected(only, "ctrloop"))
        check_counter_loop(dev);
    if (selected(only, "dio"))
        check_dio(dev);
    if (selected(only, "dmm"))
        check_dmm(dev);
    if (only && selected(only, "dmmall")) {
        check_dmm_all(dev);
        check_dmm_unverified(dev);
    }
    if (selected(only, "reset")) {
        const uint64_t t0 = md_now_us();
        EXPECT_OK(mydaq_reset(dev));
        printf("reset: %.2f s\n", (double)(md_now_us() - t0) / 1e6);
    }
    /* leave the bench as found: outputs at 0 V, DIO as inputs */
    mydaq_freqout_stop(dev);
    mydaq_ao_write(dev, MYDAQ_AO0, 0.0);
    mydaq_ao_write(dev, MYDAQ_AO1, 0.0);
    mydaq_dio_set_direction(dev, 0xff, 0);
    mydaq_close(dev);
    mydaq_context_free(ctx);
    printf("%s (%d failure(s))\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
