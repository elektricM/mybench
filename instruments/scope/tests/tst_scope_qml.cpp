// Scope display QML behaviour (offscreen): the FFT pane keeps the user's zoom across frames and
// FFT source changes until it is reset explicitly.
#include "scopecontroller.h"

#include <mybench/devicehub.h>
#include <mybench/offscreen.h>
#include <mydaq/sim_device.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using mybench::instruments::ScopeController;

class TestScopeQml : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        mybench::InstrumentController::setRestoreSavedSettings(false);
        mybench::DeviceHub::instance()->select(u"sim:0"_s);
        QTRY_VERIFY(mybench::DeviceHub::instance()->isConnected());
    }

    void fftZoomPersists()
    {
        ScopeController scope;
        scope.initialize();
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(MYBENCH_QML_DIR));
        mybench::OffscreenWindow offscreen(QSize(1000, 700));
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport MyBench.Instruments.Scope\nScopeDisplay { }\n", QUrl());
        std::unique_ptr<QObject> root(component.beginCreate(engine.rootContext()));
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setProperty("controller", QVariant::fromValue(&scope));
        component.completeCreate();
        auto* item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(item);
        offscreen.setRootItem(item);

        scope.setFftSource(ScopeController::FftChannel0);
        QSignalSpy frames(&scope, &ScopeController::recordChanged);
        scope.run();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 2, 20000);
        offscreen.renderFrame();
        auto* plot = root->findChild<QObject*>("fftPlot");
        QVERIFY(plot);
        auto* xAxis = plot->property("xAxis").value<QObject*>();
        QVERIFY(xAxis);
        const double home = xAxis->property("max").toDouble();
        QVERIFY(home > 1);

        // The user zooms in (interactive zoom sets a view range, not a new home).
        QVERIFY(QMetaObject::invokeMethod(xAxis, "setViewRange", Q_ARG(double, home * 0.2), Q_ARG(double, home * 0.4)));
        QVERIFY(xAxis->property("zoomed").toBool());

        // Frames keep arriving while it runs.
        const int before = frames.count();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= before + 5, 20000);
        for (int i = 0; i < 5; ++i) {
            offscreen.renderFrame();
            QCoreApplication::processEvents();
        }
        QVERIFY(xAxis->property("zoomed").toBool());
        QCOMPARE(xAxis->property("min").toDouble(), home * 0.2);
        QCOMPARE(xAxis->property("max").toDouble(), home * 0.4);

        // Another FFT source: same axis, same zoom.
        scope.setFftSource(ScopeController::FftChannel1);
        const int seen = frames.count();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= seen + 3, 20000);
        offscreen.renderFrame();
        QVERIFY(xAxis->property("zoomed").toBool());
        QCOMPARE(xAxis->property("min").toDouble(), home * 0.2);

        // Explicit reset.
        QVERIFY(QMetaObject::invokeMethod(plot, "resetView"));
        QVERIFY(!xAxis->property("zoomed").toBool());
        QCOMPARE(xAxis->property("max").toDouble(), home);

        scope.stop();
        QTRY_VERIFY(scope.isIdle());
        offscreen.setRootItem(nullptr);
    }
};

QTEST_MAIN(TestScopeQml)
#include "tst_scope_qml.moc"
