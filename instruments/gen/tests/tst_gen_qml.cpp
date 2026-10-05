// Generator panel layout (offscreen, round-2 layout): display left, channel cards and the tool
// palette right, no form inspector; waveform pictures and the Editor tool drive the controller.
#include "gencontroller.h"

#include <mybench/devicehub.h>
#include <mybench/offscreen.h>

#include <QHash>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using mybench::instruments::GenChannel;
using mybench::instruments::GenController;

class TestGenQml : public QObject
{
    Q_OBJECT

    struct Panel
    {
        std::unique_ptr<QQmlEngine> engine = std::make_unique<QQmlEngine>();
        std::unique_ptr<QObject> root;
        QString error;
    };

    // Repeater delegates are not QObject children of the view: search the visual item tree.
    static QQuickItem* findItem(QQuickItem* item, const QString& name)
    {
        if (!item)
            return nullptr;
        if (item->objectName() == name)
            return item;
        const auto children = item->childItems();
        for (QQuickItem* child : children) {
            if (QQuickItem* found = findItem(child, name))
                return found;
        }
        return nullptr;
    }

    static void create(Panel& p, GenController* gen)
    {
        p.engine->addImportPath(QStringLiteral(MYBENCH_QML_DIR));
        QQmlComponent component(p.engine.get());
        component.setData("import QtQuick\nimport MyBench.Instruments.Gen\nGenPanel { }\n", QUrl());
        p.root.reset(component.beginCreate(p.engine->rootContext()));
        if (!p.root) {
            p.error = component.errorString();
            return;
        }
        p.root->setProperty("controller", QVariant::fromValue(gen));
        component.completeCreate();
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
        QTest::newRow("split") << QSize(900, 700);
        QTest::newRow("narrow") << QSize(640, 700);
    }

    // The readout lines and previews (display, left) never overlap the control column (right);
    // both channel cards, the tool palette and the Run button are laid out inside the pane, and
    // the generator view has no inspector.
    void layoutAtSizes()
    {
        QFETCH(QSize, size);
        GenController gen;
        gen.initialize();
        mybench::OffscreenWindow offscreen(size);
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        Panel p;
        create(p, &gen);
        QVERIFY2(p.root, qPrintable(p.error));
        auto* item = qobject_cast<QQuickItem*>(p.root.get());
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
        const QRectF pane(0, 0, item->width(), item->height());
        QHash<QString, QRectF> rects;
        for (const auto* name : {"genControls", "genChannelCard0", "genChannelCard1", "genTools", "genRunButton",
                                 "genPreview0", "previewLegend0"}) {   // AO 1 is Off by default
            auto* child = findItem(item, QString::fromLatin1(name));
            QVERIFY2(child, name);
            QVERIFY2(child->isVisible(), name);
            const QRectF r = rectOf(child);
            QVERIFY2(r.width() > 0 && r.height() > 0, qPrintable(u"%1 not laid out: %2"_s.arg(QLatin1StringView(name), describe(r))));
            if (QLatin1StringView(name) != "genChannelCard1" && QLatin1StringView(name) != "genTools")   // may scroll
                QVERIFY2(pane.adjusted(-0.5, -0.5, 0.5, 0.5).contains(r),
                         qPrintable(u"%1 outside the pane: %2"_s.arg(QLatin1StringView(name), describe(r))));
            rects.insert(QString::fromLatin1(name), r);
        }
        const QRectF controls = rects.value(u"genControls"_s);
        QVERIFY(rects.value(u"genPreview0"_s).right() <= controls.left() + 0.5);
        // No sentence summaries on the display: values are shown once, in the channel cards.
        QVERIFY(!findItem(item, u"readoutLine0"_s));
        QVERIFY(!findItem(item, u"readoutLine1"_s));
        QVERIFY(rects.value(u"genChannelCard0"_s).left() >= controls.left() - 0.5);
        auto* pane2 = p.root->findChild<QObject*>(u"genPane"_s);
        QVERIFY(pane2);
        QCOMPARE(pane2->property("hasInspector").toBool(), false);
        offscreen.setRootItem(nullptr);
    }

    // Waveform pictures set the channel mode/shape; the matching knob fields show; the Editor tool
    // opens the Waveform Editor view (no inspector there either).
    void picturesAndTools()
    {
        GenController gen;
        gen.initialize();
        mybench::OffscreenWindow offscreen(QSize(1440, 900));
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        Panel p;
        create(p, &gen);
        QVERIFY2(p.root, qPrintable(p.error));
        auto* item = qobject_cast<QQuickItem*>(p.root.get());
        offscreen.setRootItem(item);
        offscreen.renderFrame();

        auto* card = findItem(item, u"genChannelCard0"_s);
        QVERIFY(card);
        auto* dc = findItem(card, u"waveDC"_s);
        QVERIFY(dc);
        QMetaObject::invokeMethod(dc, "clicked");
        QCOMPARE(gen.channel0()->mode(), GenChannel::Dc);
        offscreen.renderFrame();
        auto* level = findItem(card, u"levelField"_s);
        QVERIFY(level);
        QVERIFY(level->isVisible());
        auto* frequency = findItem(card, u"frequencyField"_s);
        QVERIFY(frequency);
        QVERIFY(!frequency->isVisible());

        auto* square = findItem(card, u"waveSquare"_s);
        QVERIFY(square);
        QMetaObject::invokeMethod(square, "clicked");
        QCOMPARE(gen.channel0()->mode(), GenChannel::Function);
        QCOMPARE(gen.channel0()->shape(), GenChannel::Square);
        offscreen.renderFrame();
        QVERIFY(frequency->isVisible());
        QVERIFY(findItem(card, u"dutyField"_s));
        QVERIFY(findItem(card, u"dutyField"_s)->isVisible());

        // A second click on the picked waveform sets the channel Off.
        QMetaObject::invokeMethod(square, "clicked");
        QCOMPARE(gen.channel0()->mode(), GenChannel::Off);

        auto* tools = findItem(item, u"genTools"_s);
        QVERIFY(tools);
        QMetaObject::invokeMethod(tools, "activated", Q_ARG(QString, u"sweep"_s));
        QCOMPARE(p.root->property("tool").toString(), u"sweep"_s);
        QMetaObject::invokeMethod(tools, "activated", Q_ARG(QString, u"editor"_s));
        QCOMPARE(gen.activeTab(), 1);
        QCOMPARE(p.root->property("tool").toString(), QString());
        QCOMPARE(p.root->findChild<QObject*>(u"genPane"_s)->property("hasInspector").toBool(), false);
        gen.setActiveTab(0);
        offscreen.setRootItem(nullptr);
    }

    // The header switch follows the channel after a click, also when the request is refused
    // (an Arbitrary channel without a waveform cannot be switched on).
    void outputSwitchKeepsItsBinding()
    {
        GenController gen;
        gen.initialize();
        mybench::OffscreenWindow offscreen(QSize(1440, 900));
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        Panel p;
        create(p, &gen);
        QVERIFY2(p.root, qPrintable(p.error));
        auto* item = qobject_cast<QQuickItem*>(p.root.get());
        offscreen.setRootItem(item);
        offscreen.renderFrame();

        gen.channel0()->setMode(GenChannel::Arbitrary);
        offscreen.renderFrame();
        auto* card = findItem(item, u"genChannelCard0"_s);
        QVERIFY(card);
        auto* sw = findItem(card, u"channelSwitch"_s);   // ChannelCard's header switch
        QVERIFY(sw);
        QVERIFY(!gen.channel0()->outputOn());
        // A click: the control flips `checked`, then emits toggled().
        sw->setProperty("checked", true);
        QMetaObject::invokeMethod(sw, "toggled");
        QVERIFY(!gen.channel0()->outputOn());           // refused: no waveform loaded
        QCOMPARE(sw->property("checked").toBool(), false);

        // Function mode: the click switches the output on; the controller switches it off again.
        gen.channel0()->setMode(GenChannel::Function);
        offscreen.renderFrame();
        sw->setProperty("checked", true);
        QMetaObject::invokeMethod(sw, "toggled");
        QVERIFY(gen.channel0()->outputOn());
        QCOMPARE(sw->property("checked").toBool(), true);
        gen.setOutput(0, false);
        QCOMPARE(sw->property("checked").toBool(), false);
        offscreen.setRootItem(nullptr);
    }

    // Waveform Editor view: no inspector; the waveform display is the largest item, left of the
    // right column (Document and Segment cards, tool palette); tools open the floating panel;
    // Draw starts drawing a sketch; Back returns to the Generator.
    void editorView_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("1440") << QSize(1440, 900);
        QTest::newRow("1024") << QSize(1024, 700);
        QTest::newRow("split") << QSize(900, 700);   // compact column
    }
    void editorView()
    {
        QFETCH(QSize, size);
        GenController gen;
        gen.initialize();
        mybench::OffscreenWindow offscreen(size);
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        Panel p;
        create(p, &gen);
        QVERIFY2(p.root, qPrintable(p.error));
        auto* item = qobject_cast<QQuickItem*>(p.root.get());
        offscreen.setRootItem(item);
        gen.setActiveTab(1);
        offscreen.renderFrame();
        offscreen.renderFrame();
        QCOMPARE(p.root->findChild<QObject*>(u"genPane"_s)->property("hasInspector").toBool(), false);

        const auto rectOf = [](QQuickItem* i) {
            return QRectF(i->mapToScene(QPointF(0, 0)), QSizeF(i->width(), i->height()));
        };
        const QRectF pane(0, 0, item->width(), item->height());
        auto* plot = findItem(item, u"editorPlot"_s);
        QVERIFY(plot);
        QVERIFY(plot->isVisible());
        const QRectF plotRect = rectOf(plot);
        QVERIFY(pane.adjusted(-0.5, -0.5, 0.5, 0.5).contains(plotRect));
        for (const auto* name : {"editorTopBar", "editorControls", "editorDocumentCard", "editorSegmentCard",
                                 "editorTools", "backToGenerator"}) {
            auto* child = findItem(item, QString::fromLatin1(name));
            QVERIFY2(child, name);
            QVERIFY2(child->isVisible(), name);
            const QRectF r = rectOf(child);
            QVERIFY2(r.width() > 0 && r.height() > 0, name);
            QVERIFY2(plotRect.width() * plotRect.height() > r.width() * r.height(), name);
        }
        const QRectF controls = rectOf(findItem(item, u"editorControls"_s));
        QVERIFY(plotRect.right() <= controls.left() + 0.5);
        QVERIFY2(pane.adjusted(-0.5, -0.5, 0.5, 0.5).contains(controls), "controls column outside the pane");
        for (const auto* name : {"editorDocumentCard", "editorSegmentCard", "documentRateField"}) {
            const QRectF r = rectOf(findItem(item, QString::fromLatin1(name)));
            QVERIFY2(r.left() >= controls.left() - 0.5 && r.right() <= controls.right() + 0.5,
                     qPrintable(u"%1 wider than the column"_s.arg(QLatin1StringView(name))));
        }
        // No in-display menu bar (File / Edit / Help) and no menu item overrides.
        QVERIFY(!findItem(item, u"editorFileMenu"_s));

        auto* tools = findItem(item, u"editorTools"_s);
        QVERIFY(tools);
        auto* panel = findItem(item, u"editorToolPanel"_s);
        QVERIFY(panel);
        QVERIFY(!panel->isVisible());
        for (const auto* id : {"functions", "import", "export", "display", "scale", "info"}) {
            QMetaObject::invokeMethod(tools, "activated", Q_ARG(QString, QString::fromLatin1(id)));
            offscreen.renderFrame();
            QVERIFY2(panel->isVisible(), id);
            QVERIFY2(!panel->property("title").toString().isEmpty(), id);
        }
        QMetaObject::invokeMethod(tools, "activated", Q_ARG(QString, u"info"_s));   // second click closes
        QVERIFY(!panel->isVisible());

        // Functions: picking a library function makes the selection a component of that function.
        QMetaObject::invokeMethod(tools, "activated", Q_ARG(QString, u"functions"_s));
        auto* view = findItem(item, u"editorView"_s);
        QVERIFY(view);
        QVERIFY(QMetaObject::invokeMethod(view, "applyFunction", Q_ARG(int, 0), Q_ARG(int, 2)));
        QCOMPARE(gen.editor()->selectionKind(), 2);
        QCOMPARE(gen.editor()->componentType(), 0);
        QCOMPARE(gen.editor()->libraryFunction(), 2);

        // Draw: a sketch component, drawing on.
        QMetaObject::invokeMethod(tools, "activated", Q_ARG(QString, u"draw"_s));
        QVERIFY(gen.editor()->drawMode());
        QCOMPARE(gen.editor()->componentType(), 2);
        QMetaObject::invokeMethod(tools, "activated", Q_ARG(QString, u"draw"_s));
        QVERIFY(!gen.editor()->drawMode());

        auto* back = findItem(item, u"backToGenerator"_s);
        QVERIFY(back);
        QMetaObject::invokeMethod(back, "clicked");
        QCOMPARE(gen.activeTab(), 0);
        offscreen.setRootItem(nullptr);
    }
};

QTEST_MAIN(TestGenQml)
#include "tst_gen_qml.moc"
