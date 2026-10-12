// Data Logger panel layout (offscreen): chart left, control column right, value
// cards at the bottom, at wide, split and narrow sizes; the Log File tool's single Format choice
// shows a restored ELVISmx layout.
#include "logcontroller.h"

#include <mybench/devicehub.h>
#include <mybench/offscreen.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using mybench::instruments::LogController;

// Items by objectName through the visual tree (Repeater delegates have no QObject parent).
static QQuickItem* findItem(QQuickItem* item, const QString& name)
{
    if (!item)
        return nullptr;
    if (item->objectName() == name)
        return item;
    for (QQuickItem* child : item->childItems())
        if (QQuickItem* found = findItem(child, name))
            return found;
    return nullptr;
}

class TestLogQml : public QObject
{
    Q_OBJECT


    // Every object with a display title (the InstrumentPanel and its pane parts) shows none:
    // the values are in their controls ("less text, more instrument").
    static void verifyNoDisplayText(QObject* root)
    {
        int panels = 0;
        for (QObject* o : root->findChildren<QObject*>()) {
            if (o->metaObject()->indexOfProperty("displayTitle") < 0)
                continue;
            ++panels;
            QCOMPARE(o->property("displayTitle").toString(), QString());
            if (o->metaObject()->indexOfProperty("displayInfo") >= 0)
                QCOMPARE(o->property("displayInfo").toString(), QString());
        }
        QVERIFY(panels > 0);
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        mybench::InstrumentController::setRestoreSavedSettings(false);
    }

    void layoutAtSizes_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("1440") << QSize(1440, 900);
        QTest::newRow("split") << QSize(640, 700);
        QTest::newRow("narrow") << QSize(420, 620);
    }

    void layoutAtSizes()
    {
        QFETCH(QSize, size);
        LogController log;
        log.initialize();
        log.setFileLayout(LogController::ElvismxLayout);     // as restored from an old session
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(MYBENCH_QML_DIR));
        mybench::OffscreenWindow offscreen(size);
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport MyBench.Instruments.Log\nLogPanel { }\n", QUrl());
        std::unique_ptr<QObject> root(component.beginCreate(engine.rootContext()));
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setProperty("controller", QVariant::fromValue(&log));
        component.completeCreate();
        auto* item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(item);
        offscreen.setRootItem(item);
        offscreen.renderFrame();
        offscreen.renderFrame();

        const auto rectOf = [](QQuickItem* i) {
            return QRectF(i->mapToScene(QPointF(0, 0)), QSizeF(i->width(), i->height()));
        };
        const auto describe = [](const QRectF& r) {
            return u"%1,%2 %3x%4"_s.arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
        };
        const QRectF pane(-0.5, -0.5, item->width() + 1, item->height() + 1);
        auto* plot = root->findChild<QQuickItem*>(u"logPlot"_s);
        auto* controls = root->findChild<QQuickItem*>(u"logControls"_s);
        auto* cards = root->findChild<QQuickItem*>(u"logCards"_s);
        QVERIFY(plot && controls && cards && root->findChild<QQuickItem*>(u"logRunButton"_s));
        const QRectF p = rectOf(plot), c = rectOf(controls);
        QVERIFY2(p.width() > 0 && p.height() > 0 && c.width() > 0, qPrintable(describe(p)));
        QVERIFY2(pane.contains(p) && pane.contains(c), qPrintable(describe(p) + u" / "_s + describe(c)));
        QVERIFY2(p.right() <= c.left() + 0.5, qPrintable(describe(p) + u" vs "_s + describe(c)));
        QVERIFY(c.width() < p.width() || size.width() < 500);   // the display stays the larger part

        // Less text: no display title or info line; the AI cards carry only the range chip.
        verifyNoDisplayText(root.get());
        if (QTest::currentTestFailed())
            return;
        auto* ai0 = root->findChild<QObject*>(u"logCardAi0"_s);
        QVERIFY(ai0);
        QCOMPARE(ai0->property("subtitle").toString(), QString());
        QCOMPARE(ai0->property("facts").toStringList(), QStringList{u"±10 V"_s});

        // No "Monitoring" / "Not recording" chip (the Record button shows that); the
        // DMM card off = header only, on = function pictures + a range knob, running = the
        // function and range as facts (never a grid of disabled buttons); tools pinned and shown.
        QVERIFY(!root->findChild<QQuickItem*>(u"logRecordState"_s)->isVisible());
        auto* tools = root->findChild<QQuickItem*>(u"logTools"_s);
        QVERIFY(tools && tools->isVisible() && tools->height() > 20);
        auto* dmmFunctions = findItem(item, u"logDmmFunctions"_s);
        QVERIFY(dmmFunctions);
        QVERIFY(!dmmFunctions->isVisible());
        log.setDmmEnabled(true);
        offscreen.renderFrame();
        QVERIFY(dmmFunctions->isVisible());
        for (int i = 0; i < 5; ++i)
            QVERIFY(!findItem(item, u"logDmmFunction"_s + QString::number(i))->property("iconName").toString().isEmpty());
        QVERIFY(findItem(item, u"logDmmRange"_s)->isVisible());
        auto* dmmValue = findItem(item, u"logValueDmm"_s);
        QVERIFY(dmmValue && dmmValue->isVisible());
        QVERIFY2(dmmValue->width() < item->width() * 0.6, "a card, not a strip across the pane");
        if (const QString dir = qEnvironmentVariable("MYBENCH_TEST_SHOT_DIR"); !dir.isEmpty())
            offscreen.grab().save(u"%1/log-dmm-%2.png"_s.arg(dir, QString::fromLatin1(QTest::currentDataTag())));
        log.setDmmEnabled(false);
        offscreen.renderFrame();

        // Log File tool: one Format choice; the restored ELVISmx layout shows as "ELVISmx".
        auto* fileType = root->findChild<QQuickItem*>(u"logFileType"_s);
        QVERIFY(fileType);
        QCOMPARE(fileType->property("currentIndex").toInt(), int(LogController::ElvismxFile));
        root->setProperty("tool", u"file"_s);
        offscreen.renderFrame();
        auto* filePanel = root->findChild<QQuickItem*>(u"filePanel"_s);
        QVERIFY(filePanel && filePanel->isVisible());
        QVERIFY2(rectOf(filePanel).right() <= c.left() + 0.5, qPrintable(describe(rectOf(filePanel))));
        // Review aid: MYBENCH_TEST_SHOT_DIR=<dir> saves the frame with the tool panel open.
        if (const QString dir = qEnvironmentVariable("MYBENCH_TEST_SHOT_DIR"); !dir.isEmpty())
            offscreen.grab().save(u"%1/%2-%3.png"_s.arg(dir, u"log-tool"_s, QString::fromLatin1(QTest::currentDataTag())));

        if (size.width() == 1440) {
            // Running: the DMM settings are locked and shown as facts, not as disabled buttons.
            log.setDmmEnabled(true);
            log.run();
            QTRY_VERIFY_WITH_TIMEOUT(log.state() == mybench::InstrumentController::Running, 5000);
            offscreen.renderFrame();
            QVERIFY(!dmmFunctions->isVisible());
            QVERIFY(!findItem(item, u"logDmmRange"_s)->isVisible());
            const QStringList facts = root->findChild<QObject*>(u"logCardDmm"_s)->property("facts").toStringList();
            QCOMPARE(facts, QStringList({u"DC V"_s, u"Auto"_s}));
            log.stop();
            QTRY_VERIFY_WITH_TIMEOUT(log.state() == mybench::InstrumentController::Idle, 5000);
        }
        offscreen.setRootItem(nullptr);
    }
};

QTEST_MAIN(TestLogQml)
#include "tst_log_qml.moc"
