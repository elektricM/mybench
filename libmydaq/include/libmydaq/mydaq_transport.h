/*
 * libmydaq transport interface (advanced use).
 *
 * The library talks to the device through a small transport vtable. The built-in transport is
 * libusb; tests plug in a mock (libmydaq/tests/mock) that replays captured USB exchanges or
 * emulates the device, so the whole library can be exercised without hardware.
 *
 * Endpoints used by the myDAQ: 0x03 bulk OUT (commands), 0x81 bulk IN (responses),
 * 0x82 interrupt IN (event frames), 0x84 bulk IN (AI samples); see docs/protocol.md.
 *
 * Contract for implementations:
 *   - write/read are synchronous and may be called from any thread (the library serialises
 *     command traffic itself, but event and stream traffic run concurrently with it).
 *   - Streams are asynchronous: the transport keeps up to `num_transfers` transfers (buffers of
 *     `transfer_size` bytes) queued on the endpoint and reports each completion through the
 *     callback, from inside handle_events(). The callback decides every transfer's length: it
 *     is called once per transfer with data == NULL when the stream opens (initial length), and
 *     after each completion; it returns the length to (re)submit, 0 to retire the transfer.
 *     This lets a finite acquisition request exactly the bytes still expected.
 *   - handle_events() is called repeatedly by the library's event thread while a stream is open.
 *   - Errors are mydaq_error codes (MYDAQ_ERR_NO_DEVICE once the device is gone).
 */
#ifndef LIBMYDAQ_MYDAQ_TRANSPORT_H
#define LIBMYDAQ_MYDAQ_TRANSPORT_H

#include "libmydaq/mydaq.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mydaq_endpoint_type {
    MYDAQ_EP_BULK = 0,
    MYDAQ_EP_INTERRUPT = 1
} mydaq_endpoint_type;

/**
 * Stream callback. On open: data == NULL, len == requested == 0, status MYDAQ_OK: return the
 * initial length of one transfer (0 = do not submit it). On completion: `data`/`len` are the bytes
 * received for a transfer of `requested` bytes, `status` is MYDAQ_OK or the transfer error
 * (MYDAQ_ERR_NO_DEVICE, MYDAQ_ERR_USB, ...); cancelled transfers are not reported. Returns the
 * length to resubmit (clamped to transfer_size), 0 to retire the transfer.
 */
typedef size_t (*mydaq_stream_cb)(void *user, const uint8_t *data, size_t len, size_t requested,
                                  int status);

typedef struct mydaq_transport_ops {
    /** Synchronous OUT transfer. *actual (may be NULL) receives the bytes accepted, also on
     *  MYDAQ_ERR_TIMEOUT (a bulk OUT the device NAKs completes partially). */
    int (*write)(void *impl, uint8_t endpoint, const uint8_t *data, size_t len, size_t *actual,
                 unsigned timeout_ms);
    /** Synchronous IN transfer; *actual receives the byte count. */
    int (*read)(void *impl, uint8_t endpoint, mydaq_endpoint_type type, uint8_t *buf,
                size_t cap, size_t *actual, unsigned timeout_ms);
    /** Starts an asynchronous IN stream; *stream receives a handle for stream_close. */
    int (*stream_open)(void *impl, uint8_t endpoint, mydaq_endpoint_type type,
                       size_t transfer_size, unsigned num_transfers, mydaq_stream_cb cb,
                       void *user, void **stream);
    /** Cancels the stream's transfers and waits until none is in flight; no callback runs after
     *  it returns. Must not be called from inside a stream callback. */
    void (*stream_close)(void *impl, void *stream);
    /** Processes completions for up to timeout_ms. */
    int (*handle_events)(void *impl, unsigned timeout_ms);
    /** Releases the transport (called once by mydaq_close). */
    void (*close)(void *impl);
} mydaq_transport_ops;

/** Identification the transport provides for mydaq_get_info(). */
typedef struct mydaq_transport_info {
    char serial[MYDAQ_SERIAL_MAX];
    char location[MYDAQ_LOCATION_MAX];
    uint8_t bus, address;   /* USB bus number and device address (0 if not applicable) */
} mydaq_transport_info;

/**
 * Opens a device on a custom transport and runs the same initialisation as mydaq_open().
 * `ops` must outlive the device; `impl` is passed to every op and released through ops->close
 * by mydaq_close() (also when this call fails).
 */
int mydaq_open_transport(mydaq_context *ctx, const mydaq_transport_ops *ops, void *impl,
                         const mydaq_transport_info *info, unsigned flags, mydaq_device **dev);

#ifdef __cplusplus
}
#endif

#endif /* LIBMYDAQ_MYDAQ_TRANSPORT_H */
