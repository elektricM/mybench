#include "mybench/appinfo.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QFile>
#include <QJSEngine>
#include <QPalette>
#include <QQuickStyle>

using namespace Qt::StringLiterals;

namespace mybench {

AppInfo::AppInfo(QObject* parent) : QObject(parent) {}

QString AppInfo::controlsStyle() const
{
    return QQuickStyle::name();
}

bool AppInfo::nativeControls() const
{
    const QString style = QQuickStyle::name();
    return !style.isEmpty() && style != QLatin1String("MyBench.Style") && style != QLatin1String("Basic");
}

void AppInfo::setApplicationPalette(const QVariantMap& colors)
{
    if (colors.isEmpty()) {
        QGuiApplication::setPalette(QPalette());
        return;
    }
    QPalette palette = QGuiApplication::palette();
    const auto set = [&](const char* key, QPalette::ColorRole role) {
        const QVariant value = colors.value(QLatin1String(key));
        if (value.isValid())
            palette.setColor(QPalette::All, role, value.value<QColor>());
    };
    set("window", QPalette::Window);
    set("windowText", QPalette::WindowText);
    set("base", QPalette::Base);
    set("alternateBase", QPalette::AlternateBase);
    set("text", QPalette::Text);
    set("button", QPalette::Button);
    set("buttonText", QPalette::ButtonText);
    set("highlight", QPalette::Highlight);
    set("highlightedText", QPalette::HighlightedText);
    set("accent", QPalette::Accent);
    set("placeholderText", QPalette::PlaceholderText);
    set("mid", QPalette::Mid);
    set("dark", QPalette::Dark);
    set("light", QPalette::Light);
    set("midlight", QPalette::Midlight);
    set("shadow", QPalette::Shadow);
    set("toolTipBase", QPalette::ToolTipBase);
    set("toolTipText", QPalette::ToolTipText);
    const QVariant disabled = colors.value(QLatin1String("disabledText"));
    if (disabled.isValid()) {
        const QColor c = disabled.value<QColor>();
        for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            palette.setColor(QPalette::Disabled, role, c);
    }
    QGuiApplication::setPalette(palette);
}

bool AppInfo::offscreen() const
{
    if (m_screenshot)
        return true;
    const QString platform = QGuiApplication::platformName();
    return platform == QLatin1String("offscreen") || platform == QLatin1String("minimal");
}

AppInfo* AppInfo::instance()
{
    static AppInfo* const info = new AppInfo;
    return info;
}

AppInfo* AppInfo::create(QQmlEngine*, QJSEngine*)
{
    AppInfo* info = instance();
    QJSEngine::setObjectOwnership(info, QJSEngine::CppOwnership);
    return info;
}

QString AppInfo::name() const
{
    return QStringLiteral(MYBENCH_APP_NAME);
}

QString AppInfo::version() const
{
    return QStringLiteral(MYBENCH_VERSION);
}

QString AppInfo::homepage() const
{
    return QStringLiteral(MYBENCH_HOMEPAGE);
}

QString AppInfo::qtVersion() const
{
    return QString::fromLatin1(qVersion());
}

bool AppInfo::usbBackend() const
{
    return MYBENCH_WITH_USB != 0;
}

QString AppInfo::licenseText() const
{
    QFile file(u":/mybench/LICENSE"_s);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

QString AppInfo::platform() const
{
#if defined(Q_OS_MACOS)
    return u"macos"_s;
#elif defined(Q_OS_WIN)
    return u"windows"_s;
#elif defined(Q_OS_LINUX)
    return u"linux"_s;
#else
    return u"other"_s;
#endif
}

QStringList AppInfo::valueOptions()
{
    return {u"instrument"_s, u"panel"_s, u"split"_s, u"run"_s, u"open"_s, u"page"_s, u"device"_s, u"system-palette"_s,
            u"sidebar"_s, u"inspector"_s};
}

QStringList AppInfo::panels() const
{
    QStringList list = m_values.value(u"instrument"_s);
    list += m_values.value(u"panel"_s);
    list.removeDuplicates();
    return list;
}

void AppInfo::setLaunch(const QStringList& arguments, bool screenshot, const QString& theme)
{
    m_screenshot = screenshot;
    m_theme = theme;
    const QStringList valued = valueOptions();
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const QString& argument = arguments[i];
        if (!argument.startsWith(u"--"_s)) {
            m_positional.append(argument);
            continue;
        }
        QString name = argument.mid(2);
        QString value;
        const qsizetype equals = name.indexOf(u'=');
        if (equals >= 0) {
            value = name.mid(equals + 1);
            name.truncate(equals);
            m_values[name].append(value);
        } else if (valued.contains(name) && i + 1 < arguments.size() && !arguments[i + 1].startsWith(u"--"_s)) {
            m_values[name].append(arguments[++i]);
        } else {
            m_flags.append(name);
        }
    }
}

}  // namespace mybench
