/* Device lifetime: open (on any transport), initialisation, calibration memory, reset, close. */
#include "internal.h"
#include "proto.h"

#include <stdlib.h>
#include <string.h>

/* ---- initialisation (docs/protocol.md section 3) --------------------------------------- */

typedef struct init_cmd {
    uint16_t channel;
    uint8_t op;
    const char *body;
} init_cmd;

/* Sent before the calibration-memory reads. The identity commands are issued separately. */
static const init_cmd init_pre[] = {
    {MD_CH_OBJECT, MD_OP_CAPS, "02fe0000 00000000"},   /* capability table */
    {MD_CH_CORE, MD_OP_IDENTITY, "00000000"},
    {MD_CH_OBJECT, MD_OP_RELEASE, "02fe0000"},
    {MD_CH_CORE, MD_OP_STATUS, "02fe0001 03000000"},    /* status bytes, meaning unknown */
    {MD_CH_CORE, MD_OP_STATUS, "02fe0001 81000000"},
};

/* Sent after them: object resets, DMM controller reset (~1.6 s after attach) and the property
 * records. Replayed verbatim from NI's driver; constant across all captures. */
static const init_cmd init_post[] = {
    {MD_CH_OBJECT, MD_OP_RESET, "05500002"},
    {MD_CH_OBJECT, MD_OP_RESET, "05130002"},
    {MD_CH_OBJECT, MD_OP_RESET, "05600000 01000000"},
    {MD_CH_OBJECT, MD_OP_START, "05200000 00000000"},
    {MD_CH_OBJECT, MD_OP_RESET, "0c900000"},
    {MD_CH_OBJECT, MD_OP_WRITE,
     "05010001 00000032 8919895efe1eb9c86717e8721010d8e48e744d47f9737dd1607a2c6b1f8e7912f04c122c"
     "1b7ba92ff4b9c2115523f4ec 00000032 00000000000000000000000000000000000000000000000000000000"
     "000000000000000000000000000000000000"},
    {MD_CH_OBJECT, MD_OP_WRITE, "05010001 00000006 416a2b93 00000006 00000001"},
    {MD_CH_OBJECT, MD_OP_WRITE, "05010001 00000006 80d0b3e6 00000006 00000000"},
    {MD_CH_OBJECT, MD_OP_WRITE, "05010001 00000006 87d9e4ed 00000006 00000000"},
    {MD_CH_OBJECT, MD_OP_WRITE, "05010001 00000006 99fccdab 00000006 0000001f"},
    {MD_CH_OBJECT, MD_OP_WRITE, "05010001 00000006 21aeef59 00000006 00000000"},
};

static int run_list(mydaq_device *dev, const init_cmd *list, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        const int rc =
            md_cmd_hex(dev, list[i].channel, list[i].op, list[i].body, MD_TIMEOUT_LONG, 0, NULL);
        if (rc != MYDAQ_OK)
            return rc;
    }
    return MYDAQ_OK;
}

static int read_identity(mydaq_device *dev)
{
    md_resp r;
    size_t i;
    int rc = md_cmd_hex(dev, MD_CH_CORE, MD_OP_IDENTITY, "00000000", MD_TIMEOUT_CMD, 0, &r);
    if (rc != MYDAQ_OK)
        return rc;
    for (i = 0; i < 4 && 4 * i + 4 <= r.payload_len; ++i)
        dev->info.identity[i] = md_get_u32(r.payload + 4 * i);
    rc = md_cmd_hex(dev, MD_CH_CORE, MD_OP_CORE_INFO, "00000000", MD_TIMEOUT_CMD, 0, &r);
    if (rc != MYDAQ_OK)
        return rc;
    for (i = 0; i < 3 && 4 * i + 4 <= r.payload_len; ++i)
        dev->info.core_info[i] = md_get_u32(r.payload + 4 * i);
    return MYDAQ_OK;
}

static int read_capabilities(mydaq_device *dev)
{
    md_resp r;
    size_t i;
    const int rc =
        md_cmd_hex(dev, init_pre[0].channel, init_pre[0].op, init_pre[0].body, MD_TIMEOUT_CMD, 0, &r);
    if (rc != MYDAQ_OK)
        return rc;
    /* 0000001e, then seven u32 sizes */
    for (i = 0; i < 7 && 8 + 4 * i <= r.payload_len; ++i)
        dev->info.stream_buffer_sizes[i] = md_get_u32(r.payload + 4 + 4 * i);
    return MYDAQ_OK;
}

int md_read_calmem(mydaq_device *dev)
{
    uint32_t off;
    for (off = 0; off < MYDAQ_CALMEM_SIZE; off += MD_CALMEM_CHUNK) {
        const uint32_t n =
            MYDAQ_CALMEM_SIZE - off < MD_CALMEM_CHUNK ? MYDAQ_CALMEM_SIZE - off : MD_CALMEM_CHUNK;
        const uint32_t words[2] = {off, n};
        const uint8_t *data;
        size_t len;
        md_resp r;
        int rc = md_cmd_words(dev, MD_CH_OBJECT, MD_OP_RESET, MD_OBJ_CALMEM, words, 2,
                              MD_TIMEOUT_CMD, &r);
        if (rc != MYDAQ_OK)
            return rc;
        rc = md_resp_blob(&r, 0, &data, &len);
        if (rc != MYDAQ_OK || len != n) {
            MD_ERR(dev->ctx, "calibration memory read at 0x%x: bad length", (unsigned)off);
            return MYDAQ_ERR_PROTOCOL;
        }
        memcpy(dev->calmem + off, data, n);
    }
    return MYDAQ_OK;
}

/* hwverify.verify (05 06 m08) answers -225270 while the device is still starting up; NI's
 * driver repeats it every 50 ms for up to 10 s (docs/driver-notes.md 1.1). */
#define HWVERIFY_STARTING ((int32_t)-225270)

static int hardware_verify(mydaq_device *dev)
{
    const uint64_t deadline = md_now_ms() + 10000u;
    for (;;) {
        md_resp r;
        int rc = md_cmd_hex(dev, MD_CH_OBJECT, MD_OP_RESET, "05060000", MD_TIMEOUT_CMD,
                            MD_CMD_ANY_STATUS, &r);
        if (rc != MYDAQ_OK)
            return rc;
        if (r.kind != MD_KIND_DATA || r.status == 0)
            return MYDAQ_OK;
        if (r.status != HWVERIFY_STARTING || md_now_ms() > deadline) {
            MD_ERR(dev->ctx, "hardware verification failed: status %ld", (long)r.status);
            return MYDAQ_ERR_DEVICE;
        }
        md_sleep_ms(50);
    }
}

int md_device_init_sequence(mydaq_device *dev)
{
    const uint64_t t0 = md_now_us();
    int rc = read_identity(dev);
    if (rc == MYDAQ_OK)
        rc = read_capabilities(dev);
    if (rc == MYDAQ_OK)
        rc = run_list(dev, init_pre + 1, sizeof init_pre / sizeof init_pre[0] - 1);
    if (rc == MYDAQ_OK)
        rc = md_read_calmem(dev);
    if (rc == MYDAQ_OK)
        rc = hardware_verify(dev);
    if (rc == MYDAQ_OK)
        rc = run_list(dev, init_post, sizeof init_post / sizeof init_post[0]);
    dev->info.init_seconds = (double)(md_now_us() - t0) / 1e6;
    return rc;
}

/* ---- open / close ---------------------------------------------------------------------- */

static uint32_t parse_hex_serial(const char *s)
{
    uint32_t v = 0;
    int digits = 0;
    for (; *s; ++s) {
        const char c = *s;
        int d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            return 0;
        if (++digits > 8)
            return 0;
        v = v << 4 | (uint32_t)d;
    }
    return v;
}

static void device_free(mydaq_device *dev)
{
    if (dev->ops && dev->ops->close)
        dev->ops->close(dev->impl);
    if (dev->locks_ready) {
        md_mutex_destroy(&dev->cmd_lock);
        md_mutex_destroy(&dev->out_lock);
        md_mutex_destroy(&dev->state_lock);
        md_mutex_destroy(&dev->pump_ctl);
        md_mutex_destroy(&dev->pump_lock);
        md_mutex_destroy(&dev->events_ctl);
        md_mutex_destroy(&dev->events_lock);
    }
    free(dev);
}

int mydaq_open_transport(mydaq_context *ctx, const mydaq_transport_ops *ops, void *impl,
                         const mydaq_transport_info *info, unsigned flags, mydaq_device **out)
{
    mydaq_device *dev;
    int rc;

    if (out)
        *out = NULL;
    if (!ctx || !ops || !out) {
        if (ops && ops->close)
            ops->close(impl);
        return MYDAQ_ERR_INVALID_ARG;
    }
    dev = calloc(1, sizeof *dev);
    if (!dev) {
        ops->close(impl);
        return MYDAQ_ERR_NO_MEM;
    }
    dev->ctx = ctx;
    dev->ops = ops;
    dev->impl = impl;
    /* Mutex initialisation cannot fail with default attributes on the supported platforms. */
    md_mutex_init(&dev->cmd_lock);
    md_mutex_init(&dev->out_lock);
    md_mutex_init(&dev->state_lock);
    md_mutex_init(&dev->pump_ctl);
    md_mutex_init(&dev->pump_lock);
    md_mutex_init(&dev->events_ctl);
    md_mutex_init(&dev->events_lock);
    dev->locks_ready = 1;
    if (info)
        dev->tinfo = *info;
    dev->connected = 1;
    memcpy(dev->info.serial, dev->tinfo.serial, sizeof dev->info.serial);
    memcpy(dev->info.location, dev->tinfo.location, sizeof dev->info.location);
    dev->info.serial[sizeof dev->info.serial - 1] = '\0';
    dev->info.location[sizeof dev->info.location - 1] = '\0';
    dev->info.serial_number = parse_hex_serial(dev->info.serial);
    dev->info.usb_bus = dev->tinfo.bus;
    dev->info.usb_address = dev->tinfo.address;
    dev->info.usb_vid = MYDAQ_USB_VID;
    dev->info.usb_pid = MYDAQ_USB_PID;
    memcpy(dev->info.product, "NI myDAQ", sizeof "NI myDAQ");

    /* Stale frames from a previous session would be taken for our responses. */
    md_drain(dev, MD_EP_CMD_IN, MYDAQ_EP_BULK, 16);
    md_drain(dev, MD_EP_EVENT_IN, MYDAQ_EP_INTERRUPT, 4);

    md_cmd_lock(dev);
    if (flags & MYDAQ_OPEN_NO_INIT) {
        rc = read_identity(dev);
        if (rc == MYDAQ_OK)
            rc = read_capabilities(dev);
        if (rc == MYDAQ_OK)
            rc = md_read_calmem(dev);
    } else {
        rc = md_device_init_sequence(dev);
    }
    md_cmd_unlock(dev);
    if (rc == MYDAQ_OK) {
        rc = mydaq_parse_calibration(dev->calmem, sizeof dev->calmem, &dev->cal);
        if (rc != MYDAQ_OK)
            MD_ERR(ctx, "myDAQ %s: calibration memory not recognised", dev->info.serial);
    }
    if (rc != MYDAQ_OK) {
        md_log(ctx, rc == MYDAQ_ERR_NO_DEVICE ? MYDAQ_LOG_DEBUG : MYDAQ_LOG_ERROR,
               "myDAQ %s: initialisation failed: %s", dev->info.serial, mydaq_strerror(rc));
        device_free(dev);
        return rc;
    }
    MD_INFO(ctx, "myDAQ %s at %s ready (init %.2f s)", dev->info.serial, dev->info.location,
            dev->info.init_seconds);
    *out = dev;
    return MYDAQ_OK;
}

static int open_usb(mydaq_context *ctx, const char *serial, const char *location,
                    struct libusb_device_handle *handle, unsigned flags, mydaq_device **out)
{
    const mydaq_transport_ops *ops = NULL;
    void *impl = NULL;
    mydaq_transport_info info;
    int rc;
    if (!out)
        return MYDAQ_ERR_INVALID_ARG;
    *out = NULL;
    if (!ctx)
        return MYDAQ_ERR_INVALID_ARG;
    rc = md_usb_open_by(ctx, serial, location, handle, &ops, &impl, &info);
    if (rc != MYDAQ_OK)
        return rc;
    return mydaq_open_transport(ctx, ops, impl, &info, flags, out);
}

int mydaq_open(mydaq_context *ctx, const char *serial, unsigned flags, mydaq_device **dev)
{
    return open_usb(ctx, serial, NULL, NULL, flags, dev);
}

int mydaq_open_location(mydaq_context *ctx, const char *location, unsigned flags,
                        mydaq_device **dev)
{
    if (!location || !*location)
        return MYDAQ_ERR_INVALID_ARG;
    return open_usb(ctx, NULL, location, NULL, flags, dev);
}

int mydaq_open_handle(mydaq_context *ctx, struct libusb_device_handle *handle, unsigned flags,
                      mydaq_device **dev)
{
    if (!handle)
        return MYDAQ_ERR_INVALID_ARG;
    return open_usb(ctx, NULL, NULL, handle, flags, dev);
}

int mydaq_enumerate(mydaq_context *ctx, mydaq_device_desc *list, size_t max, size_t *count,
                    unsigned flags)
{
    if (!ctx || !count || (max > 0 && !list))
        return MYDAQ_ERR_INVALID_ARG;
    *count = 0;
    return md_usb_enumerate(ctx, list, max, count, flags);
}

void mydaq_close(mydaq_device *dev)
{
    mydaq_ai_task *task;
    mydaq_ao_task *ao;
    mydaq_ctr_task *ctr;
    if (!dev)
        return;
    md_mutex_lock(&dev->state_lock);
    task = dev->ai_task;
    ao = dev->ao_task;
    ctr = dev->ctr_task;
    md_mutex_unlock(&dev->state_lock);
    if (task)
        md_ai_task_force_destroy(task);
    if (ao)
        md_ao_task_force_destroy(ao);
    if (ctr)
        mydaq_ctr_task_destroy(ctr);
    mydaq_freqout_stop(dev);
    device_free(dev);
}

int mydaq_is_connected(const mydaq_device *dev)
{
    return dev ? md_is_connected((mydaq_device *)(uintptr_t)dev) : 0;
}

int mydaq_get_info(mydaq_device *dev, mydaq_info *info)
{
    if (!dev || !info)
        return MYDAQ_ERR_INVALID_ARG;
    md_mutex_lock(&dev->state_lock);
    *info = dev->info;
    md_mutex_unlock(&dev->state_lock);
    return MYDAQ_OK;
}

int32_t mydaq_last_device_status(const mydaq_device *dev)
{
    int32_t s;
    mydaq_device *d = (mydaq_device *)(uintptr_t)dev;
    if (!d)
        return 0;
    md_mutex_lock(&d->state_lock);
    s = d->last_status;
    md_mutex_unlock(&d->state_lock);
    return s;
}

int mydaq_reset(mydaq_device *dev)
{
    mydaq_ai_task *task;
    mydaq_ao_task *ao;
    mydaq_ctr_task *ctr;
    int rc;
    if (!dev)
        return MYDAQ_ERR_INVALID_ARG;
    md_mutex_lock(&dev->state_lock);
    task = dev->ai_task;
    ao = dev->ao_task;
    ctr = dev->ctr_task;
    md_mutex_unlock(&dev->state_lock);
    if (task)
        md_ai_task_abort(task);
    if (ao)
        md_ao_task_abort(ao);
    if (ctr)
        mydaq_ctr_task_stop(ctr);
    mydaq_freqout_stop(dev);

    md_cmd_lock(dev);
    rc = md_device_init_sequence(dev);
    md_cmd_unlock(dev);
    if (rc != MYDAQ_OK)
        return rc;
    /* outputs to 0 V (a stopped timed task no longer holds them) */
    rc = md_ao_write_code_unchecked(dev, MYDAQ_AO0, 0.0);
    if (rc == MYDAQ_OK)
        rc = md_ao_write_code_unchecked(dev, MYDAQ_AO1, 0.0);
    if (rc == MYDAQ_OK) {
        uint8_t free_lines;
        md_mutex_lock(&dev->state_lock);
        free_lines = (uint8_t)~dev->dio_reserved;   /* lines of an existing counter task stay */
        md_mutex_unlock(&dev->state_lock);
        rc = mydaq_dio_set_direction(dev, free_lines, 0);
    }
    return rc;
}
