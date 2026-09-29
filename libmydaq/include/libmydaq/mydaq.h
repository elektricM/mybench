/*
 * libmydaq - native driver library for the NI myDAQ (USB 3923:755b).
 *
 * Plain C11, depends only on libusb-1.0. The protocol was reverse-engineered from USB captures
 * (docs/protocol.md in the MYBENCH repository); no NI software is needed or used.
 *
 * Overview
 *   mydaq_context   one per application or plugin; owns the libusb context and the log settings.
 *   mydaq_device    one opened myDAQ. Opening runs the device initialisation (about 2 s) and reads
 *                   the calibration memory; every conversion uses the device's own constants.
 *   mydaq_ai_task   a hardware-timed analog-input acquisition (finite or continuous; AI0/AI1
 *                   or the audio inputs).
 *   mydaq_ao_task   a hardware-timed generation (finite, regenerated, streamed; AO0/AO1 or the
 *                   audio outputs).
 *   mydaq_ctr_task  a ctr0 task (edge count, frequency, period, pulse train); FREQOUT is a call.
 *   Everything else (on-demand AI, static AO, DIO, DMM) is a plain call on the device.
 *
 * Errors
 *   Functions returning int return MYDAQ_OK (0) or a negative mydaq_error. mydaq_strerror()
 *   describes a code; detailed diagnostics go to the context's log handler.
 *
 * Threading (see also docs/libmydaq.md)
 *   - No global state: independent contexts can be used concurrently.
 *   - A context is thread-safe. A device is thread-safe: calls from several threads are
 *     serialised on the device's command channel, which is strictly one request / one response.
 *     A long call (a DMM reading takes 1-3 s) therefore delays other calls on the same device;
 *     running acquisitions are not affected (their data flows on separate endpoints).
 *   - A task is used from one thread at a time, except that *_stop(), *_available(),
 *     *_space_available() and *_is_running() may be called from any thread (stop wakes a blocked
 *     read/write/wait). Tasks of different subsystems run concurrently.
 *   - While tasks run, the device has an internal event thread (stream completions, event frames)
 *     and each running AO task a writer thread; log callbacks can be invoked from them.
 *   - Destroy tasks before closing their device; mydaq_close() destroys any that are left.
 *   - mydaq_close() and mydaq_context_free() must not race with other calls on the same object.
 */
#ifndef LIBMYDAQ_MYDAQ_H
#define LIBMYDAQ_MYDAQ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIBMYDAQ_VERSION_MAJOR 0
#define LIBMYDAQ_VERSION_MINOR 1
#define LIBMYDAQ_VERSION_PATCH 0

#define MYDAQ_USB_VID 0x3923
#define MYDAQ_USB_PID 0x755b

/** Library version string, e.g. "0.1.0". */
const char *mydaq_version(void);

/* ------------------------------------------------------------------------------------------ */
/* Errors                                                                                     */

typedef enum mydaq_error {
    MYDAQ_OK = 0,
    MYDAQ_ERR_INVALID_ARG = -1,   /**< bad argument or configuration (channel, range, rate, count) */
    MYDAQ_ERR_NO_MEM = -2,        /**< allocation failed */
    MYDAQ_ERR_NOT_FOUND = -3,     /**< no matching myDAQ is attached */
    MYDAQ_ERR_NO_DEVICE = -4,     /**< the device was disconnected (sticky for that handle) */
    MYDAQ_ERR_ACCESS = -5,        /**< permission denied by the OS (e.g. missing udev rule) */
    MYDAQ_ERR_BUSY = -6,          /**< device used by another program, or subsystem reserved by a task */
    MYDAQ_ERR_TIMEOUT = -7,       /**< operation did not complete in time */
    MYDAQ_ERR_OVERFLOW = -8,      /**< acquisition data was lost (host did not read fast enough) */
    MYDAQ_ERR_UNDERFLOW = -9,     /**< generation ran out of data */
    MYDAQ_ERR_USB = -10,          /**< other USB transport failure */
    MYDAQ_ERR_PROTOCOL = -11,     /**< malformed or unexpected response from the device */
    MYDAQ_ERR_DEVICE = -12,       /**< the device rejected a command (non-zero status) */
    MYDAQ_ERR_NOT_SUPPORTED = -13,/**< not implemented (protocol not decoded yet) or not available */
    MYDAQ_ERR_STATE = -14,        /**< call not valid in the current state (e.g. read before start) */
    MYDAQ_ERR_CALIBRATION = -15   /**< calibration memory missing or inconsistent */
} mydaq_error;

/** Human-readable description of an error code (static string, never NULL). */
const char *mydaq_strerror(int err);
/** Symbolic name of an error code, e.g. "MYDAQ_ERR_TIMEOUT" (static string, never NULL). */
const char *mydaq_error_name(int err);

/* ------------------------------------------------------------------------------------------ */
/* Context and logging                                                                        */

typedef struct mydaq_context mydaq_context;
struct libusb_context;
struct libusb_device_handle;

typedef enum mydaq_log_level {
    MYDAQ_LOG_NONE = 0,
    MYDAQ_LOG_ERROR = 1,
    MYDAQ_LOG_WARNING = 2,
    MYDAQ_LOG_INFO = 3,
    MYDAQ_LOG_DEBUG = 4,
    MYDAQ_LOG_TRACE = 5   /**< every command frame as hex */
} mydaq_log_level;

/** Log sink. Called with a complete message (no trailing newline), possibly from any thread. */
typedef void (*mydaq_log_fn)(void *user, mydaq_log_level level, const char *message);

/** Creates a context with its own libusb context. Initial log level: WARNING, to stderr. */
int mydaq_context_new(mydaq_context **ctx);
/** Creates a context on an existing libusb context (e.g. libsigrok's). The caller keeps
 *  ownership of `usb` and must keep it alive until mydaq_context_free(). */
int mydaq_context_new_with_libusb(struct libusb_context *usb, mydaq_context **ctx);
/** Creates a context without USB access (only mydaq_open_transport() works). For tests. */
int mydaq_context_new_no_usb(mydaq_context **ctx);
/** Frees the context. All devices opened from it must be closed first. NULL is allowed. */
void mydaq_context_free(mydaq_context *ctx);
/** Sets the log threshold and sink (fn NULL = stderr). Thread-safe. */
void mydaq_context_set_log(mydaq_context *ctx, mydaq_log_level level, mydaq_log_fn fn, void *user);

/* ------------------------------------------------------------------------------------------ */
/* Enumeration and opening                                                                    */

#define MYDAQ_SERIAL_MAX 24
#define MYDAQ_LOCATION_MAX 32

/** One attached myDAQ. */
typedef struct mydaq_device_desc {
    char serial[MYDAQ_SERIAL_MAX];     /**< USB serial string, e.g. "036CD1C6"; "" if not read */
    char location[MYDAQ_LOCATION_MAX]; /**< "bus-port.port...", e.g. "1-2.3": stable per USB port */
    uint8_t bus;
    uint8_t address;                   /**< changes on every re-plug */
    int serial_status;                 /**< MYDAQ_OK, or why the serial could not be read */
} mydaq_device_desc;

/** mydaq_enumerate() flag: open each device briefly to read its serial string. Without it,
 *  enumeration never opens a device (safe while another program, e.g. a VM, owns it). */
#define MYDAQ_ENUM_READ_SERIAL 0x1u

/** Lists attached myDAQs. Writes up to `max` entries to `list` (may be NULL if max is 0) and
 *  the number of attached devices to *count (may exceed max). */
int mydaq_enumerate(mydaq_context *ctx, mydaq_device_desc *list, size_t max, size_t *count,
                    unsigned flags);

typedef struct mydaq_device mydaq_device;

/** mydaq_open*() flags. */
#define MYDAQ_OPEN_NO_INIT 0x1u   /**< skip the init sequence (device already initialised by this
                                       host); the calibration memory is still read */

/** Opens the myDAQ with the given serial (NULL or "" = the first one found), claims it and runs
 *  the initialisation. Fails with MYDAQ_ERR_BUSY if another program holds it. Blocks ~2 s. */
int mydaq_open(mydaq_context *ctx, const char *serial, unsigned flags, mydaq_device **dev);
/** Same, selecting the device by mydaq_device_desc.location. */
int mydaq_open_location(mydaq_context *ctx, const char *location, unsigned flags,
                        mydaq_device **dev);
/** Same, on a libusb handle the caller opened (e.g. libsigrok's). The handle stays owned by the
 *  caller: mydaq_close() releases the interface but does not close the handle. */
int mydaq_open_handle(mydaq_context *ctx, struct libusb_device_handle *handle, unsigned flags,
                      mydaq_device **dev);
/** Stops and destroys remaining tasks, releases the device. Outputs keep their last value.
 *  NULL is allowed. */
void mydaq_close(mydaq_device *dev);
/** Like NI-DAQmx Device.Reset: stops all tasks, re-runs the initialisation, sets AO0/AO1 to
 *  0 V and all DIO lines to inputs. */
int mydaq_reset(mydaq_device *dev);
/** Returns once the device accepts commands after an open or reset: right after an init it may
 *  still reject them for a second or two (seen after another host released it). One on-demand
 *  AI0 read (~40 ms), re-sent while the device reports "not ready". */
int mydaq_wait_ready(mydaq_device *dev);
/** 1 while the device is attached, 0 once a transfer reported it disconnected. */
int mydaq_is_connected(const mydaq_device *dev);

typedef struct mydaq_info {
    char serial[MYDAQ_SERIAL_MAX];     /**< "036CD1C6" */
    char location[MYDAQ_LOCATION_MAX];
    uint8_t usb_bus, usb_address;      /**< the address changes on every re-plug */
    uint32_t serial_number;            /**< serial as a number, 0x036CD1C6 = 57463238 (NI's decimal) */
    uint16_t usb_vid, usb_pid;
    char product[16];                  /**< "NI myDAQ" */
    uint32_t identity[4];              /**< device-core identity words (meaning unknown) */
    uint32_t core_info[3];             /**< device-core info words (meaning unknown) */
    uint32_t stream_buffer_sizes[7];   /**< capability table sizes (per stream endpoint, [H]) */
    double init_seconds;               /**< duration of the last initialisation */
} mydaq_info;

int mydaq_get_info(mydaq_device *dev, mydaq_info *info);

/* ------------------------------------------------------------------------------------------ */
/* Calibration (read from the device's calibration memory at open)                            */

#define MYDAQ_CALMEM_SIZE 2048
#define MYDAQ_CAL_AI_ENTRIES 7    /* ±10, ±5, ±2, ±1, ±0.5, ±0.2, ±0.1 V; myDAQ uses [0] and [2] */
#define MYDAQ_CAL_AO_BLOCKS 4     /* ao0, ao1, audio out L, audio out R (nominal, uncalibrated) */
#define MYDAQ_CAL_AO_ENTRIES 4    /* ±10, ±5, ±2, ±1 V */
#define MYDAQ_CAL_DMM_ENTRIES 28

/** A float32 (gain, offset) pair exactly as stored; conversions are evaluated in double. */
typedef struct mydaq_cal_pair {
    float gain;
    float offset;
} mydaq_cal_pair;

typedef struct mydaq_calibration {
    int64_t ai_ao_time;       /**< external calibration time of the AI/AO section, Unix seconds UTC */
    int64_t dmm_time;         /**< calibration time of the DMM section, Unix seconds UTC */
    uint32_t ai_ao_temperature;   /**< section header word after the time (37: probably deg C) */
    uint32_t dmm_temperature;
    /** AI: volts = offset + gain * code (signed 16-bit ADC code). */
    mydaq_cal_pair ai[MYDAQ_CAL_AI_ENTRIES];
    /** AO: code = round(offset + gain * volts), [block][range: ±10, ±5, ±2, ±1 V]. */
    mydaq_cal_pair ao[MYDAQ_CAL_AO_BLOCKS][MYDAQ_CAL_AO_ENTRIES];
    /** DMM: value = gain * display + offset; slot per (function, range digit), see
     *  mydaq_dmm_cal_slot(). Unused slots hold (1, 0). */
    mydaq_cal_pair dmm[MYDAQ_CAL_DMM_ENTRIES];
    char user_info[8];        /**< DAQmx UserDefinedCalibrationInfo, e.g. "NI" */
    int backup_matches;       /**< 1 if the redundant copies of both sections are identical */
    int checksums_ok;         /**< 1 if the directory and all sections pass their CRC-16 */
} mydaq_calibration;

/** Parses a calibration memory image (pure function; `len` must be MYDAQ_CALMEM_SIZE). */
int mydaq_parse_calibration(const uint8_t *mem, size_t len, mydaq_calibration *cal);
/** The device's parsed calibration (valid until mydaq_close). */
const mydaq_calibration *mydaq_get_calibration(const mydaq_device *dev);
/** Copies the raw calibration memory read at open (up to `len` bytes, normally 2048). */
int mydaq_get_calibration_memory(const mydaq_device *dev, uint8_t *buf, size_t len);

/* ------------------------------------------------------------------------------------------ */
/* Analog input                                                                               */

typedef enum mydaq_ai_channel {
    MYDAQ_AI0 = 0,
    MYDAQ_AI1 = 1,
    MYDAQ_AUDIO_IN_LEFT = 2,    /**< ±2 V, AC coupled, timed tasks only (not with AI0/AI1) */
    MYDAQ_AUDIO_IN_RIGHT = 3
} mydaq_ai_channel;

typedef enum mydaq_ai_range {
    MYDAQ_AI_RANGE_10V = 0,     /**< ±10 V */
    MYDAQ_AI_RANGE_2V = 1       /**< ±2 V */
} mydaq_ai_range;

typedef struct mydaq_ai_channel_config {
    mydaq_ai_channel channel;
    mydaq_ai_range range;
} mydaq_ai_channel_config;

#define MYDAQ_AI_MAX_CHANNELS 2
#define MYDAQ_TIMEBASE_HZ 100e6          /* sample clocks are 100 MHz / divisor */
#define MYDAQ_AI_MAX_RATE 200e3          /* per channel */
#define MYDAQ_AO_MAX_RATE 200e3          /* per channel */
#define MYDAQ_MIN_RATE (100e6 / 4294967295.0)

/** Converts an ADC code with the calibration (bit-exact with NI-DAQmx). NaN for a bad range. */
double mydaq_ai_code_to_volts(const mydaq_calibration *cal, mydaq_ai_range range, int16_t code);

/** One on-demand scan (software timed, ~30 ms): one value per channel, in the given order.
 *  AI0/AI1 only: audio inputs need a sample clock (MYDAQ_ERR_NOT_SUPPORTED). */
int mydaq_ai_read_scan(mydaq_device *dev, const mydaq_ai_channel_config *channels,
                       size_t num_channels, double *volts);
/** Same, returning raw ADC codes. */
int mydaq_ai_read_scan_raw(mydaq_device *dev, const mydaq_ai_channel_config *channels,
                           size_t num_channels, int16_t *codes);

typedef enum mydaq_sample_mode {
    MYDAQ_FINITE = 0,
    MYDAQ_CONTINUOUS = 1
} mydaq_sample_mode;

typedef struct mydaq_ai_config {
    mydaq_ai_channel_config channels[MYDAQ_AI_MAX_CHANNELS];
    size_t num_channels;            /**< 1 or 2, no duplicates */
    double sample_rate;             /**< per channel, S/s; coerced (mydaq_ai_coerce_rate) */
    mydaq_sample_mode mode;
    uint64_t samples_per_channel;   /**< finite: exact count per start; continuous: buffer hint */
    size_t buffer_samples;          /**< continuous: exact host buffer per channel. 0 = automatic:
                                         the device is told samples_per_channel (as NI-DAQmx does;
                                         ~100 ms of data if 0) and the host keeps at least 2 s. The
                                         device sends data in bursts of that size (latency). */
} mydaq_ai_config;

typedef struct mydaq_ai_task mydaq_ai_task;
typedef struct mydaq_ao_task mydaq_ao_task;
typedef struct mydaq_ctr_task mydaq_ctr_task;

/** Achievable per-channel rate as NI-DAQmx coerces it: 100 MHz / floor(100 MHz / requested),
 *  within 0.0233 S/s .. 200 kS/s. */
double mydaq_ai_coerce_rate(double requested, size_t num_channels);

/** Creates a task and reserves the analog input (MYDAQ_ERR_BUSY if another task has it). */
int mydaq_ai_task_create(mydaq_device *dev, const mydaq_ai_config *config, mydaq_ai_task **task);
/** Stops the task if needed and releases the reservation. NULL is allowed. */
void mydaq_ai_task_destroy(mydaq_ai_task *task);
/** Starts an acquisition (finite: a new block of samples_per_channel). */
int mydaq_ai_task_start(mydaq_ai_task *task);
/** Stops the acquisition; unread samples are discarded. Idempotent, callable from any thread. */
int mydaq_ai_task_stop(mydaq_ai_task *task);
/** 1 while acquiring (finite: until the last sample has arrived). */
int mydaq_ai_task_is_running(const mydaq_ai_task *task);
/** Samples per channel readable now without blocking. */
size_t mydaq_ai_task_available(const mydaq_ai_task *task);
/** Samples per channel still to be read in a finite acquisition (0 for continuous). */
uint64_t mydaq_ai_task_remaining(const mydaq_ai_task *task);
double mydaq_ai_task_rate(const mydaq_ai_task *task);           /**< actual rate per channel */
/** Inter-channel skew, s: channel k is sampled k*skew after channel 0 within a scan. */
double mydaq_ai_task_channel_skew(const mydaq_ai_task *task);

/** Output layout of mydaq_ai_task_read(). */
typedef enum mydaq_layout {
    MYDAQ_GROUP_BY_CHANNEL = 0,   /**< all of channel 0, then all of channel 1 */
    MYDAQ_GROUP_BY_SCAN = 1       /**< interleaved: c0 c1 c0 c1 ... */
} mydaq_layout;

/**
 * Reads exactly `samples_per_channel` samples per channel, blocking up to timeout_ms
 * (negative = forever). On MYDAQ_ERR_TIMEOUT nothing is consumed. Returns MYDAQ_ERR_OVERFLOW
 * once data was lost, MYDAQ_ERR_NO_DEVICE after an unplug, MYDAQ_ERR_STATE if the task is not
 * running and the request exceeds what is buffered, MYDAQ_ERR_INVALID_ARG if a finite request
 * exceeds the remaining samples. `volts` holds samples_per_channel * num_channels values.
 */
int mydaq_ai_task_read(mydaq_ai_task *task, double *volts, size_t samples_per_channel,
                       mydaq_layout layout, int timeout_ms);
/** Same, raw ADC codes, always interleaved by scan. */
int mydaq_ai_task_read_raw(mydaq_ai_task *task, int16_t *codes, size_t samples_per_channel,
                           int timeout_ms);

/* ------------------------------------------------------------------------------------------ */
/* Analog output                                                                              */

typedef enum mydaq_ao_channel {
    MYDAQ_AO0 = 0,
    MYDAQ_AO1 = 1,
    MYDAQ_AUDIO_OUT_LEFT = 2,   /**< ±2 V; timed tasks only (not with AO0/AO1) */
    MYDAQ_AUDIO_OUT_RIGHT = 3
} mydaq_ao_channel;

/** Converts volts to a DAC code with the ±10 V calibration of AO0/AO1 (round to nearest, ties to
 *  even; bit-exact with NI-DAQmx). MYDAQ_ERR_INVALID_ARG outside ±10 V or for another channel. */
int mydaq_ao_volts_to_code(const mydaq_calibration *cal, mydaq_ao_channel channel, double volts,
                           int16_t *code);
/** Output range of AO0/AO1 (the audio outputs are always ±2 V). */
typedef enum mydaq_ao_range {
    MYDAQ_AO_RANGE_10V = 0,   /**< ±10 V (default) */
    MYDAQ_AO_RANGE_2V = 1     /**< ±2 V: finer steps, the ±2 V calibration record */
} mydaq_ao_range;

/** Static (software-timed) update of one output, ±10 V range. */
int mydaq_ao_write(mydaq_device *dev, mydaq_ao_channel channel, double volts);
/** Same in a given range (MYDAQ_ERR_INVALID_ARG outside it). */
int mydaq_ao_write_range(mydaq_device *dev, mydaq_ao_channel channel, double volts,
                         mydaq_ao_range range);
/** Same with a raw DAC code. */
int mydaq_ao_write_raw(mydaq_device *dev, mydaq_ao_channel channel, int16_t code);

/** Hardware-timed output (AO0/AO1 ±10 V, or the audio outputs ±2 V; not mixed). */
typedef struct mydaq_ao_config {
    mydaq_ao_channel channels[2];
    size_t num_channels;     /**< 1 or 2, no duplicates */
    double sample_rate;      /**< per channel, S/s; coerced like AI (100 MHz / floor divisor) */
    mydaq_sample_mode mode;
    /** Continuous only. 1: the buffer written before start repeats until stop (the host
     *  re-sends it). 0: streaming, the application keeps appending with mydaq_ao_task_write. */
    int regenerate;
    size_t buffer_samples;   /**< streaming: host buffer per channel (0 = 1 s, at least 4096) */
    mydaq_ao_range ranges[2];   /**< per channel, AO0/AO1 (0 = ±10 V); ignored for audio */
} mydaq_ao_config;

/** Code for a DAC channel in a timed task (AO: ±10 V record, audio: ±2 V record). */
int mydaq_ao_task_volts_to_code(const mydaq_calibration *cal, mydaq_ao_channel channel,
                                double volts, int16_t *code);
/** Same in a given AO range (±2 V: the ±2 V record, as the audio outputs). */
int mydaq_ao_task_volts_to_code_range(const mydaq_calibration *cal, mydaq_ao_channel channel,
                                      double volts, mydaq_ao_range range, int16_t *code);
/** Creates a task and reserves the timed output (MYDAQ_ERR_BUSY if another task has it);
 *  static writes to its channels are refused while it exists. */
int mydaq_ao_task_create(mydaq_device *dev, const mydaq_ao_config *config, mydaq_ao_task **task);
/** Stops the task if needed (outputs keep their last value) and releases it. NULL allowed. */
void mydaq_ao_task_destroy(mydaq_ao_task *task);
/**
 * Finite or regenerated: sets the buffer (replacing the previous one; before start only,
 * MYDAQ_ERR_STATE while generating). Streaming: appends, blocking up to timeout_ms (negative =
 * forever) until the host buffer has room for the whole block; before start it pre-fills.
 * Returns MYDAQ_ERR_UNDERFLOW once the output ran out of data.
 */
int mydaq_ao_task_write(mydaq_ao_task *task, const double *volts, size_t samples_per_channel,
                        mydaq_layout layout, int timeout_ms);
/** Same with DAC codes, interleaved by scan. */
int mydaq_ao_task_write_raw(mydaq_ao_task *task, const int16_t *codes, size_t samples_per_channel,
                            int timeout_ms);
/** Starts generating (the buffer, or the pre-filled stream, must not be empty). */
int mydaq_ao_task_start(mydaq_ao_task *task);
/** Stops; outputs keep their last value; unsent streaming data is discarded. Idempotent. */
int mydaq_ao_task_stop(mydaq_ao_task *task);
/** Finite: waits until the last sample was output (MYDAQ_ERR_TIMEOUT otherwise). */
int mydaq_ao_task_wait_done(mydaq_ao_task *task, int timeout_ms);
int mydaq_ao_task_is_running(const mydaq_ao_task *task);
/** Streaming: samples per channel that can be written without blocking. */
size_t mydaq_ao_task_space_available(const mydaq_ao_task *task);
/** The error that ended the generation (MYDAQ_ERR_UNDERFLOW, MYDAQ_ERR_NO_DEVICE, ...), or OK. */
int mydaq_ao_task_error(const mydaq_ao_task *task);
double mydaq_ao_task_rate(const mydaq_ao_task *task);
/** Samples per channel sent to the device since start. */
uint64_t mydaq_ao_task_samples_sent(const mydaq_ao_task *task);

/* ------------------------------------------------------------------------------------------ */
/* Digital I/O (port0, lines DIO0..7, static)                                                 */

/** Lines with a 1 in `mask` become outputs (output != 0) or inputs (output == 0).
 *  MYDAQ_ERR_BUSY if the mask includes a line a counter/FREQOUT task uses. */
int mydaq_dio_set_direction(mydaq_device *dev, uint8_t mask, int output);
/** Current direction (1 = output), as last set through this handle. The device cannot report
 *  it: the cache starts as all inputs (power-up state), so a program that finds the device left
 *  by someone else should set the direction explicitly. */
int mydaq_dio_get_direction(mydaq_device *dev, uint8_t *direction);
/** Sets the output latch of the lines in `mask`. Lines that are inputs take the latched value
 *  when they become outputs (write before switching to avoid glitches). */
int mydaq_dio_write(mydaq_device *dev, uint8_t value, uint8_t mask);
/** Levels of all 8 lines (outputs read back their driven level). */
int mydaq_dio_read(mydaq_device *dev, uint8_t *levels);

/* ------------------------------------------------------------------------------------------ */
/* DMM (Cyrustek ES51981 converter behind the device's command channel; isolated)             */

typedef enum mydaq_dmm_function {
    MYDAQ_DMM_DC_VOLTS = 0,
    MYDAQ_DMM_AC_VOLTS = 1,
    MYDAQ_DMM_DC_CURRENT = 2,
    MYDAQ_DMM_AC_CURRENT = 3,
    MYDAQ_DMM_RESISTANCE = 4,
    MYDAQ_DMM_DIODE = 5,
    MYDAQ_DMM_CONTINUITY = 6
} mydaq_dmm_function;
#define MYDAQ_DMM_FUNCTION_COUNT 7

/** One DMM range of a function. */
typedef struct mydaq_dmm_range_info {
    mydaq_dmm_function function;
    double full_scale;         /**< base units (V, A, ohm) */
    const char *label;         /**< "20 V", "200 mA", "2 MOhm" (ASCII) */
    double resolution;         /**< display LSB in base units */
    int supported;             /**< 1 if this library can measure it (protocol decoded) */
    int verified;              /**< 1 if its mode byte was confirmed by a capture of NI-DAQmx */
} mydaq_dmm_range_info;

/** The ranges of a function, largest first (static table). */
int mydaq_dmm_ranges(mydaq_dmm_function function, const mydaq_dmm_range_info **ranges,
                     size_t *count);

/** A decoded ES51981 serial packet (11 bytes: range, 4 digits, function, status, option1,
 *  option2, CR, LF; each character 0x30 + value). Flag meanings follow the public Cyrustek
 *  format; only range, digits, function, sign and overload are verified on the myDAQ. */
typedef struct mydaq_es51981 {
    uint8_t raw[11];
    int range;        /**< range digit 0..7 (meaning depends on the function) */
    int digits;       /**< display magnitude 0..9999 */
    int function;     /**< function nibble (0xB = voltage, ...) */
    int negative;     /**< sign flag */
    int overload;     /**< OL flag */
    int judge, battery_low;
    int hold_max, hold_min, relative, rmr;
    int dc, ac, autorange, vahz;
} mydaq_es51981;

/** Parses one packet (at least 9 bytes; CR LF optional). MYDAQ_ERR_PROTOCOL if malformed. */
int mydaq_es51981_parse(const uint8_t *packet, size_t len, mydaq_es51981 *out);

typedef struct mydaq_dmm_reading {
    double value;            /**< calibrated, base units (RMS for AC); ±range when overloaded */
    double display;          /**< uncalibrated display value, base units */
    int overload;            /**< -1 / +1 (sign of the overload) when over range, else 0 */
    mydaq_es51981 packet;    /**< the converter's packet */
} mydaq_dmm_reading;

/** Calibration slot (index into mydaq_calibration.dmm) for a packet measured with `function`,
 *  or -1 if the reading is not calibrated (diode, continuity) or the packet does not fit. */
int mydaq_dmm_cal_slot(mydaq_dmm_function function, const mydaq_es51981 *packet);

/** Converts a packet to a reading exactly as NI's driver does (bit-exact with DAQmx):
 *  display = digits scaled by (function byte, range digit), value = gain * display + offset.
 *  `cal` may be NULL (value = display). MYDAQ_ERR_PROTOCOL for an unknown function byte. */
int mydaq_dmm_convert(const mydaq_calibration *cal, mydaq_dmm_function function,
                      const mydaq_es51981 *packet, double full_scale, mydaq_dmm_reading *reading);

/** mydaq_dmm_read_ex() flag: let the meter chip choose the range (not with continuity).
 *  Unverified on hardware. */
#define MYDAQ_DMM_HW_AUTORANGE 0x1u

/** mydaq_dmm_read_ex() flag: send dmm.set_mode only when the function/range differs from the
 *  last one sent (as ELVISmx: set the mode once, then read). The meter then delivers a reading
 *  every 0.5 s (V, A, Ohm) or ~94 ms (diode, continuity); without it every read re-sends the
 *  mode like NI-DAQmx and waits ~2.5 s for the meter to settle. */
#define MYDAQ_DMM_KEEP_MODE 0x2u

/** Like mydaq_dmm_read() with flags (MYDAQ_DMM_HW_AUTORANGE, MYDAQ_DMM_KEEP_MODE). */
int mydaq_dmm_read_ex(mydaq_device *dev, mydaq_dmm_function function, double range,
                      unsigned flags, mydaq_dmm_reading *reading);
/** Same with the device-side wait for the next packet (mydaq_dmm_read_ex: 10000 ms);
 *  MYDAQ_ERR_TIMEOUT when the meter sends none within timeout_ms. */
int mydaq_dmm_read_timeout(mydaq_device *dev, mydaq_dmm_function function, double range,
                           unsigned flags, int timeout_ms, mydaq_dmm_reading *reading);

/** One blocking DMM reading (1-3 s: selects the function/range, waits for a fresh packet).
 *  `range` is a full_scale from mydaq_dmm_ranges(). MYDAQ_ERR_NOT_SUPPORTED for functions whose
 *  protocol is not decoded yet. */
int mydaq_dmm_read(mydaq_device *dev, mydaq_dmm_function function, double range,
                   mydaq_dmm_reading *reading);

/* ------------------------------------------------------------------------------------------ */
/* Counter ctr0 and FREQOUT (their signals use the DIO lines: PFI n = DIO n)                  */

typedef enum mydaq_ctr_kind {
    MYDAQ_CTR_COUNT_EDGES = 0,   /**< edges on DIO0 (or `input`), counting up from initial_count */
    MYDAQ_CTR_PULSE_TRAIN = 1,   /**< pulse train on DIO3 (idle low: low phase first) */
    MYDAQ_CTR_FREQUENCY = 2,     /**< frequency of the signal on DIO1 (100 MHz ticks per period) */
    MYDAQ_CTR_PERIOD = 3,        /**< period of the signal on DIO1 */
    MYDAQ_CTR_PULSE_WIDTH = 4,   /**< high time (rising `edge`) or low time (falling) on DIO1 */
    MYDAQ_CTR_PULSE_TIME = 5,    /**< high and low time of one period on DIO1 (two values per read;
                                      the myDAQ has no semi-period mode: derive it from these) */
    MYDAQ_CTR_LINEAR_ENCODER = 6,  /**< quadrature position count: A DIO0, B DIO2, Z DIO1 */
    MYDAQ_CTR_ANGULAR_ENCODER = 7, /**< same counts (only the device's application code differs) */
    MYDAQ_CTR_TWO_EDGE_SEPARATION = 8   /**< time from an edge on DIO2 (`falling`) to the next edge
                                             on DIO1 (`second_falling`); the DIO routes are
                                             inferred, verified on the FREQOUT input */
} mydaq_ctr_kind;

/** Signal a counter input measures (instead of its DIO terminal). */
typedef enum mydaq_ctr_input {
    MYDAQ_CTR_INPUT_TERMINAL = 0,   /**< the default DIO line of the measurement */
    MYDAQ_CTR_INPUT_FREQOUT = 1,    /**< the internal FREQOUT signal (no wiring; FREQOUT must run) */
    MYDAQ_CTR_INPUT_20MHZ = 2,      /**< count edges only: the 20 MHz timebase */
    MYDAQ_CTR_INPUT_100KHZ = 3      /**< count edges only: the 100 kHz timebase */
} mydaq_ctr_input;

/** Encoder decoding (values as on the wire). */
typedef enum mydaq_ctr_decoding {
    MYDAQ_CTR_X1 = 1,
    MYDAQ_CTR_X2 = 2,
    MYDAQ_CTR_X4 = 3,
    MYDAQ_CTR_TWO_PULSE = 4
} mydaq_ctr_decoding;

typedef struct mydaq_ctr_config {
    mydaq_ctr_kind kind;
    double frequency;        /**< pulse train, Hz (100 MHz / whole ticks) */
    double duty_cycle;       /**< pulse train, 0 < duty < 1 (high and low >= 2 ticks) */
    mydaq_sample_mode mode;  /**< pulse train: finite or continuous */
    uint64_t pulses;         /**< finite pulse train: number of pulses */
    /* Added with the stage-3 counter modes (zero = the previous behaviour). */
    int falling;             /**< inputs: count/measure falling edges; pulse width: low time */
    mydaq_ctr_input input;   /**< count edges, frequency, period, pulse width/time, encoder A */
    uint32_t initial_count;  /**< count edges: first value */
    int reset_on_dio1;       /**< count edges: a rising edge on DIO1 resets the count */
    mydaq_ctr_decoding decoding;   /**< encoders (0 = X4) */
    int z_index;             /**< encoders: Z input on DIO1 enabled */
    int idle_high;           /**< pulse train: output high while idle (high phase first) */
    double initial_delay;    /**< pulse train: seconds before the first pulse (0 = none) */
    int second_falling;      /**< two-edge separation: the second edge is falling */
} mydaq_ctr_config;

/** Low/high times in 100 MHz ticks for a pulse train, and the resulting frequency/duty. */
int mydaq_ctr_coerce_pulse(double frequency, double duty, uint32_t *low_ticks,
                           uint32_t *high_ticks, double *actual_frequency, double *actual_duty);
/** Creates a ctr0 task, reserving ctr0 and its DIO line (MYDAQ_ERR_BUSY if ctr0 is in use, or
 *  the line is reserved or configured as a DIO output). */
int mydaq_ctr_task_create(mydaq_device *dev, const mydaq_ctr_config *config, mydaq_ctr_task **task);
void mydaq_ctr_task_destroy(mydaq_ctr_task *task);
int mydaq_ctr_task_start(mydaq_ctr_task *task);
int mydaq_ctr_task_stop(mydaq_ctr_task *task);
int mydaq_ctr_task_is_running(const mydaq_ctr_task *task);
/** Raw value on demand: edge count since start, encoder position (two's complement), or 100 MHz
 *  ticks of the last period / pulse (pulse time: the high time). The device waits up to
 *  timeout_ms for a value (MYDAQ_ERR_TIMEOUT: no complete period, e.g. no signal). */
int mydaq_ctr_task_read_count(mydaq_ctr_task *task, uint32_t *count, int timeout_ms);
/** Pulse time: high and low time of one period, in 100 MHz ticks. */
int mydaq_ctr_task_read_pulse_time(mydaq_ctr_task *task, uint32_t *high, uint32_t *low,
                                   int timeout_ms);
/** Same, in the measurement's unit: edges, Hz (frequency), s (period, pulse width, pulse time:
 *  the period), encoder counts (signed). */
int mydaq_ctr_task_read(mydaq_ctr_task *task, double *value, int timeout_ms);
/** Finite pulse train: waits for the last pulse (MYDAQ_ERR_TIMEOUT otherwise). */
int mydaq_ctr_task_wait_done(mydaq_ctr_task *task, int timeout_ms);
/** Actual pulse-train frequency and duty cycle. */
int mydaq_ctr_task_pulse(const mydaq_ctr_task *task, double *frequency, double *duty_cycle);
/** DIO lines the task reserves (bit n = DIOn). */
uint8_t mydaq_ctr_task_dio_lines(const mydaq_ctr_task *task);
/** Running continuous pulse train: new frequency / duty cycle without stopping (the device
 *  switches at the next period; MYDAQ_ERR_STATE if not running or not continuous). */
int mydaq_ctr_task_update_pulse(mydaq_ctr_task *task, double frequency, double duty_cycle);

/** FREQOUT (square wave on DIO4): 20 MHz / n, 10 MHz / n or 100 kHz / n, n = 1..16. Nearest
 *  achievable frequency (a tie goes to the faster timebase). */
double mydaq_freqout_coerce(double hz);
/** Starts FREQOUT, reserving DIO4; *actual (may be NULL) receives the frequency. */
int mydaq_freqout_start(mydaq_device *dev, double hz, double *actual);
int mydaq_freqout_stop(mydaq_device *dev);
int mydaq_freqout_running(const mydaq_device *dev);

/* ------------------------------------------------------------------------------------------ */
/* Diagnostics                                                                                */

/** Device status word of the last command the device rejected (0 if none). */
int32_t mydaq_last_device_status(const mydaq_device *dev);

#ifdef __cplusplus
}
#endif

#endif /* LIBMYDAQ_MYDAQ_H */
