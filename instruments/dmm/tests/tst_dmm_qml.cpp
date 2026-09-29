// DMM panel layout (offscreen): the trend's time-range selector never overlaps the plot area,
// where the shared plot toolbar (zoom, reset, export) lives, at wide and narrow widths.
#include "dmmcontroller.h"

#include <mybench/devicehub.h>
#include <mybench/offscreen.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using mybench::instruments::DmmController;

class TestDmmQml : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        mybench::InstrumentController::setRestoreSavedSettings(false);
    }

    void trendControlsDoNotOverlap_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("1440") << QSize(1440, 900);
        QTest::newRow("1000") << QSize(1000, 700);
        QTest::newRow("split") << QSize(640, 700);
        QTest::newRow("narrow") << QSize(420, 620);
    }

    void trendControlsDoNotOverlap()
    {
        QFETCH(QSize, size);
        DmmController dmm;
        dmm.initialize();
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(MYBENCH_QML_DIR));
        mybench::OffscreenWindow offscreen(size);
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport MyBench.Instruments.Dmm\nDmmPanel { }\n", QUrl());
        std::unique_ptr<QObject> root(component.beginCreate(engine.rootContext()));
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setProperty("controller", QVariant::fromValue(&dmm));
        component.completeCreate();
        auto* item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(item);
        offscreen.setRootItem(item);
        offscreen.renderFrame();
        offscreen.renderFrame();

        auto* selector = root->findChild<QQuickItem*>("trendWindow");
        auto* plot = root->findChild<QQuickItem*>("trendPlot");
        QVERIFY(selector && plot);
        const QRectF s(selector->mapToScene(QPointF(0, 0)), QSizeF(selector->width(), selector->height()));
        const QRectF p(plot->mapToScene(QPointF(0, 0)), QSizeF(plot->width(), plot->height()));
        QVERIFY2(s.width() > 0 && p.height() > 0, "not laid out");
        QVERIFY2(!s.intersects(p), qPrintable(u"selector %1,%2 %3x%4 vs plot %5,%6 %7x%8"_s
                                                  .arg(s.x()).arg(s.y()).arg(s.width()).arg(s.height())
                                                  .arg(p.x()).arg(p.y()).arg(p.width()).arg(p.height())));
        QVERIFY(s.right() <= item->width() + 0.5 && s.left() >= 0);
        offscreen.setRootItem(nullptr);
    }
};

QTEST_MAIN(TestDmmQml)
#include "tst_dmm_qml.moc"
