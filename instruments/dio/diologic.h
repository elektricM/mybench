// Pure Digital I/O logic (no Qt objects, no device): line sets and ELVISmx group presets,
// right-justified bit mapping, Digital Writer patterns and actions, numeric value formatting.
// Unit-tested in instruments/dio/tests/.
//
// Sources: docs/instruments/algorithms/dio.md (ELVISmx behaviour, authoritative) and
// docs/instruments/dio.md (functional spec).
#pragma once

#include <QString>

#include <cstdint>
#include <vector>

namespace mybench::instruments::dio {

inline constexpr int kLines = 8;
inline constexpr int kMinimumIntervalMs = 10;
inline constexpr int kMaximumStepIntervalMs = 10000;
inline constexpr int kMaximumSampleIntervalMs = 1000;
inline constexpr int kDefaultIntervalMs = 100;   // ELVISmx Writer step (algorithms doc 3)

// "Lines to Read" / "Lines to Write" presets (ELVISmx: 0 - 3, 4 - 7, 0 - 7; MYBENCH adds None
// and Custom).
enum Preset { Lines0to3, Lines4to7, Lines0to7, NoLines, CustomLines };
uint8_t presetMask(Preset preset);        // Custom -> 0
Preset presetOf(uint8_t mask);            // exact preset or Custom (0 -> NoLines)

// Digital Writer patterns (ELVISmx "Pattern" ring).
enum Pattern { Manual, Ramp, Alternating, WalkingOnes };
enum Action { Toggle, Rotate, Shift };
enum Direction { Left, Right };

int lineCount(uint8_t mask);
inline int maxValue(int width) { return width <= 0 ? 0 : (1 << width) - 1; }

// Right-justified mapping: bit k of `value` drives / is read from the k-th lowest line of `mask`
// (for the presets: 0 - 3 -> bits 0..3, 4 - 7 -> bits 0..3 on lines 4..7, 0 - 7 -> 0..7).
uint8_t scatter(int value, uint8_t mask);   // value -> line levels (only bits of `mask`)
int gather(uint8_t levels, uint8_t mask);   // line levels -> value
// Bit index of `line` within `mask` (-1 if the line is not in the mask).
int bitOfLine(int line, uint8_t mask);

// One full cycle of a predefined pattern for `width` lines:
//   Ramp 0, 1, ..., 2^width - 1;  Alternating 0x55 & max, 0xAA & max (4 lines: 0x5, 0xA);
//   Walking 1's 1, 2, 4, ..., 2^(width-1) (Left) or the reverse (Right).
// Manual: empty. width <= 0: empty.
std::vector<int> sequence(Pattern pattern, int width, Direction direction);

// Actions on the manual pattern value within `width` bits (spec 3.2):
//   Toggle ~v; Rotate 1 bit circular; Shift 1 bit logical (zero fill).
int applyAction(Action action, int value, int width, Direction direction);

// Numeric value text in a radix (ELVISmx "Numeric Value": right-justified, no leading zeros,
// uppercase hex): Decimal "13", Hex "D", Octal "15", Binary "1101" (padded to `width` digits
// when `padBinary`), SI notation "13".
enum Radix { Decimal, Hex, Octal, Binary, SiNotation };
QString formatValue(int value, Radix radix, int width = 0, bool padBinary = true);
// A value typed into the Pattern Number field, read in `radix` (SI notation = decimal, a leading
// "0x" is allowed for hex). ELVISmx does not accept a value above the maximum: the previous value
// stays. Returns -1 for an empty / malformed / out-of-range entry.
int parseEntry(const QString& text, Radix radix, int maximum);
// Radix prefix letter as LabVIEW shows it: d, x, o, b, p.
QString radixLetter(Radix radix);

// Line list label: "4 - 7" for the presets, "1, 4, 6" otherwise, "none" for 0.
QString linesLabel(uint8_t mask);

}  // namespace mybench::instruments::dio
