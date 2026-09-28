/* mydaq-cli: manual testing tool for libmydaq.
 *
 *   mydaq-cli [options] list
 *   mydaq-cli [options] info [--dump FILE]
 *   mydaq-cli [options] ai [-c 0|1|0,1] [-r 10|2] [-n COUNT]
 *   mydaq-cli [options] ai-stream [-c 0|1|0,1] [-r 10|2] [--rate HZ] [-n SAMPLES] [--csv FILE]
 *             [--cont SECONDS]   continuous: read blocks of SAMPLES for SECONDS, report losses
 *   mydaq-cli [options] ao -c 0|1 --volts V
 *   mydaq-cli [options] ao-gen [-c 0|1|0,1|L|R|L,R] [--rate HZ] [--amp V] [--n SAMPLES] [--secs S]
 *             finite sine (one period of n samples), or regenerated for S seconds with --secs
 *   mydaq-cli [options] ctr edges|freq|period [--secs S]   read once per second (DIO0 / DIO1)
 *   mydaq-cli [options] ctr pulse --freq HZ [--duty D] [--secs S | --n PULSES]   (DIO3)
 *   mydaq-cli [options] freqout --freq HZ [--secs S]                              (DIO4)
 *   mydaq-cli [options] dio read | dio write VALUE [--mask M] | dio dir MASK in|out
 *   mydaq-cli [options] dmm [-f dcv] [-r RANGE] [-n COUNT]
 *   mydaq-cli [options] reset
 * options: --serial S (default: first myDAQ), --no-init, -v (debug log), -vv (trace frames)
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   /* nanosleep */
#endif
#include "libmydaq/mydaq.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

static void sleep_s(double seconds)
{
#if defined(_WIN32)
    Sleep((DWORD)(seconds * 1000.0));
#else
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
#endif
}

typedef struct args {
    int argc;
    char **argv;
} args;

static const char *opt(const args *a, const char *name, const char *def)
{
    int i;
    for (i = 0; i + 1 < a->argc; ++i)
        if (strcmp(a->argv[i], name) == 0)
            return a->argv[i + 1];
    return def;
}

static int flag(const args *a, const char *name)
{
    int i;
    for (i = 0; i < a->argc; ++i)
        if (strcmp(a->argv[i], name) == 0)
            return 1;
    return 0;
}

static int fail(const char *what, int rc)
{
    fprintf(stderr, "mydaq-cli: %s: %s (%s)\n", what, mydaq_strerror(rc), mydaq_error_name(rc));
    return 1;
}

static size_t parse_channels(const char *s, mydaq_ai_channel_config *ch, mydaq_ai_range range)
{
    size_t n = 0;
    for (; *s && n < MYDAQ_AI_MAX_CHANNELS; ++s) {
        if (*s == '0' || *s == '1') {
            ch[n].channel = *s == '0' ? MYDAQ_AI0 : MYDAQ_AI1;
            ch[n].range = range;
            ++n;
        }
    }
    return n;
}

static mydaq_ai_range parse_range(const char *s) { return atof(s) <= 2.0 ? MYDAQ_AI_RANGE_2V : MYDAQ_AI_RANGE_10V; }

static void stats(const double *v, size_t n, size_t stride, double *mean, double *sd)
{
    double s = 0, q = 0;
    size_t i;
    for (i = 0; i < n; ++i)
        s += v[i * stride];
    *mean = n ? s / (double)n : 0;
    for (i = 0; i < n; ++i)
        q += (v[i * stride] - *mean) * (v[i * stride] - *mean);
    *sd = n > 1 ? sqrt(q / (double)(n - 1)) : 0;
}

static int cmd_list(mydaq_context *ctx)
{
    mydaq_device_desc list[16];
    size_t n, i;
    const int rc = mydaq_enumerate(ctx, list, 16, &n, 0);
    if (rc != MYDAQ_OK)
        return fail("enumerate", rc);
    printf("%zu myDAQ(s)\n", n);
    for (i = 0; i < n && i < 16; ++i)
        printf("  location %s (bus %u address %u)\n", list[i].location, list[i].bus, list[i].address);
    return 0;
}

static int cmd_info(mydaq_device *dev, const args *a)
{
    mydaq_info info;
    const mydaq_calibration *cal = mydaq_get_calibration(dev);
    const char *dump = opt(a, "--dump", NULL);
    char when[64];
    time_t t;
    int i;
    mydaq_get_info(dev, &info);
    printf("%s serial %s (%lu) at %s, init %.2f s\n", info.product, info.serial,
           (unsigned long)info.serial_number, info.location, info.init_seconds);
    printf("identity %08x %08x %08x %08x | core %08x %08x %08x\n", info.identity[0],
           info.identity[1], info.identity[2], info.identity[3], info.core_info[0], info.core_info[1],
           info.core_info[2]);
    t = (time_t)cal->ai_ao_time;
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S UTC", gmtime(&t));
    printf("calibration %s, info \"%s\", checksums %s, backup %s\n", when, cal->user_info,
           cal->checksums_ok ? "ok" : "BAD", cal->backup_matches ? "identical" : "DIFFERENT");
    printf("AI +-10 V: volts = %.9g + %.9g * code\n", cal->ai[0].offset, cal->ai[0].gain);
    printf("AI +-2 V:  volts = %.9g + %.9g * code\n", cal->ai[2].offset, cal->ai[2].gain);
    for (i = 0; i < 2; ++i)
        printf("AO%d +-10 V: code = round(%.9g + %.9g * volts)\n", i, cal->ao[i][0].offset,
               cal->ao[i][0].gain);
    if (dump) {
        uint8_t mem[MYDAQ_CALMEM_SIZE];
        FILE *f = fopen(dump, "wb");
        mydaq_get_calibration_memory(dev, mem, sizeof mem);
        if (!f || fwrite(mem, 1, sizeof mem, f) != sizeof mem) {
            fprintf(stderr, "cannot write %s\n", dump);
            if (f)
                fclose(f);
            return 1;
        }
        fclose(f);
        printf("calibration memory -> %s\n", dump);
    }
    return 0;
}

static int cmd_ai(mydaq_device *dev, const args *a)
{
    mydaq_ai_channel_config ch[2];
    const size_t nch = parse_channels(opt(a, "-c", "0"), ch, parse_range(opt(a, "-r", "10")));
    const int count = atoi(opt(a, "-n", "10"));
    double *v;
    size_t c;
    int k, rc;
    if (nch == 0 || count < 1)
        return fail("arguments", MYDAQ_ERR_INVALID_ARG);
    v = malloc(sizeof *v * nch * (size_t)count);
    if (!v)
        return 1;
    for (k = 0; k < count; ++k) {
        rc = mydaq_ai_read_scan(dev, ch, nch, v + (size_t)k * nch);
        if (rc != MYDAQ_OK) {
            free(v);
            return fail("ai read", rc);
        }
    }
    for (c = 0; c < nch; ++c) {
        double mean, sd;
        stats(v + c, (size_t)count, nch, &mean, &sd);
        printf("ai%d +-%s V: n=%d mean=%.6f V sd=%.3f mV first=%.17g\n", (int)ch[c].channel,
               ch[c].range == MYDAQ_AI_RANGE_2V ? "2" : "10", count, mean, sd * 1e3, v[c]);
    }
    free(v);
    return 0;
}

static int cmd_ai_stream(mydaq_device *dev, mydaq_context *ctx, const args *a)
{
    mydaq_ai_config cfg;
    mydaq_ai_task *task = NULL;
    const char *csv = opt(a, "--csv", NULL);
    double *v;
    size_t c, n;
    int rc;
    (void)ctx;
    const double cont = atof(opt(a, "--cont", "0"));
    memset(&cfg, 0, sizeof cfg);
    cfg.num_channels = parse_channels(opt(a, "-c", "0"), cfg.channels, parse_range(opt(a, "-r", "10")));
    cfg.sample_rate = atof(opt(a, "--rate", "1000"));
    cfg.mode = cont > 0 ? MYDAQ_CONTINUOUS : MYDAQ_FINITE;
    cfg.samples_per_channel = (uint64_t)atoll(opt(a, "-n", "1000"));
    n = (size_t)cfg.samples_per_channel;
    rc = mydaq_ai_task_create(dev, &cfg, &task);
    if (rc != MYDAQ_OK)
        return fail("create AI task", rc);
    v = malloc(sizeof *v * n * cfg.num_channels);
    if (!v) {
        mydaq_ai_task_destroy(task);
        return 1;
    }
    printf("%s AI: %zu channel(s) at %.6g S/s (skew %.3g us), %zu samples%s\n",
           cont > 0 ? "continuous" : "finite", cfg.num_channels, mydaq_ai_task_rate(task),
           mydaq_ai_task_channel_skew(task) * 1e6, n, cont > 0 ? " per read" : "");
    rc = mydaq_ai_task_start(task);
    if (rc == MYDAQ_OK && cont > 0) {
        const long blocks = (long)ceil(cont * mydaq_ai_task_rate(task) / (double)n);
        long k;
        for (k = 0; k < blocks && rc == MYDAQ_OK; ++k)
            rc = mydaq_ai_task_read(task, v, n, MYDAQ_GROUP_BY_CHANNEL,
                                    (int)(5000 + 1000.0 * (double)n / mydaq_ai_task_rate(task)));
        printf("%ld block(s) of %zu read: %s\n", k, n, mydaq_error_name(rc));
    } else if (rc == MYDAQ_OK) {
        rc = mydaq_ai_task_read(task, v, n, MYDAQ_GROUP_BY_CHANNEL,
                                (int)(5000 + 1000.0 * (double)n / mydaq_ai_task_rate(task)));
    }
    if (rc != MYDAQ_OK) {
        free(v);
        mydaq_ai_task_destroy(task);
        return fail("acquisition", rc);
    }
    for (c = 0; c < cfg.num_channels; ++c) {
        double mean, sd;
        stats(v + c * n, n, 1, &mean, &sd);
        printf("ai%d: mean=%.6f V sd=%.3f mV first %.17g %.17g %.17g\n", (int)cfg.channels[c].channel,
               mean, sd * 1e3, v[c * n], n > 1 ? v[c * n + 1] : 0.0, n > 2 ? v[c * n + 2] : 0.0);
    }
    if (csv) {
        FILE *f = fopen(csv, "w");
        size_t i;
        if (f) {
            for (i = 0; i < n; ++i) {
                fprintf(f, "%zu", i);
                for (c = 0; c < cfg.num_channels; ++c)
                    fprintf(f, ",%.17g", v[c * n + i]);
                fputc('\n', f);
            }
            fclose(f);
            printf("samples -> %s\n", csv);
        }
    }
    free(v);
    mydaq_ai_task_destroy(task);
    return 0;
}

static int cmd_ao(mydaq_device *dev, const args *a)
{
    const int ch = atoi(opt(a, "-c", "0"));
    const char *vs = opt(a, "--volts", NULL);
    const mydaq_calibration *cal = mydaq_get_calibration(dev);
    int16_t code = 0;
    double volts;
    int rc;
    if (!vs || (ch != 0 && ch != 1))
        return fail("arguments (need -c 0|1 --volts V)", MYDAQ_ERR_INVALID_ARG);
    volts = atof(vs);
    rc = mydaq_ao_volts_to_code(cal, (mydaq_ao_channel)ch, volts, &code);
    if (rc == MYDAQ_OK)
        rc = mydaq_ao_write(dev, (mydaq_ao_channel)ch, volts);
    if (rc != MYDAQ_OK)
        return fail("ao write", rc);
    printf("ao%d = %g V (code %d)\n", ch, volts, code);
    return 0;
}

static int cmd_ao_gen(mydaq_device *dev, const args *a)
{
    const char *c = opt(a, "-c", "0");
    const double amp = atof(opt(a, "--amp", "0.5"));
    const size_t n = (size_t)atol(opt(a, "--n", "1000"));
    const double secs = atof(opt(a, "--secs", "0"));
    mydaq_ao_config cfg;
    mydaq_ao_task *t = NULL;
    double *v;
    size_t i, ch;
    int rc;
    memset(&cfg, 0, sizeof cfg);
    for (; *c && cfg.num_channels < 2; ++c) {
        if (*c == '0' || *c == '1')
            cfg.channels[cfg.num_channels++] = *c == '0' ? MYDAQ_AO0 : MYDAQ_AO1;
        else if (*c == 'L' || *c == 'R')
            cfg.channels[cfg.num_channels++] = *c == 'L' ? MYDAQ_AUDIO_OUT_LEFT : MYDAQ_AUDIO_OUT_RIGHT;
    }
    cfg.sample_rate = atof(opt(a, "--rate", "10000"));
    cfg.mode = secs > 0 ? MYDAQ_CONTINUOUS : MYDAQ_FINITE;
    cfg.regenerate = 1;
    if (n == 0 || cfg.num_channels == 0)
        return fail("arguments", MYDAQ_ERR_INVALID_ARG);
    v = malloc(sizeof *v * n * cfg.num_channels);
    if (!v)
        return 1;
    for (ch = 0; ch < cfg.num_channels; ++ch)
        for (i = 0; i < n; ++i)
            v[ch * n + i] = amp * sin(6.283185307179586 * (double)i / (double)n);
    rc = mydaq_ao_task_create(dev, &cfg, &t);
    if (rc == MYDAQ_OK)
        rc = mydaq_ao_task_write(t, v, n, MYDAQ_GROUP_BY_CHANNEL, 1000);
    if (rc == MYDAQ_OK)
        rc = mydaq_ao_task_start(t);
    if (rc == MYDAQ_OK && secs > 0) {
        const long ms = (long)(secs * 1000.0);
        long k;
        for (k = 0; k < ms && mydaq_ao_task_error(t) == MYDAQ_OK; k += 100)
            sleep_s(0.1);
        rc = mydaq_ao_task_error(t);
        printf("%llu samples/channel sent\n", (unsigned long long)mydaq_ao_task_samples_sent(t));
    } else if (rc == MYDAQ_OK) {
        rc = mydaq_ao_task_wait_done(t, (int)(5000 + 1000.0 * (double)n / mydaq_ao_task_rate(t)));
    }
    printf("AO %s at %.6g S/s: %s\n", secs > 0 ? "regenerated sine" : "finite sine",
           mydaq_ao_task_rate(t), mydaq_error_name(rc));
    mydaq_ao_task_destroy(t);
    free(v);
    return rc == MYDAQ_OK ? 0 : 1;
}

static int cmd_ctr(mydaq_device *dev, const args *a, int at)
{
    const char *what = at < a->argc ? a->argv[at] : "edges";
    const double secs = atof(opt(a, "--secs", "3"));
    mydaq_ctr_config cfg;
    mydaq_ctr_task *t = NULL;
    int rc, k;
    memset(&cfg, 0, sizeof cfg);
    if (strcmp(what, "pulse") == 0) {
        cfg.kind = MYDAQ_CTR_PULSE_TRAIN;
        cfg.frequency = atof(opt(a, "--freq", "1000"));
        cfg.duty_cycle = atof(opt(a, "--duty", "0.5"));
        cfg.pulses = (uint64_t)atoll(opt(a, "--n", "0"));
        cfg.mode = cfg.pulses ? MYDAQ_FINITE : MYDAQ_CONTINUOUS;
    } else {
        cfg.kind = strcmp(what, "freq") == 0     ? MYDAQ_CTR_FREQUENCY
                   : strcmp(what, "period") == 0 ? MYDAQ_CTR_PERIOD
                                                 : MYDAQ_CTR_COUNT_EDGES;
    }
    rc = mydaq_ctr_task_create(dev, &cfg, &t);
    if (rc == MYDAQ_OK)
        rc = mydaq_ctr_task_start(t);
    if (rc != MYDAQ_OK) {
        mydaq_ctr_task_destroy(t);
        return fail("counter", rc);
    }
    if (cfg.kind == MYDAQ_CTR_PULSE_TRAIN) {
        double f, d;
        mydaq_ctr_task_pulse(t, &f, &d);
        printf("pulse train on DIO3: %.9g Hz, duty %.6g\n", f, d);
        if (cfg.mode == MYDAQ_FINITE) {
            rc = mydaq_ctr_task_wait_done(t, (int)(5000 + 1000.0 * (double)cfg.pulses / f));
            printf("%llu pulses: %s\n", (unsigned long long)cfg.pulses, mydaq_error_name(rc));
        } else {
            sleep_s(secs);
        }
    } else {
        for (k = 0; k < (int)secs && rc != MYDAQ_ERR_NO_DEVICE; ++k) {
            double v = 0;
            sleep_s(1.0);
            rc = mydaq_ctr_task_read(t, &v, 1000);
            printf("%s: %.9g (%s)\n", what, v, mydaq_error_name(rc));
        }
    }
    mydaq_ctr_task_destroy(t);
    return 0;
}

static int cmd_freqout(mydaq_device *dev, const args *a)
{
    const double secs = atof(opt(a, "--secs", "3"));
    double f = 0;
    const int rc = mydaq_freqout_start(dev, atof(opt(a, "--freq", "10000")), &f);
    if (rc != MYDAQ_OK)
        return fail("freqout", rc);
    printf("FREQOUT on DIO4: %.9g Hz for %g s\n", f, secs);
    sleep_s(secs);
    mydaq_freqout_stop(dev);
    return 0;
}

static int cmd_dio(mydaq_device *dev, const args *a, int at)
{
    const char *what = at < a->argc ? a->argv[at] : "read";
    uint8_t v = 0, dir = 0;
    int rc;
    if (strcmp(what, "write") == 0 && at + 1 < a->argc) {
        const unsigned value = (unsigned)strtoul(a->argv[at + 1], NULL, 0);
        const unsigned mask = (unsigned)strtoul(opt(a, "--mask", "0xff"), NULL, 0);
        rc = mydaq_dio_write(dev, (uint8_t)value, (uint8_t)mask);
        if (rc == MYDAQ_OK)
            rc = mydaq_dio_set_direction(dev, (uint8_t)mask, 1);
        if (rc != MYDAQ_OK)
            return fail("dio write", rc);
    } else if (strcmp(what, "dir") == 0 && at + 2 < a->argc) {
        const unsigned mask = (unsigned)strtoul(a->argv[at + 1], NULL, 0);
        rc = mydaq_dio_set_direction(dev, (uint8_t)mask, strcmp(a->argv[at + 2], "out") == 0);
        if (rc != MYDAQ_OK)
            return fail("dio dir", rc);
    } else if (strcmp(what, "read") != 0) {
        return fail("arguments", MYDAQ_ERR_INVALID_ARG);
    }
    rc = mydaq_dio_read(dev, &v);
    if (rc != MYDAQ_OK)
        return fail("dio read", rc);
    mydaq_dio_get_direction(dev, &dir);
    printf("DIO levels 0x%02x, outputs 0x%02x\n", v, dir);
    return 0;
}

static int cmd_dmm(mydaq_device *dev, const args *a)
{
    const double range = atof(opt(a, "-r", "20"));
    const int count = atoi(opt(a, "-n", "1"));
    const char *f = opt(a, "-f", "dcv");
    mydaq_dmm_function fn = MYDAQ_DMM_DC_VOLTS;
    int k;
    if (strcmp(f, "acv") == 0)
        fn = MYDAQ_DMM_AC_VOLTS;
    else if (strcmp(f, "dca") == 0)
        fn = MYDAQ_DMM_DC_CURRENT;
    else if (strcmp(f, "aca") == 0)
        fn = MYDAQ_DMM_AC_CURRENT;
    else if (strcmp(f, "ohm") == 0)
        fn = MYDAQ_DMM_RESISTANCE;
    else if (strcmp(f, "diode") == 0)
        fn = MYDAQ_DMM_DIODE;
    else if (strcmp(f, "cont") == 0)
        fn = MYDAQ_DMM_CONTINUITY;
    for (k = 0; k < count; ++k) {
        mydaq_dmm_reading r;
        const int rc = mydaq_dmm_read(dev, fn, range, &r);
        if (rc != MYDAQ_OK)
            return fail("dmm read", rc);
        printf("DMM %s %g: packet \"%.9s\" display %.6g -> %.17g%s\n", f, range,
               (const char *)r.packet.raw, r.display, r.value,
               r.overload ? (r.overload > 0 ? " (+OVERLOAD)" : " (-OVERLOAD)") : "");
    }
    return 0;
}

int main(int argc, char **argv)
{
    args a = {argc - 1, argv + 1};
    mydaq_context *ctx = NULL;
    mydaq_device *dev = NULL;
    const char *cmd = NULL;
    mydaq_log_level level = MYDAQ_LOG_WARNING;
    int i, at = 0, rc;

    for (i = 0; i < a.argc; ++i) {
        const char *s = a.argv[i];
        if (s[0] != '-') {
            cmd = s;
            at = i + 1;
            break;
        }
        if (strcmp(s, "--serial") == 0 || strcmp(s, "--dump") == 0)
            ++i;
    }
    if (flag(&a, "-v"))
        level = MYDAQ_LOG_DEBUG;
    if (flag(&a, "-vv"))
        level = MYDAQ_LOG_TRACE;
    if (!cmd || flag(&a, "-h") || flag(&a, "--help")) {
        fprintf(stderr, "usage: mydaq-cli [--serial S] [--no-init] [-v|-vv] "
                        "list|info|ai|ai-stream|ao|ao-gen|dio|dmm|ctr|freqout|reset ...\n");
        return 2;
    }
    rc = mydaq_context_new(&ctx);
    if (rc != MYDAQ_OK)
        return fail("libusb", rc);
    mydaq_context_set_log(ctx, level, NULL, NULL);
    if (strcmp(cmd, "list") == 0) {
        rc = cmd_list(ctx);
        mydaq_context_free(ctx);
        return rc;
    }
    rc = mydaq_open(ctx, opt(&a, "--serial", NULL), flag(&a, "--no-init") ? MYDAQ_OPEN_NO_INIT : 0,
                    &dev);
    if (rc != MYDAQ_OK) {
        mydaq_context_free(ctx);
        return fail("open", rc);
    }
    if (strcmp(cmd, "info") == 0)
        rc = cmd_info(dev, &a);
    else if (strcmp(cmd, "ai") == 0)
        rc = cmd_ai(dev, &a);
    else if (strcmp(cmd, "ai-stream") == 0)
        rc = cmd_ai_stream(dev, ctx, &a);
    else if (strcmp(cmd, "ao") == 0)
        rc = cmd_ao(dev, &a);
    else if (strcmp(cmd, "ao-gen") == 0)
        rc = cmd_ao_gen(dev, &a);
    else if (strcmp(cmd, "ctr") == 0)
        rc = cmd_ctr(dev, &a, at);
    else if (strcmp(cmd, "freqout") == 0)
        rc = cmd_freqout(dev, &a);
    else if (strcmp(cmd, "dio") == 0)
        rc = cmd_dio(dev, &a, at);
    else if (strcmp(cmd, "dmm") == 0)
        rc = cmd_dmm(dev, &a);
    else if (strcmp(cmd, "reset") == 0)
        rc = mydaq_reset(dev) == MYDAQ_OK ? 0 : 1;
    else
        rc = fail(cmd, MYDAQ_ERR_INVALID_ARG);
    mydaq_close(dev);
    mydaq_context_free(ctx);
    return rc;
}
