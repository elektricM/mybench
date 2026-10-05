// Display conversions of the Spectrum Analyzer: unit labels (dsa.md §3.2 table), the
// fundamental-power indicator (§3.4), readout text and the Auto/Manual magnitude scale (§3.5).
// Pure functions, used on the UI thread and in the unit tests.
#pragma once

#include <mydaq/dsp/spectrum.h>

#include <QString>

#include <optional>
#include <span>
#include <vector>

namespace mybench::instruments::dsa {

// Y-axis unit of the spectrum ("Magnitude (<unit>)"):
//   dB:     dBVrms, dBVpk, dBVrms/√Hz, dBVpk/√Hz     dBm: dBm, dBm/Hz
//   Linear: Vrms², Vpk², Vrms²/Hz, Vpk²/Hz   (power, as ELVISmx, which writes "Vrms^2")
QString magnitudeUnit(const mydaq::dsp::SpectrumFormat& format);

// Unit of "Fundamental Frequency Power (<unit>)": dBVrms/dBVpk, dBm, or Vrms²/Vpk² in Linear
// (a power, never a density).
QString powerUnit(mydaq::dsp::SpectrumUnit unit, mydaq::dsp::SpectrumMode mode);

// dsp::formatSpectrum() with Linear as V^2 (power) instead of the core's amplitude.
std::vector<double> formatSpectrum(const mydaq::dsp::PowerSpectrum& spectrum, const mydaq::dsp::SpectrumFormat& format);

// A tone power (V² RMS) in the units of powerUnit(): Peak doubles it; Linear shows the power
// itself; dB 10 log10(P / 1 V²); dBm 10 log10(P / 50 Ω / 1 mW). Logarithmic units floor at -400.
double powerValue(double powerVrms2, mydaq::dsp::SpectrumUnit unit, mydaq::dsp::SpectrumMode mode);

// A tone power as an amplitude level in the spectrum's (non-density) unit, e.g. for markers and
// the harmonics/peaks tables: dBVrms, dBm or Vrms² (the power).
double levelValue(double powerVrms2, mydaq::dsp::SpectrumUnit unit, mydaq::dsp::SpectrumMode mode);

// Level of `power` relative to `reference` in dB (dBc); 0 if undefined.
double relativeDb(double power, double reference);

// Fixed-decimal text that never shows NaN/infinity ("0.00" instead), with U+2212 minus.
QString fixedText(double value, int decimals = 2);
// Value + unit for readouts and tables: fixed decimals for logarithmic units ("-3.01 dBVrms"),
// engineering notation for linear ones ("707.1 mVrms", "500.0 mVrms²").
QString levelText(double value, const QString& unit, bool logarithmic);

bool isLogarithmic(mydaq::dsp::SpectrumUnit unit);

struct ValueRange {
    double minimum = 0;
    double maximum = 0;
    bool operator==(const ValueRange&) const = default;
};

// Manual Maximum/Minimum defaults (ELVISmx): dB/dBm 10 / -120, Linear 1 / 0.
ValueRange defaultManualRange(mydaq::dsp::SpectrumUnit unit);

// Auto scale: fits `values` with nice steps (10 dB in logarithmic units, 1-2-5 in Linear); in
// logarithmic units the bottom is limited to top - 160 dB so the -400 dB floor of empty lines
// does not flatten the view. `previous` adds hysteresis: the range grows at once but shrinks
// only when the data moved by more than one step, so a fluctuating noise floor doesn't make the
// axis jump. Non-finite values are ignored.
ValueRange autoRange(std::span<const double> values, mydaq::dsp::SpectrumUnit unit,
                     const std::optional<ValueRange>& previous = std::nullopt);

}  // namespace mybench::instruments::dsa
