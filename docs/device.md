# NI myDAQ: capabilities as NI-DAQmx exposes them

Source: `captures/capabilities.txt`, produced by `tools/daqmx-scripts/capabilities.ps1` (DAQmx 25.8,
every Device and physical-channel property, the terminal list, and the default terminal of each
counter function from verified but never started tasks). Unit under test: serial 57463238
(0x036CD1C6). Protocol-level facts are in `docs/protocol.md`.

"libmydaq" column: **must** = needed for ELVISmx-equivalent instruments; **later** = DAQmx
feature we should expose eventually (sigrok/API users); **no** = not needed.

## Identity and calibration

| Property | Value |
|---|---|
| ProductType / ProductNumber / Category | NI myDAQ / 30043 (0x755b is the USB PID) / NIElvis |
| SerialNumber | 57463238 = 0x036CD1C6 (also the USB iSerial string) |
| BusType | USB (high speed); NumberOfDmaChannels 0 |
| External / self calibration date | 6/11/2026 8:55:53 AM (local), recommended interval 12 months |
| IsSelfCalibrationSupported | False (calibration constants are only read, from the device's calibration memory) |
| UserDefinedCalibrationInfo | "NI" (max 5 chars) |
| Temperature sensor | none |

## Analog input

| Item | Value | libmydaq |
|---|---|---|
| Channels | ai0, ai1 (Differential only); audioInputLeft/Right (RSE only, DAQmx rejects ±10 V on them); dmm | must |
| Voltage ranges | ±10 V, ±2 V (ai0/ai1); audio ±2 V | must |
| Couplings | the device lists AC, DC, but DAQmx accepts only AC on the audio inputs (DC is rejected; ai0/ai1 coupling not tested); audio inputs are timed-only (no on-demand reads, -201087) | must |
| Measurement types | ai0/ai1: Voltage (Current, Resistance, Thermocouple via scaling); dmm: Voltage, VoltageRms, Current, CurrentRms, Resistance | must (voltage); later (the scaled types) |
| Max rate | 200 kS/s single channel; 400 kS/s aggregate (2 × 200 kS/s) | must |
| Min rate | 0.023283064370808 S/s = 100 MHz / 2^32 (32-bit divider on the 100 MHz timebase) | must (rate coercion) |
| Simultaneous sampling | **No** (channels are multiplexed: there is an inter-channel skew) | must (report skew) |
| Sample modes | Finite, Continuous | must |
| Triggers | none (AITriggerUsage None, no analog/digital/time trigger) | no (software trigger in the app) |
| Resolution | 16-bit signed codes (from captures) | must |
| TEDS | not supported | no |

## Analog output

| Item | Value | libmydaq |
|---|---|---|
| Channels | ao0, ao1, audioOutputLeft, audioOutputRight (all RSE); audio outputs are timed-only (no static writes, -201087) | must |
| Ranges | ±10 V, ±2 V (DAQmx lists both; audio is ±2 V) | must |
| Output type | Voltage only | must |
| Max / min rate | 200 kS/s / 0.023283 S/s (100 MHz / 2^32) | must |
| Sample clock | supported; modes Finite, Continuous | must |
| Triggers | none | no |

## Digital I/O

| Item | Value | libmydaq |
|---|---|---|
| Port | port0, 8 lines (line0..7), DIPortWidth = DOPortWidth = 8 | must |
| Timing | static only: no DI/DO sample clock, no change detection, no triggers, no digital filters | must (software timed) |
| Direction | per line (lines not written as outputs stay inputs) | must |
| PFI | DAQmx terminals `/myDAQ1/PFI0..7` are the same 8 screw terminals (labelled DIO0..7 on the myDAQ); PFIn = DIOn is the expected mapping. DAQmx refuses a DI task on lines reserved by a counter, so the confirmation is a native test (docs/protocol.md 14, P4) | later |

## Counters / timers

| Item | Value | libmydaq |
|---|---|---|
| Channels | ctr0 (input and output), freqout (output only) | later |
| Counter size / timebase | 32 bit, max timebase 100 MHz | later |
| CI measurement types | CountEdges, Frequency, Period, TwoEdgeSeparation, PulseWidth, Angular/Linear encoder, Pulse (frequency/time/ticks) | later |
| CI sample clock | not supported; modes Finite, Continuous | later |
| CO output types | ctr0: PulseFrequency, PulseTime, PulseTicks; freqout: PulseFrequency | later |
| FREQOUT | 4-bit divider (1..16) of 20 MHz, 10 MHz or 100 kHz (from the DAQmx error text) | later |
| Timebases | 100MHzTimebase, 20MHzTimebase, 100kHzTimebase (advanced) | internal |

Default terminals (resolved by DAQmx for ctr0), i.e. which DIO line each counter signal uses:

| Function | Signal → terminal (= DIO line) |
|---|---|
| CountEdges | edges → PFI0; count direction → PFI2; count reset → PFI1 |
| Frequency / Period / PulseWidth | input → PFI1 |
| TwoEdgeSeparation | first → PFI2, second → PFI1 |
| Linear/angular encoder | A → PFI0, B → PFI2, Z → PFI1 |
| CO pulse train (ctr0) | output → PFI3 |
| FREQOUT | output → PFI4 |

Counter terminal names: Ctr0Source, Ctr0Gate, Ctr0Aux, Ctr0A, Ctr0B, Ctr0Z, FrequencyOutput.

## DMM (channel `dmm`)

DAQmx has no DMM-specific range properties; the ranges below are those of the myDAQ (and
ELVISmx DMM). DC V is decoded (`docs/protocol.md` section 9), the rest is stage 2. The converter is a
Cyrustek ES51981 behind the device's command channel, isolated from AI/AO.

| Function | Ranges | libmydaq |
|---|---|---|
| DC voltage | 60 V, 20 V, 2 V, 200 mV (verified mode bytes 2, 1, 0, 3) | must |
| AC voltage (RMS) | 20 V, 2 V, 200 mV | must |
| DC / AC current | 1 A, 200 mA, 20 mA | must |
| Resistance | 20 MΩ … 200 Ω | must |
| Diode, continuity | 2 V | must |

## Other

- Supplies (+5 V, ±15 V) are fixed; no software control.
- No accessory detection, no switch topology, no time triggers, no hardware TEDS, no watchdog.
