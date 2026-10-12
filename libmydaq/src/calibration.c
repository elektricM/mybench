/* Calibration memory (object 05 07, 2048 bytes; docs/protocol.md section 4) and conversions.
 *
 * The constants are float32; conversions are evaluated in double exactly as NI-DAQmx does,
 * which reproduces every DAQmx value in the captures bit-exactly. The
 * expression must not be contracted into a fused multiply-add (different rounding), hence the
 * pragma here and -ffp-contract=off in the build. */
#include "internal.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

_Static_assert(sizeof(float) == 4, "float must be IEEE-754 binary32");

#define LABVIEW_EPOCH_TO_UNIX 2082844800   /* seconds from 1904-01-01 to 1970-01-01 (UTC) */

/* Directory ids of the sections (driver-notes 5.1) */
#define SEC_AIAO 0x40u
#define SEC_AIAO_BACKUP 0x42u
#define SEC_USER_INFO 0x44u
#define SEC_DMM 0xcb0du
#define SEC_DMM_BACKUP 0xcb0fu

#define DIR_START 0x0cu      /* directory CRC covers 0x0c..0x3f, check word at 0x40 */
#define DIR_CHECK 0x40u
#define DIR_PAIRS 0x18u

/* Offsets from the section start (the u16 length word) */
#define SEC_TIME 0x02u       /* u64 seconds since 1904-01-01 UTC */
#define SEC_TEMPERATURE 0x0au
#define AIAO_AI_TABLE 0x56u  /* 4 mode records (17 bytes) after the 16-byte header */
#define AIAO_AO_BLOCK0 0x9fu /* ranges of the first AO block (after its 17-byte mode record) */
#define AIAO_AO_BLOCK_STRIDE 0x31u
#define DMM_TABLE 0x12u

static float get_f32(const uint8_t *p)
{
    const uint32_t bits = md_get_u32le(p);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static void get_pair(const uint8_t *p, mydaq_cal_pair *out)
{
    out->gain = get_f32(p);
    out->offset = get_f32(p + 4);
}

/* CRC-16, polynomial 0x8005, init 0xffff, MSB first, no final xor, over the data with a zero byte
 * after every byte (NI processes each byte as a 16-bit step). */
static uint16_t crc16_nulled(const uint8_t *p, size_t n)
{
    uint16_t c = 0xffffu;
    size_t i;
    int half, bit;
    for (i = 0; i < n; ++i) {
        for (half = 0; half < 2; ++half) {
            c ^= (uint16_t)((half ? 0u : p[i]) << 8);
            for (bit = 0; bit < 8; ++bit)
                c = (uint16_t)((c & 0x8000u) ? ((unsigned)c << 1) ^ 0x8005u : (unsigned)c << 1);
        }
    }
    return c;
}

/* A section: [off, off+2) length word, content, u16 check word. The AI/AO sections store
 * length = content + 2, the others length = content. */
typedef struct cal_section {
    uint32_t off;
    uint32_t content;    /* content start */
    uint32_t check;      /* offset of the check word */
    int crc_ok;
} cal_section;

static int find_section(const uint8_t *mem, uint32_t id, cal_section *sec)
{
    const uint32_t dir_len = md_get_u32le(mem + 8);
    uint32_t i, pairs;
    if (dir_len < 16u || DIR_PAIRS + dir_len - 16u > DIR_CHECK)
        return 0;
    pairs = (dir_len - 16u) / 8u;
    for (i = 0; i < pairs; ++i) {
        const uint8_t *e = mem + DIR_PAIRS + 8u * i;
        if (md_get_u32le(e) == id) {
            const uint32_t off = md_get_u32le(e + 4);
            uint32_t len;
            if (off + 2u > MYDAQ_CALMEM_SIZE)
                return 0;
            len = md_get_u16le(mem + off);
            sec->off = off;
            sec->content = off + 2u;
            sec->check = (id == SEC_AIAO || id == SEC_AIAO_BACKUP) ? off + len : off + 2u + len;
            if (sec->check < sec->content || sec->check + 2u > MYDAQ_CALMEM_SIZE)
                return 0;
            sec->crc_ok = crc16_nulled(mem + sec->content, sec->check - sec->content) ==
                          md_get_u16le(mem + sec->check);
            return 1;
        }
    }
    return 0;
}

static int usable(const mydaq_cal_pair *p)
{
    return isfinite(p->gain) && isfinite(p->offset) && p->gain != 0.0f;
}

static int64_t labview_time(const uint8_t *p)
{
    const uint64_t secs = (uint64_t)md_get_u32le(p) | (uint64_t)md_get_u32le(p + 4) << 32;
    if (secs > (uint64_t)INT64_MAX)
        return 0;
    return (int64_t)secs - LABVIEW_EPOCH_TO_UNIX;
}

static int same_content(const uint8_t *mem, const cal_section *a, const cal_section *b)
{
    const uint32_t n = a->check + 2u - a->off;
    return b->check + 2u - b->off == n && memcmp(mem + a->off, mem + b->off, n) == 0;
}

int mydaq_parse_calibration(const uint8_t *mem, size_t len, mydaq_calibration *cal)
{
    cal_section aiao, aiao_b, dmm, dmm_b, user;
    int have_aiao_b, have_dmm_b, have_user;
    size_t i, k;

    if (!mem || !cal || len != MYDAQ_CALMEM_SIZE)
        return MYDAQ_ERR_INVALID_ARG;
    memset(cal, 0, sizeof *cal);
    if (!find_section(mem, SEC_AIAO, &aiao) || !find_section(mem, SEC_DMM, &dmm))
        return MYDAQ_ERR_CALIBRATION;
    if (aiao.check < aiao.off + AIAO_AO_BLOCK0 + (MYDAQ_CAL_AO_BLOCKS - 1) * AIAO_AO_BLOCK_STRIDE +
                         8u * MYDAQ_CAL_AO_ENTRIES ||
        dmm.check < dmm.off + DMM_TABLE + 8u * MYDAQ_CAL_DMM_ENTRIES)
        return MYDAQ_ERR_CALIBRATION;

    cal->ai_ao_time = labview_time(mem + aiao.off + SEC_TIME);
    cal->dmm_time = labview_time(mem + dmm.off + SEC_TIME);
    cal->ai_ao_temperature = md_get_u32le(mem + aiao.off + SEC_TEMPERATURE);
    cal->dmm_temperature = md_get_u32le(mem + dmm.off + SEC_TEMPERATURE);
    for (i = 0; i < MYDAQ_CAL_AI_ENTRIES; ++i)
        get_pair(mem + aiao.off + AIAO_AI_TABLE + 8u * i, &cal->ai[i]);
    for (k = 0; k < MYDAQ_CAL_AO_BLOCKS; ++k)
        for (i = 0; i < MYDAQ_CAL_AO_ENTRIES; ++i)
            get_pair(mem + aiao.off + AIAO_AO_BLOCK0 + AIAO_AO_BLOCK_STRIDE * k + 8u * i,
                     &cal->ao[k][i]);
    for (i = 0; i < MYDAQ_CAL_DMM_ENTRIES; ++i)
        get_pair(mem + dmm.off + DMM_TABLE + 8u * i, &cal->dmm[i]);

    /* calibration info: UTF-16LE strings; the first is UserDefinedCalibrationInfo */
    have_user = find_section(mem, SEC_USER_INFO, &user);
    if (have_user) {
        for (i = 0; i + 1 < sizeof cal->user_info && user.content + 2u * i + 2u <= user.check; ++i) {
            const uint16_t c = md_get_u16le(mem + user.content + 2u * i);
            if (c == 0 || c > 0x7e)
                break;
            cal->user_info[i] = (char)c;
        }
    }

    have_aiao_b = find_section(mem, SEC_AIAO_BACKUP, &aiao_b);
    have_dmm_b = find_section(mem, SEC_DMM_BACKUP, &dmm_b);
    cal->backup_matches = have_aiao_b && have_dmm_b && same_content(mem, &aiao, &aiao_b) &&
                          same_content(mem, &dmm, &dmm_b);
    cal->checksums_ok = crc16_nulled(mem + DIR_START, DIR_CHECK - DIR_START) ==
                            md_get_u16le(mem + DIR_CHECK) &&
                        aiao.crc_ok && dmm.crc_ok && (!have_aiao_b || aiao_b.crc_ok) &&
                        (!have_dmm_b || dmm_b.crc_ok) && (!have_user || user.crc_ok);

    if (!usable(&cal->ai[0]) || !usable(&cal->ai[2]) || !usable(&cal->ao[0][0]) ||
        !usable(&cal->ao[1][0]))
        return MYDAQ_ERR_CALIBRATION;
    return MYDAQ_OK;
}

const mydaq_calibration *mydaq_get_calibration(const mydaq_device *dev)
{
    return dev ? &dev->cal : NULL;
}

int mydaq_get_calibration_memory(const mydaq_device *dev, uint8_t *buf, size_t len)
{
    if (!dev || !buf)
        return MYDAQ_ERR_INVALID_ARG;
    memcpy(buf, dev->calmem, len < MYDAQ_CALMEM_SIZE ? len : MYDAQ_CALMEM_SIZE);
    return MYDAQ_OK;
}

/* ---- conversions ----------------------------------------------------------------------- */

static int ai_entry(mydaq_ai_range range)
{
    switch (range) {
    case MYDAQ_AI_RANGE_10V: return 0;
    case MYDAQ_AI_RANGE_2V: return 2;
    }
    return -1;
}

double mydaq_ai_code_to_volts(const mydaq_calibration *cal, mydaq_ai_range range, int16_t code)
{
    const int e = ai_entry(range);
    double offset, gain;
    if (!cal || e < 0)
        return NAN;
    offset = (double)cal->ai[e].offset;
    gain = (double)cal->ai[e].gain;
    return offset + gain * (double)code;
}

int mydaq_ao_volts_to_code(const mydaq_calibration *cal, mydaq_ao_channel channel, double volts,
                           int16_t *code)
{
    double offset, gain, c;
    if (!cal || !code || (channel != MYDAQ_AO0 && channel != MYDAQ_AO1))
        return MYDAQ_ERR_INVALID_ARG;
    if (!(volts >= -10.0 && volts <= 10.0))   /* also rejects NaN */
        return MYDAQ_ERR_INVALID_ARG;
    offset = (double)cal->ao[channel][0].offset;
    gain = (double)cal->ao[channel][0].gain;
    c = nearbyint(offset + gain * volts);   /* round half to even (default rounding mode) */
    if (c > 32767.0)
        c = 32767.0;
    if (c < -32768.0)
        c = -32768.0;
    *code = (int16_t)c;
    return MYDAQ_OK;
}
