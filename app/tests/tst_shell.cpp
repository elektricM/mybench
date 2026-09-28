// QuickTest harness for the shell QML (WorkspaceState, CommandPalette, ...): registers three
// fake instruments ("aa", "bb", "cc") whose controllers count the shell hooks they receive.
#include <mybench/framework.h>

#include <QQmlEngine>
#include <QQuickStyle>
#include <QSettings>
#include <QTemporaryDir>
#include <QtQuickTest>

using namespace mybench;
using namespace Qt::StringLiterals;

class FakeInstrument : public InstrumentController
{
    Q_OBJECT
    Q_PROPERTY(int closedCount READ closedCount NOTIFY hooked)
    Q_PROPERTY(int safeStateCount READ safeStateCount NOTIFY hooked)
    Q_PROPERTY(double level READ level WRITE setLevel NOTIFY levelChanged)

public:
    explicit FakeInstrument(const QString& id) : InstrumentController(id)
    {
        connect(this, &InstrumentController::panelClosed, this, [this] { ++m_closed; emit hooked(); });
        addCommand({.id = u"level"_s, .title = u"Set level"_s, .argumentLabel = u"Level (V)"_s,
                    .argumentUnit = u"V"_s, .run = [this](const QVariant& v) { setLevel(v.toDouble()); }});
        addCommand({.id = u"poke"_s, .title = u"Poke"_s});
    }
    ~FakeInstrument() override { shutdown(); }
    int closedCount() const { return m_closed; }
    int safeStateCount() const { return m_safe; }
    double level() const { return m_level; }
    void setLevel(double level)
    {
        if (level != m_level) {
            m_level = level;
            emit levelChanged();
        }
    }

signals:
    void hooked();
    void levelChanged();

protected:
    WorkerFunction createWorker() override
    {
        return [](WorkerContext& ctx) {
            while (!ctx.stopRequested())
                ctx.sleepMs(5);
        };
    }
    void applySafeState() override
    {
        ++m_safe;
        emit hooked();
    }

private:
    int m_closed = 0;
    int m_safe = 0;
    double m_level = 0;
};

class Setup : public QObject
{
    Q_OBJECT
public slots:
    void applicationAvailable()
    {
        QQuickStyle::setStyle(u"MyBench.Style"_s);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
        QCoreApplication::setOrganizationName(u"MYBENCH-test"_s);
        QCoreApplication::setApplicationName(u"tst_shell"_s);
        InstrumentController::setRestoreSavedSettings(false);
        // MYBENCH_SHOT_DIR: tests write review images there (AppInfo.value("shot-dir")).
        const QString shots = qEnvironmentVariable("MYBENCH_SHOT_DIR");
        if (!shots.isEmpty())
            AppInfo::instance()->setLaunch({u"--shot-dir="_s + shots}, false, {});
        InstrumentRegistry* registry = InstrumentRegistry::instance();
        int order = 1;
        for (const QString& id : {u"aa"_s, u"bb"_s, u"cc"_s}) {
            registry->registerInstrument({.id = id, .title = id.toUpper() + u" Instrument"_s, .shortTitle = id.toUpper(),
                                          .module = u"Test.Fake"_s, .order = order++,
                                          .createController = [id] { return new FakeInstrument(id); }});
        }
        registry->createAll();
    }
    void qmlEngineAvailable(QQmlEngine*) {}
    void cleanupTestCase() { InstrumentRegistry::instance()->shutdown(); }

private:
    QTemporaryDir m_dir;
};

QUICK_TEST_MAIN_WITH_SETUP(tst_shell, Setup)
#include "tst_shell.moc"
