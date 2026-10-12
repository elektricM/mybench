# NI myDAQ USB protocol (reverse-engineered)

Status: **stage 3 done** (2026-09-29; open items: section 14). The single source of truth for libmydaq. Facts come
from USB captures of NI's own driver (DAQmx 25.8 in the Windows VM, `captures/`, full payloads
since the 256 KiB pcap patch) and from native replays on macOS (internal Python prototype, not published). The
static analysis of the driver (`docs/driver-notes.md`) supplies NI's object
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
| AI hardware timed, finite + continuous (incl. audio in) | 6 | **R, N** (audio in: native via libmydaq, 48 kS/s) | 27/27 sequences byte-identical from the builder; native 2 × 200 kS/s |
| Stream credit / overflow events | 6.4, 6.5 | **R, N** | native credit experiments |
| AO static (ao0/ao1, 1–2 ch) | 7.1 | **R, N** | |
| AO hardware timed, finite + continuous, 1–2 ch, audio out | 7.2 | **R, N** | 10/10 sequences byte-identical; native: libmydaq hwtest (finite 2 × 20 k, regen/stream up to 2 × 200 kS/s, audio 48 k) and an internal AI + AO test |
| **AI + AO concurrently** (second timed task on stream 5) | 7.3 | **R, N** | 4 DAQmx captures (either order, finite AI inside AO, 2 ch); native both orders; negative control reproduces the stream-4 clash |
| AO underflow event (0561 event 2, then 1) | 7.3 | **R** (captured) | `aiao-ao01stream-ai01cont-10k` |
| DIO static (masks, directions) | 8 | **R, N** | |
| DMM DC V, AC V, DC A, AC A, Ω 200 Ω / 2 kΩ | 9 | **R, N** (every mode read natively, internal test) | 26/26 readings bit-exact, overload = NaN |
| DMM Ω 20 kΩ – 20 MΩ | 9.2, 9.5 | **R** MODE 16–19 (16: **N**; 17–19: **C**, statuses replayed natively) | open-lead captures; finite-value scaling of these ranges **BLOCKED** (real resistors) |
| DMM diode, continuity | 9.2, 9.5 | **R, N** — MODE 20 / 21 | ELVISmx GUI captures `dmm-elvis-diode/-cont`; native open-lead packets identical |
| DMM hardware autorange | 9.5 | **BLOCKED** (needs the ELVISmx DMM GUI operated by hand in Auto) | `dmm-elvis-dcv-auto` has no DMM traffic |
| Counter: edge count, pulse train (cont/finite), FREQOUT | 10 | **R, N** | captures; libmydaq; native replays |
| Counter: frequency, period, two-edge separation (FREQOUT loop or PFI) | 10.5 | **R, N** | `ctr-freq/-period/-twoedge-freqout-10k`: native reads 10000 / 10000 / 5000 = DAQmx |
| Counter: pulse width, pulse time, encoders (lin/ang, X1/X2/X4/2-pulse, Z), count edges falling / initial count / reset on PFI1, sources 20 MHz / 100 kHz | 10.5 | **R, N** (timebase sources and reset: **R, C**) | an internal replay tool reads match DAQmx |
| Pulse train idle high, initial delay (cont + finite), finite duty order, **live update** (`m1e` + `m18`) | 10.5 | **R, C** (live update replayed natively: statuses 0) | 4 captures |
| Counter: semi-period, buffered counter input, count down / external direction | 10.5 | **not supported by DAQmx on the myDAQ**; emulate (10.5); ext. direction sense **BLOCKED** (jumper) | DAQmx -200431 / programmed I/O only / -200077 |
| FREQOUT timebases 20 MHz / 10 MHz / 100 kHz, divider 1–16 | 10.4 | **R, N** | 7 captures; internal check: all within 0.01 % |
| AO ±2 V on ao0/ao1 (static, timed) | 7.1, 7.2 | **R** (static: **N**; timed: builder byte-exact, `ao_range=2`) | 3 captures; codes = ±2 V record exactly |
| Timeouts (AI read, DMM read), reset during a task | 2.1 | **R** (DMM timeout: **N**) | 3 captures; no vendor abort request anywhere |
| PFI0 = DIO0 | 10.6 | **N** | ctr0 counted 25 DIO0 toggles on PFI0 |

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

Internal scripts (not published) decode frames and rebuild every captured request byte-exactly.

### 2.1 Timeouts, aborts, reset during a task — **READY for libmydaq** **[C]**

Captures `ai0-finite-timeout`, `dmm-dcv-timeout`, `ai0-cont-resetduring` (s3-final). In none of
them does DAQmx send a vendor control request: the only control transfers are the enumeration
ones every capture has. NI's abort mechanism (driver-notes 1.2: OUT 0x40 bRequest 0xFC?, wValue =
slot, wIndex = 1) is **never needed** in these paths; libmydaq must not implement it blind.

- **AI read timeout** (finite 100 samples at 10 S/s, read with a 1 s timeout): the host just gives
  up and tears the task down with the normal 6.1 stop sequence (`endpoint.stop`, `in_timing.status`,
  `disarm`, `ai_fe.release`, `unconfigure`, `endpoint.flush` → -225229). Only differences from a
  completed task: event 5 on in_timing arrives *after* `endpoint.stop`, and `status` returns
  (1, 0) instead of (0, 1). libmydaq: on a host-side timeout, run the normal stop.
- **DMM read timeout**: the timeout travels **in the request**, `dmm.read(1, TIMEOUT_MS)` (DAQmx
  200 ms → `00 00 00 01 00 00 00 c8`); the device itself answers after ≈ 200 ms with status
  **-225202** (`ff fc 90 4e`) and an 11-byte all-zero packet (length field 0x000d). `set_mode` is
  not affected (it still took 1.55 s). libmydaq: pass the caller's timeout in the read, map
  -225202 to a timeout error, send nothing else.
- **Device reset while continuous AI runs**: DAQmx first stops the task — `session.identify`,
  `ai_fe.release`, `in_timing.status` (event 5 arrives), `disarm`, `unconfigure`,
  `endpoint[4].stop`, `endpoint[4].flush` (-225229) — then sends the **complete section-3 init**
  (identify, limits, stream table, open, calibration reads, hwverify, resets, `dmm.init`, the
  init routes). The 14.1 prediction ("init without stopping first") is refuted. libmydaq: reset =
  stop every running task, then re-run init.

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
is kept in an internal decoder that decodes every request and reply in every capture.

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
  (checked with internal scripts), including the clipped -32768 → -2.2353725027060136 V.
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
Byte-exact template: the internal prototype's `ai_read_code()` (one channel; for two channels the
scan list, the 0x0513.09 count and CONVERT change, as in `ai01-r10-static-x2`).

## 6. Analog input, hardware timed (finite and continuous) — **READY for libmydaq**

Sources: 27 captures (`ai*-finite-*`, `ai*-cont*`, `audio[LR]*-finite-*`; 1 S/s … 200 kS/s, 1–2
channels, both ranges, both channel orders, mixed ranges, audio L/R, N = 2 … 5000, continuous
1 k … 200 kS/s). The internal frame builder `ai_timed_frames()` rebuilds **all 27**
of NI's command sequences
byte-for-byte from (channels, ranges, rate, N) (internal self-check), and all 98,322 DAQmx
values they printed are reproduced bit-exactly from the stream codes (internal check). Natively verified:
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
  that many more bytes on the stream. Grants are **additive** (internal native test:
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
`u16[] {0, 1}` after configuring both DACs).

**±2 V range** (`ao0-r2-static-p0.5`, `ao1-r2-static-m0.5`) **[C]**, replayed natively **[N]**:
`ao_dac.configure` becomes `u8 0, 1, 5, 0, 0, 1` (the SEL = 5, AUD = 1 pair audio out uses) and
the code comes from the channel's ±2 V record (4.1: ao0 0x1af, ao1 0x1e0): +0.5 V on ao0 → 7226
(0x1c3a), −0.5 V on ao1 → −7532 (0xe294), both = round(offset + gain · V) exactly. Nothing else
changes (`write`, `update` identical). Frame: `00 01 00 14 00 10 01 08 05 20 00 CH 00 01 05 00 00 01 00 00`. Captures `ao*-static-*`; natively verified
(codes identical to DAQmx's; outputs left at 0 V). Nothing is wired, so the analog output voltage
itself is not measured.

### 7.2 Analog output, hardware timed (finite, continuous, 1–2 channels, audio out) — **READY for libmydaq**

Sources: 10 captures (`ao0/ao1/ao01-finite-*`, `ao0/ao01-cont-*-regen/-stream`,
`audiooutLR-finite-48k-*`; 1 k … 200 kS/s). the internal `ao_timed_frames()` rebuilds
**all 11** of NI's command sequences byte-for-byte (self-check, incl. ±2 V), and
every streamed DAC code equals `round(offset + gain · volts)` of DAQmx's waveform (internal check).
Run natively **[N]** (2026-09-29, plus libmydaq's
hw tests): finite ao0, ao0+ao1 and audio L+R all end with event `0001` on 0560.

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
| SEL, AUD | ao0/ao1 ±10 V: 0, 0; **ao0/ao1 ±2 V: 5, 1** (`ao0-r2-finite-1k-n100-ramp`: the only frame that differs from `ao0-finite-1k-n100-ramp`; stream codes = ±2 V record, 100/100 exact) **[C]**; audio out: 5, 1 (static AO uses `0 1 0 0 0 0`, ±2 V `0 1 5 0 0 1`, section 7.1) |
| finite | A = 1, B = N (samples per channel), C = 0, FLAGS `00 01 00 00 00 01 01 00 00 00 01 00 00 00 01 01 00 00 00 00 00 00`, CREDIT = 2 · n_ch · N |
| continuous | A = B = C = 0xfffffffe, FLAGS as finite but byte 11 = 01, CREDIT = 0x7fffffff |
| DIVISOR | `floor(100e6 / rate)`, as AI (48000 → 2083 = 48007.68 S/s) |
| samples | little-endian i16 DAC codes, interleaved in channel-list order; code = round(offset + gain · V) with the channel's calibration record: ao0/ao1 ±10 V (4.1), audio L/R ±2 V record (gain 16241.287 / 16262.039, offset 0) |
| regeneration | **host side**: DAQmx's "regenerate" and "stream" modes send identical commands; with regeneration DAQmx simply re-sends the buffer. The device FIFO holds ≈ 18 KB ahead of the output (`ao0-cont-10k-*`: 60500 B sent in 2.1 s at 20000 B/s) |
| after stop | the outputs keep the last sample (update per channel at the end) |

Underflow (host too slow in continuous mode): event 0002 then 0001 on 0561, see 7.3. Audio out cannot be written on demand (DAQmx -201087: a sample clock is required), so
audio out is timed-only.

### 7.3 AI and AO at the same time (stream allocation) — **READY for libmydaq** **[N]**

Sources: `aiao-ao0cont-ai0cont-aofirst` (AO continuous running, AI continuous started and stopped
inside it), `aiao-ai0cont-ao0cont-aifirst` (AI continuous running, AO continuous started and stopped
inside it), `aiao-ao0cont-ai0finite`, `aiao-ao01stream-ai01cont-10k` (AI + AO capture batch). DAQmx ran all of them without error.

**Streams are allocated per task.** The first hardware-timed task (AI or AO) takes stream 4; a task
started while stream 4 is busy takes **stream 5** (the capability table in section 3 lists streams
4..10; 4 and 5 both have 0x800-sized buffers). The stream number S appears in exactly these places;
everything else is byte-identical to the single-task sequences of 6.1 / 7.2:

| where | single task | second task |
|---|---|---|
| `endpoint[S]` (0x02fc instance S): configure (u8 1 = IN / 0 = OUT, u8 0x10), start, stop, flush | 4 | 5 |
| `stream[S]` (0x0002 instance S, slot 7): credit `08`, AI read-position report `09` | 4 | 5 |
| AI `in_fifo.configure(0, 0, BURST, 0, 0, u8 S, 0, 0)` | 4 | 5 |
| AO `out_timing.m0b(u8 S, u8 0)` | 4 | 5 |
| data: AI bulk IN `0x80 \| S`, AO bulk OUT `S` | 0x84 / 0x04 | 0x85 / 0x05 |

Observed pairs: AO on 4 + AI on 5 (AO first) and AI on 4 + AO on 5 (AI first). Stopping one task
leaves the other running; each teardown touches only its own `endpoint[S]` (DAQmx deferred the
second task's `endpoint[5].flush` until the other task's teardown in one capture — ordering only).
The AI front end (0x0512/0x0550/0x0551 instance 2) and the AO objects (0x0520/0x0560/0x0561) are
separate, so nothing else is shared. **Byte-exact**: the internal self-check rebuilds both tasks of all 4 captures from their
parameters with `stream=` 4/5 and finds every frame, in order, in NI's sequence.

**Native** (internal test, builders `ai_timed_frames(..., stream=)` /
`ao_timed_frames(..., stream=)`): AO continuous on 4 + AI continuous on 5 started inside it (AI 1843
scans in 2 s, AO kept streaming, no error) and AI on 4 + AO on 5 started inside it (AI 3928 scans in
4 s, AO 110 buffers) both run cleanly. Negative control `same4` (AI configured on stream 4 while AO
streams on 4 — what libmydaq did): the AI works but the AO's bulk OUT 0x04 fails with
LIBUSB_ERROR_IO — exactly libmydaq's symptom.

**AO underflow** (bonus from `aiao-ao01stream-ai01cont-10k`, stream mode, the script stopped writing
while it read AI): ≈ 0.49 s after the last 1000-byte write at 40 kB/s (≈ 19.5 KB of device FIFO)
the device raised event `0002` on `0561 0000` (out_fifo) immediately followed by `0001` on
`0561 0000`, both status 0; DAQmx then reported "generation has stopped to prevent the
regeneration of old samples" (-200290) and ran its normal AO teardown. libmydaq: treat 0561/2 as
Underflow (P9).

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
conversion from driver-notes 8 **[B]** (internal check), reproduced bit-exactly for every
finite DAQmx reading (26/26); overload readings are NaN on both sides.

### 9.1 Commands **[N]**

```
slot op  object     arguments                     reply
1    09  0c90 0000  u8 MODE, u8 0                 status (returns after ~0.5–1.5 s: the meter settles)
1    0a  0c90 0000  u32 1, u32 TIMEOUT_MS          status, u8[] PACKET (11 bytes); -225202 on timeout (2.1)
```

Init (section 3) sends `0c90.08()` once (~1.6 s after attach). DAQmx sends `set_mode` + `read` per
reading; ELVISmx sends `set_mode` once and then loops `read` (faster, see 9.4) **[N]**. The DMM is isolated from and independent of AI/AO.

### 9.2 MODE byte per function and range **[N]** (captures) / **[C]** where noted

| function | ranges → MODE |
|---|---|
| DC voltage | 2 V → 0, 20 V → 1, 60 V → 2, 200 mV → 3 |
| AC voltage | 2 V → 4, 20 V → 5, 200 mV → 7 (6 is unused: no 60 V AC range) |
| DC current | 20 mA → 8, 200 mA → 9, 1 A → 10 |
| AC current | 20 mA → 11, 200 mA → 12, 1 A → 13 |
| resistance | 200 Ω → 14, 2 kΩ → 15, 20 kΩ → 16 (`dmm-ohm-r20k-x2`), 200 kΩ … 20 MΩ → 17 … 19 (see 9.5 for the evidence) |
| diode | **20** (one range; `dmm-elvis-diode`, ELVISmx GUI) **[N]** |
| continuity | **21** (one range; `dmm-elvis-cont`, ELVISmx GUI) **[N]** |

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
| `1` 0x31 | diode | ×1 (R = 0) | V (`016181800` = 1.618 V, a red LED; open = `020001900` overload) **[C]** |
| `5` 0x35 | continuity | (reading − D4/1000) × 100 (R = 0) | Ω (`014945800` → 149 Ω for a 150 Ω resistor; wire `0000[0-4]5800` → 0 Ω; open = `020005900`) **[C]** |

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

The meter chip streams packets by itself; `read` blocks until the **next** packet. Measured
natively after one `set_mode` (2026-09-29, `scratchpad` cadence check, 11 intervals each):
**0.50 s** per packet for V, A and Ω (modes 0, 4, 14), **0.094 s** for diode and continuity
(modes 20, 21; ELVISmx's GUI reads at the same 95 ms median, `dmm-elvis-diode/-cont`).
So a meter UI sends `set_mode` **once** per function/range change and then loops `read` — the way
ELVISmx does — getting 2 or ~10.6 readings/s. DAQmx instead sends `set_mode` + `read` per reading
(~1–1.5 s each, as in the `dmm-*` captures). AC is as fast as DC (the chip computes RMS itself).
The first packets after `set_mode` may still be settling (ELVISmx shows them anyway) **[H]**.

### 9.5 Diode, continuity, Ω 20 k–20 M, hardware autorange

- **Ω 20 kΩ … 20 MΩ = MODE 16 … 19** **[C]** (`dmm-ohm-r20k/-r200k/-r2M/-r20M-x2`, DAQmx, open
  leads): `set_mode(0x10…0x13, 0)`, replies `R20003900` with range digit R = 2, 3, 4, 5 and F = '3'
  (overload → NaN, as DAQmx). The multiplier per R (×10 k, ×100 k, ×1 M, ×10 M) and the
  calibration slots 24–27 (all (1, 0) on this unit) are **[B]** from driver-notes 8: no finite
  reading on these ranges has been captured yet (needs real resistors).
- **Diode = MODE 20, continuity = MODE 21** **[N]**: ELVISmx's DMM GUI sent exactly one
  `set_mode(0x14, 0)` / `set_mode(0x15, 0)` when the function was selected, then `read(1, 10000)`
  every ~95 ms (`dmm-elvis-diode`, `dmm-elvis-cont`). Native replay (2026-09-29 03:10, open leads):
  both accepted, replies `020001900` / `020005900`, byte-identical to ELVISmx's open-lead packets.
  Values in 9.3 (1.618 V for a red LED, 149 Ω for his 150 Ω resistor). No calibration.
  DAQmx itself refuses every route to these functions (-200233 / -200431), so these
  come only from the ELVISmx captures.
- **Hardware autorange: BLOCKED** (needs the ELVISmx DMM GUI in Auto): `dmm-elvis-dcv-auto` holds only the init's
  `0c90.08`, no `set_mode` or `read` at all, and the ssh session into the VM closed at t+65 s
  ("Connection … closed by remote host") — probably the VM going down then, which is also why it
  was not running when the 03:00 device window started. Prediction (driver-notes 8):
  `set_mode(m, 1)`. Do not send the second byte ≠ 0 until captured; MYBENCH autoranges in software.

## 10. Counter / timer and FREQOUT — **READY for libmydaq** (native **[N]** where marked)

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

Exact bytes of each task are in the captures; the internal native test replays them verbatim.

### 10.2 Task parameters (differential)

| task | 0c41.09 U[0..8] ; B[0..3] | 0c41.14: u32, u32, then u16 + 4 × u8 as bytes | reply / event |
|---|---|---|---|
| count edges (PFI0 or internal) | all 0 ; 0 1 0 1 | 0, 0, `0000 01 01 00 00` | `1d` → u32[] {count} (DAQmx 10567 = 0x2947 for FREQOUT 10 kHz ≈ 1 s) |
| frequency (1 counter, PFI1) | 1, 0, 1, 0 … ; 0 3 1 0 | 1, 0, `0000 03 00 00 00` | `1d` with no signal → status -200474 (timeout) after 10 s |
| pulse train continuous | 2, 0, LOW, HIGH, 0 … ; 1 9 0 1 | LOW, HIGH, `0000 09 01 00 01` | none; runs until `15` |
| pulse train finite N | 2, 2, 0, 2N, 0, 0, T6, T7, T8 ; 0 11 0 1 | 0, 2N, `0000 0b 01 00 00` | event 0009 on 0c41 when done |

LOW/HIGH = low and high time in **100 MHz ticks**, low phase first (idle low): 1 kHz 50 % →
50000/50000; 10 kHz 25 % → 7500/2500 (**[C]**, the 25 % capture fixes the order); 100 Hz →
500000/500000. Frequency = 1e8 / (LOW + HIGH), duty = HIGH / (LOW + HIGH). Finite: T6..T8 =
high, low, high and U0 = initial delay (settled in 10.5 by `co-finite-10-1k-d0.25-delay5ms`).

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

**Timebase and divider** **[C]** (`freqout-10k/-100k/-2M/-1M25/-6250/-1M/-4M`, s3-final):
`configure(u16 DIV, u8 A, u8 B)`, f = timebase / n, n = 1..16, **DIV = n mod 16** (n = 16 is sent
as 0):

| (A, B) | timebase | examples (DIV) |
|---|---|---|
| (1, 0) | 100 kHz | 100 kHz (1), 10 kHz (10), 6250 Hz (0 = 16) |
| (0, 0) | 20 MHz | 2 MHz (10), 1.25 MHz (0 = 16), 4 MHz (5) |
| (0, 1) | 10 MHz | 1 MHz (10) |

DAQmx picks 20 MHz whenever 20 MHz / n hits the frequency (2 MHz = 20/10, not 10/5), then 10 MHz,
then 100 kHz; other frequencies are refused (DAQmx rejects 1 kHz, naming the three timebases). libmydaq: same
preference order. Native: an internal check counts the output with ctr0 on the
internal loop.

### 10.5 Stage-3 counter modes — decoded **[C]**, replayed natively where marked **[N]**

Sources: the stage-3 counter capture batch, all on
the internal FREQOUT loop or with open inputs; an internal replay tool replays a
capture's requests verbatim from macOS and prints the reads. Names below use the internal decoder's
method names (`configure` = 09, `m0b`…`m12`, `commit` = 13, `arm` = 14, `read` = 1d). Every task
is the 10.1 skeleton; `configure` = 9 × u32 **U[0..8]**, 2 × u16 (0, 0), 4 × u8 **B[0..3]**.

**Routes and polarity.** Each input route is a pair (select id, polarity id); polarity 0 = rising /
active high, 1 = falling / inverted (`ctrloop-falling-10k` changed only 0x37cf14dd 0 → 1;
`ctr-pw-falling-freqout-10k` sets 0x11f9be17 = 1). The PFI lines a task reads are first set to
input (their select/enable pair = 0/0: PFI0 0xc51cc492/0x8e309935, PFI1 0xb21bf404/0xf937a9a3,
PFI2 0x2b12a5be/0x603ef819).

| input | select id / polarity id | values |
|---|---|---|
| ctr0 source (edges counted / timebase) | 0x587c6efe / 0x37cf14dd | PFI n = n + 1, **20 MHz = 0x1c**, **100 kHz = 0x12**, 100 MHz = 0x1e, FREQOUT = 0x2b |
| ctr0 gate (measured signal, count reset) | 0x097d2c54 / 0x11f9be17 | PFI1 = 2, FREQOUT = 0x29 (with 0x5e3c3848 = 0x24; pulse width also sets 0x77c75630/0x5d392cd4 = 0/0) |
| ctr0 aux | 0xc7c20613 / 0x88fbba44 | 0x2f (none) in every task seen |
| encoder A / B / Z | 0x3da647a3 / 0xa4af1619 / 0xb7c38e4f | A: PFI0 = 1, FREQOUT = 0x2e (+ 0x5e3c3848 = 0x24); B: PFI2 = 3; Z: PFI1 = 2 (only with Z index on) |

**Input modes** (app code = B[1] = `arm` u8 #1 = last byte of `read`; `m0c` = 1, except 2 for encoders):

| mode | U[0..2] | B | m0b (non-zero entries) | other | read → | evidence |
|---|---|---|---|---|---|---|
| count edges | U0 = **initial count** | 0,1,0,1 | [7]=2, [12]=1 | count reset on PFI1: gate route = 2, m0b [3]=1, [7]=3, [9]=2 | u32 count | `ctrloop-*` **[N]** (falling: 12017 native vs 11941 DAQmx edges in ~1.1 s) |
| pulse width (app 4) | 0 | 0,4,0,1 | [3]=1, [7]=2, [9]=1, [12]=1 | m10 [2]=1, [4]=1; gate polarity = which level is measured | u32 ticks of 100 MHz | `ctr-pw-falling-freqout-10k` **[N]** (5000 = 50 µs) |
| pulse time (app 0x0f) | 1,0,1 | 0,0x0f,1,1 | [3]=1, [7]=3, [8]=1, [9]=3, [12]=1 | `read(1, 2, …)` asks for 2 values | u32 {high, low} ticks | `ctr-pulsetime-freqout-10k` **[N]** (5000, 5000) |
| linear / angular encoder (app 7 / 0x0e) | 1,0,1 | 0,app,2,1 | [7]=3, [12]=1 | **m0c = 2**; **m0f = (0,0,0,3, Z enable, DECODING)**, DECODING X1 = 1, X2 = 2, X4 = 3, two-pulse = 4 | u32 count (signed position) | `ctr-linenc-x4/-x1-z`, `ctr-angenc-x2`, `ctr-angenc-2pulse-freqout-10k` **[N]** (11518 native vs 11443 DAQmx counts) |

| frequency (app 3) | 1,0,1 | 0,3,1,0 | [3]=1, [7]=3, [9]=2, [12]=1 | m10 [2]=1, [4]=1; `arm(1,0,0, 3,0,0,0)` | u32 **period** ticks of 100 MHz; f = 1e8 / ticks | `ctr-freq-freqout-10k` (10000 → 10 kHz = DAQmx) |
| period (app 2) | as frequency | 0,2,1,0 | as frequency | `arm(1,0,0, 2,0,0,0)` | u32 period ticks | `ctr-period-freqout-10k` (10000 → 100 µs = DAQmx) |
| two-edge separation (app 6) | 0 | 0,6,1,1 | [3]=1, [7]=2, [9]=1, [12]=1, **[14]=1** | m10 [4]=1; **first edge = route 0x77c75630 / polarity 0x5d392cd4**, second edge = gate 0x097d2c54 / 0x11f9be17; `arm(0,0,0, 6,1,0,0)` | u32 ticks first → second edge | `ctr-twoedge-freqout-10k` (rising → falling on FREQOUT 10 kHz: 5000 = 50 µs = DAQmx) |

Frequency / period / two-edge on the FREQOUT loop (`-Op cimeas`, nothing wired): the gate (measured
signal) is FREQOUT = 0x29 plus 0x5e3c3848 = 0x24; for PFI1 it is 2 with PFI1 set to input
(0xb21bf404/0xf937a9a3 = 0/0, `ctr-cifreq-pfi1-timeout`). Two-edge's first-edge input takes the
same values as encoder A (FREQOUT = 0x2e; PFI n = n + 1 **[H]** for PFIs, by analogy with 0x3da647a3).
The 14.1 prediction "first edge on aux 0xc7c20613" is refuted: aux stays 0x2f.
Scaling is host side (ticks / 100 MHz; encoder counts × distance per pulse or 360° / (ppr · X)).

**Not supported by the myDAQ in DAQmx** (so nothing to capture; MYBENCH must emulate or hide):
- semi-period (`CreateSemiPeriodChannel` → -200431): use pulse time (both half-periods per read);
- **buffered / implicit-timed counter input**: the counter's only data transfer mechanism is
  programmed I/O ("Non-buffered hardware-timed operations are not supported", `ctr-*-buf50-*`);
  emulate with repeated on-demand reads. The counter therefore never takes a stream (7.3);
- count **down** and **external direction** (-200077: only `Up`). External direction can be
  built on the encoder mode: X1 decoding with A = the signal (PFI0) and B = the direction line
  (PFI2) — needs a jumper test for the direction sense; "down" = host-side `initial − count`;
- two-edge separation with both edges on the same terminal and the same edge (DAQmx rule).

**Pulse trains** (app 9 continuous, 0x0b finite; U in 100 MHz ticks):
- **U0 = initial delay** (minimum 2; DAQmx sends 2 when no delay is asked): 10 ms → 1,000,000
  (`co-ptrain-1k-d0.25-delay10ms`), 5 ms → 500,000 finite (`co-finite-10-1k-d0.25-delay5ms`). With
  a delay the continuous task also has B0 = 0 and `arm` last byte 0 (1 without delay).
- Continuous: U2 = time in the **idle** state, U3 = time in the active state; `arm(U2, U3, 0, 9,
  1, 0, B0)`. **Idle high** = `m0d(2, 1)` (instead of (2, 0)) with the loads swapped accordingly
  (`co-ptrain-1k-d0.25-idlehigh`: U2 = 25000 high, U3 = 75000 low at 1 kHz 25 %).
- Finite N pulses: U1 = 2, U3 = 2N, U6 = high, U7 = low, U8 = high (1 kHz 25 %: 25000, 75000,
  25000); `arm(0, 2N, 0, 0x0b, 1, 0, 0)`; event 9 on 0c41 at the end (10.2).
- **Live update while running** (`co-update-1k-to-2k-d0.25`: 1 kHz 50 % → 2 kHz 25 %) **[C]**:
  `ctr.m1e()` (reply: status 0 + u32 0), then `ctr.m18(u32 IDLE, u32 ACTIVE)` with the new
  idle-state / active-state ticks (37500, 12500), nothing else; the train keeps running and the
  new values take effect at the next period **[H]** (not measured: nothing wired). Frames:
  `00 01 00 0c 00 08 01 1e 0c 41 00 00` then `00 01 00 14 00 10 01 18 0c 41 00 00 <u32 IDLE> <u32 ACTIVE>`.
  Prediction `m18(low, high)` confirmed; for idle high the pair is presumably (high, low) as U2/U3 **[H]**.

### 10.6 Open

- **PFIn = DIOn**: PFI0 = DIO0 **[N]** (internal test: ctr0 counted exactly 25
  rising edges on PFI0 while DIO0 was toggled 25 times as an output); PFI3 = DIO3 (libmydaq's pulse
  train). DAQmx refuses a DI task on lines a counter reserves (`pfidio-co-100hz-port0`).
- Open: the meaning of the remaining 0c41 fields (send them verbatim per mode, as in 10.5); two-edge
  first-edge values for PFI inputs (**[H]** n + 1); external-direction sense (jumper test).

## 11. Native validation (internal Python prototype)

macOS + pyusb only, VM detached:

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
| DMM all modes (2026-09-29) | every confirmed MODE incl. Ω 20 k, diode (20), continuity (21) read with open leads; diode/continuity packets byte-identical to ELVISmx's; `read(1, 200)` → -225202 after 200 ms |
| counter (2026-09-29) | freq / period / two-edge on the FREQOUT loop: 10000 / 10000 / 5000 ticks = DAQmx; live update, AO ±2 V static, Ω 200 k–20 M replayed with status 0 |
| FREQOUT (2026-09-29) | 10 k, 100 k, 6250, 1 M, 2 M, 1.25 M, 4 MHz counted by ctr0: all within 0.01 % (internal check) |
| stage 2 (2026-09-29) | timed AO finite/cont, audio in/out, all DMM modes, ctr loop (10607 edges), PFI0 = DIO0 — all pass (AO done events re-checked: 0001 on 0560 for ao0, ao0+ao1, audio L+R) |

Offline, byte-exact: all 27 AI/audio-in and 11 AO/audio-out (incl. ±2 V) timed task sequences
(internal self-check); 98,322 streamed AI values, all AO stream codes, 26 DMM readings and
all on-demand AI values reproduced from raw data + calibration (internal checks).

Statistical AI comparison (open, floating inputs): right after an init the floating ai0 node
relaxes for a minute or two (native: -2.11 V at 0 s, -2.63 V at 120 s); DAQmx reads 30–100 s after
its bind-time init. Settled, the two agree: DAQmx -2.6372 V / sd 0.422 mV and -2.6628 V / sd
0.415 mV vs native -2.6342 V / sd 0.438 mV (finite 1 kS/s × 100, ai0 ±10 V).

## 12. Captures (`captures/`, each with a `.txt` of DAQmx output + host timestamps)


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
| `aiao-ao0cont-ai0cont-aofirst`, `aiao-ai0cont-ao0cont-aifirst`, `aiao-ao0cont-ai0finite`, `aiao-ao01stream-ai01cont-10k` | AI + AO concurrently: stream 5 for the second task; AO underflow events (7.3) |
| `ctr-pw-falling-freqout-10k`, `ctr-pulsetime-freqout-10k`, `ctr-linenc-x4`, `ctr-linenc-x1-z`, `ctr-angenc-x2`, `ctr-angenc-2pulse-freqout-10k`, `ctrloop-falling-10k`, `ctrloop-init1000-10k`, `ctrloop-resetpfi1-10k`, `ctr-countedges-20mhz-1s`, `ctr-countedges-100khz-1s`, `co-ptrain-1k-d0.25-idlehigh`, `co-ptrain-1k-d0.25-delay10ms`, `co-finite-10-1k-d0.25-delay5ms` | stage-3 counter modes (10.5) |
| `ctr-semiperiod-freqout-1k`, `ctr-twoedge-rr-freqout-10k`, `ctr-*-buf50-freqout-10k`, `ctrloop-down-10k`, `ctrloop-extdir-10k` | DAQmx refusals (no counter traffic): semi-period, same-edge two-edge, buffered CI, count down/external direction |
| `dmm-ohm-r20k-x2`, `dmm-ohm-r200k-x2`, `dmm-ohm-r2M-x2`, `dmm-ohm-r20M-x2` | Ω 20 k … 20 MΩ = MODE 16 … 19 (open leads) |
| `dmm-elvis-diode`, `dmm-elvis-cont`, `dmm-elvis-dcv-auto` | ELVISmx DMM GUI operated by hand: diode (LED), continuity (wire, 150 Ω); dcv-auto holds no DMM traffic |
| `co-update-1k-to-2k-d0.25` | live pulse-train update (`m1e`, `m18`) |
| `ao0-r2-static-p0.5`, `ao1-r2-static-m0.5`, `ao0-r2-finite-1k-n100-ramp` | AO ±2 V range |
| `ctr-freq-freqout-10k`, `ctr-period-freqout-10k`, `ctr-twoedge-freqout-10k` | frequency, period, two-edge on the FREQOUT loop |
| `freqout-2M`, `-1M25`, `-6250`, `-1M`, `-4M` | FREQOUT timebases and divider 16 |
| `ai0-finite-timeout`, `dmm-dcv-timeout`, `ai0-cont-resetduring` | timeouts and reset during a task (2.1) |
| `calmem-036CD1C6.bin` | full calibration memory of this unit (device data; test fixture) |
| `capabilities.txt` | every DAQmx device/channel property, terminals, counter default terminals |
| `enum-and-failed-first-transfer.pcap` | historical: unconfigured-device failure before the QEMU patch |

Captures since the stage-2 VM restart hold complete payloads (QEMU `usb-pcap-data-len.patch`,
256 KiB per transfer). Stage-1 captures truncate transfers to 256 bytes (the calibration reads).

## 13. Tools

The capture and analysis tooling used for this document (DAQmx capture scripts, pcap readers, frame
decoders, the Python native prototype and replay tests) is internal and not published. In the repo:
`tools/devlock.sh` serialises access to the single physical myDAQ between test runs, and
`tools/pcap2mock.py` converts a capture into a mock-device fixture for `libmydaq` tests.

## 14. Pending experiments (device needed), in priority order

Every device access goes through `tools/devlock.sh`.

The stage-3 capture batch ran on 2026-09-29 (03:18–04:42); P1, P3–P8 are done
(verdicts in 14.1). Still open:

| # | goal | status / command |
|---|---|---|
| P1b | Ω 20 k–20 M **scaling** with real resistors (MODE bytes done) | **BLOCKED**: needs a 100 kΩ resistor in the jacks, then a DMM capture on Ω 200 k (likewise 2 M / 20 M with 1 MΩ / 10 MΩ) |
| P3b | DMM hardware autorange | **BLOCKED**: needs a manual capture while the ELVISmx DMM is open and Mode is set to Auto |
| P9 | AI overflow / AO underflow events | **READY** from captures (6.5: 0551/2 + 0550/7; 7.3: 0561/2 + 0561/1); native provocation optional |
| P10 | minimal init (which of the 23 init calls are needed) | **deferred** (not needed: the full init works natively in ~1 s); if ever: drop one group at a time, observed commands only |
| P11 | external-direction sense, two-edge on PFIs | **BLOCKED** (jumper DIO→PFI2 / PFI1) |

Added for stage 3 (every Counter/Timer panel mode needs a capture):
pulse width, semi-period, two-edge separation, linear and angular encoders; count edges falling
edge, count down, external direction (PFI2), initial count, count reset (PFI1); count-edge sources
20 MHz / 100 kHz timebase; buffered (sample-clocked) counter input; idle-high pulse train, initial
delay, live pulse-train update (frequency/duty change while running); AO ±2 V range on ao0/ao1;
AI + AO concurrently (both on stream 4); abort/timeout. Signals come from the internal FREQOUT loop
(`/myDAQ1/FrequencyOutput` as counter terminal) or DIO lines; tests needing a jumper (AO0→AI0,
DIO4→DIO1) are listed as such.

### 14.1 Stage-3 predictions (from driver-notes 7 / 8, written 2026-09-28 23:40 before decoding) and verdicts

Each stage-3 capture answers one question; the prediction was recorded first. Verdict: **READY**
= exact bytes known (sections named), **BLOCKED** = needs manual wiring or operation.

| item | prediction (source) | verdict (2026-09-29) |
|---|---|---|
| pulse width | app code 4 in `ctr.configure` u8 #2 / `ctr.arm` / `ctr.read` last byte; gate = PFI1 (driver-notes 7.3) | **READY** — confirmed, 10.5 **[N]** |
| two-edge separation | app 6; first edge on aux (PFI2), second on gate (PFI1) | **READY** — app 6 confirmed; first edge is route **0x77c75630** (not aux), 10.5 **[N]** (5000 ticks) |
| linear / angular encoder | app 7 / 0x0e; A = source (PFI0), B = aux (PFI2), Z = gate (PFI1); decoding X1/X2/X4/2-pulse in a `m0b` field | **READY** — apps confirmed; own A/B/Z routes, decoding in `m0f`, 10.5 **[N]** |
| count edges falling / down / external direction | an edge-polarity and a direction field in `m0b`/`m10`; external direction = aux route 0xc7c20613 = PFI2 (value 3) | falling **READY** (polarity id, 10.5 **[N]**); down / ext. direction refused by DAQmx — host emulation; direction sense **BLOCKED** (jumper) |
| initial count | `ctr.arm` first u32 (load value) = initial count | **READY** — refuted: `configure` U0, 10.5 **[N]** |
| count reset on PFI1 | gate route 0x097d2c54 = 2 plus a gate-mode field in `m10` | **READY** — gate route + `m0b` fields (not m10), 10.5 **[C]** |
| count-edge source 20 MHz / 100 kHz | new source values on 0x587c6efe (unknown numbers) | **READY** — 0x1c / 0x12, 10.5 **[C]** |
| buffered counter input | a bulk stream like AI (`iCtrBulkTransferManager`, driver-notes 7.3, **L**) | refuted — DAQmx: programmed I/O only; emulate |
| idle-high pulse train | an output-polarity field (m0b / m10); loads unchanged | **READY** — `m0d(2, 1)`, loads swapped, 10.5 **[C]** |
| initial delay | finite train U[6] = initial delay ticks (driver-notes: T6..T8 = delay, low, high) | **READY** — refuted: U0 = delay; U6/U7/U8 = high/low/high, 10.5 **[C]** |
| live pulse update | `ctr.m18(u32 low, u32 high)` (unused method, driver-notes 7.1, **L**) | **READY** — confirmed, preceded by `m1e()`, 10.5 **[C]** (native replay: statuses 0) |
| FREQOUT timebase | `freqout.configure(div, a, b)`: (1, 0) = 100 kHz (seen); (0, 0) and (0, 1) = 20 MHz / 10 MHz in some order | **READY** — (0, 0) = 20 MHz, (0, 1) = 10 MHz; divider 16 sent as 0, 10.4 |
| AO ±2 V on ao0/ao1 | a range selector byte in `ao_dac.configure` and the ±2 V calibration record | **READY** — `configure` SEL 5 / AUD 1 + ±2 V record, 7.1 / 7.2 (static **[N]**) |
| DMM Ω 20 k … 20 M | `set_mode` 16..19 (capability table order) | **READY** (MODE bytes, open leads) — 9.5; finite-value scaling **BLOCKED** (real resistors) |
| DMM diode / continuity | `set_mode(20, 0)` / `(21, 0)` (driver-notes 8) | **READY** — confirmed by ELVISmx GUI captures, native **[N]**, 9.5 |
| DMM hardware autorange | `set_mode(m, 1)` | **BLOCKED** — `dmm-elvis-dcv-auto` has no DMM traffic (needs the ELVISmx DMM GUI in Auto) |
| frequency / period on the FREQOUT loop | app 3 / 2, gate = FREQOUT (0x29), read → ≈ 10000 ticks at 10 kHz (added 2026-09-29 01:00) | **READY** — confirmed exactly (10000 / 10000), 10.5 **[N]** |
| two-edge (rising → falling, both FREQOUT) | app 6; first edge on aux 0xc7c20613, second on gate; read ≈ 5000 ticks | **READY** — 5000 ticks; first edge on 0x77c75630, 10.5 **[N]** |
| AI finite read timeout (DAQmx 1 s) | no vendor abort request; the normal stop/flush sequence of 6.1 when the task is cleared | **READY** — confirmed, 2.1 |
| DMM read timeout | the pending `dmm.read` is simply left to complete (or aborted with control request 0xFC, driver-notes 1.2) | **READY** — neither: the timeout is a `read` argument and the device answers -225202, 2.1 **[N]** |
| reset during continuous AI | the init sequence of section 3 again, without stopping the stream first | **READY** — refuted: task teardown first, then the full init, 2.1 |

Not to be done: power-supply overcurrent tests (never provoke supply faults), any flash write
(0x0507 `write`), unobserved MODE/route values.
