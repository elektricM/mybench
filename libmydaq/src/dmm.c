/* DMM: function/range table, NI-exact packet conversion, readings (docs/protocol.md section 9,
 * docs/driver-notes.md section 8). */
#include "internal.h"
#include "proto.h"

#include <math.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

/* Largest first per function (contiguous per function: mydaq_dmm_ranges returns a slice). */
static const mydaq_dmm_range_info ranges[] = {
    {MYDAQ_DMM_DC_VOLTS, 60.0, "60 V", 0.1, 1, 1},
    {MYDAQ_DMM_DC_VOLTS, 20.0, "20 V", 0.01, 1, 1},
    {MYDAQ_DMM_DC_VOLTS, 2.0, "2 V", 0.001, 1, 1},
    {MYDAQ_DMM_DC_VOLTS, 0.2, "200 mV", 0.0001, 1, 1},
    {MYDAQ_DMM_AC_VOLTS, 20.0, "20 V", 0.01, 1, 1},
    {MYDAQ_DMM_AC_VOLTS, 2.0, "2 V", 0.001, 1, 1},
    {MYDAQ_DMM_AC_VOLTS, 0.2, "200 mV", 0.0001, 1, 1},
    {MYDAQ_DMM_DC_CURRENT, 1.0, "1 A", 0.001, 1, 1},
    {MYDAQ_DMM_DC_CURRENT, 0.2, "200 mA", 0.0001, 1, 1},
    {MYDAQ_DMM_DC_CURRENT, 0.02, "20 mA", 0.00001, 1, 1},
    {MYDAQ_DMM_AC_CURRENT, 1.0, "1 A", 0.001, 1, 1},
    {MYDAQ_DMM_AC_CURRENT, 0.2, "200 mA", 0.0001, 1, 1},
    {MYDAQ_DMM_AC_CURRENT, 0.02, "20 mA", 0.00001, 1, 1},
    {MYDAQ_DMM_RESISTANCE, 20e6, "20 MOhm", 1e4, 1, 1},     /* dmm-ohm-r20M-x2 */
    {MYDAQ_DMM_RESISTANCE, 2e6, "2 MOhm", 1e3, 1, 1},       /* dmm-ohm-r2M-x2 */
    {MYDAQ_DMM_RESISTANCE, 200e3, "200 kOhm", 100.0, 1, 1},   /* dmm-ohm-r200k-x2 */
    {MYDAQ_DMM_RESISTANCE, 20e3, "20 kOhm", 10.0, 1, 1},   /* dmm-ohm-r20k-x2 */
    {MYDAQ_DMM_RESISTANCE, 2e3, "2 kOhm", 1.0, 1, 1},
    {MYDAQ_DMM_RESISTANCE, 200.0, "200 Ohm", 0.1, 1, 1},
    {MYDAQ_DMM_DIODE, 2.0, "2 V", 0.001, 1, 1},             /* dmm-elvis-diode (ELVISmx) */
    {MYDAQ_DMM_CONTINUITY, 200.0, "200 Ohm", 1.0, 1, 1},   /* dmm-elvis-cont (ELVISmx) */
};

#define RANGE_COUNT (sizeof ranges / sizeof ranges[0])

/* dmm.set_mode byte per entry of `ranges` (docs/protocol.md 9.2; -1 = not decoded yet). Every
 * mode byte is captured (`verified` = 1): DAQmx for V, A and Ohm (200 Ohm .. 20 MOhm = 13..19),
 * ELVISmx's own DMM for diode (20) and continuity (21). The 200 kOhm .. 20 MOhm captures had open
 * leads (overload only): the scaling of their finite readings (R digit 3..5) follows the driver
 * table and is not confirmed yet. */
static const int range_modes[RANGE_COUNT] = {
    2, 1, 0, 3,                 /* DC V 60, 20, 2, 0.2 */
    5, 4, 7,                    /* AC V 20, 2, 0.2 */
    10, 9, 8,                   /* DC A 1, 0.2, 0.02 */
    13, 12, 11,                 /* AC A 1, 0.2, 0.02 */
    19, 18, 17, 16, 15, 14,     /* resistance 20M .. 200 */
    20, 21,                     /* diode, continuity (driver capability table; unverified) */
};

int mydaq_dmm_ranges(mydaq_dmm_function function, const mydaq_dmm_range_info **out, size_t *count)
{
    size_t i, first = RANGE_COUNT, n = 0;
    if (!out || !count || (unsigned)function >= MYDAQ_DMM_FUNCTION_COUNT)
        return MYDAQ_ERR_INVALID_ARG;
    for (i = 0; i < RANGE_COUNT; ++i) {
        if (ranges[i].function == function) {
            if (first == RANGE_COUNT)
                first = i;
            ++n;
        }
    }
    *out = n ? &ranges[first] : NULL;
    *count = n;
    return MYDAQ_OK;
}

static int find_range(mydaq_dmm_function f, double full_scale)
{
    size_t i;
    for (i = 0; i < RANGE_COUNT; ++i)
        if (ranges[i].function == f &&
            fabs(ranges[i].full_scale - full_scale) <= 1e-9 * ranges[i].full_scale)
            return (int)i;
    return -1;
}

/* ---- conversion ------------------------------------------------------------------------ */

/* Function nibble of the ES51981 packet */
#define ES_VOLTS 0xb     /* ';' */
#define ES_MILLIAMPS 0xf /* '?' */
#define ES_AMPS 0x0      /* '0' */
#define ES_MICROAMPS 0xd /* '=' */
#define ES_OHMS 0x3      /* '3' */
#define ES_DIODE 0x1     /* '1' */
#define ES_CONTINUITY 0x5/* '5' */

static int scale_of(int function, int range, double *scale)
{
    static const double volts[] = {1.0, 10.0, 100.0, 1000.0, 0.1};
    static const double milliamps[] = {10.0e-3, 100.0e-3};
    static const double amps[] = {1.0, 10.0};
    static const double microamps[] = {100e-6, 1000e-6};
    static const double ohms[] = {1e2, 1e3, 1e4, 1e5, 1e6, 1e7};
    const double *table;
    size_t n;
    switch (function) {
    case ES_VOLTS: table = volts; n = 5; break;
    case ES_MILLIAMPS: table = milliamps; n = 2; break;
    case ES_AMPS: table = amps; n = 2; break;
    case ES_MICROAMPS: table = microamps; n = 2; break;
    case ES_OHMS: table = ohms; n = 6; break;
    case ES_DIODE: *scale = 1.0; return range == 0;
    default: return 0;
    }
    if (range < 0 || (size_t)range >= n)
        return 0;
    *scale = table[range];
    return 1;
}

int mydaq_dmm_cal_slot(mydaq_dmm_function function, const mydaq_es51981 *p)
{
    const int r = p ? p->range : -1;
    if (!p)
        return -1;
    switch (p->function) {
    case ES_VOLTS:
        if (function == MYDAQ_DMM_DC_VOLTS)
            return r == 0 || r == 1 || r == 2 || r == 4 ? r : -1;
        if (function == MYDAQ_DMM_AC_VOLTS)
            return r == 0 ? 5 : r == 1 ? 6 : r == 4 ? 9 : -1;
        return -1;
    case ES_MILLIAMPS:
        if (function == MYDAQ_DMM_DC_CURRENT)
            return r == 0 ? 10 : r == 1 ? 11 : -1;
        if (function == MYDAQ_DMM_AC_CURRENT)
            return r == 0 ? 16 : r == 1 ? 17 : -1;
        return -1;
    case ES_AMPS:
        if (function == MYDAQ_DMM_DC_CURRENT)
            return r == 0 ? 14 : -1;
        if (function == MYDAQ_DMM_AC_CURRENT)
            return r == 0 ? 20 : -1;
        return -1;
    case ES_OHMS:
        return function == MYDAQ_DMM_RESISTANCE && r >= 0 && r <= 5 ? 22 + r : -1;
    default:
        return -1;
    }
}

int mydaq_dmm_convert(const mydaq_calibration *cal, mydaq_dmm_function function,
                      const mydaq_es51981 *p, double full_scale, mydaq_dmm_reading *reading)
{
    const double d0 = p ? (double)(p->raw[1] - 0x30) : 0.0;
    const double d1 = p ? (double)(p->raw[2] - 0x30) : 0.0;
    const double d2 = p ? (double)(p->raw[3] - 0x30) : 0.0;
    const double d3 = p ? (double)(p->raw[4] - 0x30) : 0.0;
    double last, v, scale = 1.0;
    int slot;

    if (!p || !reading)
        return MYDAQ_ERR_INVALID_ARG;
    memset(reading, 0, sizeof *reading);
    reading->packet = *p;
    /* Same operations, in the same order, as NI's driver: */
    last = d3 * 0.001;
    v = d1 * 0.1 + d0 + d2 * 0.01 + last;
    if (p->function == ES_CONTINUITY) {
        v = (v - last) * 100.0;
    } else {
        if (!scale_of(p->function, p->range, &scale))
            return MYDAQ_ERR_PROTOCOL;
        v = v * scale;
    }
    if (p->negative)
        v = -v;
    reading->display = v;
    slot = mydaq_dmm_cal_slot(function, p);
    if (cal && slot >= 0) {
        const double gain = (double)cal->dmm[slot].gain;
        const double offset = (double)cal->dmm[slot].offset;
        v = gain * v + offset;
    }
    reading->value = v;
    if (p->overload) {
        reading->overload = p->negative ? -1 : 1;
        reading->value = p->negative ? -full_scale : full_scale;
    }
    return MYDAQ_OK;
}

/* ---- reading --------------------------------------------------------------------------- */

static int expected_function(mydaq_dmm_function f, int es_function)
{
    switch (f) {
    case MYDAQ_DMM_DC_VOLTS:
    case MYDAQ_DMM_AC_VOLTS: return es_function == ES_VOLTS;
    case MYDAQ_DMM_DC_CURRENT:
    case MYDAQ_DMM_AC_CURRENT:
        return es_function == ES_MILLIAMPS || es_function == ES_AMPS || es_function == ES_MICROAMPS;
    case MYDAQ_DMM_RESISTANCE: return es_function == ES_OHMS;
    case MYDAQ_DMM_DIODE: return es_function == ES_DIODE;
    case MYDAQ_DMM_CONTINUITY: return es_function == ES_CONTINUITY;
    }
    return 0;
}

int mydaq_dmm_read(mydaq_device *dev, mydaq_dmm_function function, double range,
                   mydaq_dmm_reading *reading)
{
    return mydaq_dmm_read_ex(dev, function, range, 0, reading);
}

int mydaq_dmm_read_ex(mydaq_device *dev, mydaq_dmm_function function, double range,
                      unsigned flags, mydaq_dmm_reading *reading)
{
    return mydaq_dmm_read_timeout(dev, function, range, flags, 10000, reading);
}

/* dmm.read(u32 1, u32 timeout ms): the device answers with the next packet the meter chip sends
 * (every 0.5 s for V/A/Ohm, every ~94 ms for diode/continuity once the mode is set), or with
 * status -225202 and no packet after the timeout (protocol.md 9.1, 9.4; dmm-dcv-timeout). */
#define DMM_STATUS_TIMEOUT (-225202)

int mydaq_dmm_read_timeout(mydaq_device *dev, mydaq_dmm_function function, double range,
                           unsigned flags, int timeout_ms, mydaq_dmm_reading *reading)
{
    uint32_t read_args[2] = {1, 10000};
    int r, key;
    const uint8_t *pkt;
    mydaq_es51981 es;
    size_t len;
    md_resp resp;
    md_buf b;
    int rc = MYDAQ_OK;

    if (!dev || !reading || (unsigned)function >= MYDAQ_DMM_FUNCTION_COUNT || timeout_ms <= 0)
        return MYDAQ_ERR_INVALID_ARG;
    r = find_range(function, range);
    if (r < 0)
        return MYDAQ_ERR_INVALID_ARG;
    if (!ranges[r].supported || range_modes[r] < 0)
        return MYDAQ_ERR_NOT_SUPPORTED;

    if ((flags & MYDAQ_DMM_HW_AUTORANGE) && function == MYDAQ_DMM_CONTINUITY)
        return MYDAQ_ERR_INVALID_ARG;   /* NI rejects continuity with autorange too */
    read_args[1] = (uint32_t)timeout_ms;
    key = 1 + (range_modes[r] << 1 | ((flags & MYDAQ_DMM_HW_AUTORANGE) ? 1 : 0));
    md_cmd_lock(dev);
    if (!(flags & MYDAQ_DMM_KEEP_MODE) || dev->dmm_mode_key != key) {
        md_buf_init(&b);
        md_put_u32(&b, MD_OBJ_DMM);
        md_put_u8(&b, (uint8_t)range_modes[r]);
        md_put_u8(&b, (flags & MYDAQ_DMM_HW_AUTORANGE) ? 1u : 0u);   /* set_mode(mode, autorange) */
        md_put_hex(&b, "0000");
        dev->dmm_mode_key = 0;
        rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_WRITE, &b, MD_TIMEOUT_LONG, 0, NULL);   /* ~0.5-2 s */
        if (rc == MYDAQ_OK)
            dev->dmm_mode_key = key;
    }
    if (rc == MYDAQ_OK) {
        md_buf_init(&b);
        md_put_u32(&b, MD_OBJ_DMM);
        md_put_u32(&b, read_args[0]);
        md_put_u32(&b, read_args[1]);
        rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_COMMIT, &b, (unsigned)timeout_ms + MD_TIMEOUT_CMD,
                    MD_CMD_ANY_STATUS, &resp);
        if (rc == MYDAQ_OK && resp.status == DMM_STATUS_TIMEOUT)
            rc = MYDAQ_ERR_TIMEOUT;   /* no packet within timeout_ms (e.g. the meter is busy) */
        else if (rc == MYDAQ_OK && resp.status != 0) {
            MD_ERR(dev->ctx, "DMM read: device status %ld", (long)resp.status);
            rc = MYDAQ_ERR_DEVICE;
        }
    }
    if (rc == MYDAQ_OK)
        rc = md_resp_blob(&resp, 0, &pkt, &len);
    if (rc == MYDAQ_OK)
        rc = mydaq_es51981_parse(pkt, len, &es);
    if (rc != MYDAQ_OK && rc != MYDAQ_ERR_TIMEOUT)
        dev->dmm_mode_key = 0;   /* send set_mode again next time */
    md_cmd_unlock(dev);
    if (rc != MYDAQ_OK)
        return rc;
    if (!expected_function(function, es.function))
        MD_WARN(dev->ctx, "DMM: packet function 0x%x does not match the requested function",
                (unsigned)es.function);
    return mydaq_dmm_convert(&dev->cal, function, &es, ranges[r].full_scale, reading);
}
