// The ELVISmx-style text log of the Spectrum Analyzer (dsa.md 13, layout of the shared LabVIEW
// waveform export in scope.md 8; TAB-separated, CR LF):
//     waveform<TAB>Source signal
//     t0<TAB>9/28/2026  10:15:00.000000
//     delta t<TAB>1.250000E-5
//     <blank>
//     time[0]<TAB>Source signal
//     9/28/2026  10:15:00.000000<TAB>1.234560E-1      one absolute time + value per sample
//     <blank>
//     FFT waveform
//     Units: dBVrms
//     0.00<TAB>-1.234500E+2                           one row per line (`lines` rows: 0 ... 39900 Hz)
// Values use `d.dddddd E+n` with an unpadded exponent (log.md 8.2). In Octave mode the second
// block (`Octave bands`) lists band centres and levels instead. Registered for the "dsa"
// instrument's `*.txt` files; the CSV/TSV exports are unchanged.
#pragma once

#include <mybench/dataexport.h>

namespace mybench::instruments::dsa {

// Keys of DataTable::extra written by DsaController::exportTable().
inline constexpr auto kLogT0 = "dsa.t0";        // QDateTime of the first sample
inline constexpr auto kLogDt = "dsa.dt";        // double, seconds per sample
inline constexpr auto kLogUnits = "dsa.units";  // QString, unit of the spectrum values
inline constexpr auto kLogLines = "dsa.lines";  // int, rows of the FFT block (the span without its last line)

class LogWriter : public TableWriter
{
public:
    QString name() const override;
    QStringList extensions() const override;
    bool write(const DataTable& table, const QStringList& header, QIODevice& out, QString* error) const override;
};

}  // namespace mybench::instruments::dsa
