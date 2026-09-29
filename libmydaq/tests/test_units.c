/* Unit tests of the pure parts: framing, ES51981 packets, DMM and calibration conversions,
 * rate arithmetic, scan-list encoding. Expected values are NI-DAQmx outputs from captures/. */
#include "test.h"

#include "internal.h"

#include <math.h>
#include <stdlib.h>

static int load_calmem(uint8_t *buf)
{
    FILE *f = fopen(test_data("calmem-036CD1C6.bin"), "rb");
    size_t n;
    if (!f)
        return 0;
    n = fread(buf, 1, MYDAQ_CALMEM_SIZE, f);
    fclose(f);
    return n == MYDAQ_CALMEM_SIZE;
}

static void test_frame_encode(void)
{
    /* the one-entry scan list command from captures/ai0-r10-static-x3 */
    static const uint8_t want[] = {0x00, 0x01, 0x00, 0x10, 0x00, 0x0c, 0x01, 0x0a,
                                   0x05, 0x12, 0x00, 0x00, 0x00, 0x04, 0x6a, 0x40};
    uint8_t out[64];
    md_buf b;
    size_t n;
    md_buf_init(&b);
    md_put_u32(&b, 0x05120000u);
    md_put_u16(&b, 4);
    md_put_u16(&b, 0x6a40);
    n = md_frame_encode(1, 0x0a, &b, out, sizeof out);
    CHECK_INT(n, sizeof want);
    CHECK(memcmp(out, want, n) == 0);

    /* padding to a multiple of 4 */
    md_buf_init(&b);
    md_put_hex(&b, "05120000 0006 2a40 6a41");
    n = md_frame_encode(1, 0x0a, &b, out, sizeof out);
    CHECK_INT(n, 20);
    CHECK(out[18] == 0 && out[19] == 0);

    /* odd hex digit count is a bug in a constant */
    md_buf_init(&b);
    md_put_hex(&b, "051");
    CHECK(b.overflow);
    CHECK_INT(md_frame_encode(1, 0, &b, out, sizeof out), 0);
}

static void test_frame_parse(void)
{
    static const uint8_t ack[] = {0, 1, 0, 0x10, 0, 0x0c, 1, 0, 0, 0, 0, 2, 0, 0, 0, 1};
    static const uint8_t data[] = {0, 1, 0, 0x10, 0, 0x0c, 1, 0, 0, 0, 0, 6, 0xff, 0xfc, 0x90, 0x33};
    static const uint8_t bad_len[] = {0, 1, 0, 0x14, 0, 0x10, 1, 0, 0, 0, 0, 2};
    static const uint8_t bad_kind[] = {0, 1, 0, 0x0c, 0, 0x08, 1, 0, 0, 0, 0, 3};
    md_resp r;
    CHECK_RC(md_frame_parse_response(ack, sizeof ack, &r));
    CHECK_INT(r.kind, MD_KIND_ACK);
    CHECK_INT(r.payload_len, 4);
    CHECK_INT(md_get_u32(r.payload), 1);
    CHECK_RC(md_frame_parse_response(data, sizeof data, &r));
    CHECK_INT(r.kind, MD_KIND_DATA);
    CHECK_INT(r.status, MD_STATUS_STREAM_RELEASE);
    CHECK_ERR(md_frame_parse_response(bad_len, sizeof bad_len, &r), MYDAQ_ERR_PROTOCOL);
    CHECK_ERR(md_frame_parse_response(bad_kind, sizeof bad_kind, &r), MYDAQ_ERR_PROTOCOL);
    CHECK_ERR(md_frame_parse_response(ack, 8, &r), MYDAQ_ERR_PROTOCOL);
}

static void test_es51981(void)
{
    mydaq_es51981 p;
    CHECK_RC(mydaq_es51981_parse((const uint8_t *)"10000;808\r\n", 11, &p));
    CHECK_INT(p.range, 1);
    CHECK_INT(p.digits, 0);
    CHECK_INT(p.function, 0xb);
    CHECK_INT(p.negative, 0);
    CHECK_INT(p.judge, 1);
    CHECK_INT(p.dc, 1);
    CHECK_INT(p.overload, 0);
    CHECK_RC(mydaq_es51981_parse((const uint8_t *)"40162;<08\r\n", 11, &p));
    CHECK_INT(p.range, 4);
    CHECK_INT(p.digits, 162);
    CHECK_INT(p.negative, 1);
    CHECK_ERR(mydaq_es51981_parse((const uint8_t *)"10000;8", 7, &p), MYDAQ_ERR_INVALID_ARG);
    CHECK_RC(mydaq_es51981_parse((const uint8_t *)"10000;=08", 9, &p));   /* no CR LF */
    CHECK_INT(p.overload, 1);
    CHECK_INT(p.negative, 1);
    CHECK_ERR(mydaq_es51981_parse((const uint8_t *)"1000:;808\r\n", 11, &p), MYDAQ_ERR_PROTOCOL);
    CHECK_ERR(mydaq_es51981_parse((const uint8_t *)"10000;808xx", 11, &p), MYDAQ_ERR_PROTOCOL);
    CHECK_ERR(mydaq_es51981_parse((const uint8_t *)"1a000;808\r\n", 11, &p), MYDAQ_ERR_PROTOCOL);
}

static void test_calibration(void)
{
    uint8_t mem[MYDAQ_CALMEM_SIZE];
    mydaq_calibration cal;
    CHECK(load_calmem(mem));
    CHECK_RC(mydaq_parse_calibration(mem, sizeof mem, &cal));
    CHECK_INT(cal.ai_ao_time, 1781160953);   /* 2026-06-11 06:55:53 UTC */
    CHECK_INT(cal.dmm_time, 1781160953 - 78);
    CHECK_INT(cal.ai_ao_temperature, 37);
    CHECK(strcmp(cal.user_info, "NI") == 0);
    CHECK(cal.backup_matches);
    CHECK(cal.checksums_ok);
    CHECK_EXACT(cal.ai[0].gain, 3.2511857e-4f);
    CHECK_EXACT(cal.ai[2].offset, -8.9092960e-4f);
    CHECK_EXACT(cal.ao[0][0].gain, 2943.3953f);
    CHECK_EXACT(cal.ao[1][0].offset, -220.39702f);
    CHECK_EXACT(cal.ao[2][2].gain, 16241.287f);   /* audio out L, ±2 V (nominal) */
    CHECK_EXACT(cal.dmm[0].offset, (float)7.013355934759602e-05);

    /* a corrupted directory is rejected, a corrupted value only fails the CRC */
    mem[0x1c] ^= 0xff;
    CHECK_ERR(mydaq_parse_calibration(mem, sizeof mem, &cal), MYDAQ_ERR_CALIBRATION);
    mem[0x1c] ^= 0xff;
    mem[0x160] ^= 0x01;
    CHECK_RC(mydaq_parse_calibration(mem, sizeof mem, &cal));
    CHECK(!cal.checksums_ok);
    CHECK(!cal.backup_matches);
    CHECK_ERR(mydaq_parse_calibration(mem, 100, &cal), MYDAQ_ERR_INVALID_ARG);
}

static void test_ai_conversion(void)
{
    uint8_t mem[MYDAQ_CALMEM_SIZE];
    mydaq_calibration cal;
    CHECK(load_calmem(mem));
    CHECK_RC(mydaq_parse_calibration(mem, sizeof mem, &cal));
    /* clipped input on ±2 V, captures/ai0-r2-static-x3 */
    CHECK_EXACT(mydaq_ai_code_to_volts(&cal, MYDAQ_AI_RANGE_2V, -32768), -2.2353725027060136);
    /* first streamed sample of captures/ai0-r10-finite-1k-n100 (code 0xe05e) */
    CHECK_EXACT(mydaq_ai_code_to_volts(&cal, MYDAQ_AI_RANGE_10V, (int16_t)(uint16_t)0xe05e),
                -2.6373597256024368);
    CHECK(isnan(mydaq_ai_code_to_volts(&cal, (mydaq_ai_range)7, 0)));
}

static void test_ao_conversion(void)
{
    uint8_t mem[MYDAQ_CALMEM_SIZE];
    mydaq_calibration cal;
    int16_t code;
    CHECK(load_calmem(mem));
    CHECK_RC(mydaq_parse_calibration(mem, sizeof mem, &cal));
    CHECK_RC(mydaq_ao_volts_to_code(&cal, MYDAQ_AO1, 0.5, &code));
    CHECK_INT(code, 1253);   /* captures/ao1-static-p0.5 */
    CHECK_RC(mydaq_ao_volts_to_code(&cal, MYDAQ_AO0, 0.0, &code));
    CHECK_INT(code, -167);
    CHECK_RC(mydaq_ao_volts_to_code(&cal, MYDAQ_AO0, 10.0, &code));
    CHECK_INT(code, 29267);
    CHECK_RC(mydaq_ao_volts_to_code(&cal, MYDAQ_AO0, -10.0, &code));
    CHECK_INT(code, -29601);
    CHECK_ERR(mydaq_ao_volts_to_code(&cal, MYDAQ_AO0, 10.5, &code), MYDAQ_ERR_INVALID_ARG);
    CHECK_ERR(mydaq_ao_volts_to_code(&cal, MYDAQ_AO0, NAN, &code), MYDAQ_ERR_INVALID_ARG);
    CHECK_ERR(mydaq_ao_volts_to_code(&cal, MYDAQ_AUDIO_OUT_LEFT, 0.0, &code), MYDAQ_ERR_INVALID_ARG);
}

static void test_dmm_conversion(void)
{
    static const struct {
        const char *packet;
        double range, want;
    } cases[] = {
        {"10000;808\r\n", 20.0, 0.00072986882878467441},   /* captures/dmm-dcv-r20 */
        {"00000;<08\r\n", 2.0, 7.013355934759602e-05},     /* dmm-dcv-r2-x2 */
        {"40162;<08\r\n", 0.2, -0.018572956927664929},     /* dmm-dcv-r0.2-x2 */
        {"40242;<08\r\n", 0.2, -0.027741520770438368},
        {"20000;808\r\n", 60.0, -0.019253648817539215},    /* dmm-dcv-r60-x2 */
    };
    uint8_t mem[MYDAQ_CALMEM_SIZE];
    mydaq_calibration cal;
    mydaq_dmm_reading r;
    mydaq_es51981 p;
    size_t i;
    CHECK(load_calmem(mem));
    CHECK_RC(mydaq_parse_calibration(mem, sizeof mem, &cal));
    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CHECK_RC(mydaq_es51981_parse((const uint8_t *)cases[i].packet, 11, &p));
        CHECK_RC(mydaq_dmm_convert(&cal, MYDAQ_DMM_DC_VOLTS, &p, cases[i].range, &r));
        CHECK_EXACT(r.value, cases[i].want);
        CHECK_INT(r.overload, 0);
    }
    /* overload keeps its sign */
    CHECK_RC(mydaq_es51981_parse((const uint8_t *)"10000;=08\r\n", 11, &p));
    CHECK_RC(mydaq_dmm_convert(&cal, MYDAQ_DMM_DC_VOLTS, &p, 20.0, &r));
    CHECK_INT(r.overload, -1);
    CHECK_EXACT(r.value, -20.0);
    /* scaling of the other functions (driver-notes 8): 1.234 kOhm on range digit 1 */
    CHECK_RC(mydaq_es51981_parse((const uint8_t *)"112343808\r\n", 11, &p));
    CHECK_INT(mydaq_dmm_cal_slot(MYDAQ_DMM_RESISTANCE, &p), 23);
    CHECK_RC(mydaq_dmm_convert(NULL, MYDAQ_DMM_RESISTANCE, &p, 2e3, &r));
    CHECK_NEAR(r.display, 1234.0, 1e-9);
    CHECK_INT(mydaq_dmm_cal_slot(MYDAQ_DMM_AC_VOLTS, &p), -1);
    {   /* diode and continuity packets of ELVISmx's DMM (captures dmm-elvis-diode / -cont) */
        static const struct {
            const char *packet;
            mydaq_dmm_function function;
            double range, want;
            int overload;
        } dc[] = {
            {"016181800\r\n", MYDAQ_DMM_DIODE, 2.0, 1.618, 0},
            {"006651800\r\n", MYDAQ_DMM_DIODE, 2.0, 0.665, 0},
            {"020001900\r\n", MYDAQ_DMM_DIODE, 2.0, 2.0, 1},            /* open: OPEN */
            {"020005900\r\n", MYDAQ_DMM_CONTINUITY, 200.0, 200.0, 1},   /* open */
            {"000045800\r\n", MYDAQ_DMM_CONTINUITY, 200.0, 0.0, 0},     /* wire */
            {"014945800\r\n", MYDAQ_DMM_CONTINUITY, 200.0, 149.0, 0},   /* resistor */
        };
        for (i = 0; i < sizeof dc / sizeof dc[0]; ++i) {
            CHECK_RC(mydaq_es51981_parse((const uint8_t *)dc[i].packet, 11, &p));
            CHECK_RC(mydaq_dmm_convert(&cal, dc[i].function, &p, dc[i].range, &r));
            CHECK_NEAR(r.value, dc[i].want, 1e-12);
            CHECK_INT(r.overload != 0, dc[i].overload);
        }
    }
    CHECK_ERR(mydaq_es51981_parse((const uint8_t *)"11234>808\r\n", 11, &p), MYDAQ_OK);
    CHECK_ERR(mydaq_dmm_convert(&cal, MYDAQ_DMM_DC_VOLTS, &p, 2.0, &r), MYDAQ_ERR_PROTOCOL);
}

static void test_dmm_ranges(void)
{
    const mydaq_dmm_range_info *r;
    size_t n, f, total = 0;
    CHECK_RC(mydaq_dmm_ranges(MYDAQ_DMM_DC_VOLTS, &r, &n));
    CHECK_INT(n, 4);
    CHECK_EXACT(r[0].full_scale, 60.0);
    CHECK_EXACT(r[3].full_scale, 0.2);
    CHECK(r[0].supported);
    for (f = 0; f < MYDAQ_DMM_FUNCTION_COUNT; ++f) {
        size_t i;
        CHECK_RC(mydaq_dmm_ranges((mydaq_dmm_function)f, &r, &n));
        CHECK(n >= 1);
        for (i = 0; i < n; ++i) {
            CHECK_INT(r[i].function, f);
            CHECK(i == 0 || r[i].full_scale < r[i - 1].full_scale);
        }
        total += n;
    }
    CHECK_INT(total, 21);
    /* verified by captures: diode and continuity (ELVISmx), 20 kOhm; 200 kOhm .. 20 MOhm not yet */
    CHECK_RC(mydaq_dmm_ranges(MYDAQ_DMM_DIODE, &r, &n));
    CHECK(n == 1 && r[0].supported && r[0].verified);
    CHECK_RC(mydaq_dmm_ranges(MYDAQ_DMM_CONTINUITY, &r, &n));
    CHECK(n == 1 && r[0].supported && r[0].verified);
    CHECK_RC(mydaq_dmm_ranges(MYDAQ_DMM_RESISTANCE, &r, &n));
    CHECK(n == 6 && !r[0].verified && !r[2].verified && r[3].verified && r[5].verified);
    CHECK_ERR(mydaq_dmm_ranges((mydaq_dmm_function)9, &r, &n), MYDAQ_ERR_INVALID_ARG);
}

static void test_rates(void)
{
    CHECK_INT(md_rate_divisor(1000.0), 100000);
    CHECK_INT(md_rate_divisor(200e3), 500);
    CHECK_INT(md_rate_divisor(1e6), 500);         /* clamped to the maximum rate */
    CHECK_INT(md_rate_divisor(3000.0), 33333);
    CHECK_INT(md_rate_divisor(7000.0), 14285);    /* floor, as DAQmx (captures/ai0-r10-finite-7k-n700) */
    CHECK_INT(md_rate_divisor(12345.0), 8100);
    CHECK_INT(md_rate_divisor(70e3), 1428);
    CHECK_INT(md_rate_divisor(0.001), 0xffffffffu);
    CHECK_INT(md_rate_divisor(-1.0), 0xffffffffu);
    CHECK_EXACT(mydaq_ai_coerce_rate(44100.0, 1), 1e8 / 2267.0);
    CHECK_INT(md_ai_convert_ticks(1, 100000), 1500);   /* captured */
    CHECK_INT(md_ai_convert_ticks(2, 100000), 1250);   /* captured (on demand) */
    CHECK_INT(md_ai_convert_ticks(2, 2000), 1000);
    CHECK_INT(md_ai_convert_ticks(2, 500), 250);
    CHECK_INT(md_ai_convert_ticks(1, 500), 500);
}

static void test_scan_list(void)
{
    mydaq_ai_channel_config c[2] = {{MYDAQ_AI0, MYDAQ_AI_RANGE_10V}, {MYDAQ_AI1, MYDAQ_AI_RANGE_10V}};
    uint16_t e;
    CHECK_RC(md_ai_scan_entry(&c[0], 1, &e));
    CHECK_INT(e, 0x6a40);
    CHECK_RC(md_ai_scan_entry(&c[0], 0, &e));
    CHECK_INT(e, 0x2a40);
    CHECK_RC(md_ai_scan_entry(&c[1], 1, &e));
    CHECK_INT(e, 0x6a41);
    c[0].range = MYDAQ_AI_RANGE_2V;
    CHECK_RC(md_ai_scan_entry(&c[0], 1, &e));
    CHECK_INT(e, 0x6c40);
    CHECK_RC(md_ai_validate_channels(c, 2, 0));
    c[1].channel = MYDAQ_AI0;
    CHECK_ERR(md_ai_validate_channels(c, 2, 1), MYDAQ_ERR_INVALID_ARG);
    c[1].channel = MYDAQ_AUDIO_IN_LEFT;
    CHECK_ERR(md_ai_validate_channels(c, 2, 1), MYDAQ_ERR_INVALID_ARG);   /* AI + audio */
    c[0].channel = MYDAQ_AUDIO_IN_RIGHT;
    c[1].range = MYDAQ_AI_RANGE_2V;
    CHECK_RC(md_ai_validate_channels(c, 2, 1));
    CHECK_ERR(md_ai_validate_channels(c, 2, 0), MYDAQ_ERR_NOT_SUPPORTED);  /* on demand */
    CHECK_RC(md_ai_scan_entry(&c[1], 0, &e));
    CHECK_INT(e, 0x2c42);   /* captures/audioLR-finite-48k-n480 */
    c[1].range = MYDAQ_AI_RANGE_10V;
    CHECK_ERR(md_ai_validate_channels(c, 2, 1), MYDAQ_ERR_INVALID_ARG);   /* audio is ±2 V */
    CHECK_ERR(md_ai_validate_channels(c, 3, 1), MYDAQ_ERR_INVALID_ARG);
    CHECK_ERR(md_ai_validate_channels(c, 0, 1), MYDAQ_ERR_INVALID_ARG);
}

static void test_errors(void)
{
    int e;
    for (e = MYDAQ_OK; e >= MYDAQ_ERR_CALIBRATION; --e) {
        CHECK(strcmp(mydaq_strerror(e), "Unknown error") != 0);
        CHECK(strncmp(mydaq_error_name(e), "MYDAQ_", 6) == 0);
    }
    CHECK(strcmp(mydaq_strerror(-999), "Unknown error") == 0);
    CHECK(strcmp(mydaq_version(), "0.1.0") == 0);
}

const test_case unit_tests[] = {
    {"unit.frame_encode", test_frame_encode},
    {"unit.frame_parse", test_frame_parse},
    {"unit.es51981", test_es51981},
    {"unit.calibration", test_calibration},
    {"unit.ai_conversion", test_ai_conversion},
    {"unit.ao_conversion", test_ao_conversion},
    {"unit.dmm_conversion", test_dmm_conversion},
    {"unit.dmm_ranges", test_dmm_ranges},
    {"unit.rates", test_rates},
    {"unit.scan_list", test_scan_list},
    {"unit.errors", test_errors},
    {NULL, NULL},
};
