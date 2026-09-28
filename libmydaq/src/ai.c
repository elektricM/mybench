/* Analog input: channel validation, scan list, sample-clock arithmetic and on-demand reads
 * (docs/protocol.md section 5). */
#include "internal.h"
#include "proto.h"

#include <math.h>

#define AI_ENTRY_BASE 0x2840u     /* constant bits of a scan-list entry (meaning unknown) */
#define AI_ENTRY_LAST 0x4000u     /* set on the last entry */
#define AI_ENTRY_RANGE_SHIFT 9u   /* bits 9..10: 1 = ±10 V, 2 = ±2 V */

#define DIVISOR_MIN 500u          /* 100 MHz / 500 = 200 kS/s per channel */
#define DIVISOR_MAX 0xffffffffu   /* 32-bit divisor: 0.0233 S/s */

int md_ai_validate_channels(const mydaq_ai_channel_config *channels, size_t n, int timed)
{
    size_t i, audio = 0;
    if (!channels || n < 1 || n > MYDAQ_AI_MAX_CHANNELS)
        return MYDAQ_ERR_INVALID_ARG;
    for (i = 0; i < n; ++i) {
        const mydaq_ai_channel_config *c = &channels[i];
        if (c->channel != MYDAQ_AI0 && c->channel != MYDAQ_AI1 &&
            c->channel != MYDAQ_AUDIO_IN_LEFT && c->channel != MYDAQ_AUDIO_IN_RIGHT)
            return MYDAQ_ERR_INVALID_ARG;
        if (c->range != MYDAQ_AI_RANGE_10V && c->range != MYDAQ_AI_RANGE_2V)
            return MYDAQ_ERR_INVALID_ARG;
        if (c->channel == MYDAQ_AUDIO_IN_LEFT || c->channel == MYDAQ_AUDIO_IN_RIGHT) {
            if (c->range != MYDAQ_AI_RANGE_2V)
                return MYDAQ_ERR_INVALID_ARG;   /* audio inputs are ±2 V only */
            ++audio;
        }
    }
    if (n == 2 && channels[0].channel == channels[1].channel)
        return MYDAQ_ERR_INVALID_ARG;
    if (audio && audio != n)
        return MYDAQ_ERR_INVALID_ARG;   /* AI and audio share the converter */
    if (audio && !timed)
        return MYDAQ_ERR_NOT_SUPPORTED; /* audio needs a sample clock (DAQmx -201087) */
    return MYDAQ_OK;
}

int md_ai_scan_entry(const mydaq_ai_channel_config *ch, int last, uint16_t *entry)
{
    /* channel field: 0 ai0, 1 ai1, 2 audio in L, 3 audio in R (audio: range 2 only) */
    const unsigned range_code = ch->range == MYDAQ_AI_RANGE_10V ? 1u : 2u;
    if ((unsigned)ch->channel > 3u)
        return MYDAQ_ERR_INVALID_ARG;
    *entry = (uint16_t)(AI_ENTRY_BASE | range_code << AI_ENTRY_RANGE_SHIFT |
                        (unsigned)ch->channel | (last ? AI_ENTRY_LAST : 0u));
    return MYDAQ_OK;
}

/* Convert (channel-to-channel) clock, in 100 MHz ticks. Observed: 1500 for one channel and 1250
 * for two, i.e. the fastest conversion (5 us single-channel / 2.5 us at the 400 kS/s aggregate)
 * plus 10 us of settling, which is NI-DAQmx's default. At rates where that no longer fits in a
 * scan the conversions are spread evenly over the scan period. [H] for rates above 1 kS/s. */
uint32_t md_ai_convert_ticks(size_t num_channels, uint32_t divisor)
{
    const uint32_t fastest = num_channels > 1 ? 250u : 500u;
    const uint32_t padded = fastest + 1000u;
    uint32_t even;
    if (num_channels == 0)
        return padded;
    if ((uint64_t)padded * num_channels <= divisor)
        return padded;
    even = (uint32_t)(divisor / num_channels);
    return even > fastest ? even : fastest;
}

/* NI-DAQmx coerces the rate to floor(100 MHz / rate) (captures: 7000 S/s -> 14285 = 7000.35 S/s,
 * 12345 -> 8100, 70k -> 1428), clamped to the 200 kS/s maximum and the 32-bit divisor. */
uint32_t md_rate_divisor(double rate)
{
    double d;
    if (!(rate > 0.0) || !isfinite(rate))
        return DIVISOR_MAX;
    d = floor(MYDAQ_TIMEBASE_HZ / rate);
    if (d < DIVISOR_MIN)
        return DIVISOR_MIN;
    if (d > DIVISOR_MAX)
        return DIVISOR_MAX;
    return (uint32_t)d;
}

double md_divisor_rate(uint32_t divisor) { return MYDAQ_TIMEBASE_HZ / (double)divisor; }

double mydaq_ai_coerce_rate(double requested, size_t num_channels)
{
    (void)num_channels;   /* the per-channel limit is the same for 1 and 2 channels */
    return md_divisor_rate(md_rate_divisor(requested));
}

/* ---- on-demand reads ------------------------------------------------------------------- */

int md_ai_send_scan_list(mydaq_device *dev, const mydaq_ai_channel_config *ch, size_t n)
{
    md_buf b;
    md_resp r;
    size_t i;
    int rc;
    md_buf_init(&b);
    md_put_u32(&b, MD_OBJ_AI_FRONTEND);
    md_put_u16(&b, (uint16_t)(2u + 2u * n));   /* byte length counting itself */
    for (i = 0; i < n; ++i) {
        uint16_t entry;
        rc = md_ai_scan_entry(&ch[i], i + 1 == n, &entry);
        if (rc != MYDAQ_OK)
            return rc;
        md_put_u16(&b, entry);
    }
    rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_START, "05120000", MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_COMMIT, &b, MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_QUERY, "05120000", MD_TIMEOUT_CMD, 0, &r);
    if (rc == MYDAQ_OK && (r.payload_len < 4 || md_get_u32(r.payload) != n))
        MD_WARN(dev->ctx, "AI scan list: device reports an unexpected entry count");
    return rc;
}

static int read_scan_locked(mydaq_device *dev, const mydaq_ai_channel_config *ch, size_t n,
                            int16_t *codes)
{
    const uint32_t timing[13] = {3, md_ai_convert_ticks(n, DIVISOR_MAX), 0, 0, 0, 0,
                                 0x00010000u, 1, 0x1f000000u, 0x01000001u, 0, 0x100, 0x100};
    const uint32_t datapath[6] = {0, 0, 0, 0, 0, 0xff010000u};
    const uint32_t per_scan[1] = {(uint32_t)n};
    const uint32_t fetch[4] = {1, 0, 10000 /* ms, [H] */, 0x00f50300u};
    const uint8_t *data;
    size_t len, i;
    md_resp r;
    int rc;

    rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_STOP, "05130002 00000002", MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_ai_send_scan_list(dev, ch, n);
    if (rc == MYDAQ_OK)
        rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_WRITE, "05010001 00000006 3212f413 00000006 00000000",
                        MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_WRITE, MD_OBJ_AI_TIMING, timing, 13,
                          MD_TIMEOUT_CMD, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_RESET, MD_OBJ_AI_DATAPATH, datapath, 6,
                          MD_TIMEOUT_CMD, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_COMMIT, "05500002", MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_START, "05500002 00000000", MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_WRITE, MD_OBJ_AI_FIFO, per_scan, 1,
                          MD_TIMEOUT_CMD, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_START, MD_OBJ_AI_FIFO, fetch, 4, MD_TIMEOUT_LONG,
                          &r);
    if (rc != MYDAQ_OK)
        return rc;
    /* kind 6, status, u16 0, u16 length (+2), big-endian i16 samples */
    rc = md_resp_blob(&r, 2, &data, &len);
    if (rc != MYDAQ_OK || len != 2 * n) {
        MD_ERR(dev->ctx, "AI fetch: unexpected sample block");
        return MYDAQ_ERR_PROTOCOL;
    }
    for (i = 0; i < n; ++i)
        codes[i] = (int16_t)md_get_u16(data + 2 * i);

    rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_STATUS, "05500002", MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_STOP, "05500002", MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_STATUS, "05120000", MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK)
        rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_RELEASE, "05500002", MD_TIMEOUT_CMD, 0, NULL);
    return rc;
}

int mydaq_ai_read_scan_raw(mydaq_device *dev, const mydaq_ai_channel_config *channels,
                           size_t num_channels, int16_t *codes)
{
    int rc, busy;
    if (!dev || !codes)
        return MYDAQ_ERR_INVALID_ARG;
    rc = md_ai_validate_channels(channels, num_channels, 0);
    if (rc != MYDAQ_OK)
        return rc;
    /* Checked under the command lock: a task created meanwhile cannot start before we finish. */
    md_cmd_lock(dev);
    md_mutex_lock(&dev->state_lock);
    busy = dev->ai_task != NULL;
    md_mutex_unlock(&dev->state_lock);
    rc = busy ? MYDAQ_ERR_BUSY : read_scan_locked(dev, channels, num_channels, codes);
    md_cmd_unlock(dev);
    return rc;
}

int mydaq_ai_read_scan(mydaq_device *dev, const mydaq_ai_channel_config *channels,
                       size_t num_channels, double *volts)
{
    int16_t codes[MYDAQ_AI_MAX_CHANNELS];
    size_t i;
    int rc;
    if (!volts)
        return MYDAQ_ERR_INVALID_ARG;
    rc = mydaq_ai_read_scan_raw(dev, channels, num_channels, codes);
    if (rc != MYDAQ_OK)
        return rc;
    for (i = 0; i < num_channels; ++i)
        volts[i] = mydaq_ai_code_to_volts(&dev->cal, channels[i].range, codes[i]);
    return MYDAQ_OK;
}
