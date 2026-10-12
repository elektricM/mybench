// Audio Equalizer panel QML in an offscreen window: PicoScope-7 layout (no inspector, Run / Bypass /
// Flat bar, Routing card and tools in the right column), the routing kind limits the per-side
// choices, and the console faders are the only editors of the bands and the volume.
#include "aeqcontroller.h"

#include <mybench/offscreen.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using mybench::instruments::AeqController;

class TestAeqQml : public QObject
{
    Q_OBJECT

private:
    // All items of the visual tree (Repeater delegates are not QObject children of the panel).
    static void collect(QQuickItem* item, QList<QQuickItem*>& out)
    {
        out << item;
        for (QQuickItem* child : item->childItems())
            collect(child, out);
    }
    // The choices offered on one side of the routing ("inputLeft", "outputRight", ...).
    static QStringList choices(QObject* root, const QString& side)
    {
        QObject* row = root->findChild<QObject*>(side);
        if (!row)
            return { u"<missing>"_s };
        QStringList texts = row->property("choiceText").toString().split(u'|');
        texts.sort();
        return texts;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        mybench::InstrumentController::setRestoreSavedSettings(false);
    }

    void layout()
    {
        AeqController ctl;
        ctl.initialize();
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(MYBENCH_QML_DIR));
        mybench::OffscreenWindow offscreen(QSize(1440, 900));
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport MyBench.Instruments.Aeq\nAeqPanel { }\n", QUrl());
        std::unique_ptr<QObject> root(component.beginCreate(engine.rootContext()));
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setProperty("controller", QVariant::fromValue(&ctl));
        component.completeCreate();
        auto* item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(item);
        offscreen.setRootItem(item);
        offscreen.renderFrame();

        // No inspector and no sentence-style display text.
        int panels = 0;
        for (QObject* o : root->findChildren<QObject*>()) {
            if (o->metaObject()->indexOfProperty("hasInspector") < 0)
                continue;
            ++panels;
            QVERIFY(!o->property("hasInspector").toBool());
            QCOMPARE(o->property("displayTitle").toString(), QString());
            QCOMPARE(o->property("displayInfo").toString(), QString());
        }
        QVERIFY(panels > 0);
        for (const char* name : { "aeqRunButton", "bypassButton", "flatButton", "inputCard", "outputCard", "aeqTools", "console" })
            QVERIFY2(root->findChild<QObject*>(QString::fromLatin1(name)), name);
        // The shared right column (fixed width).
        auto* column = root->findChild<QQuickItem*>(u"aeqColumn"_s);
        QVERIFY(column);
        QCOMPARE(column->width(), 260.0);

        // Bass / midtone / treble have their own colors — distinct from each other, from
        // the cursor pair (treble was Theme.cursor2) and from the accent of the After trace.
        {
            const QColor bass = root->property("bassColor").value<QColor>();
            const QColor mid = root->property("midColor").value<QColor>();
            const QColor treble = root->property("trebleColor").value<QColor>();
            QObject* theme = engine.singletonInstance<QObject*>(u"MyBench.Controls"_s, u"Theme"_s);
            QVERIFY(theme);
            const QColor cursor1 = theme->property("cursor1").value<QColor>();
            const QColor cursor2 = theme->property("cursor2").value<QColor>();
            const QColor accent = theme->property("accent").value<QColor>();
            QVERIFY(cursor2.isValid());
            for (const QColor& band : { bass, mid, treble }) {
                QVERIFY(band.isValid());
                QVERIFY(band != cursor1 && band != cursor2 && band != accent);
            }
            QVERIFY(bass != mid && mid != treble && bass != treble);
        }

        // One control per action: the routing's input kind, no extra "Use AI 0 / AI 1" button.
        {
            QList<QQuickItem*> everything;
            collect(item, everything);
            for (QQuickItem* child : everything)
                QVERIFY(child->property("text").toString() != u"Use AI 0 / AI 1"_s);
        }

        // Bypass button toggles the controller.
        QObject* bypass = root->findChild<QObject*>(u"bypassButton"_s);
        QVERIFY(QMetaObject::invokeMethod(bypass, "clicked"));
        QVERIFY(ctl.bypass());
        QVERIFY(QMetaObject::invokeMethod(bypass, "clicked"));
        QVERIFY(!ctl.bypass());

        // Tools open their floating panels.
        QObject* tools = root->findChild<QObject*>(u"aeqTools"_s);
        QVERIFY(tools);
        for (const QString& id : { u"streaming"_s, u"display"_s }) {
            QVERIFY(QMetaObject::invokeMethod(tools, "activated", Q_ARG(QString, id)));
            offscreen.renderFrame();
            QVERIFY(root->findChild<QQuickItem*>(id + u"Panel"_s)->isVisible());
        }

        // The routing kind limits each side to its own converters (no mixed routing possible).
        ctl.setInputLeft(AeqController::SourceAI0);
        ctl.setInputRight(AeqController::SourceAI1);
        ctl.setOutputLeft(AeqController::SinkAO0);
        ctl.setOutputRight(AeqController::SinkAO1);
        offscreen.renderFrame();
        QCOMPARE(choices(root.get(), u"inputLeft"_s), QStringList({ u"AI 0"_s, u"AI 1"_s, u"Off"_s }));
        QCOMPARE(choices(root.get(), u"inputRight"_s), QStringList({ u"AI 0"_s, u"AI 1"_s, u"Off"_s }));
        QCOMPARE(choices(root.get(), u"outputLeft"_s), QStringList({ u"AO 0"_s, u"AO 1"_s, u"Off"_s }));
        QVERIFY(ctl.routingError().isEmpty());

        QObject* inputKind = root->findChild<QObject*>(u"inputKind"_s);
        QVERIFY(inputKind);
        QVERIFY(QMetaObject::invokeMethod(inputKind, "activated", Q_ARG(int, 0)));   // Audio in
        offscreen.renderFrame();
        QCOMPARE(ctl.inputLeft(), AeqController::SourceAudioInLeft);
        QCOMPARE(ctl.inputRight(), AeqController::SourceAudioInRight);
        QCOMPARE(choices(root.get(), u"inputLeft"_s), QStringList({ u"In L"_s, u"In R"_s, u"Off"_s }));
        QCOMPARE(choices(root.get(), u"inputRight"_s), QStringList({ u"In L"_s, u"In R"_s, u"Off"_s }));
        QObject* outputKind = root->findChild<QObject*>(u"outputKind"_s);
        QVERIFY(QMetaObject::invokeMethod(outputKind, "activated", Q_ARG(int, 0)));  // Audio out
        offscreen.renderFrame();
        QCOMPARE(ctl.outputLeft(), AeqController::SinkAudioOutLeft);
        QCOMPARE(ctl.outputRight(), AeqController::SinkAudioOutRight);
        QCOMPARE(choices(root.get(), u"outputRight"_s), QStringList({ u"Off"_s, u"Out L"_s, u"Out R"_s }));
        QVERIFY(ctl.routingError().isEmpty());

        // The faders are the only EQ editors: four of them, no slider / spin box / SI field.
        int faders = 0;
        QList<QQuickItem*> all;
        collect(item, all);
        for (QQuickItem* child : all) {
            if (child->objectName().startsWith(u"fader_"_s))
                ++faders;
            const QString cls = QString::fromLatin1(child->metaObject()->className());
            QVERIFY2(!child->inherits("QQuickSlider") && !child->inherits("QQuickSpinBox") && !cls.startsWith(u"SiField"_s),
                     qPrintable(cls));
        }
        QCOMPARE(faders, 4);

        // A typed value reaches the controller (clamped to ±12 dB).
        auto* bass = root->findChild<QQuickItem*>(u"fader_bass"_s);
        QVERIFY(bass);
        QVERIFY(QMetaObject::invokeMethod(bass, "commitText", Q_ARG(QString, u"4.5"_s)));
        QCOMPARE(ctl.bass(), 4.5);
        QVERIFY(QMetaObject::invokeMethod(bass, "commitText", Q_ARG(QString, u"−30"_s)));
        QCOMPARE(ctl.bass(), -12.0);
        auto* volume = root->findChild<QQuickItem*>(u"fader_volume"_s);
        QVERIFY(QMetaObject::invokeMethod(volume, "commitText", Q_ARG(QString, u"25 %"_s)));
        QCOMPARE(ctl.volume(), 0.25);

        offscreen.setRootItem(nullptr);
    }
};

QTEST_MAIN(TestAeqQml)
#include "tst_aeq_qml.moc"
