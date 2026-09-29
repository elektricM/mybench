#include "mybench/dataexport.h"

#include "mybench/devicehub.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QImage>
#include <QLocale>
#include <QJSEngine>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#ifdef MYBENCH_HAVE_PRINT
#include <QPrinter>
#include <QPrinterInfo>
#endif

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mybench {

namespace {

const QString kLastFolderKey = u"export/lastFolder"_s;

// Remembers the folder of `path`, also per file type (app.md 12).
void rememberFolder(const QString& path)
{
    const QFileInfo info(path);
    QSettings settings;
    settings.setValue(kLastFolderKey, info.absolutePath());
    if (!info.suffix().isEmpty())
        settings.setValue(u"export/lastFolder/"_s + info.suffix().toLower(), info.absolutePath());
}

// Reason text for a file that could not be written (callers prefix "Could not save <name>: ").
// An existing target that cannot be replaced is usually open in another program (Excel on
// Windows) or read-only, so the message names that.
QString saveError(const QString& path, const QString& reason)
{
    if (QFileInfo::exists(path))
        return QCoreApplication::translate("mybench::DataExport",
                   "%1. The file may be open in another program (for example Excel) or read-only: close it or choose another file name.")
            .arg(reason);
    return reason;
}

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
    m_writers.push_back({std::make_unique<DelimitedWriter>(u"CSV"_s, QStringList{u"csv"_s}, u','), {}});
    m_writers.push_back({std::make_unique<DelimitedWriter>(tr("Tab-separated"), QStringList{u"tsv"_s, u"txt"_s}, u'\t'), {}});
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

void DataExport::registerWriter(std::unique_ptr<TableWriter> writer, const QString& instrumentId)
{
    if (!writer)
        return;
    m_writers.push_back({std::move(writer), instrumentId});
    emit writersChanged();
}

const TableWriter* DataExport::writerFor(const QString& path, const QString& instrumentId) const
{
    const QString extension = QFileInfo(path).suffix().toLower();
    // The instrument's own writers first, then the generic ones (later registrations win).
    if (!instrumentId.isEmpty()) {
        for (auto it = m_writers.rbegin(); it != m_writers.rend(); ++it) {
            if (it->instrumentId == instrumentId && it->writer->extensions().contains(extension))
                return it->writer.get();
        }
    }
    for (auto it = m_writers.rbegin(); it != m_writers.rend(); ++it) {
        if (it->instrumentId.isEmpty() && it->writer->extensions().contains(extension))
            return it->writer.get();
    }
    return m_writers.front().writer.get();   // CSV
}

QStringList DataExport::nameFiltersFor(const QString& instrumentId) const
{
    auto filter = [](const TableWriter& writer, const QStringList& extensions) {
        QStringList patterns;
        for (const QString& extension : extensions)
            patterns.append(u"*."_s + extension);
        return u"%1 (%2)"_s.arg(writer.name(), patterns.join(u' '));
    };
    QStringList filters;
    QStringList claimed;
    if (!instrumentId.isEmpty()) {
        for (auto it = m_writers.rbegin(); it != m_writers.rend(); ++it) {
            if (it->instrumentId != instrumentId)
                continue;
            QStringList extensions;
            for (const QString& extension : it->writer->extensions()) {
                if (!claimed.contains(extension))
                    extensions.append(extension);
            }
            claimed += extensions;
            if (!extensions.isEmpty())
                filters.append(filter(*it->writer, extensions));
        }
    }
    for (const auto& entry : m_writers) {
        if (!entry.instrumentId.isEmpty())
            continue;
        QStringList extensions;
        for (const QString& extension : entry.writer->extensions()) {
            if (!claimed.contains(extension))
                extensions.append(extension);
        }
        if (!extensions.isEmpty())
            filters.append(filter(*entry.writer, extensions));
    }
    return filters;
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

    // Binary mode: the writers choose their line endings themselves (LF; the ELVISmx-style DSA
    // log writes CR LF). Text mode would turn each LF into CR LF on Windows (CR CR LF for DSA).
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return failed(saveError(path, file.errorString()));
    const TableWriter* writer = writerFor(path, table.instrumentId);
    if (!writer->write(table, standardHeader(table), file, &message)) {
        file.cancelWriting();
        return failed(message);
    }
    if (!file.commit())
        return failed(saveError(path, file.errorString()));

    rememberFolder(path);
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

QUrl DataExport::lastFolderFor(const QString& extension) const
{
    const QString folder = QSettings().value(u"export/lastFolder/"_s + extension.toLower()).toString();
    if (!folder.isEmpty() && QFileInfo(folder).isDir())
        return QUrl::fromLocalFile(folder);
    return lastFolder();
}

QUrl DataExport::suggestedFileUrl(const QString& instrumentId, const QString& extension) const
{
    return QUrl::fromLocalFile(QDir(lastFolderFor(extension.isEmpty() ? u"csv"_s : extension).toLocalFile()).filePath(suggestedFileName(instrumentId, extension)));
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

// --- Print / export image (ELVISmx Print dialog) ------------------------------------------

namespace {

bool option(const QVariantMap& options, const char* key, bool fallback = false)
{
    const QVariant value = options.value(QString::fromLatin1(key));
    return value.isValid() ? value.toBool() : fallback;
}

// Everything above the image and the table below it, measured for a width and resolution.
struct PageParts {
    QString title;
    QStringList lines;                               // date/device/identity lines
    QList<std::pair<QString, QString>> table;        // settings table
    int groups = 2;                                  // Setting|Value column pairs side by side
    QFont heading, body;
    int headingHeight = 0, lineHeight = 0, rowHeight = 0;

    PageParts(const QString& t, const QVariantMap& metadata, const QVariantMap& options, double dpi, bool landscape)
        : title(t)
    {
        heading.setFamilies({u"Helvetica"_s, u"Arial"_s});
        heading.setPixelSize(std::max(1, int(std::lround(14.0 * dpi / 72.0))));
        heading.setBold(true);
        body = heading;
        body.setBold(false);
        body.setPixelSize(std::max(1, int(std::lround(8.5 * dpi / 72.0))));
        headingHeight = QFontMetrics(heading).height();
        lineHeight = QFontMetrics(body).height();
        rowHeight = int(lineHeight * 1.35);

        DataTable header;
        header.title = t;
        const QStringList standard = DataExport::standardHeader(header);
        const bool legacy = options.isEmpty();   // old callers: date/device lines + metadata lines
        for (qsizetype i = 1; i < standard.size(); ++i) {   // line 0 repeats the title
            if (!legacy && standard[i].startsWith(u"Date:"_s))
                continue;   // replaced by the identity line (Time stamp option)
            lines.append(standard[i]);
        }
        const QString identity = DataExport::identityLine(options);
        if (!identity.isEmpty())
            lines.append(identity);
        if (legacy || option(options, "settingsTable", true)) {
            for (auto it = metadata.cbegin(); it != metadata.cend(); ++it)
                table.append({it.key(), it.value().toString()});
        }
        groups = landscape ? 3 : 2;
    }
    int headerHeight() const { return headingHeight + lineHeight / 2 + int(lines.size()) * lineHeight + lineHeight / 2; }
    int tableRows() const { return table.isEmpty() ? 0 : int((table.size() + groups - 1) / groups); }
    int tableHeight() const { return table.isEmpty() ? 0 : lineHeight / 2 + (tableRows() + 1) * rowHeight; }

    void paintHeader(QPainter& painter, const QRect& area) const
    {
        painter.setPen(Qt::black);
        painter.setFont(heading);
        painter.drawText(QRect(area.left(), area.top(), area.width(), headingHeight), Qt::AlignLeft | Qt::AlignVCenter, title);
        painter.setFont(body);
        int y = area.top() + headingHeight + lineHeight / 2;
        for (const QString& line : lines) {
            painter.drawText(QRect(area.left(), y, area.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter, line);
            y += lineHeight;
        }
    }
    void paintTable(QPainter& painter, const QRect& area) const
    {
        if (table.isEmpty())
            return;
        painter.setFont(body);
        const int top = area.top() + lineHeight / 2;
        const int rows = tableRows();
        const int groupWidth = area.width() / groups;
        const int pad = lineHeight / 3;
        const QColor rule(0xB0, 0xB4, 0xBA), fill(0xEE, 0xF0, 0xF3);
        for (int g = 0; g < groups; ++g) {
            const int x = area.left() + g * groupWidth;
            const int keyWidth = int(groupWidth * 0.45);
            const QRect head(x, top, groupWidth - pad, rowHeight);
            painter.fillRect(head, fill);
            QFont bold = body;
            bold.setBold(true);
            painter.setFont(bold);
            painter.setPen(Qt::black);
            painter.drawText(head.adjusted(pad, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("Setting"));
            painter.drawText(head.adjusted(keyWidth + pad, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("Value"));
            painter.setFont(body);
            for (int r = 0; r < rows; ++r) {
                const qsizetype index = qsizetype(g) * rows + r;
                if (index >= table.size())
                    break;
                const QRect row(x, top + (r + 1) * rowHeight, groupWidth - pad, rowHeight);
                painter.setPen(rule);
                painter.drawLine(row.bottomLeft(), row.bottomRight());
                painter.setPen(Qt::black);
                const QFontMetrics metrics(body);
                painter.drawText(row.adjusted(pad, 0, 0, 0).intersected(QRect(x, row.top(), keyWidth, rowHeight)),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 metrics.elidedText(table[index].first, Qt::ElideRight, keyWidth - 2 * pad));
                painter.drawText(row.adjusted(keyWidth + pad, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                                 metrics.elidedText(table[index].second, Qt::ElideRight, groupWidth - keyWidth - 2 * pad));
            }
        }
    }
};

}  // namespace

QString DataExport::identityLine(const QVariantMap& options)
{
    QStringList parts;
    const QVariantMap who = instance()->identity();
    if (option(options, "userName"))
        parts.append(tr("User: %1").arg(who.value(u"user"_s).toString()));
    if (option(options, "computerName"))
        parts.append(tr("Computer: %1").arg(who.value(u"computer"_s).toString()));
    if (option(options, "timeStamp", !options.isEmpty()))
        parts.append(QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat));
    return parts.join(u"  ·  "_s);
}

QVariantMap DataExport::identity() const
{
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        user = qEnvironmentVariable("USERNAME");
    return {{u"user"_s, user}, {u"computer"_s, QSysInfo::machineHostName()}};
}

QImage DataExport::inkSaver(const QImage& source)
{
    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = line[x];
            const int r = qRed(px), g = qGreen(px), b = qBlue(px);
            const int chroma = std::max({r, g, b}) - std::min({r, g, b});
            QColor color = QColor::fromRgb(px);
            if (chroma < 48) {
                // Neutral (background, grid, text): invert the lightness.
                const int l = 255 - color.lightness();
                color = QColor(l, l, l);
            } else {
                // Coloured (traces, markers): same hue, dark enough for white paper.
                float h, sat, l, a;
                color.getHslF(&h, &sat, &l, &a);
                color = QColor::fromHslF(h, std::max(sat, 0.55f), std::min(l, 0.42f));
            }
            line[x] = qRgba(color.red(), color.green(), color.blue(), qAlpha(px));
        }
    }
    return image;
}

void DataExport::paintPage(QPainter& painter, const QRect& area, double dpi, const QImage& image, const QString& title,
                           const QVariantMap& metadata, const QVariantMap& options) const
{
    const PageParts parts(title, metadata, options, dpi, area.width() >= area.height());
    painter.fillRect(area, Qt::white);
    parts.paintHeader(painter, area);
    const int top = area.top() + parts.headerHeight();
    const int tableHeight = parts.tableHeight();
    const QRect imageArea(area.left(), top, area.width(), std::max(1, area.bottom() - top - tableHeight));
    const QSize scaled = image.size().scaled(imageArea.size(), Qt::KeepAspectRatio);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRect(imageArea.topLeft(), scaled), image);
    parts.paintTable(painter, QRect(area.left(), imageArea.top() + scaled.height(), area.width(), tableHeight));
}

bool DataExport::writePdf(const QUrl& file, const QString& imagePath, const QString& title, const QVariantMap& metadata,
                          const QVariantMap& options)
{
    const QString path = localPath(file);
    QImage image(imagePath);
    if (path.isEmpty() || image.isNull()) {
        setLastError(tr("Nothing to print."));
        return false;
    }
    if (option(options, "inkSaver"))
        image = inkSaver(image);
    QPdfWriter pdf(path);
    pdf.setTitle(title);
    pdf.setCreator(QCoreApplication::applicationName() + u' ' + QCoreApplication::applicationVersion());
    const bool portrait = options.value(u"orientation"_s).toString() == u"portrait"_s;
    pdf.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), portrait ? QPageLayout::Portrait : QPageLayout::Landscape,
                                  QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter));
    pdf.setResolution(300);
    QPainter painter;
    if (!painter.begin(&pdf)) {
        setLastError(tr("Could not create %1.").arg(QFileInfo(path).fileName()));
        return false;
    }
    const QRect page = pdf.pageLayout().paintRectPixels(pdf.resolution());
    paintPage(painter, QRect(0, 0, page.width(), page.height()), pdf.resolution(), image, title, metadata, options);
    painter.end();

    rememberFolder(path);
    emit lastFolderChanged();
    setLastError({});
    emit exported(path);
    return true;
}

bool DataExport::writeImage(const QUrl& file, const QString& imagePath, const QString& title, const QVariantMap& metadata,
                            const QVariantMap& options)
{
    const QString path = localPath(file);
    QImage image(imagePath);
    if (path.isEmpty() || image.isNull()) {
        setLastError(tr("Nothing to save."));
        return false;
    }
    if (option(options, "inkSaver"))
        image = inkSaver(image);
    const bool decorated = option(options, "userName") || option(options, "computerName")
                           || option(options, "timeStamp") || option(options, "settingsTable");
    if (decorated) {
        // Header above and settings table below the image, at the image's scale (a 2x grab of
        // a ~1300 px display gets ~11 pt text).
        const double dpi = 72.0 * std::max(1.0, image.width() / 900.0);
        const PageParts parts(title, metadata, options, dpi, true);
        const int margin = parts.lineHeight;
        const int width = image.width() + 2 * margin;
        const int height = margin + parts.headerHeight() + image.height() + parts.tableHeight() + margin;
        QImage page(width, height, QImage::Format_ARGB32);
        page.fill(Qt::white);
        QPainter painter(&page);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const QRect area(margin, margin, image.width(), height - 2 * margin);
        parts.paintHeader(painter, area);
        const int top = margin + parts.headerHeight();
        painter.drawImage(QPoint(margin, top), image);
        parts.paintTable(painter, QRect(margin, top + image.height(), image.width(), parts.tableHeight()));
        painter.end();
        image = page;
    }
    if (!image.save(path, "PNG")) {
        setLastError(tr("Could not save %1: %2").arg(QFileInfo(path).fileName(), saveError(path, tr("The image could not be written"))));
        return false;
    }
    rememberFolder(path);
    emit lastFolderChanged();
    setLastError({});
    emit exported(path);
    return true;
}

bool DataExport::printingAvailable() const
{
#ifdef MYBENCH_HAVE_PRINT
    return true;
#else
    return false;
#endif
}

QStringList DataExport::printers() const
{
#ifdef MYBENCH_HAVE_PRINT
    return QPrinterInfo::availablePrinterNames();
#else
    return {};
#endif
}

QString DataExport::defaultPrinter() const
{
#ifdef MYBENCH_HAVE_PRINT
    return QPrinterInfo::defaultPrinterName();
#else
    return {};
#endif
}

bool DataExport::print(const QString& imagePath, const QString& title, const QVariantMap& metadata, const QVariantMap& options)
{
    QImage image(imagePath);
    if (image.isNull()) {
        setLastError(tr("Nothing to print."));
        return false;
    }
#ifdef MYBENCH_HAVE_PRINT
    if (option(options, "inkSaver"))
        image = inkSaver(image);
    QPrinter printer(QPrinter::HighResolution);
    const QString name = options.value(u"printer"_s).toString();
    if (!name.isEmpty())
        printer.setPrinterName(name);
    if (!printer.isValid()) {
        setLastError(tr("No printer is available."));
        return false;
    }
    printer.setDocName(title);
    printer.setCopyCount(std::max(1, options.value(u"copies"_s, 1).toInt()));
    printer.setPageOrientation(options.value(u"orientation"_s).toString() == u"portrait"_s ? QPageLayout::Portrait
                                                                                            : QPageLayout::Landscape);
    QPainter painter;
    if (!painter.begin(&printer)) {
        setLastError(tr("Could not start printing on %1.").arg(printer.printerName()));
        return false;
    }
    const QRect page = printer.pageLayout().paintRectPixels(printer.resolution());
    paintPage(painter, QRect(0, 0, page.width(), page.height()), printer.resolution(), image, title, metadata, options);
    painter.end();
    setLastError({});
    return true;
#else
    Q_UNUSED(title)
    Q_UNUSED(metadata)
    Q_UNUSED(options)
    setLastError(tr("Printing is not available in this build."));
    return false;
#endif
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
    return nameFiltersFor({});
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
