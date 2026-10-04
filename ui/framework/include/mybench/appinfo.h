// AppInfo: application identity and launch options for QML.
//
//     AppInfo.name            "MYBENCH" (MYBENCH_APP_NAME, never hard-coded elsewhere)
//     AppInfo.version         "0.1.0"
//     AppInfo.homepage        project web site (MYBENCH_HOMEPAGE)
//     AppInfo.qtVersion       Qt version at run time (qVersion())
//     AppInfo.usbBackend      true if the native libusb backend is built in (MYBENCH_WITH_USB)
//     AppInfo.licenseText()   the repository LICENSE (or the pending-license notice), compiled in
//     AppInfo.screenshot      true when rendering offscreen (--screenshot): no animations needed,
//                             no dialogs, deterministic layout
//     AppInfo.offscreen       true when nothing is shown on a real screen: --screenshot, or the
//                             offscreen/minimal platform (tests, CI). Popups and menus then stay
//                             Popup.Item and confirmations stay in the window (docs/UI.md §13.9)
//     AppInfo.theme           "dark" | "light" | "" (--theme)
//     AppInfo.instrument      --instrument/--panel <id> (or a positional instrument id)
//     AppInfo.panels          every --panel <id> (repeatable)
//     AppInfo.value("split")  value of --split <id>; "" if absent
//     AppInfo.flag("tile")    true if --tile was given
//
// Options understood by the shells (see shell.h / --help): --panel <id> (repeatable, alias
// --instrument), --split <id>, --run <ids|all>, --device <id>, --open <file.mybench>,
// --page overview|home, --sidebar full|compact|hidden, --inspector shown|hidden, and the flags
// --simulated, --tile, --show-help, --palette, --log, --preferences, --about, --device-info,
// --simulator-settings, --dark-displays, --fresh, --keep-settings, --no-animations,
// --no-unified-titlebar.
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace mybench {

class AppInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString platform READ platform CONSTANT)   // "macos", "windows", "linux", ...
    Q_PROPERTY(QString homepage READ homepage CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(bool usbBackend READ usbBackend CONSTANT)
    Q_PROPERTY(bool screenshot READ screenshot CONSTANT)
    Q_PROPERTY(bool offscreen READ offscreen CONSTANT)
    Q_PROPERTY(QString theme READ theme CONSTANT)
    Q_PROPERTY(QString instrument READ instrument CONSTANT)
    Q_PROPERTY(QStringList panels READ panels CONSTANT)
    Q_PROPERTY(QStringList arguments READ arguments CONSTANT)   // unparsed (positional) arguments
    Q_PROPERTY(bool animations READ animations CONSTANT)        // false with --no-animations / screenshots

public:
    static AppInfo* instance();
    static AppInfo* create(QQmlEngine*, QJSEngine*);

    QString name() const;
    QString version() const;
    QString platform() const;
    QString homepage() const;
    QString qtVersion() const;
    bool usbBackend() const;
    Q_INVOKABLE QString licenseText() const;
    bool screenshot() const { return m_screenshot; }
    bool offscreen() const;
    QString theme() const { return m_theme; }
    QString instrument() const
    {
        const QStringList list = panels();
        return list.isEmpty() ? QString() : list.first();
    }
    QStringList panels() const;
    QStringList arguments() const { return m_positional; }
    bool animations() const { return !m_screenshot && !flag(QStringLiteral("no-animations")); }

    // Last value of `--option <value>`; "" if absent.
    Q_INVOKABLE QString value(const QString& option) const
    {
        const QStringList list = m_values.value(option);
        return list.isEmpty() ? QString() : list.last();
    }
    Q_INVOKABLE QStringList values(const QString& option) const { return m_values.value(option); }
    Q_INVOKABLE bool flag(const QString& option) const { return m_flags.contains(option); }

    // Set by the shell bootstrap before QML loads. `arguments` excludes the program name and the
    // offscreen helper's options. Options taking a value: see valueOptions().
    void setLaunch(const QStringList& arguments, bool screenshot, const QString& theme);
    void setValue(const QString& option, const QString& value) { m_values.insert(option, {value}); }
    static QStringList valueOptions();

private:
    explicit AppInfo(QObject* parent = nullptr);

    bool m_screenshot = false;
    QString m_theme;
    QHash<QString, QStringList> m_values;
    QStringList m_flags;
    QStringList m_positional;
};

}  // namespace mybench
