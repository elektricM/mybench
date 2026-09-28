# NI myDAQ USB protocol (reverse-engineered)

Status: **stage 2 in progress** (2026-09-28). The single source of truth for libmydaq. Facts come
from USB captures of NI's own driver (DAQmx 25.8 in the Windows VM, `captures/`, full payloads
since the 256 KiB pcap patch) and from native replays on macOS (`tools/mydaq_native.py`). The
driver-RE agent's static analysis (`docs/driver-notes.md`, `tools/drv_*.py`) supplies NI's object
and method names, argument types, the DMM packet conversion and the calibration layout; those are
tagged **[B]** and noted where a capture confirms or contradicts them.

Organisation: sections 1–4 are the transport, init and calibration every subsystem needs; sections
5 onwards are one subsystem each. A section marked **READY for libmydaq** contains the exact command
sequence, field layouts and data formats to implement it; the status table below is the index.

Confidence tags:

| Tag | Meaning |
|---|---|
| **[N]** | Verified natively: our own code sent it and the device behaved as expected / values match DAQmx. |
| **[C]** | Seen consistently in captures, including differential captures; not yet exercised natively. |
| **[H]** | Hypothesis: consistent with the data, but not tested by varying the field. |
| **[?]** | Unknown; replay verbatim. |
| **[B]** | From NI binary analysis (not from traffic). |

Byte order: frame headers and command fields are **big-endian**; calibration memory contents
and streamed samples are **little-endian**.

## Implementation status (for libmydaq)

Status: **R** = READY for libmydaq (exact sequences + formats, byte-verified against NI);
**N** = also verified natively from macOS; **C** = decoded from captures, native run pending.

| Subsystem | Section | Status | Evidence |
|---|---|---|---|
| USB setup, framing, replies, slots, one-way frames | 1, 2 | **R, N** | every capture decodes; native sessions |
| Init sequence | 3 | **R, N** | identical in every capture; native replay |
| Calibration memory, AI/AO/audio/DMM formulas | 4, 9.3 | **R, N** | 98k+ AI values, all AO codes, 26 DMM readings bit-exact |
| AI on demand (ai0/ai1, ±10/±2 V, 1–2 ch) | 5 | **R, N** | |
| AI hardware timed, finite + continuous (incl. audio in) | 6 | **R, N** (audio in: **R, C**) | 27/27 sequences byte-identical from the builder; native 2 × 200 kS/s |
| Stream credit / overflow events | 6.4, 6.5 | **R, N** | native credit experiments |
| AO static (ao0/ao1, 1–2 ch) | 7.1 | **R, N** | |
| AO hardware timed, finite + continuous, 1–2 ch, audio out | 7.2 | **R, C** | 10/10 sequences byte-identical; all stream codes match |
| DIO static (masks, directions) | 8 | **R, N** | |
| DMM DC V, AC V, DC A, AC A, Ω 200 Ω / 2 kΩ | 9 | **R** (DC V: **N**) | 26/26 readings bit-exact, overload = NaN |
| DMM Ω 20 kΩ – 20 MΩ | 9.2 | MODE 16–19 **[H]** | capture P1 |
| DMM diode, continuity, HW autorange | 9.5 | open | P2 / P3 |
| Counter: edge count, pulse train (cont/finite), FREQOUT | 10 | decoded **C** (replay-level) | P4 native; field meanings partly [H] |
| Counter: frequency/period/pulse width/encoder | 10.5 | open | P5, P7 |
| Abort / timeouts / reset during task | 2.1 | open | P8 |

Pending experiments, with commands, are listed in section 14.

## 1. USB level — **READY for libmydaq**

- VID:PID `3923:755b` (after NI's one-time firmware update), high speed, 1 configuration,
  1 interface (class ff), 17 endpoints. **[N]**
- macOS leaves the device unconfigured: the host must `SET_CONFIGURATION(1)` and claim
  interface 0. No kernel driver to detach. **[N]**
- Endpoint roles:

| EP | Type | Role |
|---|---|---|
| 0x03 | bulk OUT | command frames (host → device) **[N]** |
| 0x81 | bulk IN | response frames, exactly one per command **[N]** |
| 0x82 | interrupt IN, 64 B | asynchronous event frames (e.g. "acquisition done") **[N]** |
| 0x84 | bulk IN | AI sample stream (stream endpoint 4) **[N]** |
| 0x04 | bulk OUT | AO / audio-out sample stream (stream 4, OUT direction) **[C]** |
| 0x85–0x8a / 0x05–0x0a | bulk | further stream endpoints, unused so far; `op13` lists stream endpoints 4..10 **[H]** |

- Command channel: each call frame on 0x03 gets one reply on 0x81, in order; one-way frames
  (type 0, section 2) get none. No sequence numbers, tokens or session ids: the init is
  byte-for-byte identical across attaches (`idle-a` vs `idle-b`). **[N]**
- Keep one interrupt IN transfer pending on 0x82 while tasks run (events, section 6.5).
- Reading 0x81 with a 16 KiB buffer works (responses are ≤ 512 B so far; the largest is the
  512-byte calibration-memory chunk). **[N]**

## 2. Frame format — **READY for libmydaq**

All command-channel fields are big-endian. Requests go out on bulk OUT 0x03; each type-1 request
gets exactly one reply on bulk IN 0x81, in order (NI pipelines several requests before reading the
replies; that works too).

```
offset size  request                                  reply
0      u16   slot (NI "FTID")                         same slot
2      u16   total frame length (padded to /4)        total
4      u16   total - 4                                total - 4
6      u8    type: 1 = call, 0 = one-way (no reply)   1
7      u8    method (0x08.. for objects)              0
8      u16   object class                             0
10     u16   object instance                          status element (below)
12     ...   arguments, zero padded to a multiple of 4   return values
```

- **Slots** **[N]**: use 0 for the session calls of the init (`00`, `02`, `0d` on class 0 / 0x02fe as
  in section 3), 1 for all object calls, 7 for stream credits (`0002` object). NI keeps exactly
  these; a native driver can too.
- **Type 0** (one-way) **[N]**: only NI's stream read-position report (6.4) uses it; the device
  sends nothing back.
- **Status element** **[N]** (driver-notes 1.1 **[B]**): u16 L; L = 2 → success, no code; L = 6 →
  followed by an i32 status (0 = success, < 0 error, > 0 warning). Then the return values.
- **Serialisation** **[B, N]**: positional, no tags. u8/u16/u32 plain; `u8[]` = u16 byte count
  **including the count itself**, then bytes; `u16[]` = u16 (2n + 2), then n u16; `u32[]` = u32
  (4n + 2), then n u32. Examples: the calibration read returns `01ef` + 493 bytes; a scan list of
  two entries is `0006 2a40 6a41`.
- Replies are padded to /4 but the pad bytes are not always zero; parse positionally.
- The only non-zero status in normal traffic is -225229 from `endpoint[4].flush` (6.1) — NI gets it
  too; ignore it there. Error statuses elsewhere should be reported.
- Frames are at most 4096 bytes (driver-notes 1.2 **[B]**).

`tools/mydaq_proto.py` and `tools/drv_fusion.py` decode frames; `frame()` in
`tools/mydaq_native.py` builds them and rebuilds every captured request byte-exactly.

### 2.1 Aborting a call **[B]** (not yet observed)

NI's transport aborts an outstanding call with a vendor control request (OUT bmRequestType 0x40,
wValue = slot, wIndex = 1), then polls (IN 0xC0, wValue = slot, wIndex = 3, 6 bytes) until idle;
bRequest is probably 0xFC (driver-notes 1.2). Only needed for timeout recovery.

### 2.2 Objects **[B]** (ids from driver-notes 1.4; roles confirmed by captures **[N]**)

| class | instance(s) | NI name | our use |
|---|---|---|---|
| 0x0000 | 0 | session | identify (`00`), limits (`02`) — init only |
| 0x0002 | stream no. (4) | SignalStreaming | stream credit (`08`), read-position report (`09`, one-way) — slot 7 |
| 0x02fc | stream no. (4) | tEndpointManager | configure / start / stop / flush a stream endpoint |
| 0x02fe | 0 | tControl | stream table (`13`), endpoint info (`0d`), open (`0f`) — init |
| 0x0501 | 1 | tRoutable | route.connect (route ids: section 3, 6.1) |
| 0x0506 | 0 | tHardwareVerification | init |
| 0x0507 | 0 | flash data store | calibration memory read (`08`) — never write |
| 0x0512 | 0 | tAIScanningFrontEnd | AI scan list |
| 0x0513 | 2 | tAIImmediateTap | on-demand AI samples |
| 0x0520 | 0/1 (channel) | tAO_DAC | AO DAC (static writes) |
| 0x0530 | 1 | tDigController | DIO port0 |
| 0x0550 | 2 | tInputTiming | AI timing engine |
| 0x0551 | 2 | tInputFIFO | AI FIFO / burst size |
| 0x0560 / 0x0561 | 0 | tOutputTiming / tOutputFIFO | AO timing (section 7, stage 2) |
| 0x0c90 | 0 | tDMMControl | DMM |
| 0x0c40–0x0c44 | | counter objects (nistc3rk) | counter / FREQOUT (stage 2) |

Methods are numbered per class (`08`, `09`, … with class-specific meanings); the full signature table
is `M` in `tools/drv_fusion.py`, which decodes every request and reply in every capture.

## 3. Initialisation — **READY for libmydaq**

NI sends this after every attach and on `Device.Reset()` (capture `reset`: identical except for two
status bytes and a faster `0c 90`). Replaying it verbatim from macOS brings the device into a state
where every operation below works. **[N]** Total about 2 s (1.6 s of it is `op08 0c 90`, which
presumably resets the DMM controller).

```
slot op body                                   reply (after the status element: `0002` = ok, `0006 <i32>` = status)
0  00   00000000                               ok + 16 B identity: 0200a000 1980c12d 1980c12d 00020000
0  02   00000000                               ok + e0000007 00001c00 04000000
1  13   02fe0000 00000000                      status 0 + capability table (112 B, see below)
0  00   00000000                               (again)
1  0f   02fe0000                               ok
0  0d   02fe0001 03000000                      ok + 020000ff (after attach) / 02000000 (after use)
0  0d   02fe0001 81000000                      ok + 02000000 (after attach) / 020000ff (after use)
1  08   0507 0000 <u32 off> <u32 len>          5 reads: off 0,0x1ed,0x3da,0x5c7 (len 0x1ed), 0x7b4 (len 0x4c)
1  08   05060000
1  08   05500002
1  08   05130002
1  08   05600000 01000000
1  0b   05200000 00000000
1  08   0c900000                               (1.6 s after attach, 56 ms after reset)
1  09   05010001 00000032 <50 opaque bytes> 00000032 <zeros>   (116-byte frame)
1  09   05010001 00000006 416a2b93 00000006 00000001
1  09   05010001 00000006 80d0b3e6 00000006 00000000
1  09   05010001 00000006 87d9e4ed 00000006 00000000
1  09   05010001 00000006 99fccdab 00000006 0000001f
1  09   05010001 00000006 21aeef59 00000006 00000000
```

Capability table (`op13` result, after status): `0000001e`, then seven u32 sizes
`00000800 00000800 00000400 00000400 00000400 00000200 00000200`, then `0010`, then u16 list
`0004 0005 0006 0007 0008 0009 000a 0010`, then zeros. Our reading: per-stream buffer sizes and
the stream endpoint numbers 4..10 **[H]**.

The two `op0d 02fe 0001 xx` status bytes flip after the device has been used (`ff/00` after a
fresh attach, `00/ff` after a reset or native session). Meaning unknown; NI and we ignore them **[C]**.

The `05 01` records look like (u32 length 6?, u32 property hash, u32 length 6?, u32 value); the
116-byte record carries a 50-byte opaque blob. Constant across all captures, replayed as is **[?]**.

## 4. Calibration memory (object 0x0507) — **READY for libmydaq**

`slot 1, method 08, object 0507 0000, u32 offset, u32 length` returns status 0 and `u8[]` data (u16 length + 2, then the bytes).
NI reads the whole 2048 bytes in 493-byte chunks. Our dump: `captures/calmem-036CD1C6.bin`
(read natively). NI never writes it; libmydaq must not either. **[N]**

Layout (little-endian):

| offset | content |
|---|---|
| 0x000 | directory: u32 0x5bc (used size), 4 bytes `00 00 00 01`, u32 0x38, u32 4, u32 1, u32 1, then (u32 id, u32 offset) pairs: (0x40, 0x100) (0x42, 0x254) (0x44, 0x598) (0xcb0d, 0x3a8) (0xcb0f, 0x4a0), then u32 0x9375 (checksum?) **[H]** |
| 0x100 | section 0x40: AI/AO calibration (below). 0x254: section 0x42 = identical copy **[C]** |
| 0x3a8 | section 0xcb0d: DMM calibration, 28 (gain, offset) pairs from 0x3ba. 0x4a0: section 0xcb0f = identical copy **[C]** |
| 0x598 | section 0x44: UTF-16 "NI", "NATI" and a few words (DAQmx `UserDefinedCalibrationInfo = NI`) **[H]** |

Each section starts with a timestamp: u32 seconds since 1904-01-01 UTC (LabVIEW epoch) at
section+2 (`0xe6500c79` = 2026-06-11 06:55:53 UTC, identical to DAQmx
`ExternalCalibrationDate = 6/11/2026 8:55:53 AM` local time). The DMM sections are 78 s older. **[N]**

### 4.1 AI/AO section layout — **READY for libmydaq**

Section 0x40 at 0x100 (copy A) and section 0x42 at 0x254 (copy B) are byte-identical on this unit;
use copy A (offsets below), or copy B = A + 0x154. All values float32 little-endian. Layout per
driver-notes 5.2 **[B]**, values and their use verified against DAQmx **[N]**:

| block | offset | content |
|---|---|---|
| AI mode record | 0x112 | u8 order 3 + 4 float32 polynomial c0..c3 = (0, 1, 0, 0): identity |
| AI range records | 0x156 + 8·r | (gain, offset) for r = 0 ±10 V, 1 ±5, 2 ±2 V, 3 ±1, 4 ±0.5, 5 ±0.2, 6 ±0.1; only r 0 and 2 are calibrated (the others hold nominal gains, offset 0) |
| ao0 block | 0x18e | mode record (17 B), then range records at 0x19f + 8·r: r 0 ±10 V, 1 ±5, 2 ±2 V, 3 ±1 |
| ao1 block | 0x1bf | ranges at 0x1d0 + 8·r |
| audio out L block | 0x1f0 | ranges at 0x201 + 8·r (mode record unprogrammed, `ff`) |
| audio out R block | 0x221 | ranges at 0x232 + 8·r |

Values on this unit:

| record | gain | offset |
|---|---|---|
| AI ±10 V (0x156) | 3.2511857e-4 V/code | -4.5495466e-3 V |
| AI ±2 V (0x166) | 6.8190966e-5 V/code | -8.9092960e-4 V |
| ao0 ±10 V (0x19f) / ±2 V (0x1af) | 2943.3953 / 14624.223 codes/V | -167.23958 / -85.792076 codes |
| ao1 ±10 V (0x1d0) / ±2 V (0x1e0) | 2947.0942 / 14642.908 codes/V | -220.39702 / -210.53654 codes |
| audio out L / R ±2 V (0x211 / 0x242) | 16241.287 / 16262.039 codes/V | 0 / 0 |

### 4.2 Formulas **[N]**

- **AI**: `volts = offset + gain · code` (code = signed 16-bit ADC value; evaluate in double from the
  float32 constants). ai0 and ai1 share the table; the scan-list range field (1 = ±10 V, 2 = ±2 V)
  maps to record r = 0 / 2. Reproduces every DAQmx AI value in all captures bit-exactly
  (`tools/verify_cal.py`, `tools/aitiming.py`), including the clipped -32768 → -2.2353725027060136 V.
- **AO**: `code = round-to-nearest(offset + gain · volts)`, clamped to the i16 range (exact .5 ties
  do not occur with these float products, so the tie rule is untested; truncation is wrong:
  -0.5 V → -1639). Reproduces all DAQmx static AO codes.

### 4.3 DMM section

Section 0xcb0d at 0x3a8 (copy 0xcb0f at 0x4a0, identical): 28 float32 LE (gain, offset) pairs from
0x3ba; which slot belongs to which function/range and the formula are in 9.3.

## 5. Analog input, on demand — **READY for libmydaq**

One scan (1 or 2 channels) per call, ~30 ms natively. Captures `ai*-static-*`; natively verified.

```
slot op  object      arguments                                               NI method
1    0e  0513 0002   u32 2                                                   ai_tap.configure?
1    0b  0512 0000                                                           ai_fe.clear
1    0a  0512 0000   u16[] scan list (6.2)                                   ai_fe.set_scanlist
1    0c  0512 0000                                -> u32 n entries           ai_fe.commit
1    09  0501 0001   u32[] {0x3212f413}, u32[] {0}                           route.connect
1    09  0550 0002   u32 3, CONVERT, 0, 0, 0, 0, u16 1,
                     u8 × 25: 00 00 00 00 00 01 1f 00 00 00 01 00 00 01 00 00 00 00 00 00 01 00 00 00 01
                                                                             in_timing.configure
1    08  0551 0002   u32 0, 0, 0, 0, 0, u8 0xff, u8 1, u8 0                   in_fifo.configure (no stream)
1    0a  0550 0002                                                           in_timing.arm
1    0b  0550 0002   u8 0, u8 0                                              in_timing.m0b
1    09  0513 0002   u32 n channels                                          ai_tap.arm?
1    0b  0513 0002   u32 1, u32 0, u32 10000, u16 0xf5, u8 3
                                     -> u16 0, u8[] n × i16 BIG-endian codes   ai_tap.read
1    0d  0550 0002                                -> u8, u8                  in_timing.status
1    0e  0550 0002                                                           in_timing.disarm
1    0d  0512 0000                                -> u32                     ai_fe.release
1    0f  0550 0002                                                           in_timing.unconfigure
```

CONVERT = 1500 (1 channel) / 1250 (2 channels). Scan list and calibration as for timed AI (6.2, 4).
Byte-exact template: `ai_read_code()` in `tools/mydaq_native.py` (one channel; for two channels the
scan list, the 0x0513.09 count and CONVERT change, as in `ai01-r10-static-x2`).

## 6. Analog input, hardware timed (finite and continuous) — **READY for libmydaq**

Sources: 27 captures (`ai*-finite-*`, `ai*-cont*`, `audio[LR]*-finite-*`; 1 S/s … 200 kS/s, 1–2
channels, both ranges, both channel orders, mixed ranges, audio L/R, N = 2 … 5000, continuous
1 k … 200 kS/s). The frame builder `ai_timed_frames()` in `tools/mydaq_native.py` rebuilds **all 27**
of NI's command sequences
byte-for-byte from (channels, ranges, rate, N) (`mydaq_native.py selfcheck`), and all 98,322 DAQmx
values they printed are reproduced bit-exactly from the stream codes (`tools/aitiming.py`). Natively verified:
finite 2 × 200 kS/s, continuous 1 k and 2 × 200 kS/s for 5 s (1,000,237 scans, no overflow),
2 × 100 kS/s for 20 s. Class/method names in brackets are NI's (driver-notes.md 1.4) **[B]**.

### 6.1 Command sequence **[N]**

```
slot op   object      arguments (BE)                                  role
1    0b   0512 0000                                                   ai_fe.clear
1    0a   0512 0000   u16[] scan list (6.2)                           ai_fe.set_scanlist
1    0c   0512 0000                                -> u32 n entries   ai_fe.commit
1    08   02fc 0004   u8 1, u8 0x10                                   endpoint[4].configure
1    09   0501 0001   u32[] {0x3212f413}, u32[] {0}                   route.connect (AI route reset)
1    09   0550 0002   u32 3, u32 CONVERT, u32 4, u32 DIVISOR, u32 N, u32 0,
                      u16 1, 25 × u8 FLAGS (6.3)                      in_timing.configure
1    08   0551 0002   u32 0, u32 0, u32 BURST, u32 0, u32 0, u8 4, u8 0, u8 0   in_fifo.configure
1    09   02fc 0004                                                   endpoint[4].start
7    08   0002 0004   u32 CREDIT                                      stream[4].in_credit
1    0a   0550 0002                                                   in_timing.arm
1    0b   0550 0002   u8 0, u8 0                                      in_timing.m0b
1    0c   0550 0002                                                   in_timing.start
     ... samples on bulk IN 0x84; continuous: credit refresh (6.4) ...
1    0a   02fc 0004                                                   endpoint[4].stop
1    0d   0550 0002                                -> u8, u8 (1, 0)   in_timing.status
1    0e   0550 0002                                                   in_timing.disarm
1    0d   0512 0000                                -> u32 n entries   ai_fe.release
1    0f   0550 0002                                                   in_timing.unconfigure
1    0b   02fc 0004                                -> status -225229  endpoint[4].flush (ignore the error; NI gets it too)
```

The `u8[]`/`u16[]`/`u32[]` encodings are in driver-notes 1.1 (u16 byte count counting itself;
u32[] count = 4n + 2). Note the 0x0501 argument as bytes: `00000006 3212f413 00000006 00000000`.

### 6.2 Parameters **[N]**

| name | rule | evidence |
|---|---|---|
| scan-list entry | `0x2840 \| range << 9 \| ch`, `\| 0x4000` on the last entry; range 1 = ±10 V, 2 = ±2 V; ch 0 = ai0, 1 = ai1, **2 = audio in L, 3 = audio in R** (audio: range 2 only); any order, per-channel range | `ai10-*` → `[2a41, 6a40]`, `ai01-r10r2-*` → `[2a40, 6c41]`, `audioLR-*` → `[2c42, 6c43]` |
| DIVISOR | `floor(100e6 / rate)`; actual rate = 100e6 / DIVISOR (DAQmx prints exactly that) | 7000 → 14285 (7000.35 S/s), 12345 → 8100, 70k → 1428, 1 → 1e8, 200k → 500 |
| CONVERT | `min(1500 if 1 channel else 1250, DIVISOR // n_channels)` (100 MHz ticks between channel conversions: 15 / 12.5 µs, or evenly spread at high rates) | 1 ch: 1500 (≤ 66.6 kS/s), 1250 @80k, 1000 @100k, 500 @200k; 2 ch: 1250, 1000 @50k, 714 @70k, 500 @100k, 250 @200k |
| N | finite: samples per channel; continuous: 0 | |
| FLAGS | finite `00 00 00 00 00 01 00 00 00 00 00 00 00 01 00 00 01 00 00 00 00 00 01 00 00`; continuous `00 00 00 00 00 01 00 00 00 00 00 00 00 01 00 00 00 00 00 00 01 00 00 00 00` — **they differ at byte indices 16, 20 and 22** (driver-notes 3.4's claim that finite and continuous share the flags is wrong); on-demand uses a third set (section 5) | constant over all rates/channels/ranges within each mode |
| BURST | finite: total bytes = 2 · n_ch · N. Continuous: the device sends the data in bursts of this many bytes, each ending the USB transfer (short packet); DAQmx uses its read size (2 · n_ch · samples per read). Choose ≈ 100 ms of data | 1k × 1 ch: 2000 B arrive once per second; 100k × 2: 80000 B = 32768 + 32768 + 14464 with NI's 32 KiB requests |
| CREDIT | finite: total bytes; continuous: 0x7fffffff | all captures |

Channel skew for the API: channel k of a scan is sampled `k · CONVERT / 100 MHz` after channel 0.

### 6.3 Stream format **[N]**

Bulk IN 0x84: little-endian i16 codes, channels interleaved in scan-list order, no header or
counters. Volts per channel with the range's calibration pair (4.1). Read with a request size of
one burst (or the remaining total) **rounded up to a multiple of 512** (the packet size; smaller or
odd sizes make libusb fail with LIBUSB_ERROR_OVERFLOW when the device's last packet does not fit).

### 6.4 Stream credit and read-position reports — **[N]**

- `stream[4].08(u32 bytes)` on slot 7 (type 1, with reply) **grants** the device permission to send
  that many more bytes on the stream. Grants are **additive** (native test `native_credit_test2.py`:
  initial 40000 B plus +2000 B per 2000 B consumed streamed without limit). When the credit is used
  up the device stops sending, its FIFO overflows and it raises event 0002 on 0551 (below).
  Finite tasks: grant exactly the total. Continuous: NI grants 0x7fffffff (2 GiB ≈ 45 min at
  2 × 200 kS/s); a long-running driver must top it up (e.g. +1 GiB whenever less than 1 GiB remains).
- NI additionally sends, each time the application consumed one burst, a **one-way** frame
  (type 0, no reply) `stream[4].09(u32 0x7fffffff − bytes consumed)` on slot 7. It does **not**
  extend the credit (native test: with a 40000 B grant, these reports did not let more data through);
  it is a read-position report. Sending it like NI is harmless; libmydaq may omit it.
- Native throughput note: a single synchronous pyusb reader sustained 2 × 200 kS/s for 5 s once and
  overflowed after ~3 s in another run; 2 × 100 kS/s ran 20 s cleanly. libmydaq should keep several
  asynchronous bulk IN transfers queued (NI queues 32 KiB requests; DAQmx itself overflows at
  2 × 200 kS/s with its default buffering, `ai01-r10-contlong-200k-default`).

### 6.5 Events (interrupt IN 0x82) **[N]**

64-byte frames: `u16 0, u16 0x40, u16 0x3c, u16 event, u16 class, u16 instance, i32 status, …`
(driver-notes 1.3). Seen:

| event | when |
|---|---|
| `0005 0550 0002` status 0 | acquisition finished (finite: after the last sample; continuous: after stop) |
| `0002 0551 0002` then `0007 0550 0002` (then `0005`) | **device FIFO overflow** (host too slow, or stream credit used up): DAQmx reports "onboard device memory overflow" (`ai01-r10-contlong-200k-*`) |
| `0001 0560 0000` status 0 | finite AO finished (7.2) |
| `0009 0c41 0000` status 0 | finite pulse train finished (10.2) |

Keep an interrupt IN transfer pending while a task runs; treat 0551/2 or 0550/7 as Overflow.

## 7. Analog output

### 7.1 Static — **READY for libmydaq**

```
slot op  object       arguments                                  NI method
1    08  0520 <ch>    u8 0, 1, 0, 0, 0, 0                        ao_dac.configure (static)
1    09  0520 0000    u32[] {code as u32}, u16[] {ch}             ao_dac.write
1    0a  0520 <ch>                                               ao_dac.update
```

`code` = i16 from 4.2 (±10 V range record), sent in the low half of the u32 (e.g. -0.5 V on ao0 →
0x0000f999). Note the write goes to instance 0 with the channel in the u16 list; several channels
can be written in one call (both lists longer: `ao01-static-p0.5` writes `u32[] {0x518, 0xf962}`,
`u16[] {0, 1}` after configuring both DACs). Captures `ao*-static-*`; natively verified
(codes identical to DAQmx's; outputs left at 0 V). Nothing is wired, so the analog output voltage
itself is not measured.

### 7.2 Analog output, hardware timed (finite, continuous, 1–2 channels, audio out) — **READY for libmydaq**

Sources: 10 captures (`ao0/ao1/ao01-finite-*`, `ao0/ao01-cont-*-regen/-stream`,
`audiooutLR-finite-48k-*`; 1 k … 200 kS/s). `ao_timed_frames()` in `tools/mydaq_native.py` rebuilds
**all 10** of NI's command sequences byte-for-byte (`mydaq_native.py selfcheck`), and every streamed
DAC code equals `round(offset + gain · volts)` of DAQmx's waveform (`tools/aotiming.py`).
Not yet run natively (`native_stage2_test.py aofinite aocont audioout`) **[C]**.

```
slot type op  object      arguments                                            NI method
1    1    08  0520 <ch>   u8 1, 1, SEL, 0, 0, AUD   (per DAC channel)           ao_dac.configure (timed)
1    1    0a  0560 0000   u32 A, u32 B, u32 4, u32 DIVISOR, u32 C, 22 × u8 FLAGS out_timing.configure
2    1    09  0561 0000   u8[] DAC channel list, u8 2, u8 0     (slot 2!)       out_fifo.configure
1    1    08  02fc 0004   u8 0 (= OUT), u8 0x10                                endpoint[4].configure
1    1    0b  0560 0000   u8 4, u8 0                                            out_timing.m0b
1    1    09  0c92 0000                                                         power.m09
1    1    08  0561 0000   u8 1                                                  out_fifo enable
1    1    09  02fc 0004                                                         endpoint[4].start
7    0    08  0002 0004   u32 CREDIT            (one-way, no reply)             stream[4] output credit
     ... samples → bulk OUT 0x04 (NI: the whole finite buffer / the first buffer here) ...
1    1    0c  0560 0000   u8 1                                                  out_timing.start
     ... continuous: keep writing to 0x04; the device paces the host by NAKing while its FIFO is full
     ... finite: event 0001 on 0560 0000 (status 0) when the last sample has been output
1    1    0d  0560 0000                                                         out_timing stop
1    1    13  0560 0000                        -> u32, u32, u8                   out_timing.status
1    1    18  0560 0000                        (finite only)                    out_timing.m18
1    1    17  0560 0000                        -> u32 (1 finite done, 0 cont)    out_timing.m17
1    1    08  0561 0000   u8 0                                                  out_fifo disable
1    1    0a  02fc 0004                                                         endpoint[4].stop
1    1    0e  0560 0000                                                         out_timing.disarm
1    1    0b  02fc 0004                        -> status -225229 (ignore)        endpoint[4].flush
1    1    0f  0560 0000                                                         out_timing.unconfigure
1    1    0a  0520 <ch>                        (per DAC channel)                ao_dac.update
```

| field | value |
|---|---|
| DAC channel numbers | 0 = ao0, 1 = ao1, 2 = audio out L, 3 = audio out R (instances of 0x0520 and entries of the out_fifo list) |
| SEL, AUD | ao0/ao1: 0, 0; audio out: 5, 1 (static AO uses `0 1 0 0 0 0`, section 7) |
| finite | A = 1, B = N (samples per channel), C = 0, FLAGS `00 01 00 00 00 01 01 00 00 00 01 00 00 00 01 01 00 00 00 00 00 00`, CREDIT = 2 · n_ch · N |
| continuous | A = B = C = 0xfffffffe, FLAGS as finite but byte 11 = 01, CREDIT = 0x7fffffff |
| DIVISOR | `floor(100e6 / rate)`, as AI (48000 → 2083 = 48007.68 S/s) |
| samples | little-endian i16 DAC codes, interleaved in channel-list order; code = round(offset + gain · V) with the channel's calibration record: ao0/ao1 ±10 V (4.1), audio L/R ±2 V record (gain 16241.287 / 16262.039, offset 0) |
| regeneration | **host side**: DAQmx's "regenerate" and "stream" modes send identical commands; with regeneration DAQmx simply re-sends the buffer. The device FIFO holds ≈ 18 KB ahead of the output (`ao0-cont-10k-*`: 60500 B sent in 2.1 s at 20000 B/s) |
| after stop | the outputs keep the last sample (update per channel at the end) |

Underflow (host too slow in continuous mode) was not provoked; driver-notes 1.3 expects an event
on 0x0561. Audio out cannot be written on demand (DAQmx -201087: a sample clock is required), so
audio out is timed-only.

## 8. Digital I/O (port0, 8 lines) — **READY for libmydaq**

```
slot op  object      arguments                       reply              NI method
1    0a  0530 0001   u32 mask, u8 dir (0 in / 1 out)                     dio.set_direction
1    08  0530 0001                                   u32 levels         dio.read
1    09  0530 0001   u32 mask, u32 value                                 dio.write
```

- Read: only bits 0..7 are port0; bits 8..15 read `ff` and are not port data (driver-notes 9 **[B]**).
- DAQmx DI read = set_direction(0xff, 0) + read. DAQmx DO write (glitch-free) = set_direction(0, 0),
  read, write(mask, 0), set_direction(mask, 1), write(mask, value). Lines not in the mask keep
  their direction.
- Natively verified: 0x55 / 0xAA driven and read back, then back to inputs.

## 9. DMM (object 0c90: Cyrustek ES51981 meter chip) — **READY for libmydaq** (V, A, Ω)

Sources: `dmm-*` captures (every DAQmx-reachable function and range, leads open), packet
conversion from driver-notes 8 **[B]** (`tools/drv_dmm.py`), reproduced bit-exactly for every
finite DAQmx reading (26/26); overload readings are NaN on both sides.

### 9.1 Commands **[N]**

```
slot op  object     arguments                     reply
1    09  0c90 0000  u8 MODE, u8 0                 status (returns after ~0.5–1.5 s: the meter settles)
1    0a  0c90 0000  u32 1, u32 10000 (timeout ms)  status, u8[] PACKET (11 bytes)
```

Init (section 3) sends `0c90.08()` once (~1.6 s after attach). One `set_mode` + `read` per reading,
as DAQmx does even when the mode does not change. The DMM is isolated from and independent of AI/AO.

### 9.2 MODE byte per function and range **[N]** (captures) / **[C]** where noted

| function | ranges → MODE |
|---|---|
| DC voltage | 2 V → 0, 20 V → 1, 60 V → 2, 200 mV → 3 |
| AC voltage | 2 V → 4, 20 V → 5, 200 mV → 7 (6 is unused: no 60 V AC range) |
| DC current | 20 mA → 8, 200 mA → 9, 1 A → 10 |
| AC current | 20 mA → 11, 200 mA → 12, 1 A → 13 |
| resistance | 200 Ω → 14, 2 kΩ → 15, 20 kΩ … 20 MΩ → 16 … 19 **[H]** (captures pending) |
| diode, continuity | not reachable through public DAQmx calls (see 9.5) |

DAQmx resistance channels only accept 1 mA "excitation" on every range (it is a DAQmx formality:
the meter chip sources its own test current; DAQmx rejects other values with -200077).

### 9.3 Packet → value **[N]**

`R D1 D2 D3 D4 F S O1 O2 CR LF`, each byte `0x30 + value`:

- reading = `D1 + D2/10 + D3/100 + D4/1000` — for bit-exactness compute it as
  `D2·0.1 + D1 + D3·0.01 + D4·0.001` (the order NI uses, driver-notes 8), then × the multiplier of
  (F, R), then negate if S bit 2 is set;
- **overload** when S bit 0 is set (`…;9…`, `…39…`): value = NaN (DAQmx returns NaN);
- F (function) and R (range digit) → multiplier and unit:

| F | function | R → multiplier | unit |
|---|---|---|---|
| `;` 0x3b | voltage | 0 ×1 (2 V), 1 ×10 (20 V), 2 ×100 (60 V), 4 ×0.1 (200 mV) | V |
| `?` 0x3f | mA | 0 ×0.01 (20 mA), 1 ×0.1 (200 mA) | A |
| `0` 0x30 | A | 0 ×1 (1 A) | A |
| `3` 0x33 | Ω | 0 ×100 (200 Ω), 1 ×1 k, 2 ×10 k, 3 ×100 k, 4 ×1 M, 5 ×10 M | Ω |
| `1` 0x31 | diode | ×1 | V (driver-notes; not captured) |
| `5` 0x35 | continuity | (reading − D4/1000) × 100 | Ω (driver-notes; not captured) |

- O2 = `8` for DC, `4` for AC (the packet does not otherwise say AC/DC; the value's meaning comes
  from the MODE you set). S bit 3 is always set in our captures.
- then calibration: `value = gain · value + offset` with the (gain, offset) float32 LE pair number
  `slot` at 0x3ba + 8·slot (copy at 0x4b2), **not** for diode/continuity:

| function (R) | slot |
|---|---|
| DC V 2 V (0) / 20 V (1) / 60 V (2) / 200 mV (4) | 0 / 1 / 2 / 4 |
| AC V 2 V (0) / 20 V (1) / 200 mV (4) | 5 / 6 / 9 |
| DC mA 20 mA (0) / 200 mA (1); DC 1 A | 10 / 11; 14 |
| AC mA 20 mA (0) / 200 mA (1); AC 1 A | 16 / 17; 20 |
| Ω R = 0 … 5 | 22 … 27 (all (1, 0) on this unit) |

Examples: `10003;804` in AC 20 V mode → 0.03 × 10 = 0.03 V → 1.2727573 × 0.03 + 0.0130675 =
0.05125017192214727 V (DAQmx: same); `020003900` in 200 Ω mode → overload → NaN.

### 9.4 Timing

Each reading takes ~1–1.5 s (set_mode settling + one meter conversion), AC as fast as DC (the meter
chip computes RMS itself).

### 9.5 Open **[?]**

- **Resistance 20 kΩ … 20 MΩ**: MODE 16–19 follow the pattern but their captures were lost (first
  attempt used excitation values DAQmx rejects; re-run pending, item P1 in section 14).
- **Stage-3 result (2026-09-28 22:20, `dmmtry.ps1`, one variant per process)**: every DAQmx route
  to diode / continuity is refused *before any device traffic*: `DAQmxSetChanAttribute(0x3051)` on a
  resistance channel (named or physical channel string) and on a voltage channel → -200233; reading
  0x3051 back → -200233; generic 0x1874/0x1875 excitation on a voltage channel → -200233;
  `DAQmxCreateAIVoltageChanWithExcit` → -200431 (the dmm channel only offers Current, CurrentRMS,
  Resistance, Temp…, Voltage, VoltageRMS); `DAQmxSetAIExcitSrc` on a voltage channel **kills the
  32-bit PowerShell process** (no output after the call). So diode/continuity can only be captured
  from ELVISmx's own DMM (GUI fallback, P3); driver-notes 8 predicts `set_mode(20, 0)` and
  `set_mode(21, 0)`.
- **Diode and continuity**: ELVISmx selects them with device-specific DAQmx channel attributes
  (docs/instruments/algorithms/dmm.md: continuity = resistance channel + attribute 0x3051 = TRUE;
  diode = voltage channel ±2 V + internal excitation 1 mA; hardware autorange = 0x2FF8). Our first
  attempt (`tools/daqmx-scripts/dmmc.ps1`, `DAQmxSetChanAttribute` from 32-bit nicaiu.dll) got
  -200233 "property not valid for this function" for 0x3051, 0x2FF8 and AI.Excit.Src
  (`dmm-dcv-hwauto-x2`, `dmm-ohm-hwauto-x2` show no DMM traffic). A variant script
  (`dmmtry.ps1`: typed setters, named vs physical channel, set before/after range, voltage channel
  with excitation) is ready (P2); fallback: capture the ELVISmx DMM GUI in diode and continuity mode
  (P3). Until then libmydaq cannot offer diode/continuity (driver-notes' packet conversion for
  F = '1'/'5' is ready; only the MODE bytes are missing; hypothesis 20/21 — do not send unverified).

## 10. Counter / timer and FREQOUT — decoded **[C]** (replayable; not yet run natively)

Sources: `ctr-countedges-pfi0-1s`, `ctr-cifreq-pfi1-timeout`, `co-ptrain-1k-d0.5`,
`co-ptrain-10k-d0.25`, `co-finite-100-1k`, `freqout-10k`, `freqout-100k`, `ctrloop-freqout-10k`.
Objects: 0x0c41 = ctr0 (methods 08–1d), 0x0c40 = FREQOUT (08–0b) **[B for the class ids]**.
Method semantics below are from differential captures; argument field names are ours.

### 10.1 Common skeleton (all ctr0 tasks)

```
1 09 0501 0001  route.connect(ids, values)  (task-specific routes, 10.3)
1 08 0c41 0000                               reset
1 09 0c41 0000  9 × u32 U[0..8], 2 × u16 (0, 0), 4 × u8 B[0..3]   configure (see 10.2)
1 0a 0c41 0000  00000000
1 0b 0c41 0000  (16 × u16, task specific, 10.2)
1 0c 0c41 0000  u16 (count edges 0x0001, else 0)
1 0d 0c41 0000  00020000
1 0e 0c41 0000
1 0f 0c41 0000  000000000000000300000000   (finite pulse train: last byte 07)
1 10 0c41 0000  (count edges/freq: zeros; pulse train: 0001 0000 0000 0001 0001 0000 0000 0000)
1 11 0c41 0000  000000010000000000000000
1 12 0c41 0000  000000000000000000000000   (finite pulse train: …0100…)
[pulse train: 1 09 0501 0001 route PFI3 output, 10.3]
1 13 0c41 0000                               arm
1 12 0c41 0000  (as above)
1 14 0c41 0000  u32, u32, u16, 4 × u8          start (see 10.2)
    ... count window / output runs ...
[input tasks: 1 1d 0c41 0000 u32 1, u32 1, u32 10015 (0x271f, timeout ms), u32 1, u16 1, u8 1|3, u8 0
               -> status, u32[] {count}   (read)]
1 15 0c41 0000 ; 1 16 0c41 0000 ; 1 17 0c41 0000     stop / disarm / release
```

Exact bytes of each task are in the captures; `native_stage2_test.py` replays them verbatim.

### 10.2 Task parameters (differential)

| task | 0c41.09 U[0..8] ; B[0..3] | 0c41.14: u32, u32, then u16 + 4 × u8 as bytes | reply / event |
|---|---|---|---|
| count edges (PFI0 or internal) | all 0 ; 0 1 0 1 | 0, 0, `0000 01 01 00 00` | `1d` → u32[] {count} (DAQmx 10567 = 0x2947 for FREQOUT 10 kHz ≈ 1 s) |
| frequency (1 counter, PFI1) | 1, 0, 1, 0 … ; 0 3 1 0 | 1, 0, `0000 03 00 00 00` | `1d` with no signal → status -200474 (timeout) after 10 s |
| pulse train continuous | 2, 0, LOW, HIGH, 0 … ; 1 9 0 1 | LOW, HIGH, `0000 09 01 00 01` | none; runs until `15` |
| pulse train finite N | 2, 2, 0, 2N, 0, 0, T6, T7, T8 ; 0 11 0 1 | 0, 2N, `0000 0b 01 00 00` | event 0009 on 0c41 when done |

LOW/HIGH = low and high time in **100 MHz ticks**, low phase first (idle low): 1 kHz 50 % →
50000/50000; 10 kHz 25 % → 7500/2500 (**[C]**, the 25 % capture fixes the order); 100 Hz →
500000/500000. Frequency = 1e8 / (LOW + HIGH), duty = HIGH / (LOW + HIGH). Finite: T6..T8 were
all 50000 at 1 kHz 50 % (initial delay, low, high **[H]** — needs a finite capture with duty ≠ 50 %,
P7).

### 10.3 Routes (route.connect: `u32[] ids, u32[] values`) **[C]**

| id | value | meaning (from the task that sets it) |
|---|---|---|
| 0x587c6efe (+ 0x37cf14dd = 0) | 0x01 = PFI0, 0x02 = PFI1 (n + 1 for PFIn), 0x1e = 100 MHz timebase, 0x2b = internal FREQOUT | ctr0 source / edge input |
| 0xc7c20613 (+ 0x88fbba44 = 0) | 0x2f | set in every ctr0 task (gate/aux default) |
| 0xc51cc492 (+ 0x8e309935 = 0) | 0 | count-edges only |
| 0xb21bf404 / 0x097d2c54 (+ pairs) | 0 / 0x02 (PFI1) | frequency: gate = PFI1 |
| 0x5c159528 (+ 0x1739c88f = 1) | 0x0d | ctr0 output → PFI3 (pulse train) |
| 0xc271008b (+ 0x895d5d2c = 1) | 0x0f | FREQOUT output → PFI4 |
| 0x5e3c3848 | 0x24 | extra route in the internal FREQOUT → ctr0 loop |

### 10.4 FREQOUT

```
1 09 0501 0001  route.connect([0xc271008b, 0x895d5d2c], [0x0f, 1])   output on PFI4
1 08 0c40 0000  u16 DIV, u8 1, u8 0         configure: f = 100 kHz / DIV (10 kHz → 10, 100 kHz → 1)
1 09 0c40 0000                               start
1 0a 0c40 0000 ; 1 0b 0c40 0000              stop, release
```

DAQmx's error text says the 4-bit FREQOUT divider (1..16) runs from 20 MHz, 10 MHz or 100 kHz; only
the 100 kHz timebase (u8 1) was observed. Other timebases: pending (P6).

### 10.5 Open

PFIn = DIOn (DAQmx refuses a DI task on the lines a counter reserves, `pfidio-co-100hz-port0`;
native test P4); period / pulse-width / two-edge / encoder measurements; frequency with a real
signal (internal FREQOUT loop, P5); the meaning of the remaining 0c41 fields.

## 11. Native validation (`tools/mydaq_native.py`, `tools/native_*test*.py`)

macOS + pyusb only, VM detached (through `tools/devlock.sh native`):

| operation | result |
|---|---|
| init replay (section 3) | all commands accepted in every session (~2 s). Once, when the device was pulled from the VM right after NI had re-bound it, `op08 05 06` answered status -225270; repeating the init worked (the tool retries once) |
| calibration memory | full 2048 bytes read; tables in section 4 |
| AI on demand | ai0/ai1 × ±10/±2 V, ~30 ms per read |
| AI finite | 1 ch 1 kS/s × 100, 2 ch 200 kS/s × 2000, ±2 V: all samples, same done event as NI |
| AI continuous | 1 kS/s 3 s; 2 × 200 kS/s 5 s (1,000,237 scans, no overflow); 2 × 100 kS/s 20 s without credit refreshes; credit semantics (6.4) |
| AO static | ao0/ao1 codes identical to DAQmx's for the same volts; left at 0 V |
| DIO | read 0x00 (open inputs, as DAQmx); DO 0x55/0xAA driven and read back; back to inputs |
| DMM DC V | all 4 ranges; values identical to DAQmx for the same display reading |

Offline, byte-exact: all 27 AI/audio-in and 10 AO/audio-out timed task sequences
(`mydaq_native.py selfcheck`); 98,322 streamed AI values, all AO stream codes, 26 DMM readings and
all on-demand AI values reproduced from raw data + calibration (`aitiming.py`, `aotiming.py`,
`drv_dmm.py`, `verify_cal.py`).

Statistical AI comparison (open, floating inputs): right after an init the floating ai0 node
relaxes for a minute or two (native: -2.11 V at 0 s, -2.63 V at 120 s); DAQmx reads 30–100 s after
its bind-time init. Settled, the two agree: DAQmx -2.6372 V / sd 0.422 mV and -2.6628 V / sd
0.415 mV vs native -2.6342 V / sd 0.438 mV (finite 1 kS/s × 100, ai0 ±10 V).

## 12. Captures (`captures/`, each with a `.txt` of DAQmx output + host timestamps)

The command lists that produced the stage-2 captures are in `tools/lists/s2-*.txt`
(`tools/capture-batch.sh LIST` re-runs them; existing captures are skipped).

| capture(s) | shows |
|---|---|
| `idle-a`, `idle-b`, `reset` | init after attach (identical twice); `Device.Reset()` = init again |
| `ai0-r10-static-x3/-x5`, `ai0-r2-static-x3`, `ai1-r10-static-x3`, `ai1-r2-static-x3`, `ai01-r10-static-x2` | on-demand AI: channel, range, clipping, two channels |
| `ai0-r10-finite-{1,10,7k,10k,12345,80k,100k,200k}-*`, `ai0-r10-finite-1k-n{100,5000}`, `-1k-n100-b` | AI rate → divisor/convert, sample counts |
| `ai01-r10-finite-{1k,10k,50k,70k,100k,200k}-*`, `ai10-*`, `ai01-r10r2-*`, `ai0-r2-finite-*` | 2 channels, order, mixed ranges |
| `ai0-r10-cont-1k-n1000`, `-cont-1k-xfer4k`, `ai01-r10-cont-10k-n5000`, `-cont-100k-n20000` | continuous AI, bursts, credit/read-position reports |
| `ai01-r10-contlong-200k-default`, `-xfer256k` | 2 × 200 kS/s overflow (events 0551/2, 0550/7) |
| `audioLR-finite-48k-n480`, `audioL-finite-48k-n480-ac/-dc` | audio in = scan-list channels 2/3; DC coupling rejected by DAQmx (identical traffic) |
| `audioL/R-static-x3`, `audiooutL/R-static-p0.5` | DAQmx refuses on-demand audio (-201087); no device traffic |
| `ao0-static-*`, `ao1-static-p0.5`, `ao01-static-p0.5` | static AO, one and two channels |
| `ao0/ao1/ao01-finite-*`, `ao0-finite-{10k,200k}-*` | timed AO finite |
| `ao0-cont-1k-n100-regen`, `ao0-cont-10k-n1000-stream`, `ao01-cont-10k-n1000-regen/-stream` | timed AO continuous; regeneration is host-side |
| `audiooutLR-finite-48k-n480-sine` | audio out: DAC channels 2/3, audio calibration |
| `di-port0-read`, `di-line4-7-read`, `do-port0-write-0x55/-0xaa`, `do-line0-3-write-0x5`, `do-line7-write-1`, `do-line0-3-writeread-0xa` | DIO masks, port-relative values, readback |
| `dmm-dcv-*`, `dmm-acv-*`, `dmm-dca-*`, `dmm-aca-*`, `dmm-ohm-r200-x2`, `dmm-ohm-r2k-x2` | DMM MODE bytes, packets, overload |
| `dmm-dcv-hwauto-x2`, `dmm-ohm-hwauto-x2` | failed attribute attempts (DAQmx -200233; no DMM traffic) |
| `ctr-countedges-pfi0-1s`, `ctr-cifreq-pfi1-timeout`, `co-ptrain-1k-d0.5`, `co-ptrain-10k-d0.25`, `co-finite-100-1k`, `freqout-10k/-100k`, `ctrloop-freqout-10k`, `pfidio-co-100hz-port0` | counter / FREQOUT (section 10) |
| `calmem-036CD1C6.bin` | full calibration memory of this unit (device data; test fixture) |
| `capabilities.txt` | every DAQmx device/channel property, terminals, counter default terminals |
| `enum-and-failed-first-transfer.pcap` | historical: unconfigured-device failure before the QEMU patch |

Captures since the stage-2 VM restart hold complete payloads (QEMU `usb-pcap-data-len.patch`,
256 KiB per transfer). Stage-1 captures truncate transfers to 256 bytes (the calibration reads).

## 13. Tools

| tool | use |
|---|---|
| `tools/capture.sh NAME script.ps1 args` | re-attach with pcap, run one DAQmx script, log timestamps |
| `tools/capture-batch.sh LIST` | run a list of captures, taking the device lock per capture |
| `tools/devlock.sh vm/native OWNER -- CMD` | shared-device lock (other agents use the myDAQ too) |
| `tools/vm-restart.sh` | restart the main VM and wait for the myDAQ (hold the lock) |
| `tools/daqmx-scripts/exp.ps1` | one DAQmx operation per run (AI/AO/DIO/DMM/counter/abort ops) |
| `tools/daqmx-scripts/dmmc.ps1`, `dmmtry.ps1` | DMM attribute experiments through the DAQmx C API |
| `tools/daqmx-scripts/capabilities.ps1` | DAQmx capability dump (`captures/capabilities.txt`) |
| `tools/usbpcap.py`, `tools/usbflow.py` | pcap reader; event-ordered view, capture diff, stats |
| `tools/mydaq_proto.py`, `tools/rawseq.py`, `tools/drv_fusion.py` | frame decoders (drv_fusion: typed, NI names) |
| `tools/aitiming.py`, `tools/aotiming.py`, `tools/streams.py` | timed AI/AO parameters and stream checks vs DAQmx values |
| `tools/aical.py`, `tools/verify_cal.py`, `tools/drv_dmm.py` | calibration / DMM conversion checks |
| `tools/mydaq_native.py` | native prototype: `info ai aitimed aifinite ao di do dmm all selfcheck`; builders `ai_timed_frames`, `ao_timed_frames` |
| `tools/native_stream_test.py`, `native_credit_test*.py`, `native_stage2_test.py` | native experiments (run through devlock) |

## 14. Pending experiments (device needed), in priority order

All through the lock; captures with `tools/devlock.sh vm usb -- tools/capture.sh NAME tools/daqmx-scripts/SCRIPT args`
(or put the lines in a list and run `tools/capture-batch.sh`), native tests with
`tools/devlock.sh native usb -- /usr/bin/python3 tools/…`.

| # | goal | command |
|---|---|---|
| P1 | DMM Ω MODE 16–19 (20 k … 20 MΩ) | `tools/capture-batch.sh tools/lists/s3-pending.txt` runs P1, P5–P8 in one go (~15 captures, ~40 min); P1 = the four `dmm-ohm-r20k/r200k/r2M/r20M-x2` lines |
| P2 | find a DAQmx route to diode / continuity / HW autorange | in the VM (no capture): `devlock.sh vm usb -- mydaq-vm ssh 'C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe -ExecutionPolicy Bypass -File C:\setup\dmmtry.ps1'`; for the variant that reads OK, capture it as `dmm-diode-x2` / `dmm-cont-x2` with `dmmc.ps1` adjusted to that variant |
| P3 | fallback for P2: capture ELVISmx's own DMM in diode and continuity mode | capture.sh with a script that starts `C:\setup\ui.ps1 -Exe DMM.exe`, selects Diode (then Continuity) via UI automation and waits 10 s; name `dmm-elvis-diode`, `dmm-elvis-cont` |
| P4 | native go for stage 2: timed AO, audio, DMM all modes, counter loop, PFI0 = DIO0 | `tools/devlock.sh native usb -- /usr/bin/python3 tools/native_stage2_test.py` |
| P5 | frequency measurement with a real signal (internal FREQOUT → ctr0) | `ctr-freq-freqout-10k`: `exp.ps1 -Op cimeas -Func freq -Rate 10000` (in s3-pending) |
| P6 | FREQOUT timebases (20 MHz, 10 MHz) and divider range | `freqout-2M` / `freqout-1M25` / `freqout-6250`: `-Op freqout -Rate 2000000 / 1250000 / 6250` (in s3-pending) |
| P7 | counter measurement types; finite pulse-train field order | `-Op cimeas -Func period / pw / twoedge -Rate 10000`, `-Op cofinite -Rate 1000 -N 100 -Duty 0.25` (in s3-pending); encoder later |
| P8 | abort / timeout paths (vendor control request 0xFC?) | `selftest`, `ai0-finite-timeout`, `dmm-dcv-timeout`, `ai0-cont-resetduring` (in s3-pending) |
| P9 | AO underflow and AI overflow events natively | native: stop writing during `ao_timed(finite=False)`; stop reading during `ai_timed(n=0)`; record the 0x82 events |
| P10 | minimal init (which of the 23 init calls are needed) | native: drop one group at a time and run `mydaq_native.py all --no-init` variants — only with observed commands, no new opcodes |

Added for stage 3 (coordinator, acceptance audit — every Counter/Timer panel mode needs a capture):
pulse width, semi-period, two-edge separation, linear and angular encoders; count edges falling
edge, count down, external direction (PFI2), initial count, count reset (PFI1); count-edge sources
20 MHz / 100 kHz timebase; buffered (sample-clocked) counter input; idle-high pulse train, initial
delay, live pulse-train update (frequency/duty change while running); AO ±2 V range on ao0/ao1;
AI + AO concurrently (both on stream 4); abort/timeout. Signals come from the internal FREQOUT loop
(`/myDAQ1/FrequencyOutput` as counter terminal) or DIO lines; tests needing a jumper (AO0→AI0,
DIO4→DIO1) are listed as such.

Not to be done: power-supply overcurrent tests (never provoke supply faults), any flash write
(0x0507 `write`), unobserved MODE/route values.
