/* Internal definitions shared by the libmydaq sources. */
#ifndef LIBMYDAQ_INTERNAL_H
#define LIBMYDAQ_INTERNAL_H

#include "libmydaq/mydaq.h"
#include "libmydaq/mydaq_transport.h"

#include "frame.h"
#include "platform.h"

#include <stdarg.h>

#if defined(__GNUC__) || defined(__clang__)
#define MD_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define MD_PRINTF(f, a)
#endif

/* ---- context --------------------------------------------------------------------------- */

struct mydaq_context {
    struct libusb_context *usb;   /* NULL for a no-USB context */
    int owns_usb;
    md_mutex log_lock;            /* guards the three fields below */
    mydaq_log_level log_level;
    mydaq_log_fn log_fn;
    void *log_user;
};

void md_log(mydaq_context *ctx, mydaq_log_level level, const char *fmt, ...) MD_PRINTF(3, 4);
int md_log_enabled(mydaq_context *ctx, mydaq_log_level level);

#define MD_ERR(ctx, ...) md_log((ctx), MYDAQ_LOG_ERROR, __VA_ARGS__)
#define MD_WARN(ctx, ...) md_log((ctx), MYDAQ_LOG_WARNING, __VA_ARGS__)
#define MD_INFO(ctx, ...) md_log((ctx), MYDAQ_LOG_INFO, __VA_ARGS__)
#define MD_DBG(ctx, ...) md_log((ctx), MYDAQ_LOG_DEBUG, __VA_ARGS__)

/* ---- device ---------------------------------------------------------------------------- */

/* A frame from the event endpoint (0x82), or a transport failure (transport_status != OK). */
typedef struct md_event {
    uint16_t id;
    uint16_t object_class;
    uint16_t instance;
    int32_t status;
    int transport_status;
} md_event;

typedef void (*md_event_fn)(void *user, const md_event *ev);

#define MD_MAX_LISTENERS 8

struct mydaq_device {
    mydaq_context *ctx;
    const mydaq_transport_ops *ops;
    void *impl;
    mydaq_transport_info tinfo;

    /* Command channel: one request, one response. Held across multi-command sequences so the
     * device's stateful objects are never driven by two callers at once. */
    md_mutex cmd_lock;
    md_mutex out_lock;            /* serialises writes to EP 0x03 (commands and one-way frames) */
    uint8_t tx[MD_FRAME_MAX];
    uint8_t rx[MD_RESPONSE_MAX];
    int cmd_desync;               /* a response may still be in flight: drain before the next */
    uint64_t settle_until_ms;     /* md_cmd re-sends commands the device rejects as not ready
                                     (-201381) until then: a few seconds after init (cmd_lock) */
    int dmm_mode_key;             /* last dmm.set_mode sent: 1 + (mode << 1 | autorange); 0 =
                                     unknown (init, reset, after a DMM error) (cmd_lock) */

    /* State below is guarded by state_lock (never held while waiting on the device). */
    md_mutex state_lock;
    int connected;
    int32_t last_status;
    mydaq_ai_task *ai_task;       /* AI reservation (on-demand reads are refused while set) */
    uint8_t dio_direction;

    /* Event thread pumping the transport while streams are open. pump_ctl serialises
     * acquire/release (incl. the join); pump_lock guards pump_stop, read by the thread. */
    md_mutex pump_ctl;
    md_mutex pump_lock;
    int pump_users;
    int pump_stop;
    md_thread pump_thread;

    /* Event endpoint listeners (events.c). */
    md_mutex events_ctl;
    md_mutex events_lock;
    struct {
        md_event_fn fn;
        void *user;
    } listeners[MD_MAX_LISTENERS];
    size_t nlisteners;
    void *event_stream;

    /* Resources reserved by tasks (state_lock). */
    mydaq_ao_task *ao_task;       /* timed AO: static writes to its channels are refused */
    mydaq_ctr_task *ctr_task;     /* ctr0 */
    int freqout_running;
    uint8_t dio_reserved;         /* DIO lines used by counter/FREQOUT (PFIn = DIOn) */
    uint16_t streams_used;        /* bit n: stream n (endpoints 0x0n / 0x8n) owned by a task */

    int locks_ready;              /* all mutexes above initialised */
    mydaq_info info;
    uint8_t calmem[MYDAQ_CALMEM_SIZE];
    mydaq_calibration cal;
};

/* Command execution. The caller holds dev->cmd_lock (md_cmd_lock/unlock). The response payload
 * stays valid until the next command. */
void md_cmd_lock(mydaq_device *dev);
void md_cmd_unlock(mydaq_device *dev);

#define MD_CMD_ANY_STATUS 0x1u    /* return MYDAQ_OK for a non-zero device status */

int md_cmd(mydaq_device *dev, uint16_t channel, uint8_t op, const md_buf *body,
           unsigned timeout_ms, unsigned flags, md_resp *resp);
/* Sends a one-way request (header byte 6 = 0: the device sends no reply). Does not need
 * cmd_lock, so it can be sent while another command is in flight (NI's stream reports). */
int md_send_oneway(mydaq_device *dev, uint16_t channel, uint8_t op, const md_buf *body);
/* Convenience: body is a hex string. */
int md_cmd_hex(mydaq_device *dev, uint16_t channel, uint8_t op, const char *hex,
               unsigned timeout_ms, unsigned flags, md_resp *resp);
/* Convenience: body is an object id followed by n u32 words. */
int md_cmd_words(mydaq_device *dev, uint16_t channel, uint8_t op, uint32_t object,
                 const uint32_t *words, size_t n, unsigned timeout_ms, md_resp *resp);

#define MD_TIMEOUT_CMD 5000u
#define MD_TIMEOUT_LONG 12000u
#define MD_SETTLE_MS 5000u      /* after init: commands rejected as not ready are re-sent */

/* Records a transport/device error: marks the device disconnected on MYDAQ_ERR_NO_DEVICE. */
int md_note_error(mydaq_device *dev, int err);

/* Stream numbers (endpoint objects 0x02fc000n, credit objects 0x0002000n, bulk endpoints 0x0n OUT /
 * 0x8n IN). NI uses stream 4 for a lone AI or AO task; a second concurrent task needs another
 * one (AI and AO on the same stream kill each other). md_stream_alloc takes the lowest free
 * stream from MD_STREAM_FIRST; MYDAQ_ERR_BUSY when none is free. */
#define MD_STREAM_FIRST 4u
#define MD_STREAM_LAST 10u
int md_stream_alloc(mydaq_device *dev, uint8_t *stream);
void md_stream_free(mydaq_device *dev, uint8_t stream);
int md_is_connected(mydaq_device *dev);

/* Reads and discards whatever is pending on an IN endpoint (short timeouts). */
void md_drain(mydaq_device *dev, uint8_t endpoint, mydaq_endpoint_type type, unsigned max_reads);

/* Event endpoint listeners (events.c): the interrupt stream runs while any is registered. */
int md_events_add(mydaq_device *dev, md_event_fn fn, void *user);
void md_events_remove(mydaq_device *dev, md_event_fn fn, void *user);

/* Event thread (reference counted). */
int md_pump_acquire(mydaq_device *dev);
void md_pump_release(mydaq_device *dev);

/* ---- subsystems ------------------------------------------------------------------------ */

int md_device_init_sequence(mydaq_device *dev);   /* caller holds cmd_lock */
int md_read_calmem(mydaq_device *dev);            /* caller holds cmd_lock */

void md_ai_task_force_destroy(mydaq_ai_task *task);   /* from mydaq_close */
int md_ai_task_abort(mydaq_ai_task *task);            /* from mydaq_reset */
void md_ao_task_force_destroy(mydaq_ao_task *task);
int md_ao_task_abort(mydaq_ao_task *task);
int md_ao_channel_in_task(mydaq_device *dev, mydaq_ao_channel ch);
int md_ao_write_code_unchecked(mydaq_device *dev, mydaq_ao_channel channel, double volts);

/* Sends the scan list (0b/0a/0c on 05 12); caller holds cmd_lock. */
int md_ai_send_scan_list(mydaq_device *dev, const mydaq_ai_channel_config *ch, size_t n);
/* AI scan-list entry for a channel (docs/protocol.md section 5). */
int md_ai_scan_entry(const mydaq_ai_channel_config *ch, int last, uint16_t *entry);
/* Validates channels (1..2, no duplicates, no AI/audio mix, audio ±2 V; audio only if timed). */
int md_ai_validate_channels(const mydaq_ai_channel_config *channels, size_t n, int timed);
/* Convert-clock period in timebase ticks for a scan of n channels at the given divisor. */
uint32_t md_ai_convert_ticks(size_t num_channels, uint32_t divisor);
/* Sample-clock divisor for a per-channel rate (after coercion). */
uint32_t md_rate_divisor(double rate);
double md_divisor_rate(uint32_t divisor);

/* libusb transport */
int md_usb_open_by(mydaq_context *ctx, const char *serial, const char *location,
                   struct libusb_device_handle *handle, const mydaq_transport_ops **ops,
                   void **impl, mydaq_transport_info *info);
int md_usb_enumerate(mydaq_context *ctx, mydaq_device_desc *list, size_t max, size_t *count,
                     unsigned flags);
int md_usb_error(int libusb_error);

#endif
