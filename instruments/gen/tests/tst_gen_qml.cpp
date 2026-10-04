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
    // opens the Waveform Editor view, which brings its inspector back.
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
        QCOMPARE(p.root->findChild<QObject*>(u"genPane"_s)->property("hasInspector").toBool(), true);
        gen.setActiveTab(0);
        offscreen.setRootItem(nullptr);
    }
};

QTEST_MAIN(TestGenQml)
#include "tst_gen_qml.moc"
