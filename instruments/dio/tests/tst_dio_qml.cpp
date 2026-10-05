// Digital I/O panel QML (offscreen, round-2 layout): the pattern picture buttons select the writer
// pattern and stay bound to the controller; only the settings of the chosen pattern are shown;
// the sample interval exists once (Reader card); the header radix sets the controller radix.
#include "diocontroller.h"

#include <mybench/devicehub.h>
#include <mybench/offscreen.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTest>

#include <functional>
#include <memory>

using namespace Qt::StringLiterals;
using mybench::instruments::DioController;

class TestDioQml : public QObject
{
    Q_OBJECT

    QQmlEngine m_engine;
    std::unique_ptr<mybench::OffscreenWindow> m_window;
    std::unique_ptr<QObject> m_root;
    std::unique_ptr<DioController> m_dio;   // outlives the panel (destroyed in cleanup())

    QObject* find(const QString& name) const
    {
        std::function<QObject*(QQuickItem*)> walk = [&](QQuickItem* item) -> QObject* {
            if (item->objectName() == name)
                return item;
            for (QQuickItem* child : item->childItems())
                if (QObject* found = walk(child))
                    return found;
            return nullptr;
        };
        auto* item = qobject_cast<QQuickItem*>(m_root.get());
        return item ? walk(item) : nullptr;
    }
    bool shown(const QString& name) const
    {
        auto* item = qobject_cast<QQuickItem*>(find(name));
        return item && item->isVisible();
    }
    void render()
    {
        for (int i = 0; i < 2; ++i) {
            m_window->renderFrame();
            QCoreApplication::processEvents();
        }
    }
    DioController& controller()
    {
        m_dio = std::make_unique<DioController>();
        m_dio->initialize();
        return *m_dio;
    }
    bool createPanel(DioController& ctr)
    {
        m_window = std::make_unique<mybench::OffscreenWindow>(QSize(1440, 900));
        if (!m_window->isValid())
            return false;
        QQmlComponent component(&m_engine);
        component.setData("import QtQuick\nimport MyBench.Instruments.Dio\nDioPanel { }\n", QUrl());
        m_root.reset(component.beginCreate(m_engine.rootContext()));
        if (!m_root) {
            qWarning() << component.errorString();
            return false;
        }
        m_root->setProperty("controller", QVariant::fromValue(&ctr));
        component.completeCreate();
        m_window->setRootItem(qobject_cast<QQuickItem*>(m_root.get()));
        render();
        return true;
    }


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
        mybench::DeviceHub::instance()->select(u"sim:0"_s);
        QTRY_VERIFY(mybench::DeviceHub::instance()->isConnected());
        m_engine.addImportPath(QStringLiteral(MYBENCH_QML_DIR));
    }
    void cleanup()
    {
        m_root.reset();
        m_window.reset();
        m_dio.reset();
    }

    void patternButtonsAndSettings()
    {
        DioController& dio = controller();
        if (!createPanel(dio))
            QSKIP("no graphics device");
        QVERIFY(shown(u"dioWriterCard"_s));
        QVERIFY(shown(u"dioManualValue"_s));
        QVERIFY(!shown(u"dioStepInterval"_s));
        QObject* manual = find(u"dioPattern0"_s);
        QObject* ramp = find(u"dioPattern1"_s);
        QVERIFY(manual && ramp);
        QVERIFY(manual->property("checked").toBool());

        QMetaObject::invokeMethod(ramp, "clicked");
        render();
        QCOMPARE(dio.pattern(), DioController::Ramp);
        QVERIFY(ramp->property("checked").toBool());
        QVERIFY(!manual->property("checked").toBool());
        QVERIFY(shown(u"dioStepInterval"_s));
        QVERIFY(!shown(u"dioManualValue"_s));

        // The sample interval is edited in one place only.
        int intervals = 0;
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
            if (item->objectName() == u"dioSampleInterval"_s)
                ++intervals;
            for (QQuickItem* child : item->childItems())
                walk(child);
        };
        walk(qobject_cast<QQuickItem*>(m_root.get()));
        QCOMPARE(intervals, 1);
    }

    void headerRadixSetsController()
    {
        DioController& dio = controller();
        if (!createPanel(dio))
            QSKIP("no graphics device");
        // The radix control lives in the pane header (InstrumentPanel.headerContent).
        QObject* radix = m_root->findChild<QObject*>(u"dioRadix"_s);
        if (!radix)
            radix = find(u"dioRadix"_s);
        QVERIFY(radix);
        QMetaObject::invokeMethod(radix, "activated", Q_ARG(int, 1));
        render();
        QCOMPARE(dio.outputRadix(), DioController::Binary);
        QCOMPARE(dio.inputRadix(), DioController::Binary);
        QCOMPARE(radix->property("currentIndex").toInt(), 1);
    }

    void lessText()
    {
        DioController& dio = controller();
        if (!createPanel(dio))
            QSKIP("no graphics device");
        // The line groups show once (the Lines card's Read / Write buttons): no display title or
        // info line, no card subtitles, no prose-only Timeline tool.
        verifyNoDisplayText(m_root.get());
        if (QTest::currentTestFailed())
            return;
        for (const QString& name : {u"dioReaderCard"_s, u"dioWriterCard"_s}) {
            QObject* card = find(name);
            QVERIFY2(card, qPrintable(name));
            QCOMPARE(card->property("subtitle").toString(), QString());
        }
        QVERIFY(!find(u"dioTimelinePanel"_s));
        QObject* tools = find(u"dioTools"_s);
        QVERIFY(tools);
        QCOMPARE(tools->property("model").toList().size(), 2);   // Labels, Export
    }

    // Round 5: one place per line. The tile sets the direction (the Lines card keeps only the
    // Read / Write group shortcuts), shows HIGH / LOW in the direction's color and flips an output
    // on a click in the Manual pattern.
    void linesOnTheTiles()
    {
        DioController& dio = controller();
        if (!createPanel(dio))
            QSKIP("no graphics device");
        auto* linesCard = qobject_cast<QQuickItem*>(find(u"dioLinesCard"_s));
        QVERIFY(linesCard);
        int directionControls = 0;
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
            if (item->objectName().startsWith(u"dioDirection"_s))
                ++directionControls;
            for (QQuickItem* child : item->childItems())
                walk(child);
        };
        walk(linesCard);
        QCOMPARE(directionControls, 0);
        walk(qobject_cast<QQuickItem*>(m_root.get()));
        QCOMPARE(directionControls, 8);   // one per tile

        // Direction from the tile.
        QObject* direction5 = find(u"dioDirection5"_s);
        QVERIFY(direction5);
        QMetaObject::invokeMethod(direction5, "activated", Q_ARG(int, 1));
        render();
        QVERIFY((dio.outputMask() >> 5) & 1);
        QCOMPARE(direction5->property("currentIndex").toInt(), 1);

        // An output tile toggles its line (Manual pattern).
        QCOMPARE(dio.pattern(), DioController::Manual);
        QObject* state0 = find(u"dioState0"_s);
        QVERIFY(state0);
        QVERIFY((dio.outputMask() >> 0) & 1);
        const int before = dio.manualValue();
        QVERIFY(QMetaObject::invokeMethod(find(u"dioLine0"_s), "activate"));
        render();
        QCOMPARE(dio.manualValue(), before ^ 1);
        QCOMPARE(find(u"dioLine0"_s)->property("level").toBool(), bool(dio.manualValue() & 1));
        // Review aid: MYBENCH_SHOT_DIR=<dir> saves the tiles with a HIGH output.
        if (const QString dir = qEnvironmentVariable("MYBENCH_SHOT_DIR"); !dir.isEmpty())
            m_window->grab().save(dir + u"/dio-tiles.png"_s);
    }
};

QTEST_MAIN(TestDioQml)
#include "tst_dio_qml.moc"
