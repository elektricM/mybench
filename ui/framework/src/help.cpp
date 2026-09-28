#include "mybench/help.h"

#include <QColor>
#include <QDir>
#include <QFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>
#include <QFile>
#include <QJSEngine>
#include <QRegularExpression>
#include <QUrl>
#include <QVariantMap>

#include <functional>

using namespace Qt::StringLiterals;

namespace mybench {

namespace {

const QString kHelpRoot = u":/mybench/help"_s;

}  // namespace

Help::Help(QObject* parent) : QObject(parent) {}

Help* Help::instance()
{
    static Help* const help = new Help;
    return help;
}

Help* Help::create(QQmlEngine*, QJSEngine*)
{
    Help* help = instance();
    QJSEngine::setObjectOwnership(help, QJSEngine::CppOwnership);
    return help;
}

QString Help::resourcePath(const QString& id)
{
    return kHelpRoot + u'/' + id + u".md"_s;
}

bool Help::has(const QString& id) const
{
    return !id.isEmpty() && QFile::exists(resourcePath(id));
}

QString Help::markdown(const QString& id) const
{
    QFile file(resourcePath(id));
    if (id.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

QString Help::html(const QString& id, const QVariantMap& style) const
{
    QString text = markdown(id);
    if (text.isEmpty())
        text = u"# %1\n\n%2"_s.arg(title(id), tr("This help page has not been written yet."));
    return renderMarkdown(text, style);
}

QString Help::renderMarkdown(const QString& markdown, const QVariantMap& style)
{
    auto color = [&](const char* key, const char* fallback) {
        const QColor c(style.value(QString::fromLatin1(key)).toString());
        return c.isValid() ? c : QColor(QString::fromLatin1(fallback));
    };
    const QColor text = color("text", "#E7E9EE");
    const QColor heading = color("heading", "#FFFFFF");
    const QColor muted = color("muted", "#A1A7B3");
    const QColor accent = color("accent", "#6D8BFF");
    const QColor border = color("border", "#323641");
    const QColor code = color("codeBackground", "#0E0F13");
    const int body = style.value(u"bodySize"_s, 13).toInt();

    QTextDocument document;
    QFont font = document.defaultFont();
    font.setPixelSize(body);
    document.setDefaultFont(font);
    document.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);

    QTextCursor cursor(&document);
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        QTextCharFormat chars;
        const int level = format.headingLevel();
        if (level > 0) {
            static constexpr int kSizes[] = {22, 17, 14, 13, 13, 13};
            format.setTopMargin(block == document.begin() ? 0 : (level <= 2 ? 20 : 14));
            format.setBottomMargin(level == 1 ? 8 : 6);
            QFont headingFont = font;
            headingFont.setPixelSize(kSizes[std::clamp(level, 1, 6) - 1]);
            headingFont.setWeight(QFont::DemiBold);
            chars.setFont(headingFont, QTextCharFormat::FontPropertiesSpecifiedOnly);
            chars.setForeground(level >= 4 ? muted : heading);
        } else {
            format.setBottomMargin(8);
            format.setLineHeight(140, QTextBlockFormat::ProportionalHeight);
            chars.setForeground(text);
        }
        if (block.textList())
            format.setBottomMargin(3);
        const bool codeBlock = format.nonBreakableLines() || format.hasProperty(QTextFormat::BlockCodeFence);
        if (codeBlock) {
            format.setBackground(code);
            format.setLeftMargin(6);
            format.setBottomMargin(0);
            format.setLineHeight(120, QTextBlockFormat::ProportionalHeight);
        }
        cursor.setPosition(block.position());
        cursor.setBlockFormat(format);
        cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(chars);

        // Inline formats: links in the accent color, inline code on a tinted background.
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            QTextCharFormat f = fragment.charFormat();
            bool changed = false;
            if (f.isAnchor()) {
                f.setForeground(accent);
                f.setFontUnderline(false);
                changed = true;
            }
            if (f.fontFixedPitch() || f.fontFamilies().toStringList().contains(u"monospace"_s)) {
                f.setBackground(code);
                changed = true;
            }
            if (changed) {
                QTextCursor c(&document);
                c.setPosition(fragment.position());
                c.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                c.setCharFormat(f);
            }
        }
    }

    // Tables: hairline borders, padding, full width.
    std::function<void(QTextFrame*)> styleFrames = [&](QTextFrame* frame) {
        for (QTextFrame* child : frame->childFrames()) {
            if (auto* table = qobject_cast<QTextTable*>(child)) {
                QTextTableFormat format = table->format();
                format.setBorder(1);
                format.setBorderBrush(border);
                format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
                format.setBorderCollapse(true);
                format.setCellPadding(5);
                format.setCellSpacing(0);
                format.setTopMargin(4);
                format.setBottomMargin(10);
                format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
                table->setFormat(format);
                for (int column = 0; column < table->columns(); ++column) {
                    QTextTableCell cell = table->cellAt(0, column);
                    QTextCharFormat header = cell.format();
                    header.setFontWeight(QFont::DemiBold);
                    header.setBackground(code);
                    cell.setFormat(header);
                }
            }
            styleFrames(child);
        }
    };
    styleFrames(document.rootFrame());
    return document.toHtml();
}

QString Help::title(const QString& id) const
{
    QFile file(resourcePath(id));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.startsWith(u"# "_s))
                return line.mid(2).trimmed();
        }
    }
    return id;
}

QString Help::topicForLink(const QString& link) const
{
    const QUrl url(link);
    if (!url.scheme().isEmpty() && url.scheme() != u"file"_s && url.scheme() != u"qrc"_s)
        return {};
    QString path = url.path();
    if (!path.endsWith(u".md"_s))
        return {};
    const QString id = path.section(u'/', -1).chopped(3);
    return has(id) ? id : QString();
}

QVariantList Help::topics() const
{
    QStringList ids;
    const QStringList files = QDir(kHelpRoot).entryList({u"*.md"_s}, QDir::Files, QDir::Name);
    for (const QString& file : files)
        ids.append(file.chopped(3));
    if (ids.removeOne(u"app"_s))
        ids.prepend(u"app"_s);
    QVariantList list;
    for (const QString& id : std::as_const(ids)) {
        QVariantMap topic;
        topic.insert(u"id"_s, id);
        topic.insert(u"title"_s, title(id));
        list.append(topic);
    }
    return list;
}

}  // namespace mybench
