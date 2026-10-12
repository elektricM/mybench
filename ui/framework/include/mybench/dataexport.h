// DataExport: saving traces and tables as delimited text (the ELVISmx "Log" button), with a
// writer registry so more formats (sigrok sessions, .sr) can be plugged in later.
//
// Files start with comment lines, then one header row and the data rows:
//     # MYBENCH 0.1.0 - Oscilloscope
//     # Date: 2026-09-28T10:15:00+02:00
//     # Device: Simulated myDAQ (NI myDAQ, serial SIM0001), simulated
//     # Timebase: 5 ms/div                      <- DataTable::metadata, one "Key: value" per line
//     Time (s),AI 0 (V),AI 1 (V)
//     0,0.0012,-0.4987
// Numbers use '.' and up to 10 significant digits; NaN (and missing rows of shorter columns) are
// empty cells.
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <utility>
#include <vector>

QT_BEGIN_NAMESPACE
class QImage;
class QIODevice;
class QPainter;
class QRect;
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace mybench {

struct DataColumn {
    QString name;                 // "Time", "AI 0"
    QString unit;                 // "s", "V" (header cell: "AI 0 (V)"); may be empty
    std::vector<double> values;
};

struct DataTable {
    QString title;                                  // instrument title, e.g. "Oscilloscope"
    QString instrumentId;                           // e.g. "scope" (suggested file names)
    QList<std::pair<QString, QString>> metadata;    // settings, written as "# Key: value"
    std::vector<DataColumn> columns;
    // Free-form data for instrument-specific writers (registerWriter(writer, instrumentId)), e.g.
    // { "t0": 0.0, "dt": 5e-6, "units": "dBVrms" } for a LabVIEW-style waveform text log.
    // Ignored by the built-in CSV/TSV writers.
    QVariantMap extra;

    bool isEmpty() const;
    size_t rowCount() const;   // length of the longest column
    void addColumn(const QString& name, const QString& unit, std::vector<double> values);
    void addMetadata(const QString& key, const QString& value);
};

// One file format. Built in: CSV (".csv", comma) and tab-separated (".tsv", ".txt").
class TableWriter
{
public:
    virtual ~TableWriter() = default;
    virtual QString name() const = 0;            // for file dialogs, e.g. "CSV"
    virtual QStringList extensions() const = 0;  // lower case, without dot; first = default
    // Writes the whole file. `header` holds the standard comment lines (without "# ").
    virtual bool write(const DataTable& table, const QStringList& header, QIODevice& out,
                       QString* error) const = 0;
};

class DataExport : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // File-dialog filters of every registered format, e.g. "CSV (*.csv)".
    Q_PROPERTY(QStringList nameFilters READ nameFilters NOTIFY writersChanged)
    // Folder of the last export (persisted); file dialogs start there.
    Q_PROPERTY(QUrl lastFolder READ lastFolder NOTIFY lastFolderChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    // Print (ELVISmx Print dialog): direct printing is available (Qt Print Support), the printer
    // names and the default one.
    Q_PROPERTY(bool printingAvailable READ printingAvailable CONSTANT)
    Q_PROPERTY(QStringList printers READ printers NOTIFY printersChanged)
    Q_PROPERTY(QString defaultPrinter READ defaultPrinter NOTIFY printersChanged)
    // For the Print options: { user: "alice", computer: "bench-mac" }.
    Q_PROPERTY(QVariantMap identity READ identity CONSTANT)

public:
    static DataExport* instance();
    static DataExport* create(QQmlEngine*, QJSEngine*);

    // Adds a format (e.g. a future sigrok .sr writer). Takes precedence for its extensions.
    // With `instrumentId`, the writer is only offered for (and used by) that instrument's
    // exports, and wins over the generic writers for its extensions: e.g. the ELVISmx text log
    // layout of the Spectrum Analyzer as "*.txt" for "dsa" only (the generic tab-separated
    // writer keeps "*.tsv" there). Register once, e.g. in the instrument's registration function.
    void registerWriter(std::unique_ptr<TableWriter> writer, const QString& instrumentId = QString());
    // Writer for the extension of `path` (CSV if the extension is unknown or missing), taking
    // the writers of `instrumentId` into account.
    const TableWriter* writerFor(const QString& path, const QString& instrumentId = QString()) const;
    // File-dialog filters for an instrument's export: its own writers first, then the generic
    // ones (without the extensions its own writers claim).
    Q_INVOKABLE QStringList nameFiltersFor(const QString& instrumentId) const;

    // Writes `table` to `path` (format by extension) with the standard header. Remembers the
    // folder. Returns false and sets `error` (and lastError) on failure.
    bool write(const QString& path, const DataTable& table, QString* error = nullptr);

    // "MYBENCH 0.1.0 - <title>", "Date: ...", "Device: ..." (current device of DeviceHub).
    static QStringList standardHeader(const DataTable& table);
    // Up to 10 significant digits, '.' decimal point, empty for NaN/infinity.
    static QString formatNumber(double value);

    // "<id>_<yyyyMMdd_HHmmss>.<extension>" and the same in lastFolder (or Documents).
    Q_INVOKABLE QString suggestedFileName(const QString& instrumentId,
                                          const QString& extension = QStringLiteral("csv")) const;
    Q_INVOKABLE QUrl suggestedFileUrl(const QString& instrumentId,
                                      const QString& extension = QStringLiteral("csv")) const;

    // Print / export-image options (ELVISmx Print dialog), all optional:
    //   inkSaver (bool): dark display -> white background, dark grid/text, traces kept in colour
    //   userName, computerName, timeStamp (bool): identity line in the header
    //   settingsTable (bool): `metadata` as a Setting / Value table (default true for PDF/print)
    //   orientation ("landscape" default, "portrait"), copies (int, print), printer (name, print)
    //
    // Export PDF (ELVISmx Print): one page with `title`, the date/device lines, the identity line,
    // the image at `imagePath` (e.g. a grabToImage() of the panel saved with temporaryFilePath())
    // and the settings table. Returns false on failure (lastError).
    Q_INVOKABLE bool writePdf(const QUrl& file, const QString& imagePath, const QString& title,
                              const QVariantMap& metadata = {}, const QVariantMap& options = {});
    // Export image (PNG): the image at `imagePath`, ink-saved and/or with a header and settings
    // table below when the options ask for them (otherwise a plain copy).
    Q_INVOKABLE bool writeImage(const QUrl& file, const QString& imagePath, const QString& title,
                                const QVariantMap& metadata = {}, const QVariantMap& options = {});
    // Prints the same page as writePdf() on `options.printer` (default printer if empty),
    // `options.copies` copies, without a dialog. False (lastError) if printing is unavailable.
    Q_INVOKABLE bool print(const QString& imagePath, const QString& title, const QVariantMap& metadata = {},
                           const QVariantMap& options = {});
    // Ink saver transform: near-neutral pixels are inverted in lightness (black -> white, light
    // text -> dark), coloured pixels keep their hue and are darkened enough to read on white.
    static QImage inkSaver(const QImage& image);
    // The identity line for `options` ("User: alice · Computer: bench · 2026-09-28 10:15").
    static QString identityLine(const QVariantMap& options);
    // A path in the temporary directory for intermediate files (e.g. "png").
    Q_INVOKABLE QString temporaryFilePath(const QString& extension) const;
    // Local path of a file URL (QML FileDialog results).
    Q_INVOKABLE static QString localPath(const QUrl& file);

    // QML convenience: columns = [{ name, unit, values: [numbers] }], metadata = { key: value }.
    Q_INVOKABLE bool exportColumns(const QUrl& file, const QString& title,
                                   const QVariantList& columns, const QVariantMap& metadata = {});

    QStringList nameFilters() const;
    QUrl lastFolder() const;
    // Folder of the last export of this file type ("csv", "png", "pdf"), else lastFolder().
    Q_INVOKABLE QUrl lastFolderFor(const QString& extension) const;
    QString lastError() const { return m_lastError; }
    bool printingAvailable() const;
    QStringList printers() const;
    QString defaultPrinter() const;
    QVariantMap identity() const;

signals:
    void writersChanged();
    void lastFolderChanged();
    void lastErrorChanged();
    void printersChanged();
    void exported(const QString& path);

private:
    explicit DataExport(QObject* parent = nullptr);
    ~DataExport() override;
    void setLastError(const QString& error);
    // Lays out a printed page in `area` (device pixels at `dpi`).
    void paintPage(QPainter& painter, const QRect& area, double dpi, const QImage& image, const QString& title,
                   const QVariantMap& metadata, const QVariantMap& options) const;

    struct WriterEntry {
        std::unique_ptr<TableWriter> writer;
        QString instrumentId;   // empty: every instrument
    };
    std::vector<WriterEntry> m_writers;   // later entries win
    QString m_lastError;
};

}  // namespace mybench
