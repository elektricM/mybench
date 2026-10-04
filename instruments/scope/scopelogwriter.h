// Text log of the Oscilloscope in the layout of the ELVISmx "Log" TXT file (a LabVIEW waveform
// export, docs/instruments/scope.md §8): a header block with the channel names, t0 and delta t per
// channel, a blank line, then time/value column pairs with absolute timestamps and %E values.
//
// Registered for "scope" only (registerScope): File ▸ Export Data… and the pane's ⋯ menu offer it as
// "*.txt"; the generic tab-separated writer keeps "*.tsv". ScopeController::exportTable() puts what
// the layout needs into DataTable::extra (keys below); the channel values are its "CH n" columns.
#pragma once

#include <mybench/dataexport.h>

namespace mybench::instruments::scope {

inline constexpr auto kLogStart = "scope.start";        // QDateTime of the first displayed sample
inline constexpr auto kLogInterval = "scope.dt";        // double, s between samples
inline constexpr auto kLogShift = "scope.shift";        // double, s: sub-sample trigger alignment
inline constexpr auto kLogChannels = "scope.channels";  // QVariantList of int: channels with data
inline constexpr auto kLogSkew = "scope.skew";          // QVariantList of double: skew of CH 0, CH 1 (s)

class LogWriter : public TableWriter
{
public:
    QString name() const override;
    QStringList extensions() const override;
    bool write(const DataTable& table, const QStringList& header, QIODevice& out, QString* error) const override;
};

}  // namespace mybench::instruments::scope
