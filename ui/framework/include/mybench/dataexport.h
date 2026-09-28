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
class QIODevice;
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

public:
    static DataExport* instance();
    static DataExport* create(QQmlEngine*, QJSEngine*);

    // Adds a format (e.g. a future sigrok .sr writer). Takes precedence for its extensions.
    void registerWriter(std::unique_ptr<TableWriter> writer);
    // Writer for the extension of `path` (CSV if the extension is unknown or missing).
    const TableWriter* writerFor(const QString& path) const;

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

    // Export PDF (ELVISmx Print): one landscape page with `title`, the standard header lines
    // (date, device), `metadata` as "Key: value" lines and the image at `imagePath` (e.g. a
    // grabToImage() of the panel saved with temporaryFilePath()). Returns false on failure.
    Q_INVOKABLE bool writePdf(const QUrl& file, const QString& imagePath, const QString& title,
                              const QVariantMap& metadata = {});
    // A path in the temporary directory for intermediate files (e.g. "png").
    Q_INVOKABLE QString temporaryFilePath(const QString& extension) const;
    // Local path of a file URL (QML FileDialog results).
    Q_INVOKABLE static QString localPath(const QUrl& file);

    // QML convenience: columns = [{ name, unit, values: [numbers] }], metadata = { key: value }.
    Q_INVOKABLE bool exportColumns(const QUrl& file, const QString& title,
                                   const QVariantList& columns, const QVariantMap& metadata = {});

    QStringList nameFilters() const;
    QUrl lastFolder() const;
    QString lastError() const { return m_lastError; }

signals:
    void writersChanged();
    void lastFolderChanged();
    void lastErrorChanged();
    void exported(const QString& path);

private:
    explicit DataExport(QObject* parent = nullptr);
    ~DataExport() override;
    void setLastError(const QString& error);

    std::vector<std::unique_ptr<TableWriter>> m_writers;   // later entries win
    QString m_lastError;
};

}  // namespace mybench
