// Bode panel QML in an offscreen window: PicoScope-7 layout (no inspector, InstrumentColumn with
// the Sweep card and the tool palette, floating tool panels), header knobs that never overlap,
// picture buttons with pictograms, the wiring diagram only on an empty plot (and in the
// Connections tool), one row of result cards and the C1/C2 cursor pair in the cursor colors.
#include "bodecontroller.h"

#include <mybench/offscreen.h>

#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using mybench::instruments::BodeController;

class TestBodeQml : public QObject
{
    Q_OBJECT

    struct Panel
    {
        std::unique_ptr<QQmlEngine> engine = std::make_unique<QQmlEngine>();
        std::unique_ptr<QObject> root;
        QString error;
        QQuickItem* item() const { return qobject_cast<QQuickItem*>(root.get()); }
    };

    static void create(Panel& p, BodeController* ctl)
    {
        p.engine->addImportPath(QStringLiteral(MYBENCH_QML_DIR));
        QQmlComponent component(p.engine.get());
        component.setData("import QtQuick\nimport MyBench.Instruments.Bode\nBodePanel { }\n", QUrl());
        p.root.reset(component.beginCreate(p.engine->rootContext()));
        if (!p.root) {
            p.error = component.errorString();
            return;
        }
        p.root->setProperty("controller", QVariant::fromValue(ctl));
        component.completeCreate();
    }

    static QRectF sceneRect(QQuickItem* i)
    {
        return QRectF(i->mapToScene(QPointF(0, 0)), QSizeF(i->width(), i->height()));
    }

    // Visual check while developing: MYBENCH_TEST_SHOTS=<dir> saves the frame.
    static void shot(mybench::OffscreenWindow& offscreen, const QString& name)
    {
        const QString dir = qEnvironmentVariable("MYBENCH_TEST_SHOTS");
        if (dir.isEmpty())
            return;
        QObject* grabber = offscreen.window()->findChild<QObject*>(u"mybench_offscreen_grabber"_s);
        QImage image;
        if (grabber && QMetaObject::invokeMethod(grabber, "grab", Q_RETURN_ARG(QImage, image)))
            image.save(dir + u"/"_s + name + u".png"_s);
    }

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
        mybench::OffscreenWindow offscreen(QSize(1440, 900));
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        Panel p;
        create(p, &ctl);
        QVERIFY2(p.root, qPrintable(p.error));
        QObject* root = p.root.get();
        offscreen.setRootItem(p.item());
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

        // Right column: the shared InstrumentColumn with the Sweep card and the tool palette.
        auto* column = root->findChild<QQuickItem*>(u"bodeColumn"_s);
        QVERIFY(column);
        QCOMPARE(column->width(), 260.0);
        QVERIFY(root->findChild<QObject*>(u"sweepCard"_s));
        QVERIFY(root->findChild<QObject*>(u"stepsKnob"_s));

        // Every picture button of the Sweep card has a pictogram (Alex: "no icons here?").
        for (const char* group : {"modeButtons", "polarityButtons"}) {
            auto* row = root->findChild<QQuickItem*>(QString::fromLatin1(group));
            QVERIFY2(row, group);
            int buttons = 0;
            for (QQuickItem* child : row->childItems()) {
                if (child->metaObject()->indexOfProperty("hasPicture") < 0)
                    continue;
                ++buttons;
                QVERIFY2(child->property("hasPicture").toBool(), qPrintable(child->property("text").toString()));
            }
            QCOMPARE(buttons, 2);
        }

        // Tools: Graph, Cursors (with the markers), References, Connections; Export is the ⋯ menu's.
        QObject* tools = root->findChild<QObject*>(u"bodeTools"_s);
        QVERIFY(tools);
        QStringList ids;
        for (const QVariant& v : tools->property("model").toList())
            ids << v.toMap().value(u"id"_s).toString();
        QCOMPARE(ids, (QStringList{u"graph"_s, u"cursors"_s, u"references"_s, u"connections"_s}));
        for (const QString& id : ids) {
            QVERIFY(QMetaObject::invokeMethod(tools, "activated", Q_ARG(QString, id)));
            offscreen.renderFrame();
            for (const QString& other : ids) {
                auto* panelItem = root->findChild<QQuickItem*>(other + u"Panel"_s);
                QVERIFY2(panelItem, qPrintable(other));
                QCOMPARE(panelItem->isVisible(), other == id);
            }
        }
        QVERIFY(QMetaObject::invokeMethod(tools, "activated", Q_ARG(QString, u"connections"_s)));   // toggles closed
        QVERIFY(!root->findChild<QQuickItem*>(u"connectionsPanel"_s)->isVisible());
        QVERIFY(!root->findChild<QObject*>(u"exportPanel"_s));
        QVERIFY(!root->findChild<QObject*>(u"markersPanel"_s));

        // Result cards and the cursor readout in ONE row at 1440 px.
        qreal rowY = -1;
        for (const char* name : { "cardCutoff", "cardUnity", "cardGainMargin", "cardGainMarginFrequency",
                                  "cardPhaseMargin", "cardMaxGain", "cardCursor" }) {
            auto* card = root->findChild<QQuickItem*>(QString::fromLatin1(name));
            QVERIFY2(card, name);
            const QRectF r = sceneRect(card);
            if (rowY < 0)
                rowY = r.y();
            QVERIFY2(qAbs(r.y() - rowY) < 0.5, qPrintable(u"%1 not on the first row"_s.arg(QLatin1StringView(name))));
            QVERIFY2(r.right() <= 1440.5, name);
        }
        shot(offscreen, u"bode-empty"_s);
        offscreen.setRootItem(nullptr);
    }

    // The header's Start / Stop / Amplitude knob fields keep their implicit width and never overlap
    // (they were squeezed to 180 px and drawn over each other).
    void headerKnobsDoNotOverlap_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("1440") << QSize(1440, 900);
        QTest::newRow("1024") << QSize(1024, 700);
        QTest::newRow("split") << QSize(760, 700);
    }
    void headerKnobsDoNotOverlap()
    {
        QFETCH(QSize, size);
        BodeController ctl;
        ctl.initialize();
        mybench::OffscreenWindow offscreen(size);
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        Panel p;
        create(p, &ctl);
        QVERIFY2(p.root, qPrintable(p.error));
        offscreen.setRootItem(p.item());
        offscreen.renderFrame();
        offscreen.renderFrame();
        QList<QRectF> rects;
        for (const char* name : {"bodeRunButton", "keepReferenceButton", "startKnob", "stopKnob", "amplitudeKnob"}) {
            auto* knob = p.root->findChild<QQuickItem*>(QString::fromLatin1(name));
            QVERIFY2(knob && knob->isVisible(), name);
            QVERIFY2(knob->width() >= knob->implicitWidth() - 0.5,
                     qPrintable(u"%1 squeezed: %2 < %3"_s.arg(QLatin1StringView(name)).arg(knob->width()).arg(knob->implicitWidth())));
            const QRectF r = sceneRect(knob);
            QVERIFY2(r.left() >= -0.5 && r.right() <= size.width() + 0.5, name);
            for (const QRectF& other : rects)
                QVERIFY2(!r.adjusted(0.5, 0.5, -0.5, -0.5).intersects(other), qPrintable(u"%1 overlaps"_s.arg(QLatin1StringView(name))));
            rects << r;
        }
        offscreen.setRootItem(nullptr);
    }

    // After a sweep: no wiring diagram over the data; with only a reference shown, none either
    // (Alex image 1). Cursors: C1 / C2 in Theme.cursor1 / cursor2 on both plots and the readout
    // with Δ and the slope.
    void sweepCursorsAndReferences()
    {
        BodeController ctl;
        ctl.initialize();
        mybench::OffscreenWindow offscreen(QSize(1440, 900));
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        Panel p;
        create(p, &ctl);
        QVERIFY2(p.root, qPrintable(p.error));
        QObject* root = p.root.get();
        offscreen.setRootItem(p.item());
        offscreen.renderFrame();
        ctl.setSweepMode(BodeController::Fast);
        ctl.run();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.state() == mybench::InstrumentController::Idle, 30000);
        QCOMPARE(ctl.pointCount(), 11);
        offscreen.renderFrame();
        auto* wiring = root->findChild<QQuickItem*>(u"wiringDiagram"_s);
        QVERIFY(!wiring->isVisible());

        ctl.setCursorsOn(true);
        ctl.setCursorFrequency(2500);
        offscreen.renderFrame();
        auto* c1 = root->findChild<QObject*>(u"gainCursor1"_s);
        auto* c2 = root->findChild<QObject*>(u"gainCursor2"_s);
        QVERIFY(c1 && c2);
        QVERIFY(c1->property("visible").toBool());
        QVERIFY(c2->property("visible").toBool());
        const QColor color1 = root->property("cursor1Color").value<QColor>();
        const QColor color2 = root->property("cursor2Color").value<QColor>();
        QCOMPARE(c1->property("color").value<QColor>(), color1);
        QCOMPARE(c2->property("color").value<QColor>(), color2);
        QVERIFY(color1 != color2);
        QVERIFY(color1 != root->property("gainColor").value<QColor>());
        QVERIFY(color2 != root->property("phaseColor").value<QColor>());
        QVERIFY(color2 != root->property("marginColor").value<QColor>());
        auto* readout = root->findChild<QQuickItem*>(u"cardCursor"_s);
        QVERIFY(readout);
        QVERIFY(readout->property("cursorsShown").toBool());
        const QString slope = readout->property("slopeText").toString();
        QVERIFY2(slope.endsWith(u"dB/dec"_s) && slope.startsWith(u"−"_s), qPrintable(slope));
        QVERIFY(readout->property("deltaFrequencyText").toString().startsWith(u"+"_s));
        shot(offscreen, u"bode-cursors"_s);

        // Only a reference left: still no diagram over it.
        QVERIFY(ctl.keepAsReference(u"RC"_s));
        ctl.clearSweep();
        offscreen.renderFrame();
        QCOMPARE(ctl.pointCount(), 0);
        QVERIFY(!wiring->isVisible());
        ctl.clearReferences();
        offscreen.renderFrame();
        QVERIFY(wiring->isVisible());
        offscreen.setRootItem(nullptr);
    }
};

QTEST_MAIN(TestBodeQml)
#include "tst_bode_qml.moc"
