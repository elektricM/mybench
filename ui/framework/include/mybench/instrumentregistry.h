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
//             .group = QStringLiteral("measure"),
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

// A secondary view of an instrument, shown in a pane of its own (docs/UI.md §13.7), e.g. the
// Waveform Editor of the generator: {"gen/editor", "Waveform Editor", "wave-arbitrary",
// "GenEditorPanel"}. Its panel is <module>/<panelType>.qml and shares the instrument's controller.
struct ViewDescriptor {
    QString id;          // pane id "<instrument>/<view>", e.g. "gen/editor"
    QString title;       // "Waveform Editor"
    QString icon;        // MyBench.Controls icon name
    QString panelType;   // QML type in the instrument's module, "GenEditorPanel"

    QUrl panelUrl(const QString& module) const;   // qrc:/qt/qml/<module path>/<panelType>.qml
};

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
    int order = 100;       // position in the sidebar / on Overview (ascending)
    QString group;         // sidebar group: "measure" | "generate" | "digital" | "apps"; default "apps"
    QList<ViewDescriptor> views;   // secondary views (panes of their own), e.g. "gen/editor"
    std::function<InstrumentController*()> createController;   // required

    QUrl panelUrl() const;   // qrc:/qt/qml/MyBench/Instruments/Scope/ScopePanel.qml
    QUrl tileUrl() const;
    // Panel of a secondary view ("gen/editor"); empty for an unknown view id.
    QUrl viewUrl(const QString& viewId) const;
    const ViewDescriptor* view(const QString& viewId) const;
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
    // Something for Stop all to do: an instrument runs, is paused, or idles holding outputs.
    Q_PROPERTY(bool anyStoppable READ anyStoppable NOTIFY runningChanged)

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
        GroupRole,      // "measure" | "generate" | "digital" | "apps"
        ViewsRole,      // [{ id, title, icon, panelType, panelUrl }]
        KeyValueRole,   // InstrumentController::keyValue ("" until the controller exists)
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
    // Descriptor as a map (instrumentId, title, shortTitle, description, icon, module, panelUrl,
    // tileUrl, helpId, order, group, views: [{ id, title, icon, panelType, panelUrl }]).
    Q_INVOKABLE QVariantMap info(const QString& id) const;
    // The secondary view "<instrument>/<view>" as { id, title, icon, panelType, panelUrl,
    // instrumentId }; {} for an unknown view.
    Q_INVOKABLE QVariantMap viewInfo(const QString& viewId) const;
    Q_INVOKABLE int indexOf(const QString& id) const;

    // Stops every running instrument (and returns idle ones that hold outputs to their safe
    // state, see InstrumentController::holdsOutputs).
    Q_INVOKABLE void stopAll();
    // Opens (or focuses) the panel of `id`, after applying `preset` to its controller
    // (InstrumentController::applyPreset): e.g. the Scope's FFT opening the Spectrum Analyzer
    // with its span. `beside`: in a split pane next to the current panel. The shell handles
    // openRequested(). Returns false for an unknown id.
    Q_INVOKABLE bool openInstrument(const QString& id, const QVariantMap& preset = {}, bool beside = false);
    // The shell closed the last view of `id`: InstrumentController::closePanel() if the
    // controller exists (never creates one).
    Q_INVOKABLE void notifyPanelClosed(const QString& id);
    // "<Title>: <document>" of every controller with an unsaved document (quit prompt).
    Q_INVOKABLE QStringList unsavedDocuments() const;
    // Every existing controller: InstrumentController::resetForSession() (SessionManager).
    void resetAllForSession();
    // Stops every controller and joins its worker (at exit, before the device goes away).
    void shutdown();

    int count() const { return int(m_descriptors.size()); }
    QStringList ids() const;
    int runningCount() const;
    bool anyRunning() const { return runningCount() > 0; }
    bool anyStoppable() const;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();
    void runningChanged();
    void controllerCreated(mybench::InstrumentController* controller);
    // openInstrument(): the shell shows the panel of `id` (beside the current one if asked).
    void openRequested(const QString& id, bool beside);

private:
    explicit InstrumentRegistry(QObject* parent = nullptr);
    void watch(InstrumentController* controller);

    std::vector<InstrumentDescriptor> m_descriptors;
    QHash<QString, InstrumentController*> m_controllers;
};

}  // namespace mybench
