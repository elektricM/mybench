# myDAQ driver notes (static analysis of NI's Windows driver)

Status: 2026-09-28, first pass. Companion to `docs/protocol.md` (the USB agent's capture-based
protocol description). This file records what NI's driver binaries tell us about the protocol, in
our own words: message formats, object and method ids, argument layouts, constants, formulas,
sequences. No NI code or data is reproduced; the binaries stay in `ni-local/` (gitignored).

Sources (all DAQmx 25.8, x64; nistc3ru.dll added for the counter experts): `ni-local/allk/niraptrk.dll` (kernel device driver for the
"Raptor" family: X-series and myDAQ), `nifslk.dll` (the "Fusion" RPC transport over USB),
`nistc3rk.dll` (counter/PLL/filter objects), `nipalusbed.sys`; `ni-local/user/niraptru.dll`
(user-mode part: DAQmx attribute "experts", calibration and DMM conversion; copied from the
UI-explorer VM). Decompiled with Ghidra 12 headless; addresses below are virtual addresses in
those files (image base 0x180000000) so every claim can be re-checked.

Confidence: **H** = read directly from code and/or reproduced bit-exactly against captures;
**M** = strong inference from code structure + captures; **L** = hypothesis.
Verification scripts (all run against every capture in `captures/`):

| script | checks | result |
|---|---|---|
| `tools/drv_fusion.py` | method table below (argument and reply types) against every command frame | **3565/3565 requests, 3361/3361 replies decode exactly** (every capture in captures/, 91 files, incl. counter/FREQOUT) |
| `tools/drv_dmm.py` | DMM packet → value conversion + calibration slots + set_mode table | **30/30 DMM readings bit-exact** (DC/AC V, DC/AC A, Ω overload) |
| `tools/drv_calmem.py` | calibration memory map (directory, section CRCs, timestamps, all tables) + AI/AO formulas on every capture | **all 6 CRCs OK; 87441/87441 AI values (on-demand + streamed, 1–2 channels, mixed ranges, finite + continuous) and 4/4 AO codes bit-exact** |

---

## 0. How NI's stack is organised (why the protocol looks the way it does)

- The myDAQ is a "Fusion" USB device: the host drives **remote objects** on the device through a
  small RPC protocol ("Fusion service layer"). NI uses the same protocol over TCP for Ethernet
  devices. The device side presents objects that mirror the blocks of NI's STC3 timing/IO engine
  (AI front end, input timing, input FIFO, AO DAC, output timing, DIO, counters, routing, flash).
  There is **no raw register access** on this device; everything is object/method calls.
  (H: niraptrk only reaches the device through generated request "stubs", section 1.4.)
- niraptrk.dll contains one generated stub per (object class, method): it allocates a request,
  writes class id, instance, method id, version 1, then serialises the arguments. The class id
  is an immediate constant in each stub; the generated source file name (e.g.
  `raptorapi/.../tAIScanningFrontEnd.ipp`) names the class. This is how the table in 1.4 was
  obtained; reproduce with `tools/drv_ghidra/DecompAll.java` (Ghidra headless export, output
  kept outside the repo) + `tools/drv_stubs.py`.
- niraptru.dll (user mode) computes everything DAQmx-level: coerced rates, calibration scaling,
  DMM packet parsing. Kernel and user parts exchange "settings" objects; only the kernel talks USB.

## 1. Command channel (bulk OUT 0x03 → bulk IN 0x81)

### 1.1 Frame (H — nifslk `tRequestMessage`, `tUSBMessageDispatcher`; all captures)

```
off  type  request                          reply
0    u16   slot (NI: "FTID")                same slot
2    u16   total frame length (padded /4)   total
4    u16   message length = total - 4       total - 4
6    u8    1 = reply expected, 0 = one-way  1
7    u8    method id (>= 8 for objects)     0
8    u16   object class id                  0 (reserved)
10   u16   object instance                  status element (below), then return values
12   ...   arguments
```

- **Slot**: a small integer the host reserves per logical caller (NI: "Fusion transaction id",
  reserved from a pool by `tFTIDReserverFactory`). Each API object stores its slot in its first
  byte and every request/reply carries it; the transport keeps one outstanding transfer per slot
  and routes replies by slot (nifslk 0x180013730 indexes a per-slot table with it). Observed: 0 =
  session/identity calls, 1 = all normal calls, 7 = stream credit calls. A native driver can use
  1 for everything except the stream credits (use a separate slot, e.g. 7, if they are ever sent
  while another call is in flight). (M)
- **One-way requests** (H — niraptrk 0x18000f970 clears byte 6 before sending; captures): with
  byte 6 = 0 the device sends **no reply**. Only used for the stream read-position report
  (`stream.read_pos`, 3.3). Every other request has byte 6 = 1 and gets exactly one reply.
- **Pipelining** is allowed: NI sends the five calibration-memory reads back to back on slot 1
  before reading the replies (`idle-a`); replies come back in order. (H, capture)
- **Status element** (H — reader nifslk/niraptrk 0x18000e690): u16 length L; L = 2 means
  success with no code; L = 6 is followed by an i32 status code (0 = success, negative = error,
  positive = warning). So the "kind 2 / kind 6" word of protocol.md is `u16 0` + this element.
- **Serialisation** (H — writer/reader helpers 0x18000e170..0x180030ae0, 0x18000e480..0x180012d10;
  all big-endian, positional, no tags):

| type | wire format |
|---|---|
| u8, u16, u32 | 1/2/4 bytes BE |
| u8[] | u16 = byte count + 2 (counts itself), then the bytes |
| u16[] | u16 = 2·n + 2, then n BE u16 |
| u32[] | u32 = 4·n + 2 (sic, the prefix is 4 bytes wide but counts as 2), then n BE u32 |
| status | see above |

- **Device status codes** met in the driver (H for the handling, M for the meaning):

| code | handling in NI's driver | meaning |
|---|---|---|
| -225229 (0xfffc9033) | cleared after `endpoint.flush?`; after `control.stream_table` it makes the driver fall back to a default stream table (niraptrk 0x180011450, 0x180011f70) | "method not supported" |
| -225202 | converted to the DAQmx timeout error -200474 after `dmm.read` and `ai_tap.read` (niraptru 0x18048bef0, niraptrk 0x18006eb70) | device-side timeout: no data within the timeout argument |
| -225270 | after `hwverify.verify` (0x0506 m08) NI clears it and **re-sends verify every 50 ms for up to 10 s** before failing (niraptrk 0x180066a80 → 0x180067500). The USB agent saw it once on this call right after a VM hand-over | device still starting up: poll `hwverify.verify` until it returns 0 |

- **Padding**: the host pads requests with zeros to a multiple of 4. The device pads replies to a
  multiple of 4 but the pad bytes are not always zero (`ep_info` replies end in `ff`), and a
  reply occasionally carries 4 extra bytes (two-sample `ai_tap.read`). Parse positionally and
  ignore what follows. (H, captures)

### 1.2 USB control requests (H for the mechanism — nifslk 0x1800123c0 / 0x180013730; M for the number)

Normal operation uses no vendor control transfers. To **abort** an outstanding request, NI's
transport sends a vendor OUT request (bmRequestType 0x40, wValue = slot, wIndex = 1, no data) and
then polls a vendor IN request (0xC0, wValue = slot, wIndex = 3, 6 bytes) every 10 ms until the
device reports the slot idle. The bRequest byte is a dispatcher parameter; the only candidate the
kernel passes when it creates the USB dispatcher is **0xFC** (niraptrk 0x1800119c0, next to the
4096-byte maximum message size and the 64-byte event buffer size). Only needed for cancel/timeout
recovery. Dispatcher limits (nifslk 0x1800126f0): 256 slots, request queue depth 100 by default,
maximum message 4096 bytes (so a single command/reply never exceeds 4 KiB).

If `session.identify` fails with certain transport errors, NI's driver resets the USB port and
re-opens the device (niraptrk 0x1800e5300, log text "USB port reset … failed").

### 1.3 Events on interrupt IN 0x82 (H — nifslk `tEventMessage` 0x180008f00 area; capture)

```
off  type
0    u16   0 (slot)
2    u16   total
4    u16   message length
6    u16   event id
8    u16   object class      10  u16  object instance
12   i32   status            16  payload (event specific), rest of the 64-byte buffer is stale
```

Observers register for (class, instance, event id). Events the driver listens for:

| class/inst | event | meaning (from the handler) | conf. |
|---|---|---|---|
| in_timing 0x0550 | 1, 2, 5, 6, 7 | state notifications; each wakes a different waiter. **5 = acquisition stopped** (after the last sample of a finite task, or after stop/overflow in continuous tasks); **7 = stopped because of an error** (sent between the overflow event and 5, `ai01-r10-contlong-200k-default`); 6 also sets a "stopped" flag | H (dispatch, sequences) / M (names) |
| in_timing 0x0550 | 3, 8 | **error**: the handler raises DAQmx status -200992 on the task | H |
| in_timing 0x0550 | 9 | payload = two u32 (combined to a u64 stored by the handler: probably a sample/time counter) | H (layout) / L (meaning) |
| chgdet 0x0531 | 1, 3 | change detected / change-detection overflow | M |
| in_fifo 0x0551 | 2 | **device FIFO overflow**: the host did not read the stream fast enough. Sequence on overflow: in_fifo ev 2, in_timing ev 7, in_timing ev 5; DAQmx then reports -200361 "Onboard device memory overflow" (`ai01-r10-contlong-200k-default`, `ai01-r10-cont-100k-n20000`) | H |
| in_fifo 0x0551 | 3 | **error**: the handler raises DAQmx status -201464 | H (code) / L (meaning) |
| out_fifo 0x0561 | 1, 2 | output FIFO notifications (underflow/empty; not decoded) | L |
| power 0x0c91, inst 0 | 0xdead | **power-supply fault** (myDAQ "power task status"): the myDAQ output-timing primitive treats this event with a negative status as a task error and aborts the output task (niraptrk 0x180086e20, observer registered in 0x180086a70). Expect it on +5 V / ±15 V overcurrent | H (dispatch) / M (meaning) |

(handlers: niraptrk 0x180075850 input timing, 0x180070ff0 input FIFO; registrations 0x180072e80,
0x180070180, 0x180080200.)

### 1.4 Object classes and methods (H for ids and argument types; names are ours)

Instances seen: AI objects use instance 2 (in_timing, ai_tap, in_fifo) and 0 (ai_fe);
AO DAC instance = channel (0 = ao0, 1 = ao1; the `write` call always uses instance 0 and carries
the channel list); DIO port0 = instance 1; stream endpoint objects use the stream number (4 = EP
0x84). Reply column: types after the status element ("" = status only).

| class | NI object | method: args → reply (our name) |
|---|---|---|
| 0x0000 | session (`Global_Class`) | 00 identify: → u16 0x0200, u16 0xa000, u32 current firmware/protocol version, u32 oldest compatible version, u8[] — the driver compares the two u32 (both 0x1980c12d here) with its own expected version to decide on a firmware update (niraptrk 0x18000dde0) · 02 (limits?): → six u16 |
| 0x0002 | stream (`SignalStreaming`) | 08 in_credit(u32 bytes) · 09 out_credit?(u32) |
| 0x02fb | FPGA loader | 08 (u32, u8) · 09 () — not used by the myDAQ |
| 0x02fc | endpoint manager, inst = stream | 08 configure(u8 dir?, u8 depth?) · 09 start · 0a stop · 0b flush? |
| 0x02fe | control | 09 () · 0d ep_info(u8 endpoint address) → u16 max packet, u8 · 0f open? · 13 stream_table(u32 0) → u32[] sizes, u16[] endpoint numbers, 3 × u16[] (interface/alt settings) |
| 0x0501 | routing (`tRoutable`) | 09 connect(u32[] route ids, u32[] values) |
| 0x0502 | watchdog | 08..12 (not used by myDAQ captures) |
| 0x0503 | cal PWM | 08 (u32) |
| 0x0506 | hardware verification | 08 () |
| 0x0507 | flash / calibration memory | 08 read(u32 offset, u32 length) → u8[] · 0a write(u32 offset, u8[]) · 09, 0b, f1 → u8, f2 |
| 0x0508 | temperature sensor | 08 → u16 (myDAQ has none) |
| 0x0512 | AI scanning front end | 08 (u8) · 09 (9 × u8) · 0a set_scanlist(u16[]) · 0b clear · 0c commit → u32 · 0d release → u32 |
| 0x0513 | AI immediate tap | 08 reset · 09 (u32) · 0b read(u32 scans, u32 partial, u32 timeout ms, u16 max samples, u8 3) → u16, u8[] samples · 0e (u32) |
| 0x0516 | analog trigger | 08 (u32, u16, u16, u8, u8) · 09 · 0a (u8) — myDAQ has no analog trigger |
| 0x0517 | AI non-scanning front end | 08 · 09 (u16[]) · 0a (u8) · 0b (u8) |
| 0x0520 | AO DAC | 08 configure(6 × u8) · 09 write(u32[] codes, u16[] channels) · 0a update · 0b reset(u8) |
| 0x0530 | DIO | 08 read → u32 · 09 write(u32 mask, u32 value) · 0a set_direction(u32 mask, u8 out) · 0b · 0d (u32, u32) |
| 0x0531 | change detection | 08 (u32, u32, u16, u16) · 09 · 0a (u8, u8) · 0b (u32, u32) → u32[] · 0c · 0d · 0e → u8 |
| 0x0550 | input timing (AI inst 2) | 08 reset · 09 configure(6 × u32, u16, 25 × u8) · 0a arm · 0b (u8, u8) · 0c start · 0d status → u8, u8 · 0e disarm · 0f unconfigure · 10 (u32) |
| 0x0551 | input FIFO | 08 configure(5 × u32, 3 × u8) · 09 · 0a |
| 0x0560 | output timing | 08 (u8, u8) · 09 (u32[]) · 0a configure(5 × u32, 22 × u8) · 0b (u8, u8) · 0c (u8) · 0d–0f · 10 (u8) · 11 · 12 (u32) · 13 status → u32, u32, u8 · 14 (u32) · 15 (u32) · 17 → u32 · 18 |
| 0x0561 | output FIFO | 08 (u8) · 09 configure(u8[] one byte per output channel (DAC order list, not sample data), u8, u8) · 0a (u8[]) · 0b |
| 0x0c40 | frequency output (nistc3rk) | 08 configure(u16 divider, u8, u8) · 09 start · 0a stop · 0b release (§7) |
| 0x0c41 | counter ctr0 (nistc3rk `tSTC3CtrResource`) | 27 methods 08..22, full table in §7.1; 09 configure, 14 arm, 1d read → u32[] |
| 0x0c42 | digital input filter | 09 (u8[], u8) |
| 0x0c43 | trigger filter | 08 (u16, u8[]) |
| 0x0c44 | PLL | 08 · 09 (u16, u16, u32) · 0a · 0b |
| 0x0c90 | DMM (`tDMMControl`) | 08 init · 09 set_mode(u8 mode, u8 hw_autorange) · 0a read(u32 1, u32 timeout ms) → u8[] packet (§8) |
| 0x0c92 | power task status | 09 () — sent when an output task starts, together with a subscription to event 0xdead of class 0x0c91 (power fault) |

The complete machine-readable table is `M` in `tools/drv_fusion.py` (the counter/PLL/filter
classes are listed there only as far as needed; their full signatures are in section 7).

## 2. Device open / init (H, sequence = captures; meaning = code)

The init in protocol.md §3 decodes as:

```
session.identify; session.limits?; control.stream_table(0); session.identify; control.open?;
control.ep_info(0x03); control.ep_info(0x81)           max packet 512 for both command pipes
flash.read(0,0x1ed) … flash.read(0x7b4,0x4c)            whole 2 KiB calibration memory
hwverify.verify?; in_timing[2].reset; ai_tap[2].reset; out_timing[0].m08(1,0); ao_dac[0].reset(0)
dmm.init                                                ~1.6 s after attach (DMM chip power-up)
route[1].connect(12 ids, all 0); route.connect(0x416a2b93=1); (0x80d0b3e6=0); (0x87d9e4ed=0);
route.connect(0x99fccdab=0x1f); (0x21aeef59=0)          default signal routing (section 6.3)
```

Sizes (H — niraptrk 0x180066a80): the driver takes the max packet sizes returned by
`control.ep_info(0x03)` / `ep_info(0x81)` (512 each) and derives the largest request payload
(512 − 12 = 500 bytes) and the largest reply payload (512 − 19 = 493 = 0x1ed bytes). That is why
the calibration memory is read in 0x1ed-byte chunks: every command and every reply fits in one
USB packet. A native driver should do the same (flash.read length ≤ maxpacket − 19).

Startup readiness (H): `hwverify.verify` may answer -225270 while the device is still starting;
NI retries every 50 ms for up to 10 s (section 1.1 status table).

`stream_table` (M): seven streams; entry i = (u32 buffer size, u16 endpoint number, 3 × u16
interface/alternate setting for isochronous variants — all 0 here). Sizes 2048, 2048, 1024,
1024, 1024, 512, 512 bytes for endpoints 4..10. Stream n uses bulk IN 0x80|n and/or bulk OUT n.
The kernel only looks at alternate settings when the stream type is 4 (isochronous), so on the
myDAQ every stream is plain bulk (H, niraptrk 0x180011f70).

## 3. Analog input

### 3.1 Scan-list entry (u16, `ai_fe.set_scanlist`) (H for the bits DAQmx varies; L for the rest)

```
entry = 0x2840 | range << 9 | channel | (0x4000 if last entry of the scan)
        range: 1 = ±10 V, 2 = ±2 V        channel: 0 = ai0, 1 = ai1
```

`0x6a40` = ai0 ±10 V alone; `[0x2a40, 0x6a41]` = ai0 then ai1 (channel order = sample order in
the stream). Up to 2 entries. The constant part matches the layout of NI's M/X-series "AI
configuration FIFO" word as known from open-source drivers (bit 14 last-channel, bit 13 dither,
bits 9..11 gain, bits 6..8 channel type with 1 = differential, bits 0..3 channel), which would make
the myDAQ gain field 5 (±10 V) / 6 (±2 V) with bit 11 always set (L). Audio inputs are expected to
differ in the channel/channel-type bits (capture). The range field is **not** the calibration range
index (±10 V = field 1 but calibration range 0; ±2 V = field 2 = calibration range 2, section 5.2).

### 3.2 Sample format (H — capture + DAQmx values)

- Stream (bulk IN from the stream endpoint, EP 0x84 for AI): **little-endian i16**, scans
  interleaved in scan-list order (ai1-first scan lists put ai1 first), no header, no sequence
  counter, no padding. Bulk transfer sizes seen were multiples of the scan size, but a native
  driver should carry partial scans across transfers anyway.
  A finite task delivers exactly N·nch·2 bytes. Each sample is converted with the range record
  of its own scan-list entry (mixed ±10/±2 V scans work). `tools/drv_calmem.py` reproduces
  every streamed DAQmx value bit-exactly.
- On demand (`ai_tap.read` reply): u16 (0), then u8[] holding **big-endian** i16 samples (the
  command channel is BE). One scan per read in DAQmx's usage.

### 3.3 Hardware-timed acquisition (M: order from captures, roles from the code)

1. `ai_fe.clear; ai_fe.set_scanlist; ai_fe.commit` (commit returns the entry count).
2. `endpoint[4].configure(1, 0x10)` — enable stream 4 (the first u8 selects direction/alternate
   setting in the code; 0x10 is constant in DAQmx's traffic).
3. `route.connect(0x3212f413 = 0)` — AI route reset (section 6.3).
4. `in_timing[2].configure(...)` (3.4).
5. `in_fifo[2].configure(0, 0, B, 0, 0, 4, 0, 0)` where B = **host buffer size in bytes** =
   samples/channel × channels × 2 (finite: the whole acquisition; continuous: the DAQmx buffer,
   e.g. 80000 for 20000 samples × 2 ch). The 4 is the stream number. For on-demand reads NI uses
   `(0, 0, 0, 0, 0, 0xff, 1, 0)` (no stream, immediate tap). (H: 14 captures)
6. `endpoint[4].start`, then `stream[4].in_credit(C)` on slot 7 (hard-coded when the kernel
   creates the stream object, niraptrk 0x180010d10): finite: C = total bytes (the device sends
   exactly that much and stops); **continuous: C = 0x7fffffff** (unlimited). (H: captures)
   While a continuous task runs, the host sends one-way `stream[4].read_pos(0x7fffffff − R)`
   (class 0x0002 method 9, slot 7, byte 6 = 0, no reply), R = bytes the application has consumed
   from the host buffer so far, in whole buffers (R advances by B each time). The device keeps
   streaming regardless (only 2 reports during 2.4 MB in `ai01-r10-cont-100k-n20000`); it most
   likely uses R to detect that the host buffer would be overwritten (M). A native driver can send
   the report after each buffer's worth of data it has handed to the application.
   DAQmx reads the stream with 32 KiB bulk IN transfers (82 ms apart at 2 × 100 kS/s).
7. `in_timing.arm; in_timing.m0b(0, 0); in_timing.start`.
8. Data on EP 0x84; completion event 5 on EP 0x82 (finite).
9. Stop: `endpoint[4].stop; in_timing.status → (u8, u8); in_timing.disarm; ai_fe.release;
   in_timing.unconfigure; endpoint[4].flush?` (the last one returns -225229 also for NI: the
   device does not implement that call, and NI's wrapper explicitly clears exactly this code,
   niraptrk 0x180011450).

### 3.4 `in_timing.configure` arguments (6 × u32, u16, 25 × u8) (M/L)

Values seen (on-demand single-point read / finite 1 kS/s × 100 samples):

| arg | on-demand | timed (finite / continuous) | reading |
|---|---|---|---|
| u32 0 | 3 | 3 | constant (mode selector, L) |
| u32 1 | 1500 (1 ch) / 1250 (2 ch) | see rule | **convert (channel-to-channel) period in 100 MHz ticks** (H: fits all 11 timed captures): `convert = min(D, divisor // nch)` with D = 1500 for 1 channel, 1250 for 2 (15 µs = 1/200 kS/s + 10 µs, 12.5 µs = 1/400 kS/s + 10 µs: DAQmx's "fastest convert rate + 10 µs settling"). Seen: 1 ch 100 k → 1000, 200 k → 500; 2 ch 100 k → 500, 200 k → 250. It is also the inter-channel skew for `AiTask::channelSkew()` (convert / 100 MHz) |
| u32 2 | 0 | 4 | sample-clock source select (4 = internal timebase, 0 = none/software) (M) |
| u32 3 | 0 | divisor | **sample-clock divisor**, rate = 100 MHz / divisor (H: 10 S/s → 10 000 000, 1 k → 100 000, 10 k → 10 000, 12345 → 8100 = 12345.679 S/s as DAQmx reported, 100 k → 1000, 200 k → 500) |
| u32 4 | 0 | N | samples per channel (finite); **0 = continuous** (H) |
| u32 5 | 0 | 0 | used only for sample mode 2 (continuous, L); non-zero enables an extra mode flag |
| u16 | 1 | 1 | (L) |
| u8 × 25 | `0 0 0 0 0 1 1f 0 0 0 1 0 0 1 0 0 0 0 0 0 1 0 0 0 1` | `0 0 0 0 1 0 0 0 0 0 0 1 0 0 1 0 0 0 0 0 1 0 0 0 0` | see below |

Structure of the 25 bytes (H for the structure, from the kernel code that fills them,
niraptrk 0x180073d10; roles L): index 0, 1, 2, 5, 9, 11 are booleans; 3/4, 6/7, 12/13, 14/15,
17/18 are five **(signal source select, polarity)** pairs, i.e. the trigger/clock inputs of the
input timing engine (start trigger, reference trigger, pause, sample clock, convert clock); the
source values are STC3 routing-mux selects (0 = default/internal, 0x1f appears for the
on-demand read: probably "logic low/none", M); index 8 is a second-level source enum (0..0x22);
index 10 is a raw byte; index 16 = "sample mode is 2"; index 19 is a 0/1/2 selector; 20/21/22 are
one-hot flags of a 3-state mode (on-demand and finite both use state 2 → index 20 = 1); index 23 a
raw byte; index 24 = "sample clock and convert clock settings differ" flag. The finite vs on-demand
differences are exactly: 4 (0→1), 5 (1→0), 6 (0x1f→0), 10 (1→0), 11 (0→1), 13 (1→0), 14 (0→1),
24 (1→0). **Finite and continuous use identical bytes** (only u32 4 differs), for every rate and
channel count captured. A native driver can replay the two observed byte variants (on-demand,
timed) and compute u32 1, 3, 4.

The in_timing object selects its translator tables by **instance**: instances < 2, 2 and 3 use
different tables (AI = instance 2; the others are presumably counter/DI timing engines, L).

## 4. Analog output

- Static write (H, captures + stub): `ao_dac[ch].configure(0, 1, 0, 0, 0, 0)`;
  `ao_dac[0].write([code], [ch])` — the u32 carries the 16-bit two's-complement DAC code in its
  low half (-0.5 V → 0x0000f999); `ao_dac[ch].update`. Several channels can be written in one call
  (lists), channel list = u16[].
- `ao_dac.configure` 6 × u8 (niraptrk 0x180030250): [0] = 1 when the DAC is driven by the
  hardware-timed engine (mode field 1 or 2), else 0; [1] = a boolean (1 for static writes);
  [2] = a device-specific selector from a virtual call (range/gain select, L); [3] = another
  translated selector; [4] = a byte from the channel configuration; [5] = 1 when a mode field is 5
  or 6. Static ao0/ao1: `0 1 0 0 0 0`. Capture audio-out and timed AO to see [2]..[5] change.
- Timed output (M, no capture yet): `out_timing` (0x0560) + `out_fifo` (0x0561). `out_fifo`
  method 09 carries a list with one byte per output channel (built from the channel/DAC list,
  niraptrk 0x180080f00) plus two selectors — it configures the FIFO, it does **not** carry
  samples. Sample data therefore goes over a bulk OUT stream endpoint (one of 0x04..0x0a, the
  stream table of section 2), presumably with the same credit mechanism (`stream` method 09).
  `out_timing.configure` has 5 × u32 + 22 × u8 (divisor, count, … by analogy with 3.4). Events on
  0x0561 signal FIFO conditions.

## 5. Calibration memory (flash object 0x0507, 2048 bytes)

### 5.1 Directory (H — niraptrk tMyDAQ_EEPROMContentMap 0x1800f59f0; calmem dump)

Little-endian. `u32 0x5bc` used size; `u16 0, u16 0x100` (format version 1.0, M);
`u32 0x38` directory size; `u32 4`, `u32 1`, `u32 1` (L); then (u32 id, u32 offset) pairs;
`u32` directory check at 0x40.

| id | offset | content |
|---|---|---|
| 0x40 | 0x100 | AI/AO calibration, copy A (the kernel's "external" copy, M) |
| 0x42 | 0x254 | AI/AO calibration, copy B (identical here; selected when the requested calibration kind is id 0x3965, presumably self-cal, M) |
| 0x44 | 0x598 | calibration info: two 5-character UTF-16 strings: "NI" = DAQmx `UserDefinedCalibrationInfo` (max 5 chars, H), "NATI" (probably the calibration password, L) |
| 0xcb0d | 0x3a8 | DMM calibration, copy A |
| 0xcb0f | 0x4a0 | DMM calibration, copy B (identical) |

Each section: `u16 length`, content, `u16` check word, stored LE. **Check word (H, all 5
sections and the directory verify, `tools/drv_calmem.py`)**: CRC-16, polynomial 0x8005, init
0xFFFF, MSB-first, no final XOR, computed over the content with a 0x00 byte inserted after every
content byte (NI's table-driven routine processes each byte as a 16-bit step, niraptrk
0x18004b470). Directory check word at 0x40 = same CRC over 0x0c..0x3f. Only needed for writing. AI/AO content starts at section+2, DMM at section+0x12.
Section header (first 16 content bytes): **u64 calibration time, seconds since 1904-01-01 UTC**
(0xe6500c79 = 2026-06-11 06:55:53 UTC = DAQmx "6/11/2026 8:55:53 AM" local; H), u32 37 (L:
calibration temperature °C), u32 5 (unknown).

### 5.2 AI/AO section layout (H — generic "MCal" map niraptrk 0x1800f4e40 / 0x1800f5010, range
tables 0x1800f4ce0; offsets relative to B = section + 2)

```
B+0x00  16-byte header (5.1)
B+0x10  AI channel block (one block for all AI channels on the myDAQ):
          4 × mode record (17 bytes each: u8 order, float32 c0..c3), only mode 0 programmed
          7 × range record (8 bytes: float32 gain, float32 offset), range index:
            0 ±10 V, 1 ±5, 2 ±2 V, 3 ±1, 4 ±0.5, 5 ±0.2, 6 ±0.1 (only 0 and 2 are calibrated)
B+0x8c  AO channel blocks, 4 × 49 bytes (ao0, ao1, audio L, audio R):
          1 mode record (17 bytes) + 4 range records: 0 ±10 V, 1 ±5, 2 ±2 V, 3 ±1
```

Absolute offsets in copy A: AI mode 0x112, AI ranges 0x156 (±10 V) / 0x166 (±2 V); ao0 block
0x18e (ranges from 0x19f), ao1 0x1bf (0x1d0), audio-out L 0x1f0, R 0x221. Copy B = +0x154.
The mode polynomial here is the identity (order 3, c = 0, 1, 0, 0).

Formulas (H for AI/AO: reproduces every DAQmx value, `tools/verify_cal.py` and `drv_calmem.py`):
- AI: `v = offset + gain · P(code)` with P the mode polynomial (identity here) → `offset + gain·code`,
  evaluated in double from the float32 constants.
- AO: `code = round(offset + gain · volts)` (gain in codes/V).
- Audio out: range ±2 V records (gains 16241 and 16262 codes/V, offsets 0: factory nominal, not
  per-unit) — use range record 2 of the audio blocks (M).
- Audio in: the AI block has no separate audio entries; DAQmx presumably uses the ±2 V range
  record with the audio channel type (L, capture).

### 5.3 DMM section (H — kernel tES51981DMM_EEPROMContentMap 0x1800f55a0 + user
tMyDAQ_DMMUserCalData / CalibrationDataRetriever; `tools/drv_dmm.py`)

28 slots of (float32 gain, float32 offset) at section+0x12 + 8·k (0x3ba + 8k in copy A):

| k | function / range | k | function / range |
|---|---|---|---|
| 0 | DC V 2 V | 14 | DC A 1 A |
| 1 | DC V 20 V | 16 | AC A 20 mA |
| 2 | DC V 60 V | 17 | AC A 200 mA |
| 4 | DC V 200 mV | 20 | AC A 1 A |
| 5 | AC V 2 V | 22 | Ω 200 Ω |
| 6 | AC V 20 V | 23 | Ω 2 kΩ |
| 9 | AC V 200 mV | 24 | Ω 20 kΩ |
| 10 | DC A 20 mA | 25 | Ω 200 kΩ |
| 11 | DC A 200 mA | 26 | Ω 2 MΩ |
| | | 27 | Ω 20 MΩ |

Other slots are unused placeholders (1, 0). The slot index follows the meter chip's
(function, range digit): 5 slots per function group. On this unit all Ω slots are (1, 0).
Formula: `value = gain · reading + offset` in double (section 8).

## 6. Timing engine, routing, counters

### 6.1 Timebase (H/M)
100 MHz master timebase (`CIMaximumTimebase` = 100 MHz; minimum rate 100 MHz/2^32).
Rate coercion: divisor = 100e6 / rate rounded to an integer, actual rate = 100e6 / divisor (H for
the formula: 12345 S/s → 8100 → 12345.679 S/s; floor vs round not yet distinguished — needs a
rate whose ideal divisor has a fraction ≥ .5, e.g. 7 kS/s → 14285.7).

### 6.2 Counters and FREQOUT

See section 7 for the full counter/FREQOUT description (method tables, sequences, routing,
measurement modes).

### 6.3 Routing ids (H: they are constants in niraptrk, not hashes of names)
`route.connect` takes u32 route ids. They come from a fixed map (niraptrk 0x1800f1a90) from the
driver's internal routing-register enum (values like 0x2800002e) to device route ids, generated
from NI's STC3 routing maps (`tstc3_brdservices/counter/intimer/triggers_routingmap.cpp`).
Examples: 0x2800002e → 0x416a2b93 (set to 1 at init), 0x2000002e → 0x80d0b3e6,
0x18000028 → 0x87d9e4ed, 0x20000028 → 0x99fccdab (set to 0x1f at init), 0x10000028 →
0x21aeef59. The value written is the selected source (0x1f is a typical "none/ground" select
in STC3 routing muxes, M). Replay the init values verbatim; counter/PFI tasks will add their own
ids (capture them).

## 7. Counter (ctr0) and FREQOUT

Sources: nistc3rk.dll request stubs (class ids, method ids, argument types: **H**), nistc3ru.dll
(user-mode counter "experts": measurement-type table **H**), niraptrk.dll routing maps (**H** for
the ids), captures `ctr-countedges-pfi0-1s`, `ctr-cifreq-pfi1-timeout`, `co-ptrain-1k-d0.5`,
`co-ptrain-10k-d0.25`, `co-finite-100-1k`, `pfidio-co-100hz-port0`, `freqout-10k`,
`freqout-100k`, `ctrloop-freqout-10k` (all decode exactly with `tools/drv_fusion.py`).

### 7.1 Objects and methods (H for ids/types; names ours, role from captures = M)

`ctr` = class 0x0c41 instance 0; `freqout` = 0x0c40 instance 0.

| method | args | reply | role seen |
|---|---|---|---|
| ctr 08 | – | – | reset (first call of every task) |
| ctr 09 | 9 × u32, u16, u16, 4 × u8 | – | main configuration (7.3) |
| ctr 0a | u16 | – | 0 in all captures |
| ctr 0b | 16 × u16 | – | mode fields (7.3) |
| ctr 0c | u16 | – | 1 for input tasks, 0 for output tasks |
| ctr 0d | u16, u16 | – | (2, 0) always |
| ctr 0e | – | – | always sent |
| ctr 0f | 6 × u16 | – | (0,0,0,3,0,0); last = 7 for finite pulse generation |
| ctr 10 | 7 × u16, u8 | – | gate/edge options (7.3) |
| ctr 11 | 5 × u16 | – | (0,1,0,0,0) always |
| ctr 12 | 12 × u8 | – | all 0; byte 8 = 1 for finite pulse generation; sent twice (before and after 13) |
| ctr 13 | – | – | commit |
| ctr 14 | u32, u32, u16, 4 × u8 | – | **arm/start**: initial load values + application code |
| ctr 15, 16, 17 | – | – | stop / disarm / unconfigure (sent in this order at task stop) |
| ctr 18 | u32, u32 | – | not seen (probably "write new loads" for on-the-fly pulse-train updates, L) |
| ctr 19 | u16 | – | not seen |
| ctr 1a, 1b, 1c, 1e | – | u32 | not seen (status/count readers, L) |
| ctr 1d | **read**(u32 n, u32 1, u32 timeout_ms, u32 1, u16 1, u8 app) | u32[] values | on-demand read of n values; count 10567 read back in `ctrloop-freqout-10k` (H). With no input signal the reply status is -200474 (timeout) after the timeout (`ctr-cifreq-pfi1-timeout`) |
| ctr 1f | u8, u8 | u32 | not seen |
| ctr 20 | – | u64 | not seen |
| ctr 21 | 5 × u8 | – | not seen |
| ctr 22 | u32, u32, u16, u8, u8 | – | not seen |
| freqout 08 | configure(u16 divider, u8, u8) | – | see 7.5 |
| freqout 09 / 0a / 0b | – | – | start / stop / release |
| 0x0c42 m09 | (u8[], u8) | – | digital input filter (not used by DAQmx defaults) |
| 0x0c43 m08 | (u16, u8[]) | – | trigger filter |
| 0x0c44 m08–0b | –, (u16, u16, u32), –, → u32 | | PLL (not used by the myDAQ) |

Event: ctr (0x0c41) **event 9** status 0 arrives when a finite pulse generation completes
(`co-finite-100-1k`) (H).

### 7.2 Task sequence (H: identical order in all captures)

```
route.connect(...)                 source/gate/output routes (7.4)
ctr.reset; ctr.configure(...); ctr.m0a(0); ctr.m0b(...); ctr.m0c(in?1:0); ctr.m0d(2,0); ctr.m0e
ctr.m0f(...); ctr.m10(...); ctr.m11(0,1,0,0,0); ctr.m12(...)
[route.connect(output PFI)]        output tasks only
ctr.commit; ctr.m12(...) again
ctr.arm(loadA, loadB, 0, app, ...) counter runs
ctr.read(...)                      input tasks, on demand, as often as needed
ctr.disarm(15); ctr.m16; ctr.unconfigure(17)
FREQOUT: route PFI4; freqout.configure; freqout.start ... freqout.stop; freqout.release
```

### 7.3 Application code and the configuration fields

**Application code** (H — nistc3ru tCounterChannelExpert 0x180058fe0, keyed on the public DAQmx
measurement/output types): 1 = count edges, 2 = period, 3 = frequency, 4 = pulse width,
5 = semi-period, 6 = two-edge separation, 7 = linear encoder, 0x0e = angular encoder, 0x0f = CI
pulse (freq/time/ticks), 9 = pulse-train output (CO pulse freq/time/ticks). Captures confirm 1, 3
and 9 in `ctr.configure` u8 #2 and `ctr.arm` u8 #1 (and in the last byte of `ctr.read`), and show
**0x0b for a finite pulse train** (9 + finite, M).

Fields that change between the captured modes (M: differential, 6 tasks):

| field | count edges (PFI0) | frequency (PFI1, 1 counter) | pulse train (continuous) | finite pulse train (N=100) |
|---|---|---|---|---|
| configure u32 0..8 | all 0 | 1,0,1,0,0,0,0,0,0 | 2,0,**L**,**H**,0,0,0,0,0 | 2,2,0,**2N**,0,0,L,H,H |
| configure u8 0..3 | 0,1,0,1 | 0,3,1,0 | 1,9,0,1 | 0,0x0b,0,1 |
| m0b (16 × u16) | [7]=2, [12]=1 | [3]=1, [7]=3, [9]=2, [12]=1 | [0]=1, [2]=1, [4]=1 | [0]=1,[2]=1,[4]=1,[5]=1,[6]=2,[15]=2 |
| m0c | 1 | 1 | 0 | 0 |
| m10 | 0… | [2]=1, [4]=1 | [0]=1, [3]=1, [4]=1 | 0… |
| arm u32,u32,u16,u8×4 | 0,0,0,1,1,0,0 | 1,0,0,3,0,0,0 | L,H,0,9,1,0,1 | 0,2N,0,0x0b,1,0,0 |

- **Pulse train loads (H)**: L = low (idle) time and H = high time in ticks of the 100 MHz
  timebase: 1 kHz 50 % → 50000/50000; 10 kHz 25 % duty → L = 7500, H = 2500; 100 Hz → 500000
  each. Frequency = 100 MHz / (L + H), duty = H / (L + H). Finite: count field = 2·N (number of
  half-periods).
- **Frequency / period (M)**: the one-counter method counts the 100 MHz timebase (source select
  0x1e) between consecutive rising edges of the gate (PFI1). `ctr.read` returns the tick count per
  period: period = count / 100 MHz, frequency = 100 MHz / count. Period (app 2) differs from
  frequency (app 3) only in the application code (same expert, nistc3ru 0x180073640
  instantiated with 2 or 3) (H for the shared expert, M for identical wire fields).
- **Pulse width (4), semi-period (5), two-edge separation (6), encoders (7/0x0e)**: not captured.
  They use their own experts (nistc3ru 0x18008da80, 0x18008fc90, 0x180096880, 0x180079680);
  expect the same message sequence with the application code changed and different m0b/m10 gate
  options. Default terminals (DAQmx, `capabilities.txt`): pulse width / period / frequency gate =
  PFI1; two-edge first = PFI2 (aux), second = PFI1 (gate); encoder A = PFI0 (source), B = PFI2
  (aux), Z = PFI1 (gate); count-edges direction = PFI2, reset = PFI1. **Capture each once before
  implementing** (L for field values).
- **Buffered (implicit/sample-clocked) counter input**: not captured; the counter has a bulk stream
  path (`iCtrBulkTransferManager` in nistc3rk) that presumably uses a stream endpoint like AI (L).

### 7.4 Routing for counters and PFI lines (H for the id tables, M for the value meanings)

Route ids come from two NI routing maps inside niraptrk (counter map 0x1800f1f30, trigger/PFI map
0x1800f21e0). They are written with `route.connect([id…], [value…])`.

| id | meaning | values seen |
|---|---|---|
| 0x587c6efe (+ 0x37cf14dd = 0) | ctr0 **source** select | PFI0 = 1, 100 MHz timebase = 0x1e, FREQOUT = 0x2b |
| 0x097d2c54 (+ 0x11f9be17 = 0) | ctr0 **gate** select | PFI1 = 2 (so PFIn = n + 1, M) |
| 0xc7c20613 (+ 0x88fbba44 = 0) | ctr0 (aux/second gate?) | 0x2f in every task (none, M) |
| PFI n output select: 0xc51cc492, 0xb21bf404, 0x2b12a5be, 0x5c159528, 0xc271008b (n = 0..4) | what drives the pin | 0 = nothing (input), 0x0d = ctr0 output, 0x0f = FREQOUT |
| PFI n output enable: 0x8e309935, 0xf937a9a3, 0x603ef819, 0x1739c88f, 0x895d5d2c | pin direction | 1 = drive, 0 = input |

Input tasks set the used PFI to (select 0, enable 0); output tasks set PFI3 (ctr0 out) or PFI4
(FREQOUT) to (0x0d or 0x0f, 1). The 12 ids zeroed in the device init are the same register
families for the other lines (M). Source values for the 20 MHz and 100 kHz timebases are not
known yet (capture a count-edges task with those sources).

### 7.5 FREQOUT (H for the arg mapping, M for the timebase)

`freqout.configure(divider, a, b)`: output = timebase / divider (STC3 FREQOUT: divider 1..16).
(a, b) is a one-hot timebase selector derived from a 3-value setting (nistc3rk 0x18003e390):
setting 0 → (0, 0), 1 → (0, 1), 2 → (1, 0). DAQmx used (1, 0) with divider 10 for 10 kHz and
divider 1 for 100 kHz, so (1, 0) = 100 kHz timebase. The other two are presumably 20 MHz and
10 MHz (L). The output appears on PFI4 after `route.connect([0xc271008b, 0x895d5d2c], [0x0f, 1])`.

## 8. DMM (H — niraptru tES51981DMMUserSupervisor 0x18048c2b0; 30/30 captured readings bit-exact)

- Measurement: `dmm.set_mode(mode, 0)` (takes ~0.5–1.5 s: the meter chip settles), then
  `dmm.read(1, 10000)` → 11-byte ASCII packet from the isolated meter chip (Cyrustek ES51981).
  The kernel just forwards the 11 bytes (control code 600 in niraptrk 0x1800f02a0); **all
  conversion is on the host**. AC RMS is computed by the meter chip (the host only scales the
  displayed digits), so AC readings are as slow as DC ones.
- Packet: `R D1 D2 D3 D4 F S x x CR LF`:
  - reading = D1 + D2/10 + D3/100 + D4/1000 (digits are ASCII '0'..'9'), sign from S bit 2,
    **overload** = S bit 0 (driver returns NaN and a warning);
  - R low nibble = range digit, F = function byte, scaled per (F, R):

| F | function | multiplier by range digit | unit |
|---|---|---|---|
| 0x3b ';' | voltage (DC or AC per mode) | 0: ×1, 1: ×10, 2: ×100, 3: ×1000, 4: ×0.1 | V |
| 0x3f '?' | mA ranges | 0: ×10 mA, 1: ×100 mA | A |
| 0x30 '0' | A range | 0: ×1, 1: ×10 | A |
| 0x3d '=' | µA ranges | 0: ×100 µA, 1: ×1000 µA | A |
| 0x33 '3' | resistance | 0: ×100, 1: ×1 k, 2: ×10 k, 3: ×100 k, 4: ×1 M, 5: ×10 M | Ω |
| 0x31 '1' | diode | ×1 | V |
| 0x35 '5' | continuity | (reading − D4/1000) × 100 (last digit dropped) | Ω |

  - Range meaning (DMM ranges the driver maps each (F, R) to, used to pick the calibration slot):
    V DC: R 0 = 2 V, 1 = 20 V, 2 = 60 V, 4 = 200 mV; V AC: 0 = 2 V, 1 = 20 V, 4 = 200 mV;
    mA: 0 = 20 mA, 1 = 200 mA; A: 0 = 1 A; Ω: 0..5 = 200 Ω … 20 MΩ. AC vs DC for V and A comes
    from the requested measurement type (DAQmx VoltageRms/CurrentRms), not from the packet.
  - Then `value = gain · value + offset` with the slot of 5.3 (no calibration for diode/continuity).
- Function groups (H — niraptru tDMMChannelExpert 0x180491190, keyed on the public DAQmx
  measurement-type constants): 1 = DC voltage (DAQmx Voltage 10322; also thermocouple), 2 = DC
  current (10134), 3 = AC voltage (VoltageRMS 10350), 4 = AC current (CurrentRMS 10351),
  5 = resistance (10278; also RTD/thermistor). The same 1..5 codes are the "type" field of the
  driver's range descriptors (the kernel uses them to pick the calibration slot, 5.3). RMS types
  switch the channel coupling to AC (DAQmx 10045), others to DC (10050).
- **`set_mode(mode, hw_autorange)`** (H): the mode byte is the index of the matching entry in
  the driver's DMM capability table (niraptru 0x1805c0c00, entries = range descriptor, excitation
  flag, mode). Captures confirm every entry up to 15:

| mode | function / range | mode | function / range |
|---|---|---|---|
| 0 | DC V 2 V | 11 | AC A 20 mA |
| 1 | DC V 20 V | 12 | AC A 200 mA |
| 2 | DC V 60 V | 13 | AC A 1 A |
| 3 | DC V 200 mV | 14 | Ω 200 Ω (with current excitation) |
| 4 | AC V 2 V | 15 | Ω 2 kΩ |
| 5 | AC V 20 V | 16–19 | Ω 20 kΩ, 200 kΩ, 2 MΩ, 20 MΩ (table only) |
| (6) | unused (AC 60 V not offered) | **20** | **diode**: ±2 V DC range *with* excitation (table only) |
| 7 | AC V 200 mV | **21** | **continuity**: 200 Ω range *without* excitation (table only) |
| 8, 9, 10 | DC A 20 mA, 200 mA, 1 A | | |

  All 30 DMM readings in the captures (DC/AC V, DC/AC A, Ω overload) are reproduced bit-exactly by
  `tools/drv_dmm.py`.
- **Second byte = hardware autorange** (H for the data flow): the DMM device expert copies the
  channel's attribute **0x2FF8** (u32) into the second settings field, which the kernel sends as the
  second `set_mode` byte (niraptru 0x180496460 → kernel 0x1800f04c0 → 0x1800f1300). 0 in all
  captures. With autorange the meter chooses the range itself; the host already scales every packet
  with the range digit contained in the packet, and when 0x2FF8 ≠ 0 the driver skips its
  user-calibration polynomial (0x180494db0). Which mode byte accompanies autorange is not known
  (probably any range of the function, L): capture once.
- **Continuity** = attribute **0x3051** (u32 boolean) on a resistance channel: the expert then
  looks the caps table up *without* the excitation flag, which only matches mode 21 (H, niraptru
  0x180491ad0 / 0x1804e7d80). The driver rejects continuity unless the function is resistance with
  the 200 Ω range, and rejects continuity together with hardware autorange (0x18048fae0) (H).
  Readings come back with function byte '5' (section above).
- **Diode** = voltage channel, 2 V range, *internal current excitation* (AI.Excit.Src internal,
  AI.Excit.Val ≠ 0): matches mode 20 (H for the table; the excitation-flag test is
  "excitation source ≠ none and value ≠ 0 and continuity off", 0x180491ad0). Readings come back
  with function byte '1'.
- **Why DAQmx returned -200233** for 0x3051/0x2FF8: both are ordinary u32 attributes of the myDAQ
  DMM channel expert (declared in the channel expert constructor 0x18048e080, defaults 0 set in
  0x1803b4f00), so the rejection happens in DAQmx's generic attribute layer before the myDAQ code
  (L: likely an access-path issue, e.g. set through a non-DMM channel, wrong data type through the
  variadic C setter, or attribute visibility for this channel type). For libmydaq this does not
  matter: the device commands are `set_mode(20, 0)` (diode), `set_mode(21, 0)` (continuity) and
  `set_mode(m, 1)` (autorange). Mark them unverified until one capture (ELVISmx GUI) confirms.

## 9. Digital I/O (H — stub types + niraptrk 0x180041b40 / 0x180042310; captures)

- Object 0x0530 instance 1 = port0 (8 lines). `set_direction(mask, 1 = output / 0 = input)`,
  `write(mask, value)`, `read → u32` (the driver masks the result with the requested lines;
  bits 8..15 of the raw value are another 8-bit group — the kernel builds 16-bit masks as
  `hi << 8 | lo` for the PFI variant — so use bits 0..7 only; the 0xff seen in bits 8..15 is not
  port0 data).
- DAQmx's DO sequence (preset 0, then switch direction, then drive) avoids glitches; keep it.
- PFI/counter use of the DIO lines goes through routing (6.3) and the counter object (6.2).

## 10. Suggestions for the USB agent (captures that settle the remaining unknowns)

1. ~~Continuous AI~~ and ~~AI rate sweep~~: done by the USB agent (captures `ai0*-cont-*`,
   `ai0-r10-finite-{10,10k,12345,100k,200k}`); results are folded into 3.3/3.4. Remaining: one
   finite AI at 7 kS/s (divisor rounding) and a continuous capture that reads steadily for a few
   seconds without overflow (read_pos cadence in the normal case).
3. **Audio in** (L/R, finite): scan-list bits (expect channel type 3 = RSE, maybe different
   channel numbers) and coupling.
4. **AO timed**: finite and continuous (regenerated and streamed), 1 and 2 channels, audio out:
   which bulk OUT endpoint, `out_timing.configure` fields, `out_fifo.write` usage, `stream.out_credit`.
   Needs QEMU with a larger pcap DATA_LEN to see full bulk payloads.
5. **DMM**: done for DC/AC V, DC/AC A, Ω 200/2k (§8). Still needed: Ω 20 k–20 M, and diode,
   continuity, hardware autorange from the ELVISmx DMM GUI (expected `set_mode` 20, 21, (m, 1)).
6. **Counter**: count edges / pulse train / frequency (timeout) / FREQOUT are captured (§7). Still
   needed, one each: frequency and period *with a signal on PFI1*, pulse width, semi-period,
   two-edge separation, linear and angular encoder, count edges from the 20 MHz / 100 kHz
   timebases, a pulse-train frequency change while running, and FREQOUT at a rate needing
   another timebase (e.g. 1 MHz).
7. **Abort**: start a continuous AI task and stop it abruptly / let a read time out → vendor
   control requests of 1.2 (bRequest value).
