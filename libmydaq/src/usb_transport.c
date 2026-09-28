/* libusb-1.0 transport: enumeration, open/claim, synchronous and streaming transfers. */
#include "internal.h"
#include "proto.h"

#include <libusb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USB_INTERFACE 0
#define USB_CONFIGURATION 1
#define STREAM_CLOSE_TIMEOUT_MS 3000u

typedef struct usb_transport {
    mydaq_context *ctx;
    libusb_context *usb;
    libusb_device_handle *handle;
    int owns_handle;
    int claimed;
} usb_transport;

typedef struct usb_stream {
    usb_transport *t;
    uint8_t endpoint;
    mydaq_endpoint_type type;
    mydaq_stream_cb cb;
    void *user;
    size_t transfer_size;
    unsigned count;
    struct libusb_transfer **xfers;
    md_mutex lock;         /* guards in_flight, closing and (re)submission */
    unsigned in_flight;
    int closing;
} usb_stream;

int md_usb_error(int e)
{
    switch (e) {
    case LIBUSB_SUCCESS: return MYDAQ_OK;
    case LIBUSB_ERROR_INVALID_PARAM: return MYDAQ_ERR_INVALID_ARG;
    case LIBUSB_ERROR_ACCESS: return MYDAQ_ERR_ACCESS;
    case LIBUSB_ERROR_NO_DEVICE: return MYDAQ_ERR_NO_DEVICE;
    case LIBUSB_ERROR_NOT_FOUND: return MYDAQ_ERR_NOT_FOUND;
    case LIBUSB_ERROR_BUSY: return MYDAQ_ERR_BUSY;
    case LIBUSB_ERROR_TIMEOUT: return MYDAQ_ERR_TIMEOUT;
    case LIBUSB_ERROR_NO_MEM: return MYDAQ_ERR_NO_MEM;
    case LIBUSB_ERROR_NOT_SUPPORTED: return MYDAQ_ERR_NOT_SUPPORTED;
    default: return MYDAQ_ERR_USB;
    }
}

static int transfer_status_error(enum libusb_transfer_status s)
{
    switch (s) {
    case LIBUSB_TRANSFER_COMPLETED: return MYDAQ_OK;
    case LIBUSB_TRANSFER_TIMED_OUT: return MYDAQ_ERR_TIMEOUT;
    case LIBUSB_TRANSFER_NO_DEVICE: return MYDAQ_ERR_NO_DEVICE;
    default: return MYDAQ_ERR_USB;
    }
}

/* ---- synchronous transfers ------------------------------------------------------------- */

static int usb_write(void *impl, uint8_t ep, const uint8_t *data, size_t len, size_t *actual,
                     unsigned timeout_ms)
{
    usb_transport *t = impl;
    int done = 0;
    int rc;
    if (actual)
        *actual = 0;
    if (len > 0x7fffffffu)
        return MYDAQ_ERR_INVALID_ARG;
    rc = libusb_bulk_transfer(t->handle, ep, (unsigned char *)(uintptr_t)data, (int)len, &done,
                              timeout_ms);
    if (actual)
        *actual = done > 0 ? (size_t)done : 0;
    if (rc != LIBUSB_SUCCESS)
        return md_usb_error(rc);
    return (size_t)done == len ? MYDAQ_OK : MYDAQ_ERR_USB;
}

static int usb_read(void *impl, uint8_t ep, mydaq_endpoint_type type, uint8_t *buf, size_t cap,
                    size_t *actual, unsigned timeout_ms)
{
    usb_transport *t = impl;
    int done = 0;
    int rc;
    const int len = cap > 0x7fffffffu ? 0x7fffffff : (int)cap;
    if (type == MYDAQ_EP_INTERRUPT)
        rc = libusb_interrupt_transfer(t->handle, ep, buf, len, &done, timeout_ms);
    else
        rc = libusb_bulk_transfer(t->handle, ep, buf, len, &done, timeout_ms);
    *actual = done > 0 ? (size_t)done : 0;
    if (rc == LIBUSB_ERROR_TIMEOUT && done > 0)
        return MYDAQ_OK;
    return md_usb_error(rc);
}

/* ---- streams --------------------------------------------------------------------------- */

static void LIBUSB_CALL stream_transfer_done(struct libusb_transfer *x)
{
    usb_stream *s = x->user_data;
    int closing, status;
    size_t next = 0;

    md_mutex_lock(&s->lock);
    closing = s->closing;
    md_mutex_unlock(&s->lock);

    if (x->status != LIBUSB_TRANSFER_CANCELLED && !closing) {
        status = transfer_status_error(x->status);
        if (status == MYDAQ_ERR_USB)
            MD_WARN(s->t->ctx, "stream 0x%02x: transfer status %d", s->endpoint, (int)x->status);
        next = s->cb(s->user, x->buffer, x->actual_length > 0 ? (size_t)x->actual_length : 0,
                     (size_t)x->length, status);
        if (status != MYDAQ_OK)
            next = 0;
    }

    md_mutex_lock(&s->lock);
    if (next > 0 && !s->closing) {
        x->length = (int)(next < s->transfer_size ? next : s->transfer_size);
        if (libusb_submit_transfer(x) == LIBUSB_SUCCESS) {
            md_mutex_unlock(&s->lock);
            return;
        }
        MD_WARN(s->t->ctx, "stream 0x%02x: resubmit failed", s->endpoint);
    }
    --s->in_flight;
    md_mutex_unlock(&s->lock);
}

static void stream_free(usb_stream *s)
{
    unsigned i;
    for (i = 0; i < s->count; ++i) {
        if (s->xfers[i]) {
            free(s->xfers[i]->buffer);
            libusb_free_transfer(s->xfers[i]);
        }
    }
    free(s->xfers);
    md_mutex_destroy(&s->lock);
    free(s);
}

static void usb_stream_close(void *impl, void *stream)
{
    usb_transport *t = impl;
    usb_stream *s = stream;
    const uint64_t deadline = md_now_ms() + STREAM_CLOSE_TIMEOUT_MS;
    unsigned i, pending;

    if (!s)
        return;
    md_mutex_lock(&s->lock);
    s->closing = 1;
    for (i = 0; i < s->count; ++i)
        libusb_cancel_transfer(s->xfers[i]);   /* LIBUSB_ERROR_NOT_FOUND for idle ones */
    md_mutex_unlock(&s->lock);

    for (;;) {
        struct timeval tv = {0, 20000};
        md_mutex_lock(&s->lock);
        pending = s->in_flight;
        md_mutex_unlock(&s->lock);
        if (pending == 0)
            break;
        if (md_now_ms() > deadline) {
            /* Freeing in-flight transfers would be a use-after-free: leak them instead. */
            MD_ERR(t->ctx, "stream 0x%02x: %u transfers did not finish; leaking them", s->endpoint,
                   pending);
            return;
        }
        libusb_handle_events_timeout_completed(t->usb, &tv, NULL);
    }
    stream_free(s);
}

static int usb_stream_open(void *impl, uint8_t ep, mydaq_endpoint_type type, size_t transfer_size,
                           unsigned num_transfers, mydaq_stream_cb cb, void *user, void **out)
{
    usb_transport *t = impl;
    usb_stream *s;
    unsigned i;
    int rc = MYDAQ_OK;

    *out = NULL;
    if (transfer_size == 0 || transfer_size > 0x7fffffffu || num_transfers == 0)
        return MYDAQ_ERR_INVALID_ARG;
    s = calloc(1, sizeof *s);
    if (!s)
        return MYDAQ_ERR_NO_MEM;
    s->xfers = calloc(num_transfers, sizeof *s->xfers);
    if (!s->xfers || md_mutex_init(&s->lock) != 0) {
        free(s->xfers);
        free(s);
        return MYDAQ_ERR_NO_MEM;
    }
    s->t = t;
    s->endpoint = ep;
    s->type = type;
    s->cb = cb;
    s->user = user;
    s->transfer_size = transfer_size;
    s->count = num_transfers;   /* stream_free() skips the NULL entries */
    for (i = 0; i < num_transfers; ++i) {
        unsigned char *buf = malloc(transfer_size);
        s->xfers[i] = libusb_alloc_transfer(0);   /* zeroed: buffer NULL until filled */
        if (!buf || !s->xfers[i]) {
            free(buf);
            stream_free(s);
            return MYDAQ_ERR_NO_MEM;
        }
        if (type == MYDAQ_EP_INTERRUPT)
            libusb_fill_interrupt_transfer(s->xfers[i], t->handle, ep, buf, (int)transfer_size,
                                           stream_transfer_done, s, 0);
        else
            libusb_fill_bulk_transfer(s->xfers[i], t->handle, ep, buf, (int)transfer_size,
                                      stream_transfer_done, s, 0);
    }

    md_mutex_lock(&s->lock);
    for (i = 0; i < num_transfers; ++i) {
        size_t len = cb(user, NULL, 0, 0, MYDAQ_OK);   /* initial length of this transfer */
        if (len == 0)
            break;
        s->xfers[i]->length = (int)(len < transfer_size ? len : transfer_size);
        rc = md_usb_error(libusb_submit_transfer(s->xfers[i]));
        if (rc != MYDAQ_OK)
            break;
        ++s->in_flight;
    }
    md_mutex_unlock(&s->lock);
    if (rc != MYDAQ_OK) {
        MD_ERR(t->ctx, "stream 0x%02x: submit failed: %s", ep, mydaq_strerror(rc));
        usb_stream_close(t, s);
        return rc;
    }
    *out = s;
    return MYDAQ_OK;
}

static int usb_handle_events(void *impl, unsigned timeout_ms)
{
    usb_transport *t = impl;
    struct timeval tv;
    tv.tv_sec = (long)(timeout_ms / 1000u);
    tv.tv_usec = (int)(timeout_ms % 1000u) * 1000;
    return md_usb_error(libusb_handle_events_timeout_completed(t->usb, &tv, NULL));
}

static void usb_close(void *impl)
{
    usb_transport *t = impl;
    if (!t)
        return;
    if (t->claimed)
        libusb_release_interface(t->handle, USB_INTERFACE);
    if (t->owns_handle)
        libusb_close(t->handle);
    free(t);
}

static const mydaq_transport_ops usb_ops = {
    usb_write, usb_read, usb_stream_open, usb_stream_close, usb_handle_events, usb_close,
};

/* ---- enumeration and open -------------------------------------------------------------- */

static int is_mydaq(libusb_device *d, struct libusb_device_descriptor *desc)
{
    return libusb_get_device_descriptor(d, desc) == LIBUSB_SUCCESS &&
           desc->idVendor == MYDAQ_USB_VID && desc->idProduct == MYDAQ_USB_PID;
}

static void location_of(libusb_device *d, char *out, size_t cap)
{
    uint8_t ports[8];
    const int n = libusb_get_port_numbers(d, ports, (int)sizeof ports);
    int i, o;
    o = snprintf(out, cap, "%u", (unsigned)libusb_get_bus_number(d));
    for (i = 0; i < n && o > 0 && (size_t)o < cap; ++i)
        o += snprintf(out + o, cap - (size_t)o, "%c%u", i == 0 ? '-' : '.', (unsigned)ports[i]);
}

static int read_serial(libusb_device_handle *h, const struct libusb_device_descriptor *desc,
                       char *out, size_t cap)
{
    int n;
    out[0] = '\0';
    if (desc->iSerialNumber == 0)
        return MYDAQ_ERR_PROTOCOL;
    n = libusb_get_string_descriptor_ascii(h, desc->iSerialNumber, (unsigned char *)out, (int)cap);
    if (n < 0) {
        out[0] = '\0';
        return md_usb_error(n);
    }
    out[(size_t)n < cap ? (size_t)n : cap - 1] = '\0';
    return MYDAQ_OK;
}

int md_usb_enumerate(mydaq_context *ctx, mydaq_device_desc *list, size_t max, size_t *count,
                     unsigned flags)
{
    libusb_device **devs = NULL;
    ssize_t n, i;
    size_t found = 0;

    if (!ctx->usb)
        return MYDAQ_ERR_NOT_SUPPORTED;
    n = libusb_get_device_list(ctx->usb, &devs);
    if (n < 0)
        return md_usb_error((int)n);
    for (i = 0; i < n; ++i) {
        struct libusb_device_descriptor desc;
        if (!is_mydaq(devs[i], &desc))
            continue;
        if (found < max) {
            mydaq_device_desc *e = &list[found];
            memset(e, 0, sizeof *e);
            e->bus = libusb_get_bus_number(devs[i]);
            e->address = libusb_get_device_address(devs[i]);
            location_of(devs[i], e->location, sizeof e->location);
            e->serial_status = MYDAQ_ERR_NOT_SUPPORTED;
            if (flags & MYDAQ_ENUM_READ_SERIAL) {
                libusb_device_handle *h = NULL;
                const int rc = libusb_open(devs[i], &h);
                if (rc == LIBUSB_SUCCESS) {
                    e->serial_status = read_serial(h, &desc, e->serial, sizeof e->serial);
                    libusb_close(h);
                } else {
                    e->serial_status = md_usb_error(rc);
                }
            }
        }
        ++found;
    }
    libusb_free_device_list(devs, 1);
    *count = found;
    return MYDAQ_OK;
}

/* Configures and claims an opened handle; fills the transport. */
static int claim(mydaq_context *ctx, libusb_device_handle *h, int owns, const mydaq_transport_ops **ops,
                 void **impl, mydaq_transport_info *info)
{
    struct libusb_device_descriptor desc;
    usb_transport *t;
    int cfg = -1, rc;

    rc = libusb_get_device_descriptor(libusb_get_device(h), &desc);
    if (rc != LIBUSB_SUCCESS)
        return md_usb_error(rc);
    if (desc.idVendor != MYDAQ_USB_VID || desc.idProduct != MYDAQ_USB_PID)
        return MYDAQ_ERR_NOT_FOUND;
    memset(info, 0, sizeof *info);
    location_of(libusb_get_device(h), info->location, sizeof info->location);
    info->bus = libusb_get_bus_number(libusb_get_device(h));
    info->address = libusb_get_device_address(libusb_get_device(h));

    /* Claim before any I/O: a device held by another program (e.g. a VM) sees no request from
     * us; libusb_get_configuration() uses the cached value where the OS has one. */
    libusb_set_auto_detach_kernel_driver(h, 1);   /* Linux only; harmless elsewhere */
    /* macOS leaves the vendor-class device unconfigured: select configuration 1 ourselves. */
    rc = libusb_get_configuration(h, &cfg);
    if (rc == LIBUSB_SUCCESS && cfg != USB_CONFIGURATION) {
        rc = libusb_set_configuration(h, USB_CONFIGURATION);
        if (rc != LIBUSB_SUCCESS) {
            MD_INFO(ctx, "myDAQ at %s: set configuration failed: %s", info->location,
                    libusb_error_name(rc));
            /* macOS reports a device held by another process as ACCESS */
            return rc == LIBUSB_ERROR_ACCESS ? MYDAQ_ERR_BUSY : md_usb_error(rc);
        }
    }
    rc = libusb_claim_interface(h, USB_INTERFACE);
    if (rc != LIBUSB_SUCCESS) {
        MD_INFO(ctx, "myDAQ at %s: claim failed: %s (in use by another program?)", info->location,
                libusb_error_name(rc));
        return rc == LIBUSB_ERROR_ACCESS ? MYDAQ_ERR_BUSY : md_usb_error(rc);
    }
    if (read_serial(h, &desc, info->serial, sizeof info->serial) != MYDAQ_OK)
        MD_WARN(ctx, "myDAQ at %s: could not read the serial number", info->location);
    t = calloc(1, sizeof *t);
    if (!t) {
        libusb_release_interface(h, USB_INTERFACE);
        return MYDAQ_ERR_NO_MEM;
    }
    t->ctx = ctx;
    t->usb = ctx->usb;
    t->handle = h;
    t->owns_handle = owns;
    t->claimed = 1;
    *ops = &usb_ops;
    *impl = t;
    return MYDAQ_OK;
}

int md_usb_open_by(mydaq_context *ctx, const char *serial, const char *location,
                   libusb_device_handle *handle, const mydaq_transport_ops **ops, void **impl,
                   mydaq_transport_info *info)
{
    libusb_device **devs = NULL;
    ssize_t n, i;
    int rc = MYDAQ_ERR_NOT_FOUND;

    if (!ctx->usb)
        return MYDAQ_ERR_NOT_SUPPORTED;
    if (handle)
        return claim(ctx, handle, 0, ops, impl, info);

    n = libusb_get_device_list(ctx->usb, &devs);
    if (n < 0)
        return md_usb_error((int)n);
    for (i = 0; i < n; ++i) {
        struct libusb_device_descriptor desc;
        libusb_device_handle *h = NULL;
        char buf[MYDAQ_LOCATION_MAX];
        int orc;
        if (!is_mydaq(devs[i], &desc))
            continue;
        if (location && *location) {
            location_of(devs[i], buf, sizeof buf);
            if (strcmp(buf, location) != 0)
                continue;
        }
        orc = libusb_open(devs[i], &h);
        if (orc != LIBUSB_SUCCESS) {
            rc = md_usb_error(orc);
            MD_INFO(ctx, "myDAQ: open failed: %s", libusb_error_name(orc));
            continue;
        }
        rc = claim(ctx, h, 1, ops, impl, info);
        if (rc == MYDAQ_OK) {
            if (!serial || !*serial || strcmp(info->serial, serial) == 0)
                break;
            usb_close(*impl);   /* another myDAQ: release it and keep looking */
            *impl = NULL;
            *ops = NULL;
            rc = MYDAQ_ERR_NOT_FOUND;
            continue;
        }
        libusb_close(h);
        if (location && *location)
            break;
    }
    libusb_free_device_list(devs, 1);
    return rc;
}
