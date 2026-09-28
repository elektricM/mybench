#include "mybench/dataexport.h"

#include "mybench/devicehub.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJSEngine>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mybench {

namespace {

const QString kLastFolderKey = u"export/lastFolder"_s;

// Delimited text: CSV (comma) or tab-separated.
class DelimitedWriter final : public TableWriter
{
public:
    DelimitedWriter(QString name, QStringList extensions, QChar separator)
        : m_name(std::move(name)), m_extensions(std::move(extensions)), m_separator(separator)
    {}
    QString name() const override { return m_name; }
    QStringList extensions() const override { return m_extensions; }

    bool write(const DataTable& table, const QStringList& header, QIODevice& out, QString* error) const override
    {
        QString text;
        for (const QString& line : header)
            text += u"# "_s + line + u'\n';
        for (const auto& [key, value] : table.metadata)
            text += u"# "_s + key + u": "_s + value + u'\n';

        QStringList cells;
        for (const DataColumn& column : table.columns)
            cells.append(quote(column.unit.isEmpty() ? column.name : u"%1 (%2)"_s.arg(column.name, column.unit)));
        text += cells.join(m_separator) + u'\n';

        const size_t rows = table.rowCount();
        for (size_t row = 0; row < rows; ++row) {
            cells.clear();
            for (const DataColumn& column : table.columns)
                cells.append(row < column.values.size() ? DataExport::formatNumber(column.values[row]) : QString());
            text += cells.join(m_separator) + u'\n';
            if (text.size() > (1 << 20)) {   // stream large tables in chunks
                if (out.write(text.toUtf8()) < 0)
                    return fail(out, error);
                text.clear();
            }
        }
        if (out.write(text.toUtf8()) < 0)
            return fail(out, error);
        return true;
    }

private:
    QString quote(const QString& cell) const
    {
        if (!cell.contains(m_separator) && !cell.contains(u'"') && !cell.contains(u'\n'))
            return cell;
        QString escaped = cell;
        escaped.replace(u'"', u"\"\""_s);
        return u'"' + escaped + u'"';
    }
    static bool fail(QIODevice& out, QString* error)
    {
        if (error)
            *error = out.errorString();
        return false;
    }

    QString m_name;
    QStringList m_extensions;
    QChar m_separator;
};

}  // namespace

// --- DataTable -----------------------------------------------------------------------------

bool DataTable::isEmpty() const
{
    return rowCount() == 0;
}

size_t DataTable::rowCount() const
{
    size_t rows = 0;
    for (const DataColumn& column : columns)
        rows = std::max(rows, column.values.size());
    return rows;
}

void DataTable::addColumn(const QString& name, const QString& unit, std::vector<double> values)
{
    columns.push_back({name, unit, std::move(values)});
}

void DataTable::addMetadata(const QString& key, const QString& value)
{
    metadata.append({key, value});
}

// --- DataExport ----------------------------------------------------------------------------

DataExport::DataExport(QObject* parent) : QObject(parent)
{
    m_writers.push_back(std::make_unique<DelimitedWriter>(u"CSV"_s, QStringList{u"csv"_s}, u','));
    m_writers.push_back(std::make_unique<DelimitedWriter>(tr("Tab-separated"), QStringList{u"tsv"_s, u"txt"_s}, u'\t'));
}

DataExport::~DataExport() = default;

DataExport* DataExport::instance()
{
    static DataExport* const exporter = new DataExport;
    return exporter;
}

DataExport* DataExport::create(QQmlEngine*, QJSEngine*)
{
    DataExport* exporter = instance();
    QJSEngine::setObjectOwnership(exporter, QJSEngine::CppOwnership);
    return exporter;
}

void DataExport::registerWriter(std::unique_ptr<TableWriter> writer)
{
    if (!writer)
        return;
    m_writers.push_back(std::move(writer));
    emit writersChanged();
}

const TableWriter* DataExport::writerFor(const QString& path) const
{
    const QString extension = QFileInfo(path).suffix().toLower();
    for (auto it = m_writers.rbegin(); it != m_writers.rend(); ++it) {
        if ((*it)->extensions().contains(extension))
            return it->get();
    }
    return m_writers.front().get();   // CSV
}

QStringList DataExport::standardHeader(const DataTable& table)
{
    QStringList header;
    header.append(u"%1 %2 - %3"_s.arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion(),
                                      table.title));
    header.append(u"Date: %1"_s.arg(QDateTime::currentDateTime().toOffsetFromUtc(
        QDateTime::currentDateTime().offsetFromUtc()).toString(Qt::ISODate)));
    const DeviceHub* hub = DeviceHub::instance();
    if (!hub->currentId().isEmpty()) {
        QString device = u"Device: %1 (%2, serial %3)"_s.arg(hub->name(), hub->productType(), hub->serialNumber());
        if (hub->isSimulated())
            device += u", simulated"_s;
        header.append(device);
    }
    return header;
}

QString DataExport::formatNumber(double value)
{
    if (!std::isfinite(value))
        return {};
    return QString::number(value, 'g', 10);
}

bool DataExport::write(const QString& path, const DataTable& table, QString* error)
{
    QString message;
    auto failed = [&](const QString& text) {
        setLastError(text);
        if (error)
            *error = text;
        return false;
    };
    if (path.isEmpty())
        return failed(tr("No file name given."));
    if (table.isEmpty())
        return failed(tr("There is no data to export."));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return failed(file.errorString());
    const TableWriter* writer = writerFor(path);
    if (!writer->write(table, standardHeader(table), file, &message)) {
        file.cancelWriting();
        return failed(message);
    }
    if (!file.commit())
        return failed(file.errorString());

    const QString folder = QFileInfo(path).absolutePath();
    QSettings().setValue(kLastFolderKey, folder);
    emit lastFolderChanged();
    setLastError({});
    emit exported(path);
    return true;
}

QString DataExport::suggestedFileName(const QString& instrumentId, const QString& extension) const
{
    return u"%1_%2.%3"_s.arg(instrumentId, QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s),
                             extension.isEmpty() ? u"csv"_s : extension);
}

QUrl DataExport::suggestedFileUrl(const QString& instrumentId, const QString& extension) const
{
    return QUrl::fromLocalFile(QDir(lastFolder().toLocalFile()).filePath(suggestedFileName(instrumentId, extension)));
}

bool DataExport::exportColumns(const QUrl& file, const QString& title, const QVariantList& columns,
                               const QVariantMap& metadata)
{
    DataTable table;
    table.title = title;
    for (const QVariant& entry : columns) {
        const QVariantMap map = entry.toMap();
        std::vector<double> values;
        const QVariantList list = map.value(u"values"_s).toList();
        values.reserve(size_t(list.size()));
        for (const QVariant& v : list) {
            bool ok = false;
            const double d = v.toDouble(&ok);
            values.push_back(ok ? d : std::nan(""));
        }
        table.addColumn(map.value(u"name"_s).toString(), map.value(u"unit"_s).toString(), std::move(values));
    }
    for (auto it = metadata.cbegin(); it != metadata.cend(); ++it)
        table.addMetadata(it.key(), it.value().toString());
    return write(file.isLocalFile() ? file.toLocalFile() : file.toString(), table);
}

bool DataExport::writePdf(const QUrl& file, const QString& imagePath, const QString& title, const QVariantMap& metadata)
{
    const QString path = localPath(file);
    const QImage image(imagePath);
    if (path.isEmpty() || image.isNull()) {
        setLastError(tr("Nothing to print."));
        return false;
    }
    QPdfWriter pdf(path);
    pdf.setTitle(title);
    pdf.setCreator(QCoreApplication::applicationName() + u' ' + QCoreApplication::applicationVersion());
    pdf.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Landscape, QMarginsF(12, 12, 12, 12),
                                  QPageLayout::Millimeter));
    pdf.setResolution(300);
    QPainter painter;
    if (!painter.begin(&pdf)) {
        setLastError(tr("Could not create %1.").arg(QFileInfo(path).fileName()));
        return false;
    }
    const QRect page = pdf.pageLayout().paintRectPixels(pdf.resolution());
    const QRect area(0, 0, page.width(), page.height());

    DataTable table;
    table.title = title;
    QStringList lines = standardHeader(table);
    for (auto it = metadata.cbegin(); it != metadata.cend(); ++it)
        lines.append(it.key() + u": "_s + it.value().toString());

    QFont heading = painter.font();
    heading.setPointSizeF(14);
    heading.setBold(true);
    painter.setFont(heading);
    const int headingHeight = painter.fontMetrics().height();
    painter.drawText(QRect(0, 0, area.width(), headingHeight), Qt::AlignLeft | Qt::AlignVCenter, title);
    QFont body = heading;
    body.setPointSizeF(8.5);
    body.setBold(false);
    painter.setFont(body);
    const int lineHeight = painter.fontMetrics().height();
    int y = headingHeight + lineHeight / 2;
    for (qsizetype i = 1; i < lines.size(); ++i) {   // line 0 repeats the title
        painter.drawText(QRect(0, y, area.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
        y += lineHeight;
    }
    y += lineHeight / 2;
    const QRect imageArea(0, y, area.width(), area.height() - y);
    const QSize scaled = image.size().scaled(imageArea.size(), Qt::KeepAspectRatio);
    painter.drawImage(QRect(imageArea.topLeft(), scaled), image);
    painter.end();

    QSettings().setValue(kLastFolderKey, QFileInfo(path).absolutePath());
    emit lastFolderChanged();
    setLastError({});
    emit exported(path);
    return true;
}

QString DataExport::temporaryFilePath(const QString& extension) const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(u"mybench_%1.%2"_s.arg(QDateTime::currentMSecsSinceEpoch()).arg(extension));
}

QString DataExport::localPath(const QUrl& file)
{
    return file.isLocalFile() ? file.toLocalFile() : file.toString();
}

QStringList DataExport::nameFilters() const
{
    QStringList filters;
    for (const auto& writer : m_writers) {
        QStringList patterns;
        for (const QString& extension : writer->extensions())
            patterns.append(u"*."_s + extension);
        filters.append(u"%1 (%2)"_s.arg(writer->name(), patterns.join(u' ')));
    }
    return filters;
}

QUrl DataExport::lastFolder() const
{
    QString folder = QSettings().value(kLastFolderKey).toString();
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QUrl::fromLocalFile(folder);
}

void DataExport::setLastError(const QString& error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

}  // namespace mybench
