// Units: engineering notation for QML (wraps mydaq::dsp::formatEng / parseEng).
//
//     Units.format(1234.5, "Hz")        -> "1.235 kHz"
//     Units.format(0.5, "V", 3, false)  -> "500 mV"
//     Units.parse("1.5k", "Hz")         -> 1500      (NaN when invalid)
//     Units.fixed(3.14159, "V", 2)      -> "3.14 V"
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace mybench {

class Units : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Units* instance();
    static Units* create(QQmlEngine*, QJSEngine*);

    // SI-prefixed, `digits` significant digits; trimZeros drops trailing fractional zeros.
    // NaN/infinity give "--- <unit>".
    Q_INVOKABLE static QString format(double value, const QString& unit = {}, int digits = 4,
                                      bool trimZeros = true);
    // Fixed decimals without prefix ("3.14 V"); unit omitted when empty.
    Q_INVOKABLE static QString fixed(double value, const QString& unit = {}, int decimals = 2);
    // Engineering text -> number ("1k", "2,5 m", "10u", "4.7 kΩ", "1e-3"); NaN when invalid.
    Q_INVOKABLE static double parse(const QString& text, const QString& unit = {});
    Q_INVOKABLE static bool isValid(const QString& text, const QString& unit = {});
    // Multiplier of an SI prefix (optionally followed by the unit): "k" -> 1e3, "mV" -> 1e-3
    // (with unit "V"), "" -> 1. NaN if unknown.
    Q_INVOKABLE static double prefixMultiplier(const QString& prefixAndUnit, const QString& unit = {});
    // Decibels with sign and fixed decimals: "-3.01 dB".
    Q_INVOKABLE static QString decibels(double value, int decimals = 2);
    // Clamps to [from, to] (either bound may be NaN = unbounded).
    Q_INVOKABLE static double clamp(double value, double from, double to);

private:
    explicit Units(QObject* parent = nullptr);
};

}  // namespace mybench
