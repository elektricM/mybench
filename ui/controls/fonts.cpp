#include "fonts.h"

#include <QFontDatabase>
#include <QGuiApplication>

using namespace Qt::StringLiterals;

namespace mybench {

Fonts::Fonts(QObject* parent) : QObject(parent) {}

QFont Fonts::ui() const
{
    // The application font is the platform UI font on every platform plugin (the offscreen
    // plugin's theme would report a generic "Sans Serif" through QFontDatabase::systemFont()).
    return QGuiApplication::font();
}

QFont Fonts::mono() const
{
    // Well-known monospaced families first (the platform plugin's fixed font may be a generic
    // alias such as "Monospace" that does not exist on every platform).
    const QStringList families = QFontDatabase::families();
    QFont font = QGuiApplication::font();
    for (const QString& family : {u"SF Mono"_s, u"Menlo"_s, u"Cascadia Mono"_s, u"Consolas"_s, u"JetBrains Mono"_s,
                                  u"DejaVu Sans Mono"_s, u"Liberation Mono"_s, u"Noto Sans Mono"_s,
                                  u"Ubuntu Mono"_s, u"Courier New"_s}) {
        if (families.contains(family)) {
            font.setFamily(family);
            return font;
        }
    }
    font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setStyleHint(QFont::Monospace);
    return font;
}

}  // namespace mybench
