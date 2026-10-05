// Dsa panel QML in an offscreen window ("less text, more instrument"): every value shows once,
// in its control, so the display carries no sentence-style title or info line.
#include "dsacontroller.h"

#include <mybench/offscreen.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using mybench::instruments::DsaController;

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

class TestDsaQml : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        mybench::InstrumentController::setRestoreSavedSettings(false);
    }

    void lessText()
    {
        DsaController ctl;
        ctl.initialize();
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(MYBENCH_QML_DIR));
        mybench::OffscreenWindow offscreen(QSize(1440, 900));
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport MyBench.Instruments.Dsa\nDsaPanel { }\n", QUrl());
        std::unique_ptr<QObject> root(component.beginCreate(engine.rootContext()));
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setProperty("controller", QVariant::fromValue(&ctl));
        component.completeCreate();
        auto* item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(item);
        offscreen.setRootItem(item);
        offscreen.renderFrame();
        offscreen.renderFrame();

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
        // The Input card names the source once (its checked button), not again as a subtitle.
        auto* input = root->findChild<QObject*>(u"inputCard"_s);
        QVERIFY(input);
        QCOMPARE(input->property("subtitle").toString(), QString());

        // Result cards: SNR next to THD / SINAD; every window button shows its shape.
        auto* snr = root->findChild<QObject*>(u"cardSnr"_s);
        QVERIFY(snr);
        QCOMPARE(snr->property("title").toString(), u"SNR"_s);
        QCOMPARE(snr->property("value").toString(), u"—"_s);   // no result yet
        root->setProperty("tool", u"window"_s);   // the Window panel open
        offscreen.renderFrame();
        for (int i = 0; i < ctl.windowLabels().size(); ++i) {
            QQuickItem* button = findItem(item, u"window"_s + QString::number(i));
            QVERIFY2(button, qPrintable(QString::number(i)));
            QVERIFY(!button->property("glyph").toString().isEmpty());
        }

        // Round 5: the shared right column (fixed width, palette pinned and shown), pictograms and
        // readable labels on the input buttons, the range as ±10 V / ±2 V.
        QQuickItem* column = findItem(item, u"dsaColumn"_s);
        QVERIFY(column);
        QQuickItem* tools = findItem(item, u"dsaTools"_s);
        QVERIFY(tools);
        QQuickItem* slot = findItem(item, u"instrumentColumnTools"_s);
        QVERIFY(slot);
        QVERIFY(tools->isVisible());
        QCOMPARE(tools->parentItem(), slot);
        QVERIFY(tools->height() > 30);
        for (int i = 0; i < 4; ++i) {
            QQuickItem* button = findItem(item, u"source_"_s + QString::number(i));
            QVERIFY(button);
            QVERIFY(!button->property("glyph").toString().isEmpty());
            QCOMPARE(button->property("text").toString(), ctl.sourceLabels().at(i));
        }

        // A single spectrum on the simulator: the SNR card shows the controller's SNR, with
        // statistics once there are two results.
        ctl.runOnce();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.state() == mybench::InstrumentController::Idle && ctl.hasResult(), 10000);
        QCOMPARE(snr->property("value").toString(), QString::number(ctl.snr(), 'f', 2) + u" dB"_s);
        QVERIFY(findItem(item, u"cardPower"_s)->property("value").toString().endsWith(u" Vrms²"_s));
        offscreen.setRootItem(nullptr);
    }

    // Round 5: Single is meaningful while running (finish after this record) instead of a grey
    // slab; Restart lives beside the averaging progress, only while it applies; the magnitude axis
    // cannot be panned or zoomed into an empty region (Alex); every averaging mode has a pictogram;
    // the cursor card shows C1 and C2.
    void runningControls()
    {
        DsaController ctl;
        ctl.initialize();
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(MYBENCH_QML_DIR));
        mybench::OffscreenWindow offscreen(QSize(1440, 900));
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport MyBench.Instruments.Dsa\nDsaPanel { }\n", QUrl());
        std::unique_ptr<QObject> root(component.beginCreate(engine.rootContext()));
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setProperty("controller", QVariant::fromValue(&ctl));
        component.completeCreate();
        auto* item = qobject_cast<QQuickItem*>(root.get());
        offscreen.setRootItem(item);
        offscreen.renderFrame();

        QQuickItem* single = findItem(item, u"dsaSingleButton"_s);
        QVERIFY(single);
        QVERIFY(single->isEnabled());
        QVERIFY(!findItem(item, u"dsaRestartButton"_s)->isVisible());   // idle: not shown
        ctl.setAveragingMode(DsaController::RmsAveraging);
        ctl.setWeighting(DsaController::LinearWeighting);
        ctl.run();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.state() == mybench::InstrumentController::Running && ctl.hasResult(), 10000);
        offscreen.renderFrame();
        QVERIFY2(single->isEnabled(), "Single while running = stop after this record");
        QTRY_VERIFY(findItem(item, u"dsaRestartButton"_s)->isVisible());

        // Magnitude axis: pan and zoom stay inside the scale.
        QQuickItem* plot = findItem(item, u"dsaSpectrumPlot"_s);
        QVERIFY(plot);
        auto* axis = plot->property("yAxis").value<QObject*>();
        QVERIFY(axis);
        const double lo = ctl.spectrumMinimum(), hi = ctl.spectrumMaximum();
        auto inside = [&] {
            const double mn = axis->property("min").toDouble(), mx = axis->property("max").toDouble();
            return mn >= lo - 1e-6 && mx <= hi + 1e-6;
        };
        QMetaObject::invokeMethod(axis, "zoom", Q_ARG(double, 4.0), Q_ARG(double, 0.5 * (lo + hi)));
        QMetaObject::invokeMethod(axis, "pan", Q_ARG(double, 5.0));
        QVERIFY2(inside(), qPrintable(u"%1 … %2"_s.arg(axis->property("min").toDouble()).arg(axis->property("max").toDouble())));
        QMetaObject::invokeMethod(axis, "pan", Q_ARG(double, -5.0));
        QVERIFY(inside());
        QMetaObject::invokeMethod(axis, "zoom", Q_ARG(double, 0.05), Q_ARG(double, lo));
        QVERIFY(inside());

        single->setProperty("focus", true);
        QMetaObject::invokeMethod(single, "clicked");
        QTRY_VERIFY_WITH_TIMEOUT(ctl.state() == mybench::InstrumentController::Idle, 10000);

        root->setProperty("tool", u"averaging"_s);
        offscreen.renderFrame();
        for (int i = 0; i < 4; ++i)
            QVERIFY(!findItem(item, u"averaging"_s + QString::number(i))->property("glyph").toString().isEmpty());

        ctl.setCursorsOn(true);
        offscreen.renderFrame();
        QQuickItem* cursorCard = findItem(item, u"cardCursors"_s);
        QVERIFY(cursorCard && cursorCard->isVisible());
        QCOMPARE(findItem(item, u"cursorCardC1"_s)->property("text").toString(), ctl.cursor1Text());
        // Review aid: MYBENCH_SHOT_DIR=<dir> saves the Cursors tool with the cursor card.
        if (const QString dir = qEnvironmentVariable("MYBENCH_SHOT_DIR"); !dir.isEmpty()) {
            root->setProperty("tool", u"cursors"_s);
            offscreen.renderFrame();
            offscreen.grab().save(dir + u"/dsa-cursors.png"_s);
        }
        offscreen.setRootItem(nullptr);
    }
};

QTEST_MAIN(TestDsaQml)
#include "tst_dsa_qml.moc"
