// Qt Quick Test runner for MyBench.Controls (tst_*.qml next to this file). The MyBench style is
// selected like in the executables, and two test instruments ("alpha", "beta") are registered
// so the workspace logic (InstrumentRegistry.contains) can be exercised.
#include <mybench/framework.h>

#include <QQmlEngine>
#include <QQuickStyle>
#include <QtQuickTest>

using namespace Qt::StringLiterals;

namespace {

class IdleInstrument : public mybench::InstrumentController
{
    Q_OBJECT

public:
    explicit IdleInstrument(const QString& id) : InstrumentController(id) {}
    ~IdleInstrument() override { shutdown(); }

protected:
    WorkerFunction createWorker() override
    {
        return [](mybench::WorkerContext& ctx) {
            while (!ctx.stopRequested())
                ctx.sleepMs(10);
        };
    }
};

}  // namespace

class Setup : public QObject
{
    Q_OBJECT

public slots:
    void applicationAvailable()
    {
        // MYBENCH_TEST_STYLE runs the tests under another controls style (e.g. "macOS").
        const QString style = qEnvironmentVariable("MYBENCH_TEST_STYLE");
        QQuickStyle::setStyle(style.isEmpty() ? u"MyBench.Style"_s : style);
        mybench::InstrumentRegistry* registry = mybench::InstrumentRegistry::instance();
        for (const QString& id : {u"alpha"_s, u"beta"_s, u"gamma"_s}) {
            registry->registerInstrument({.id = id, .title = id, .shortTitle = id, .module = u"Test"_s,
                                          .createController = [id] { return new IdleInstrument(id); }});
        }
        registry->createAll();
    }
    void cleanupTestCase() { mybench::InstrumentRegistry::instance()->shutdown(); }
};

QUICK_TEST_MAIN_WITH_SETUP(controls, Setup)

#include "main.moc"
