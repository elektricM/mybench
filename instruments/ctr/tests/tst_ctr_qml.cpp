// Counter/Timer panel QML (offscreen, round-2 layout): the mode picture buttons switch the counter
// function and keep their checked state bound to the controller; the settings cards follow the
// mode; the FREQOUT knob steps over the achievable frequencies (low to high) and selects them.
#include "ctrcontroller.h"

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
using mybench::instruments::CtrController;

class TestCtrQml : public QObject
{
    Q_OBJECT

    QQmlEngine m_engine;
    std::unique_ptr<mybench::OffscreenWindow> m_window;
    std::unique_ptr<QObject> m_root;
    std::unique_ptr<CtrController> m_ctr;   // outlives the panel (destroyed in cleanup())

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
    CtrController& controller()
    {
        m_ctr = std::make_unique<CtrController>();
        m_ctr->initialize();
        return *m_ctr;
    }
    bool createPanel(CtrController& ctr)
    {
        m_window = std::make_unique<mybench::OffscreenWindow>(QSize(1440, 900));
        if (!m_window->isValid())
            return false;
        QQmlComponent component(&m_engine);
        component.setData("import QtQuick\nimport MyBench.Instruments.Ctr\nCtrPanel { }\n", QUrl());
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
        m_ctr.reset();
    }

    void modeButtonsFollowController()
    {
        CtrController& ctr = controller();
        if (!createPanel(ctr))
            QSKIP("no graphics device");
        QVERIFY(shown(u"ctrModeCard"_s));
        QVERIFY(shown(u"ctrSettingsCard"_s));
        QVERIFY(!shown(u"ctrPulseCard"_s));

        QObject* pulse = find(u"ctrMode_Pulse Train"_s);
        QObject* frequency = find(u"ctrMode_Frequency"_s);
        QVERIFY(pulse && frequency);
        QVERIFY(frequency->property("checked").toBool());

        QMetaObject::invokeMethod(pulse, "clicked");
        render();
        QCOMPARE(ctr.counterFunction(), CtrController::PulseTrain);
        QVERIFY(pulse->property("checked").toBool());
        QVERIFY(!frequency->property("checked").toBool());
        QVERIFY(shown(u"ctrPulseCard"_s));
        QVERIFY(!shown(u"ctrSettingsCard"_s));
        QVERIFY(!shown(u"ctrSingleButton"_s));

        // Clicking the active mode again keeps it checked (the binding survives the click).
        QMetaObject::invokeMethod(pulse, "clicked");
        render();
        QVERIFY(pulse->property("checked").toBool());

        QObject* period = find(u"ctrMode_Period"_s);
        QVERIFY(period);
        QMetaObject::invokeMethod(period, "clicked");
        render();
        QCOMPARE(ctr.counterFunction(), CtrController::Measure);
        QCOMPARE(ctr.measurement(), CtrController::Period);
        QVERIFY(period->property("checked").toBool());
        QVERIFY(!pulse->property("checked").toBool());
        QVERIFY(shown(u"ctrSettingsCard"_s));
    }

    void lessText()
    {
        CtrController& ctr = controller();
        if (!createPanel(ctr))
            QSKIP("no graphics device");
        // The mode shows once (its checked button): no display title or info line, no readout
        // label, and the settings card is the "Input" (its subtitle the terminals).
        verifyNoDisplayText(m_root.get());
        if (QTest::currentTestFailed())
            return;
        QObject* value = find(u"ctrValue"_s);
        QVERIFY(value);
        QCOMPARE(value->property("label").toString(), QString());
        QObject* settings = find(u"ctrSettingsCard"_s);
        QVERIFY(settings);
        QCOMPARE(settings->property("title").toString(), u"Input"_s);
        QCOMPARE(settings->property("subtitle").toString(), ctr.terminals());
    }

    void freqOutKnobSelectsTableEntries()
    {
        CtrController& ctr = controller();
        if (!createPanel(ctr))
            QSKIP("no graphics device");
        QObject* knob = find(u"ctrFreqOut"_s);
        QVERIFY(knob);
        const QVariantList values = knob->property("values").toList();
        QCOMPARE(values.size(), ctr.freqOutChoices().size());
        for (int i = 1; i < values.size(); ++i)
            QVERIFY2(values[i].toDouble() > values[i - 1].toDouble(), "knob values run low to high");

        ctr.setFreqOutEnabled(true);
        render();
        QMetaObject::invokeMethod(knob, "indexActivated", Q_ARG(int, 0));
        render();
        QCOMPARE(ctr.coercedFreqOut(), values.first().toDouble());
        QCOMPARE(knob->property("currentIndex").toInt(), 0);
        const int last = int(values.size()) - 1;
        QMetaObject::invokeMethod(knob, "indexActivated", Q_ARG(int, last));
        render();
        QCOMPARE(ctr.coercedFreqOut(), values.last().toDouble());
        QCOMPARE(knob->property("currentIndex").toInt(), last);
    }

    // Round 4: the trend is a TrendView with a time-window selector; the expected range is one
    // button per counter timebase, writing min and max together.
    void trendWindowAndRangeButtons()
    {
        CtrController& ctr = controller();
        if (!createPanel(ctr))
            QSKIP("no graphics device");
        QObject* trend = find(u"ctrTrend"_s);
        QVERIFY(trend);
        QCOMPARE(trend->property("windows").toList().size(), 4);
        QMetaObject::invokeMethod(trend, "windowActivated", Q_ARG(int, 0));
        render();
        QCOMPARE(trend->property("windowIndex").toInt(), 0);
        QCOMPARE(m_root->property("trendWindow").toInt(), 0);

        QCOMPARE(ctr.measurement(), CtrController::Frequency);
        QCOMPARE(m_root->property("expectedRangeIndex").toInt(), 0);   // 1 Hz – 1 MHz default
        QObject* slow = find(u"expectedRange1"_s);
        QVERIFY(slow);
        QVERIFY(QMetaObject::invokeMethod(slow, "click"));
        render();
        QCOMPARE(ctr.frequencyMin(), 0.01);
        QCOMPARE(ctr.frequencyMax(), 1e6);
        QVERIFY(slow->property("checked").toBool());
        QVERIFY(!find(u"expectedRange0"_s)->property("checked").toBool());

        ctr.setMeasurement(CtrController::Period);
        render();
        QObject* longest = find(u"expectedRange2"_s);
        QVERIFY(longest);
        QVERIFY(QMetaObject::invokeMethod(longest, "click"));
        render();
        QCOMPARE(ctr.timeMin(), 100e-6);
        QCOMPARE(ctr.timeMax(), 1e4);
        QCOMPARE(m_root->property("expectedRangeIndex").toInt(), 2);
    }

    // Review aid: MYBENCH_SHOT_DIR=<dir> saves the Pulse Train + FREQOUT view (dark theme default).
    void pulseTrainShot()
    {
        const QString dir = qEnvironmentVariable("MYBENCH_SHOT_DIR");
        if (dir.isEmpty())
            QSKIP("set MYBENCH_SHOT_DIR to save the image");
        CtrController& ctr = controller();
        ctr.setCounterFunction(CtrController::PulseTrain);
        ctr.setFreqOutEnabled(true);
        if (!createPanel(ctr))
            QSKIP("no graphics device");
        render();
        QVERIFY(m_window->grab().save(dir + u"/ctr-pulse-freqout.png"_s));
    }
};

QTEST_MAIN(TestCtrQml)
#include "tst_ctr_qml.moc"
