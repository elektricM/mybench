# Featured Applications and Instrument Launcher - where MYBENCH provides them

The ELVISmx Instrument Launcher shows, after the instruments, six small "Featured Applications".
This file maps each one to the MYBENCH panel that provides the same capability. Behaviour of the
ELVISmx apps is described at the level of what the user sees; all wording is ours.

| ELVISmx | What it does | In MYBENCH |
|---|---|---|
| 1. Instrument Launcher | tile launcher with Favorites, Labs, My Files and Resources pages | nav rail and Home tiles (`app.md 18`); Favorites strip, a Resources entry under Help and recent files on Home; no tiles for external programs |
| 2. DC Level | sets a constant voltage on AO 0 or AO 1 (-10 ... +10 V); Stop returns to 0 V | **DC** mode in the Waveform Generator channel cards (Level field + slider, 0.01 V steps, Stop returns to 0 V); also the command palette ("Set AO 0 to ... V") |
| 3. Data Logger | slow logging of AI voltages (1 ... 1000 S/s) to a chart and a text file | `log` panel, with a native file layout and an optional ELVISmx-compatible layout ([log.md](log.md) 8) |
| 4. Octave Analyzer | real-time third-octave band levels with exponential averaging and A/B/C weighting | Octave mode of `dsa` ([dsa.md](dsa.md) 14) |
| 5. Audio Equalizer | live three-band tone control from an input to an output | `aeq` panel ([aeq.md](aeq.md)) |
| 6. 8-Channel Oscilloscope | simple multi-channel scope; on a myDAQ nothing beyond the Oscilloscope | covered by `scope`; `High resolution` record mode (scope.md 4.2) and a same-scale lock for all channels |
| 7. Digital Waveform Viewer | ELVIS II only: hardware-timed capture of DIO lines with browsing and cursors | covered by the software-timed Timeline of `dio` (dio.md 4); the myDAQ has no hardware-timed digital input |

Common to the ELVISmx featured apps: a fixed window with a title band, a device selector, Start /
Stop and a Help side panel. MYBENCH uses its normal panel layout and run bar instead.
