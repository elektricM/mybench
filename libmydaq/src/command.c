/* Command channel (EP 0x03 -> 0x81), error bookkeeping and the event thread. */
#include "internal.h"

#include <string.h>

void md_cmd_lock(mydaq_device *dev) { md_mutex_lock(&dev->cmd_lock); }
void md_cmd_unlock(mydaq_device *dev) { md_mutex_unlock(&dev->cmd_lock); }

int md_is_connected(mydaq_device *dev)
{
    int connected;
    md_mutex_lock(&dev->state_lock);
    connected = dev->connected;
    md_mutex_unlock(&dev->state_lock);
    return connected;
}

int md_stream_alloc(mydaq_device *dev, uint8_t *stream)
{
    unsigned n;
    int rc = MYDAQ_ERR_BUSY;
    md_mutex_lock(&dev->state_lock);
    for (n = MD_STREAM_FIRST; n <= MD_STREAM_LAST; ++n)
        if (!(dev->streams_used & (1u << n))) {
            dev->streams_used = (uint16_t)(dev->streams_used | (1u << n));
            *stream = (uint8_t)n;
            rc = MYDAQ_OK;
            break;
        }
    md_mutex_unlock(&dev->state_lock);
    return rc;
}

void md_stream_free(mydaq_device *dev, uint8_t stream)
{
    if (stream < MD_STREAM_FIRST || stream > MD_STREAM_LAST)
        return;
    md_mutex_lock(&dev->state_lock);
    dev->streams_used = (uint16_t)(dev->streams_used & ~(1u << stream));
    md_mutex_unlock(&dev->state_lock);
}

int md_note_error(mydaq_device *dev, int err)
{
    if (err == MYDAQ_ERR_NO_DEVICE) {
        md_mutex_lock(&dev->state_lock);
        if (dev->connected)
            MD_WARN(dev->ctx, "device %s disconnected", dev->tinfo.serial);
        dev->connected = 0;
        md_mutex_unlock(&dev->state_lock);
    }
    return err;
}

void md_drain(mydaq_device *dev, uint8_t endpoint, mydaq_endpoint_type type, unsigned max_reads)
{
    uint8_t buf[512];
    unsigned i;
    for (i = 0; i < max_reads; ++i) {
        size_t got = 0;
        const int rc = dev->ops->read(dev->impl, endpoint, type, buf, sizeof buf, &got, 20);
        if (rc != MYDAQ_OK)
            break;
        MD_DBG(dev->ctx, "drained %zu stale bytes from endpoint 0x%02x", got, endpoint);
    }
}

static void trace_frame(mydaq_device *dev, const char *dir, const uint8_t *p, size_t n)
{
    char hex[3 * 600];
    if (!md_log_enabled(dev->ctx, MYDAQ_LOG_TRACE))
        return;
    md_hex(p, n, hex, sizeof hex);
    md_log(dev->ctx, MYDAQ_LOG_TRACE, "%s %s", dir, hex);
}

int md_cmd(mydaq_device *dev, uint16_t channel, uint8_t op, const md_buf *body,
           unsigned timeout_ms, unsigned flags, md_resp *resp)
{
    md_resp local;
    size_t out_len, got = 0;
    int rc;
    const uint32_t object = body->len >= 4 ? md_get_u32(body->data) : 0;

    if (!resp)
        resp = &local;
    if (!md_is_connected(dev))
        return MYDAQ_ERR_NO_DEVICE;
    out_len = md_frame_encode(channel, op, body, dev->tx, sizeof dev->tx);
    if (out_len == 0)
        return MYDAQ_ERR_INVALID_ARG;   /* body too large: a bug in the caller */

    if (dev->cmd_desync) {
        /* A previous command timed out: its late response must not be taken for ours. */
        md_drain(dev, MD_EP_CMD_IN, MYDAQ_EP_BULK, 8);
        dev->cmd_desync = 0;
    }

    trace_frame(dev, ">>", dev->tx, out_len);
    md_mutex_lock(&dev->out_lock);
    rc = dev->ops->write(dev->impl, MD_EP_CMD_OUT, dev->tx, out_len, NULL, timeout_ms);
    md_mutex_unlock(&dev->out_lock);
    if (rc != MYDAQ_OK) {
        /* an unplug is reported once by md_note_error, not per command */
        md_log(dev->ctx, rc == MYDAQ_ERR_NO_DEVICE ? MYDAQ_LOG_DEBUG : MYDAQ_LOG_ERROR,
               "command ch%u op%02x %08x: send failed: %s", channel, op, object, mydaq_strerror(rc));
        return md_note_error(dev, rc);
    }
    rc = dev->ops->read(dev->impl, MD_EP_CMD_IN, MYDAQ_EP_BULK, dev->rx, sizeof dev->rx, &got,
                        timeout_ms);
    if (rc != MYDAQ_OK) {
        if (rc == MYDAQ_ERR_TIMEOUT)
            dev->cmd_desync = 1;
        md_log(dev->ctx, rc == MYDAQ_ERR_NO_DEVICE ? MYDAQ_LOG_DEBUG : MYDAQ_LOG_ERROR,
               "command ch%u op%02x %08x: no response: %s", channel, op, object, mydaq_strerror(rc));
        return md_note_error(dev, rc);
    }
    trace_frame(dev, "<<", dev->rx, got);

    rc = md_frame_parse_response(dev->rx, got, resp);
    if (rc != MYDAQ_OK) {
        char hex[100];
        md_hex(dev->rx, got < 24 ? got : 24, hex, sizeof hex);
        MD_ERR(dev->ctx, "command ch%u op%02x %08x: malformed response (%zu bytes: %s)", channel,
               op, object, got, hex);
        dev->cmd_desync = 1;
        return rc;
    }
    if (resp->kind == MD_KIND_DATA && resp->status != 0) {
        md_mutex_lock(&dev->state_lock);
        dev->last_status = resp->status;
        md_mutex_unlock(&dev->state_lock);
        if (!(flags & MD_CMD_ANY_STATUS)) {
            MD_ERR(dev->ctx, "command ch%u op%02x %08x: device status %ld", channel, op, object,
                   (long)resp->status);
            return MYDAQ_ERR_DEVICE;
        }
    }
    return MYDAQ_OK;
}

int md_send_oneway(mydaq_device *dev, uint16_t channel, uint8_t op, const md_buf *body)
{
    uint8_t frame[MD_FRAME_MAX];
    size_t len;
    int rc;
    if (!md_is_connected(dev))
        return MYDAQ_ERR_NO_DEVICE;
    len = md_frame_encode(channel, op, body, frame, sizeof frame);
    if (len == 0)
        return MYDAQ_ERR_INVALID_ARG;
    frame[6] = 0;   /* no reply expected */
    trace_frame(dev, ">>", frame, len);
    md_mutex_lock(&dev->out_lock);
    rc = dev->ops->write(dev->impl, MD_EP_CMD_OUT, frame, len, NULL, MD_TIMEOUT_CMD);
    md_mutex_unlock(&dev->out_lock);
    return md_note_error(dev, rc);
}

int md_cmd_hex(mydaq_device *dev, uint16_t channel, uint8_t op, const char *hex,
               unsigned timeout_ms, unsigned flags, md_resp *resp)
{
    md_buf b;
    md_buf_init(&b);
    md_put_hex(&b, hex);
    return md_cmd(dev, channel, op, &b, timeout_ms, flags, resp);
}

int md_cmd_words(mydaq_device *dev, uint16_t channel, uint8_t op, uint32_t object,
                 const uint32_t *words, size_t n, unsigned timeout_ms, md_resp *resp)
{
    md_buf b;
    size_t i;
    md_buf_init(&b);
    md_put_u32(&b, object);
    for (i = 0; i < n; ++i)
        md_put_u32(&b, words[i]);
    return md_cmd(dev, channel, op, &b, timeout_ms, 0, resp);
}

/* ---- event thread ---------------------------------------------------------------------- */

static void pump_main(void *arg)
{
    mydaq_device *dev = arg;
    for (;;) {
        int stop, rc;
        md_mutex_lock(&dev->pump_lock);
        stop = dev->pump_stop;
        md_mutex_unlock(&dev->pump_lock);
        if (stop)
            break;
        rc = dev->ops->handle_events(dev->impl, 50);
        if (rc == MYDAQ_ERR_NO_DEVICE) {
            md_note_error(dev, rc);
            md_sleep_ms(20);   /* streams report the unplug through their callbacks */
        } else if (rc != MYDAQ_OK && rc != MYDAQ_ERR_TIMEOUT) {
            md_sleep_ms(5);
        }
    }
}

int md_pump_acquire(mydaq_device *dev)
{
    int rc = MYDAQ_OK;
    md_mutex_lock(&dev->pump_ctl);
    if (dev->pump_users == 0) {
        md_mutex_lock(&dev->pump_lock);
        dev->pump_stop = 0;
        md_mutex_unlock(&dev->pump_lock);
        if (md_thread_start(&dev->pump_thread, pump_main, dev) != 0)
            rc = MYDAQ_ERR_NO_MEM;
    }
    if (rc == MYDAQ_OK)
        ++dev->pump_users;
    md_mutex_unlock(&dev->pump_ctl);
    return rc;
}

void md_pump_release(mydaq_device *dev)
{
    md_mutex_lock(&dev->pump_ctl);
    if (dev->pump_users > 0 && --dev->pump_users == 0) {
        md_mutex_lock(&dev->pump_lock);
        dev->pump_stop = 1;
        md_mutex_unlock(&dev->pump_lock);
        md_thread_join(dev->pump_thread);
    }
    md_mutex_unlock(&dev->pump_ctl);
}
