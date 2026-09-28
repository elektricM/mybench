// Waveform file import/export for the Arbitrary channels and the Waveform Editor
// (docs/instruments/gen.md 4.2.3, 4.3.8, 4.3.9): MYBENCH .mbw documents, WAV, CSV/TXT text,
// raw 16-bit binary and, best effort, LabVIEW waveform datalog files (.wdt) written by the NI
// Waveform Editor. UI-free.
#pragma once

#include "genwaveform.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace mybench::instruments::gen {

struct LoadedWaveform {
    std::vector<double> samples;      // volts
    double sampleRate = 0;            // carried by the file, 0 = none
    QString name;                     // file name without directory
    std::optional<Document> document; // .mbw: the document (samples rendered at its rate)
    QStringList notices;              // non-fatal remarks ("3 invalid samples set to 0")
    int channels = 1;                 // WAV: channels in the file
};

// Loads any supported file by extension (.mbw, .wfm, .wav, .csv, .txt, .bin, .wdt; unknown
// extensions are tried as text). `channel` selects the WAV channel. nullopt + `error` on failure.
std::optional<LoadedWaveform> loadWaveformFile(const QString& path, QString* error = nullptr, int channel = 0);

// --- Text (4.2.3 automatic rules, 4.3.9 Text File Wizard) ----------------------------------------

struct TextImportOptions {
    bool automatic = true;   // the 4.2.3 rules; the fields below are used when false
    QString delimiters = QStringLiteral("\t");   // any of "\t;, " plus one "other" character
    int column = 1;          // 1-based
    int startLine = 0;       // 1-based; 0 = the first line whose column is numeric
    int count = 0;           // 0 = all remaining numeric values
};

std::optional<LoadedWaveform> parseText(const QByteArray& data, const TextImportOptions& options,
                                        QString* error = nullptr);

// --- WAV --------------------------------------------------------------------------------------------

struct WavData {
    double sampleRate = 0;
    std::vector<std::vector<double>> channels;   // full scale = +-1.0
};
std::optional<WavData> parseWav(const QByteArray& data, QString* error = nullptr);
QByteArray encodeWavFloat32(const std::vector<double>& samples, double sampleRate);   // mono

// --- Binary: 16-bit signed little-endian, +-peakReference V full scale ----------------------------

std::vector<double> parseBinaryI16(const QByteArray& data, double peakReference = 5.0);
QByteArray encodeBinaryI16(const std::vector<double>& samples, double peakReference = 5.0);

// --- LabVIEW waveform datalog (.wdt), best effort -----------------------------------------------------
// Finds flattened LabVIEW analog waveforms (big-endian: timestamp 16 bytes, dt DBL, I32 length, DBL
// samples) in the file and returns them in file order. Built without NI sample files: it looks for
// that byte pattern rather than decoding the datalog container.
struct WdtWaveform {
    double dt = 0;
    std::vector<double> samples;
};
std::vector<WdtWaveform> parseWdt(const QByteArray& data);
// A file in the same layout (a datalog-like header, one record holding an array of waveforms):
// the editor's "Export > LabVIEW waveform (.wdt)". MYBENCH reads it back; NI's software was not
// available to verify that it accepts it (no NI sample file, audit gen #19/#22).
QByteArray encodeWdt(const std::vector<WdtWaveform>& waveforms);

// --- NI Waveform Editor documents (.wfm), best effort ------------------------------------------------
// LabVIEW flattened, big-endian (algorithms/gen.md 3.2, 3.5): U16 Units, then an I32-counted array
// of records {I32 Type (0 segment, 1 library, 2 expression, 3 sketch), string (I32 length +
// bytes), I32 Op, I32 A, SGL B, SGL array C (I32 count + values)}. Segment: name, B = duration;
// library: A = function index, C = parameters; expression: text, C = {From, To, Cycles}; sketch:
// name, B = cycles, C = values. Times and frequencies are in the document Units (s/Hz, ms/kHz,
// us/MHz). The file has no sample rate: the document gets 10000 (in its Units). The layout comes
// from the code study (confidence M); nullopt + `error` when the file does not match it.
std::optional<Document> parseWfm(const QByteArray& data, QString* error = nullptr);
QByteArray encodeWfm(const Document& document);   // same layout (tests)

// --- Export -----------------------------------------------------------------------------------------

// "# sampleRate=<fs>" then one value per line.
QByteArray encodeCsv(const std::vector<double>& samples, double sampleRate);
// Writes `samples` to `path` by extension (.csv/.txt, .wav, .bin, .wdt; other extensions are
// refused); `clipped` receives the number of samples beyond +-1 V (WAV) or the binary reference.
bool saveWaveformFile(const QString& path, const std::vector<double>& samples, double sampleRate,
                      QString* error = nullptr, size_t* clipped = nullptr);

}  // namespace mybench::instruments::gen
