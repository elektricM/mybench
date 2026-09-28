/* Static digital I/O, port0 = DIO0..7 (docs/protocol.md section 8). */
#include "internal.h"
#include "proto.h"

static int reserved(mydaq_device *dev, uint8_t mask)
{
    int r;
    md_mutex_lock(&dev->state_lock);
    r = (dev->dio_reserved & mask) != 0;
    md_mutex_unlock(&dev->state_lock);
    return r;
}

int mydaq_dio_set_direction(mydaq_device *dev, uint8_t mask, int output)
{
    md_buf b;
    int rc;
    if (!dev)
        return MYDAQ_ERR_INVALID_ARG;
    if (reserved(dev, mask))
        return MYDAQ_ERR_BUSY;   /* a counter / FREQOUT uses the line */
    /* 0a 05300001 <u32 mask> <u8 dir> 000000 */
    md_buf_init(&b);
    md_put_u32(&b, MD_OBJ_DIO_PORT0);
    md_put_u32(&b, mask);
    md_put_u8(&b, output ? 1u : 0u);
    md_put_hex(&b, "000000");
    md_cmd_lock(dev);
    rc = md_cmd(dev, MD_CH_OBJECT, MD_OP_COMMIT, &b, MD_TIMEOUT_CMD, 0, NULL);
    if (rc == MYDAQ_OK) {
        md_mutex_lock(&dev->state_lock);
        if (output)
            dev->dio_direction |= mask;
        else
            dev->dio_direction &= (uint8_t)~mask;
        md_mutex_unlock(&dev->state_lock);
    }
    md_cmd_unlock(dev);
    return rc;
}

int mydaq_dio_get_direction(mydaq_device *dev, uint8_t *direction)
{
    if (!dev || !direction)
        return MYDAQ_ERR_INVALID_ARG;
    md_mutex_lock(&dev->state_lock);
    *direction = dev->dio_direction;
    md_mutex_unlock(&dev->state_lock);
    return MYDAQ_OK;
}

int mydaq_dio_write(mydaq_device *dev, uint8_t value, uint8_t mask)
{
    const uint32_t words[2] = {mask, value};
    int rc;
    if (!dev)
        return MYDAQ_ERR_INVALID_ARG;
    if (reserved(dev, mask))
        return MYDAQ_ERR_BUSY;
    md_cmd_lock(dev);
    rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_WRITE, MD_OBJ_DIO_PORT0, words, 2, MD_TIMEOUT_CMD,
                      NULL);
    md_cmd_unlock(dev);
    return rc;
}

int mydaq_dio_read(mydaq_device *dev, uint8_t *levels)
{
    md_resp r;
    int rc;
    if (!dev || !levels)
        return MYDAQ_ERR_INVALID_ARG;
    md_cmd_lock(dev);
    rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_RESET, MD_OBJ_DIO_PORT0, NULL, 0, MD_TIMEOUT_CMD, &r);
    if (rc == MYDAQ_OK) {
        /* kind 2 + 00 00 ff <levels> */
        if (r.kind != MD_KIND_ACK || r.payload_len < 4)
            rc = MYDAQ_ERR_PROTOCOL;
        else
            *levels = r.payload[3];
    }
    md_cmd_unlock(dev);
    return rc;
}
