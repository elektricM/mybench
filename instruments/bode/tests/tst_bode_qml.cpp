// Bode panel QML in an offscreen window: PicoScope-7 layout (no inspector, right column with the
// Sweep card and tool palette, floating tool panels), no sentence-style display text, result cards.
#include "bodecontroller.h"

#include <mybench/offscreen.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using mybench::instruments::BodeController;

class TestBodeQml : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        mybench::InstrumentController::setRestoreSavedSettings(false);
    }

    void layout()
    {
        BodeController ctl;
        ctl.initialize();
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(MYBENCH_QML_DIR));
        mybench::OffscreenWindow offscreen(QSize(1440, 900));
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport MyBench.Instruments.Bode\nBodePanel { }\n", QUrl());
        std::unique_ptr<QObject> root(component.beginCreate(engine.rootContext()));
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setProperty("controller", QVariant::fromValue(&ctl));
        component.completeCreate();
        auto* item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(item);
        offscreen.setRootItem(item);
        offscreen.renderFrame();
        offscreen.renderFrame();

        // No inspector, no display title: the wiring shows as a diagram while there is no sweep.
        int panels = 0;
        for (QObject* o : root->findChildren<QObject*>()) {
            if (o->metaObject()->indexOfProperty("hasInspector") < 0)
                continue;
            ++panels;
            QVERIFY(!o->property("hasInspector").toBool());
            QCOMPARE(o->property("displayTitle").toString(), QString());
            if (o->metaObject()->indexOfProperty("displayInfo") >= 0)
                QCOMPARE(o->property("displayInfo").toString(), QString());
        }
        QVERIFY(panels > 0);
        auto* wiring = root->findChild<QQuickItem*>(u"wiringDiagram"_s);
        QVERIFY(wiring);
        QVERIFY(wiring->isVisible());

        // Right column: the Sweep card and the tool palette; each tool opens its floating panel.
        QVERIFY(root->findChild<QObject*>(u"sweepCard"_s));
        QVERIFY(root->findChild<QObject*>(u"stepsKnob"_s));
        QVERIFY(root->findChild<QObject*>(u"polarityButtons"_s));
        QObject* tools = root->findChild<QObject*>(u"bodeTools"_s);
        QVERIFY(tools);
        const QStringList ids { u"graph"_s, u"markers"_s, u"cursor"_s, u"references"_s, u"export"_s };
        for (const QString& id : ids) {
            QVERIFY(QMetaObject::invokeMethod(tools, "activated", Q_ARG(QString, id)));
            offscreen.renderFrame();
            for (const QString& other : ids) {
                auto* panelItem = root->findChild<QQuickItem*>(other + u"Panel"_s);
                QVERIFY2(panelItem, qPrintable(other));
                QCOMPARE(panelItem->isVisible(), other == id);
            }
        }
        QVERIFY(QMetaObject::invokeMethod(tools, "activated", Q_ARG(QString, u"export"_s)));   // toggles closed
        QVERIFY(!root->findChild<QQuickItem*>(u"exportPanel"_s)->isVisible());

        // Result cards, including the gain-margin frequency.
        for (const char* name : { "cardCutoff", "cardUnity", "cardGainMargin", "cardGainMarginFrequency",
                                  "cardPhaseMargin", "cardMaxGain", "cardCursor" })
            QVERIFY2(root->findChild<QObject*>(QString::fromLatin1(name)), name);
        offscreen.setRootItem(nullptr);
    }
};

QTEST_MAIN(TestBodeQml)
#include "tst_bode_qml.moc"
