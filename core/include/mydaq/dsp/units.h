// Engineering notation with SI prefixes: "1.234 kHz", "500 mV", "-12.3 µA", "4.7 kΩ".
// Prefixes f p n µ m k M G T; the micro sign is U+00B5, ohm is U+03A9.
#pragma once

#include <QString>

#include <optional>

namespace mydaq::dsp {

// Formats `value` with `significantDigits` digits and the prefix that puts the mantissa in [1, 1000).
// trimZeros drops trailing fractional zeros ("500 mV" instead of "500.0 mV"). Zero gives "0 V";
// NaN/infinity give "--- V". An empty unit omits it ("1.5 k").
QString formatEng(double value, const QString& unit = {}, int significantDigits = 4, bool trimZeros = true);

// Parses "1k", "2.5m", "10u", "1 MHz", "-3.3 V", "4,7 kΩ", "1e-3", "100 ohm". The decimal separator
// may be '.' or ','. Prefix letters are case sensitive (m = milli, M = mega) except "K" for kilo;
// "u", "µ" (U+00B5) and "μ" (U+03BC) mean micro. If `unit` is given, the text may end with it
// ("ohm"/"ohms" also match "Ω") and nothing else may follow the prefix; otherwise any trailing
// unit letters are accepted after an optional prefix. Returns nullopt for invalid text.
std::optional<double> parseEng(const QString& text, const QString& unit = {});

}  // namespace mydaq::dsp
