// InstrumentRegistry: the instruments built into this executable.
//
// Each instrument module exports one registration function (no static-initializer magic, the
// modules are static libraries):
//
//     namespace mybench::instruments {
//     void registerScope(mybench::InstrumentRegistry& registry)
//     {
//         registry.registerInstrument({
//             .id = QStringLiteral("scope"),
//             .title = QStringLiteral("Oscilloscope"),
//             .shortTitle = QStringLiteral("Scope"),
//             .description = QStringLiteral("Two channels, edge trigger, measurements"),
//             .module = QStringLiteral("MyBench.Instruments.Scope"),
//             .icon = QStringLiteral("instrument-scope"),
//             .order = 20,
//             .createController = [] { return new ScopeController; },
//         });
//     }
//     }
//
// The app calls every built instrument's function (generated from MYBENCH_BUILT_INSTRUMENTS,
// see app/CMakeLists.txt); a dev executable calls its own. The registry owns one controller per
// instrument (created on first use, or all at once by createAll()), shared by the instrument's
// panel and its dashboard tiles.
//
// QML: the `InstrumentRegistry` singleton is a list model (roles below) and offers controller(id).
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <vector>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace mybench {

class InstrumentController;

struct InstrumentDescriptor {
    QString id;            // "scope": lower case; help page, settings key, --instrument argument
    QString title;         // "Oscilloscope"
    QString shortTitle;    // "Scope" (nav rail, tabs, chips); default: title
    QString description;   // one line for Home and the command palette
    QString module;        // QML module URI, "MyBench.Instruments.Scope"
    QString panelType;     // QML type of the full panel; default "<Name>Panel" (Name = last URI part)
    QString tileType;      // QML type of the dashboard tile; default "<Name>Tile"
    QString icon;          // MyBench.Controls icon name (Theme.icon()) or a URL; default "instrument-<id>"
    QString helpId;        // docs/help/<helpId>.md; default: id
    int order = 100;       // position in the nav rail / on Home (ascending)
    std::function<InstrumentController*()> createController;   // required

    QUrl panelUrl() const;   // qrc:/qt/qml/MyBench/Instruments/Scope/ScopePanel.qml
    QUrl tileUrl() const;
};

class InstrumentRegistry : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QStringList ids READ ids NOTIFY countChanged)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY runningChanged)
    Q_PROPERTY(bool anyRunning READ anyRunning NOTIFY runningChanged)

public:
    enum Role {
        InstrumentIdRole = Qt::UserRole + 1,
        TitleRole,
        ShortTitleRole,
        DescriptionRole,
        IconRole,
        PanelUrlRole,
        TileUrlRole,
        HelpIdRole,
        ControllerRole,   // InstrumentController* (null until created, see createAll())
        RunningRole,
        PausedRole,
        HasErrorRole,
        StateRole,
    };
    Q_ENUM(Role)

    static InstrumentRegistry* instance();
    static InstrumentRegistry* create(QQmlEngine*, QJSEngine*);

    // Adds an instrument (kept sorted by order, then id). Replaces one with the same id.
    void registerInstrument(InstrumentDescriptor descriptor);
    const InstrumentDescriptor* descriptor(const QString& id) const;
    const std::vector<InstrumentDescriptor>& descriptors() const { return m_descriptors; }

    // The instrument's controller, created (and initialize()d) on first use; nullptr for an
    // unknown id. Owned by the registry.
    Q_INVOKABLE mybench::InstrumentController* controller(const QString& id);
    // Controllers created so far.
    QList<InstrumentController*> controllers() const;
    Q_INVOKABLE void createAll();

    Q_INVOKABLE bool contains(const QString& id) const { return descriptor(id) != nullptr; }
    // Descriptor as a map (id, title, shortTitle, description, icon, panelUrl, tileUrl, helpId).
    Q_INVOKABLE QVariantMap info(const QString& id) const;
    Q_INVOKABLE int indexOf(const QString& id) const;

    // Stops every running instrument.
    Q_INVOKABLE void stopAll();
    // Stops every controller and joins its worker (at exit, before the device goes away).
    void shutdown();

    int count() const { return int(m_descriptors.size()); }
    QStringList ids() const;
    int runningCount() const;
    bool anyRunning() const { return runningCount() > 0; }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();
    void runningChanged();
    void controllerCreated(mybench::InstrumentController* controller);

private:
    explicit InstrumentRegistry(QObject* parent = nullptr);
    void watch(InstrumentController* controller);

    std::vector<InstrumentDescriptor> m_descriptors;
    QHash<QString, InstrumentController*> m_controllers;
};

}  // namespace mybench
