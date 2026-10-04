# The myDAQ in other tools: PulseView, sigrok-cli, ngscopeclient

Besides MYBENCH, the myDAQ's analog inputs can be used live from two open-source tools. Both
integrations use this repository's own USB driver (`libmydaq`), so no NI software is involved.

| Tool | How | Where |
|---|---|---|
| PulseView, sigrok-cli | libsigrok hardware driver `ni-mydaq` | [`integrations/sigrok/`](../integrations/sigrok/README.md) |
| ngscopeclient (libscopehal) | `mybench-scopehal-bridge`, a server that ngscopeclient's `digilent` driver connects to | [`integrations/scopehal/`](../integrations/scopehal/README.md) |

**One program at a time.** The myDAQ can be opened by only one program: close MYBENCH (or stop
the bridge, or PulseView) before another one uses it. The others then report the device as busy.

## What is available

| | PulseView / sigrok-cli | ngscopeclient |
|---|---|---|
| Channels | AI0, AI1 (or Audio In L/R instead, not mixed with AI) | C1 = AI0, C2 = AI1 |
| Sample rate | 1 S/s … 200 kS/s per channel (1-2-5 steps) | 10 S/s … 200 kS/s (1-2-5 steps) |
| Range | per channel: `2V`/div = ±10 V, `400mV`/div = ±2 V | chosen from the vertical scale and offset: ±2 V when the view fits, else ±10 V |
| Record | sample count or time limit, or continuous | memory depth 1 k … 1 M samples |
| Trigger | none (free-running capture) | software edge trigger (rising, falling, any; level, position), Start / Single / Force / Auto |
| Simulator | no (needs the real device) | `--sim` (AI0: 1 kHz sine, AI1: 200 Hz square) |

Not available in either tool: the digital lines (the myDAQ's DIO is software-timed and cannot
share a sample clock with the analog inputs), the outputs (AO, audio out), the DMM and the
counter. Use MYBENCH for those.

## PulseView and sigrok-cli

libsigrok has no plug-in mechanism, so the driver is compiled into libsigrok:
`integrations/sigrok/apply.sh <libsigrok-src> <prefix>` builds and installs libmydaq into
`<prefix>`, adds the driver to a libsigrok git checkout, and builds and installs that libsigrok.
PulseView and sigrok-cli built against that prefix pick up the new library without being
rebuilt. Then:

- PulseView: **Connect to a Device** → driver **NI myDAQ** → **Scan for devices** → select it.
  Channel ranges are in the channel popup (V/div).
- sigrok-cli: `sigrok-cli -d ni-mydaq -C AI0,AI1 -c samplerate=100k --samples 100k -o capture.sr`

Details, options and troubleshooting: [integrations/sigrok/README.md](../integrations/sigrok/README.md).

## ngscopeclient

Stock ngscopeclient builds work unchanged. Build MYBENCH (target `mybench-scopehal-bridge`, it is
not installed with the app yet), then:

1. Start the bridge: `mybench-scopehal-bridge` (first myDAQ on USB; `--sim` for the simulated one).
   It listens on 127.0.0.1 only, ports 5025 (commands) and 5026 (waveforms); `--bind 0.0.0.0`
   serves the network.
2. In ngscopeclient: **Add → Oscilloscope → Connect**, driver `digilent`, transport `twinlan`,
   path `localhost:5025:5026`. Or from a shell: `ngscopeclient mydaq:digilent:twinlan:localhost:5025:5026`.
3. Press **Start** (normal trigger), **Single**, or **Auto** (free-runs when nothing triggers).

The trigger runs in the bridge on the continuous AI stream. Details:
[integrations/scopehal/README.md](../integrations/scopehal/README.md).

## Tests

```sh
ctest --test-dir build -L "scopehal|sigrok" -LE hw                     # simulator / no device
MYDAQ_HW_TESTS=1 ctest --test-dir build -R "scopehal_bridge_hw|sigrok_ni_mydaq_hw"   # real myDAQ, AI only
```

The sigrok tests run only when a `sigrok-cli` that lists `ni-mydaq` is found
(`MYBENCH_SIGROK_CLI` cache variable). Hardware tests hold the device through `tools/devlock.sh`
and need nothing wired.
