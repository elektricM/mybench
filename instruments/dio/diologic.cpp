#include "diologic.h"

#include <QStringList>

#include <bit>

using namespace Qt::StringLiterals;

namespace mybench::instruments::dio {

uint8_t presetMask(Preset preset)
{
    switch (preset) {
    case Lines0to3: return 0x0F;
    case Lines4to7: return 0xF0;
    case Lines0to7: return 0xFF;
    case NoLines:
    case CustomLines: break;
    }
    return 0;
}

Preset presetOf(uint8_t mask)
{
    switch (mask) {
    case 0x0F: return Lines0to3;
    case 0xF0: return Lines4to7;
    case 0xFF: return Lines0to7;
    case 0x00: return NoLines;
    default: return CustomLines;
    }
}

int lineCount(uint8_t mask)
{
    return std::popcount(unsigned(mask));
}

uint8_t scatter(int value, uint8_t mask)
{
    uint8_t levels = 0;
    int bit = 0;
    for (int line = 0; line < kLines; ++line) {
        if (!(mask & (1 << line)))
            continue;
        if (value & (1 << bit))
            levels |= uint8_t(1 << line);
        ++bit;
    }
    return levels;
}

int gather(uint8_t levels, uint8_t mask)
{
    int value = 0;
    int bit = 0;
    for (int line = 0; line < kLines; ++line) {
        if (!(mask & (1 << line)))
            continue;
        if (levels & (1 << line))
            value |= 1 << bit;
        ++bit;
    }
    return value;
}

int bitOfLine(int line, uint8_t mask)
{
    if (line < 0 || line >= kLines || !(mask & (1 << line)))
        return -1;
    return std::popcount(unsigned(mask & ((1u << line) - 1)));
}

std::vector<int> sequence(Pattern pattern, int width, Direction direction)
{
    std::vector<int> values;
    if (width <= 0)
        return values;
    const int max = maxValue(width);
    switch (pattern) {
    case Manual:
        break;
    case Ramp:
        for (int v = 0; v <= max; ++v)
            values.push_back(v);
        break;
    case Alternating:
        values = {0x55 & max, 0xAA & max};
        break;
    case WalkingOnes:
        for (int k = 0; k < width; ++k)
            values.push_back(1 << (direction == Left ? k : width - 1 - k));
        break;
    }
    return values;
}

int applyAction(Action action, int value, int width, Direction direction)
{
    if (width <= 0)
        return 0;
    const int max = maxValue(width);
    value &= max;
    switch (action) {
    case Toggle:
        return ~value & max;
    case Rotate:
        return direction == Left ? ((value << 1) | (value >> (width - 1))) & max
                                 : (value >> 1) | ((value & 1) << (width - 1));
    case Shift:
        return direction == Left ? (value << 1) & max : value >> 1;
    }
    return value;
}

QString formatValue(int value, Radix radix, int width, bool padBinary)
{
    value = std::max(0, value);
    switch (radix) {
    case Decimal:
    case SiNotation:
        return QString::number(value);
    case Hex:
        return QString::number(value, 16).toUpper();
    case Octal:
        return QString::number(value, 8);
    case Binary: {
        const QString text = QString::number(value, 2);
        return padBinary && width > text.size() ? text.rightJustified(width, u'0') : text;
    }
    }
    return QString::number(value);
}

int parseEntry(const QString& text, Radix radix, int maximum)
{
    QString trimmed = text.trimmed();
    if (radix == Hex && trimmed.startsWith(u"0x", Qt::CaseInsensitive))
        trimmed.remove(0, 2);
    if (trimmed.isEmpty())
        return -1;
    const int base = radix == Hex ? 16 : radix == Octal ? 8 : radix == Binary ? 2 : 10;
    bool ok = false;
    const qlonglong value = trimmed.toLongLong(&ok, base);
    return ok && value >= 0 && value <= maximum ? int(value) : -1;
}

QString radixLetter(Radix radix)
{
    switch (radix) {
    case Decimal: return u"d"_s;
    case Hex: return u"x"_s;
    case Octal: return u"o"_s;
    case Binary: return u"b"_s;
    case SiNotation: return u"p"_s;
    }
    return {};
}

QString linesLabel(uint8_t mask)
{
    switch (presetOf(mask)) {
    case Lines0to3: return u"0 - 3"_s;
    case Lines4to7: return u"4 - 7"_s;
    case Lines0to7: return u"0 - 7"_s;
    case NoLines: return u"none"_s;
    case CustomLines: break;
    }
    QStringList lines;
    for (int line = 0; line < kLines; ++line) {
        if (mask & (1 << line))
            lines << QString::number(line);
    }
    return lines.join(u", "_s);
}

}  // namespace mybench::instruments::dio
