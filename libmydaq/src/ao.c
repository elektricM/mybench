/* Analog output, static updates (docs/protocol.md section 7). */
#include "internal.h"
#include "proto.h"

static int write_raw(mydaq_device *dev, mydaq_ao_channel channel, int16_t code, int two_volt);

int mydaq_ao_write_raw(mydaq_device *dev, mydaq_ao_channel channel, int16_t code)
{
    if (dev && (channel == MYDAQ_AO0 || channel == MYDAQ_AO1) && md_ao_channel_in_task(dev, channel))
        return MYDAQ_ERR_BUSY;   /* a timed task drives this output */
    return write_raw(dev, channel, code, 0);
}

int md_ao_write_code_unchecked(mydaq_device *dev, mydaq_ao_channel channel, double volts)
{
    int16_t code;
    const int rc = mydaq_ao_volts_to_code(&dev->cal, channel, volts, &code);
    return rc == MYDAQ_OK ? write_raw(dev, channel, code, 0) : rc;
}

static int write_raw(mydaq_device *dev, mydaq_ao_channel channel, int16_t code, int two_volt)
{
    md_buf b;
    int rc;
    const uint16_t ch = (uint16_t)channel;
    if (!dev)
        return MYDAQ_ERR_INVALID_ARG;
    if (channel == MYDAQ_AUDIO_OUT_LEFT || channel == MYDAQ_AUDIO_OUT_RIGHT)
        return MYDAQ_ERR_NOT_SUPPORTED;   /* audio outputs not decoded yet */
    if (channel != MYDAQ_AO0 && channel != MYDAQ_AO1)
        return MYDAQ_ERR_INVALID_ARG;

    md_cmd_lock(dev);
    /* 08 0520 <ch> 0001 SEL 00 00 AUD 0000: select the channel and its range (±10 V: 0, 0;
     * ±2 V: 5, 1 — ao0-r2-static-p0.5, ao1-r2-static-m0.5) */
    md_buf_init(&b);
    md_put_u16(&b, 0x0520);
    md_put_u16(&b, ch);
    md_put_hex(&b, two_volt ? "0001 0500 0001 0000" : "0001 0000 0000 0000");
    rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_RESET, &b, MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK) {
        /* 09 0520 0000 00000006 0000 <i16 code> 0004 <ch>: the instance stays 0000 */
        md_buf_init(&b);
        md_put_hex(&b, "0520 0000 00000006 0000");
        md_put_u16(&b, (uint16_t)code);
        md_put_u16(&b, 0x0004);
        md_put_u16(&b, ch);
        rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_WRITE, &b, MD_TIMEOUT_CMD, 0, NULL);
    }
    if (rc == MYDAQ_OK) {
        /* 0a 0520 <ch>: the output updates */
        md_buf_init(&b);
        md_put_u16(&b, 0x0520);
        md_put_u16(&b, ch);
        rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_COMMIT, &b, MD_TIMEOUT_CMD, 0, NULL);
    }
    md_cmd_unlock(dev);
    return rc;
}

int mydaq_ao_write_range(mydaq_device *dev, mydaq_ao_channel channel, double volts,
                         mydaq_ao_range range)
{
    int16_t code;
    if (!dev || (channel != MYDAQ_AO0 && channel != MYDAQ_AO1))
        return channel == MYDAQ_AUDIO_OUT_LEFT || channel == MYDAQ_AUDIO_OUT_RIGHT
                   ? MYDAQ_ERR_NOT_SUPPORTED
                   : MYDAQ_ERR_INVALID_ARG;
    if (range == MYDAQ_AO_RANGE_10V)
        return mydaq_ao_write(dev, channel, volts);
    if (range != MYDAQ_AO_RANGE_2V || mydaq_ao_task_volts_to_code_range(&dev->cal, channel, volts, range, &code) != MYDAQ_OK)
        return MYDAQ_ERR_INVALID_ARG;
    if (md_ao_channel_in_task(dev, channel))
        return MYDAQ_ERR_BUSY;
    return write_raw(dev, channel, code, 1);
}

int mydaq_ao_write(mydaq_device *dev, mydaq_ao_channel channel, double volts)
{
    int16_t code;
    int rc;
    if (!dev)
        return MYDAQ_ERR_INVALID_ARG;
    if (channel == MYDAQ_AUDIO_OUT_LEFT || channel == MYDAQ_AUDIO_OUT_RIGHT)
        return MYDAQ_ERR_NOT_SUPPORTED;
    rc = mydaq_ao_volts_to_code(&dev->cal, channel, volts, &code);
    if (rc != MYDAQ_OK)
        return rc;
    return mydaq_ao_write_raw(dev, channel, code);
}
