/*
 * Mock transport for libmydaq tests (no hardware needed).
 *
 * Two modes:
 *  - Replay: follows a script converted from a USB capture of NI's driver (tools/pcap2mock.py,
 *    tests/data/NAME.mock). Every command the library sends must equal the next captured command
 *    byte for byte; the captured responses, events and sample data are returned. This proves
 *    that libmydaq drives the device exactly like NI-DAQmx did.
 *  - Emulator: a behavioural model of the myDAQ for every decoded command (identity, calibration
 *    memory, on-demand and finite AI with generated samples, AO, DIO, DMM), with fault injection
 *    (unplug, device events). Used for API/threading tests and by the C++ backend tests.
 *
 * All functions are thread-safe.
 */
#ifndef LIBMYDAQ_MOCK_H
#define LIBMYDAQ_MOCK_H

#include "libmydaq/mydaq.h"
#include "libmydaq/mydaq_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mydaq_mock mydaq_mock;

/** Replay mock from a script file; NULL if the file cannot be read or parsed. */
mydaq_mock *mydaq_mock_new_replay(const char *path);
/** Emulator with the given calibration memory image (2048 bytes). */
mydaq_mock *mydaq_mock_new_emulator(const uint8_t *calmem, size_t len);
/** Emulator with the calibration memory loaded from a file. */
mydaq_mock *mydaq_mock_new_emulator_file(const char *calmem_path);
/** Frees the mock. The device opened on it must be closed first. */
void mydaq_mock_free(mydaq_mock *m);

const mydaq_transport_ops *mydaq_mock_ops(void);
/** Opens a device on the mock (mydaq_open_transport). The device does not own the mock. */
int mydaq_mock_open(mydaq_context *ctx, mydaq_mock *m, unsigned flags, mydaq_device **dev);
void mydaq_mock_set_serial(mydaq_mock *m, const char *serial, const char *location);
/** USB address reported for the device (default 1; changes on a simulated re-plug). */
void mydaq_mock_set_address(mydaq_mock *m, uint8_t address);

/* ---- replay ---- */
/** 1 once every command of the script was sent (and its response read). */
int mydaq_mock_replay_finished(const mydaq_mock *m);
/** Description of the first mismatch, or NULL. */
const char *mydaq_mock_error(const mydaq_mock *m);
/** The values of the n-th "=" line of the script (0-based); returns their count (<= max). */
size_t mydaq_mock_script_values(const mydaq_mock *m, size_t n, double *values, size_t max);

/* ---- emulator ---- */
/** Sample source: returns the ADC code of `channel` (0/1) for sample `index`. */
typedef int16_t (*mydaq_mock_ai_fn)(void *user, int channel, int range_code, uint64_t index);
void mydaq_mock_set_ai_source(mydaq_mock *m, mydaq_mock_ai_fn fn, void *user);
/** 1: stream samples at the configured sample rate; 0 (default): as fast as possible. */
void mydaq_mock_set_realtime(mydaq_mock *m, int realtime);
/** Before mydaq_mock_open: offer no asynchronous OUT streams (the library's synchronous AO path). */
void mydaq_mock_set_sync_out(mydaq_mock *m, int sync_out);
/** Bytes queued in asynchronous OUT transfers that the emulated device has not accepted yet. */
uint64_t mydaq_mock_ao_queued(const mydaq_mock *m);
/** Packet the DMM returns (11 bytes incl. CR LF). */
void mydaq_mock_set_dmm_packet(mydaq_mock *m, const char *packet);
/** 1: dmm.read answers with the device's timeout status (-225202) and no packet. */
void mydaq_mock_set_dmm_timeout(mydaq_mock *m, int timeout);
/** Number of dmm.set_mode commands received. */
unsigned mydaq_mock_dmm_set_modes(const mydaq_mock *m);
/** Levels seen on lines configured as inputs. */
void mydaq_mock_set_dio_inputs(mydaq_mock *m, uint8_t levels);
/** 1: three of the init's property records (the 50-byte blob, 87d9e4ed, 21aeef59) answer -225201, as unit 036528CE does. */
void mydaq_mock_set_reject_properties(mydaq_mock *m, int reject);
/** hwverify (05 06) answers "still starting" (-225270) for the next `count` calls. */
void mydaq_mock_set_hwverify_busy(mydaq_mock *m, unsigned count);
/** The next `count` commands on the AI timing engine (0550/2) answer -201381 (not ready). */
void mydaq_mock_set_ai_not_ready(mydaq_mock *m, unsigned count);
/** Simulates unplugging: every transfer fails with MYDAQ_ERR_NO_DEVICE from now on. */
void mydaq_mock_unplug(mydaq_mock *m);
/** Queues an event frame on the interrupt endpoint (object class, event id, status). */
void mydaq_mock_inject_event(mydaq_mock *m, uint16_t object_class, uint16_t event_id,
                             int32_t status);

int16_t mydaq_mock_ao_code(const mydaq_mock *m, int channel);
uint8_t mydaq_mock_dio_direction(const mydaq_mock *m);
uint8_t mydaq_mock_dio_latch(const mydaq_mock *m);
int mydaq_mock_dmm_mode(const mydaq_mock *m);
unsigned mydaq_mock_command_count(const mydaq_mock *m);
/** 1 while the emulated timing engine streams samples. */
int mydaq_mock_ai_streaming(const mydaq_mock *m);
/** Stream numbers (4..10) the host last configured for AI (in_fifo) and AO (out_timing.m0b). */
unsigned mydaq_mock_ai_stream(const mydaq_mock *m);
unsigned mydaq_mock_ao_stream(const mydaq_mock *m);
/** One-way frames (stream read-position reports) received; in replay mode, how many of them
 *  were not in the script (they depend on the host's read timing); the last reported value. */
unsigned mydaq_mock_oneway_frames(const mydaq_mock *m);
unsigned mydaq_mock_oneway_extra(const mydaq_mock *m);
uint32_t mydaq_mock_last_read_pos(const mydaq_mock *m);
/** AO stream (bulk OUT 0x04): bytes received; replay: bytes beyond the captured stream and the
 *  captured length (a finite replay must match exactly: received == expected, extra == 0). */
uint64_t mydaq_mock_ao_bytes(const mydaq_mock *m);
uint64_t mydaq_mock_ao_extra(const mydaq_mock *m);
uint64_t mydaq_mock_ao_expected(const mydaq_mock *m);
/** Emulator: scans the emulated DACs have output, and the last code per DAC channel (0..3). */
uint64_t mydaq_mock_ao_output_scans(const mydaq_mock *m);
int16_t mydaq_mock_ao_last(const mydaq_mock *m, int dac);
/** Emulator: ctr0 edge count returned by reads; ctr0 running; FREQOUT divider (0 = off). */
void mydaq_mock_set_counter(mydaq_mock *m, uint32_t count);
int mydaq_mock_ctr_started(const mydaq_mock *m);
int mydaq_mock_freqout_divider(const mydaq_mock *m);

#ifdef __cplusplus
}
#endif

#endif
