// Scope QML behaviour (offscreen): the FFT view keeps the user's zoom; the panel has every setting
// exactly once; Immediate trigger hides the Edge fields; measurements are chosen in the measurement
// strip; the controller migration shows up in the panel; horizontal and trigger controls
// always visible, no duplicate controls, fixed column, tool panels beside the display, cursor pair
// colors and readouts.
#include "scopecontroller.h"

#include <mybench/devicehub.h>
#include <mybench/offscreen.h>
#include <mydaq/sim_device.h>

#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSet>
#include <QSignalSpy>

#include <algorithm>
#include <functional>
#include <QStandardPaths>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using mybench::instruments::ScopeChannel;
using mybench::instruments::ScopeController;

namespace {

bool isA(const QObject* object, const char* qmlTypeName)
{
    for (const QMetaObject* meta = object->metaObject(); meta; meta = meta->superClass()) {
        const QByteArray name(meta->className());
        // QML types are "<Name>_QMLTYPE_<n>" (or "<Name>_QML_<n>").
        if (name == qmlTypeName || name.startsWith(QByteArray(qmlTypeName) + "_QML"))
            return true;
    }
    return false;
}

// The controls that edit a setting (composite controls count once: their inner buttons and
// fields are skipped).
bool isSettingControl(const QObject* object)
{
    for (const char* type : {"KnobField", "SiField", "StepSelector", "SegmentedControl", "ComboBox", "Switch", "CheckBox",
                             "Slider", "SpinBox", "RadioButton", "QQuickComboBox", "QQuickSwitch", "QQuickCheckBox",
                             "QQuickSlider", "QQuickSpinBox", "QQuickRadioButton"})
        if (isA(object, type))
            return true;
    return false;
}

void collect(QObject* object, QList<QObject*>& controls, QList<QObject*>& knobs, QSet<QObject*>& visited)
{
    if (visited.contains(object))
        return;
    visited.insert(object);
    if (isA(object, "Knob"))
        knobs.append(object);
    if (isSettingControl(object)) {
        controls.append(object);
        return;
    }
    const auto items = object->findChildren<QObject*>(Qt::FindDirectChildrenOnly);
    for (QObject* child : items)
        collect(child, controls, knobs, visited);
    if (auto* item = qobject_cast<QQuickItem*>(object)) {
        for (QQuickItem* child : item->childItems())
            collect(child, controls, knobs, visited);
    }
}

}  // namespace

class TestScopeQml : public QObject
{
    Q_OBJECT

    // The panel in an offscreen window wide enough for the docked inspector.
    struct Panel {
        std::unique_ptr<mybench::OffscreenWindow> window;
        std::unique_ptr<QObject> root;
        QQuickItem* item = nullptr;
        QObject* find(const QString& name) const
        {
            if (QObject* object = root->findChild<QObject*>(name))
                return object;
            // Repeater delegates are only reachable through the item tree.
            std::function<QObject*(QQuickItem*)> walk = [&](QQuickItem* item) -> QObject* {
                if (item->objectName() == name)
                    return item;
                for (QQuickItem* child : item->childItems())
                    if (QObject* found = walk(child))
                        return found;
                return nullptr;
            };
            return item ? walk(item) : nullptr;
        }
        bool shown(const QString& name) const
        {
            auto* object = qobject_cast<QQuickItem*>(find(name));
            return object && object->isVisible();
        }
    };

    QQmlEngine m_engine;

    bool createPanel(ScopeController& scope, Panel& panel, QSize size = QSize(1440, 900))
    {
        panel.window = std::make_unique<mybench::OffscreenWindow>(size);
        if (!panel.window->isValid())
            return false;
        QQmlComponent component(&m_engine);
        component.setData("import QtQuick\nimport MyBench.Instruments.Scope\nScopePanel { }\n", QUrl());
        panel.root.reset(component.beginCreate(m_engine.rootContext()));
        if (!panel.root) {
            qWarning() << component.errorString();
            return false;
        }
        panel.root->setProperty("controller", QVariant::fromValue(&scope));
        component.completeCreate();
        panel.item = qobject_cast<QQuickItem*>(panel.root.get());
        panel.window->setRootItem(panel.item);
        for (int i = 0; i < 3; ++i) {
            panel.window->renderFrame();
            QCoreApplication::processEvents();
        }
        // Loaders that switched places with the pane width (bar / right column) drop their old item.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return true;
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

    void fftZoomPersists()
    {
        ScopeController scope;
        scope.initialize();
        mybench::OffscreenWindow offscreen(QSize(1000, 700));
        if (!offscreen.isValid())
            QSKIP("no graphics device");
        QQmlComponent component(&m_engine);
        component.setData("import QtQuick\nimport MyBench.Instruments.Scope\nScopeDisplay { }\n", QUrl());
        std::unique_ptr<QObject> root(component.beginCreate(m_engine.rootContext()));
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setProperty("controller", QVariant::fromValue(&scope));
        component.completeCreate();
        auto* item = qobject_cast<QQuickItem*>(root.get());
        QVERIFY(item);
        offscreen.setRootItem(item);

        scope.setFftSource(ScopeController::FftChannel0);
        QSignalSpy frames(&scope, &ScopeController::recordChanged);
        scope.run();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 2, 20000);
        offscreen.renderFrame();
        auto* plot = root->findChild<QObject*>("fftPlot");
        QVERIFY(plot);
        auto* xAxis = plot->property("xAxis").value<QObject*>();
        QVERIFY(xAxis);
        const double home = xAxis->property("max").toDouble();
        QVERIFY(home > 1);

        // The user zooms in (interactive zoom sets a view range, not a new home).
        QVERIFY(QMetaObject::invokeMethod(xAxis, "setViewRange", Q_ARG(double, home * 0.2), Q_ARG(double, home * 0.4)));
        QVERIFY(xAxis->property("zoomed").toBool());

        // Frames keep arriving while it runs.
        const int before = frames.count();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= before + 5, 20000);
        for (int i = 0; i < 5; ++i) {
            offscreen.renderFrame();
            QCoreApplication::processEvents();
        }
        QVERIFY(xAxis->property("zoomed").toBool());
        QCOMPARE(xAxis->property("min").toDouble(), home * 0.2);
        QCOMPARE(xAxis->property("max").toDouble(), home * 0.4);

        // Another FFT source: same axis, same zoom.
        scope.setFftSource(ScopeController::FftChannel1);
        const int seen = frames.count();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= seen + 3, 20000);
        offscreen.renderFrame();
        QVERIFY(xAxis->property("zoomed").toBool());
        QCOMPARE(xAxis->property("min").toDouble(), home * 0.2);

        // Explicit reset.
        QVERIFY(QMetaObject::invokeMethod(plot, "resetView"));
        QVERIFY(!xAxis->property("zoomed").toBool());
        QCOMPARE(xAxis->property("max").toDouble(), home);

        scope.stop();
        QTRY_VERIFY(scope.isIdle());
        offscreen.setRootItem(nullptr);
    }

    // docs/UI.md §1 "Everything once": each setting has exactly one control in the panel, and the
    // panel has no knob. Every setting control carries an objectName; the list below is the
    // one-to-one map of controls and settings (a second control for a setting fails here).
    void everySettingOnce()
    {
        ScopeController scope;
        scope.initialize();
        // Show everything that can be shown: Edge trigger, roll available, math, cursors, levels.
        scope.setChannelEnabled(1, true);
        scope.setTriggerType(ScopeController::Edge);
        scope.math()->setEnabled(true);
        scope.setCursorsOn(true);
        scope.setLevelCursorsOn(true);
        scope.setTimePerDiv(50e-3);
        Panel panel;
        if (!createPanel(scope, panel))
            QSKIP("no graphics device");

        QList<QObject*> controls, knobs;
        QSet<QObject*> visited;
        collect(panel.root.get(), controls, knobs, visited);
        // Knobs (PicoScope style): timebase and trigger position (bar), trigger level (Trigger card),
        // scale and position of both channels, math scale and position.
        const auto knobFields = std::count_if(controls.cbegin(), controls.cend(),
                                              [](const QObject* c) { return isA(c, "KnobField"); });
        QCOMPARE(knobFields, 9);

        QStringList names;
        for (QObject* control : std::as_const(controls)) {
            const QString name = control->objectName();
            // Controls of the shared frame (pane header, dialogs, plot tools) are not ours.
            if (name.isEmpty()) {
                bool inFrame = false;
                for (QObject* p = control->parent(); p && !inFrame; p = p->parent())
                    inFrame = isA(p, "PaneHeader") || isA(p, "TraceStyleDialog") || isA(p, "PrintDialog")
                              || isA(p, "PlotToolbar") || isA(p, "MeasurementTable") || isA(p, "Menu")
                              || isA(p, "ChannelCard") || isA(p, "ToolPanel") || isA(p, "MeasurementCard")
                              || isA(p, "QQuickPopup");
                QVERIFY2(inFrame, qPrintable(u"setting control without objectName: "_s + QString::fromLatin1(control->metaObject()->className())));
                continue;
            }
            // The shared ChannelCard names its switch "channelSwitch": qualify it by the card.
            QString qualified = name;
            if (name == u"channelSwitch"_s) {
                bool switchless = false;
                for (QObject* p = control->parent(); p; p = p->parent())
                    if (isA(p, "ChannelCard")) {
                        switchless = !p->property("checkable").toBool();   // e.g. the Trigger card
                        const QString card = p->objectName();
                        qualified = card.startsWith(u"channelCard"_s) ? name + card.mid(11) : name + u"_"_s + card;
                        break;
                    }
                if (switchless)
                    continue;
            }
            names.append(qualified);
        }
        names.sort();
        QStringList expected{
            // Channel cards: switch, scale and position knob fields, source and input range.
            u"channelSwitch0"_s, u"source0"_s, u"scale0"_s, u"position0"_s, u"inputRange0"_s,
            u"channelSwitch1"_s, u"source1"_s, u"scale1"_s, u"position1"_s, u"inputRange1"_s,
            // Instrument bar: horizontal (timebase, trigger position) and view.
            u"timeDiv"_s, u"horizontalPosition"_s, u"displayMode"_s,
            // Trigger card (type = picture buttons, not counted here), with its advanced section.
            u"triggerMode"_s, u"triggerSource"_s, u"triggerSlope"_s, u"triggerLevel"_s,
            u"triggerTimeout"_s, u"triggerHoldoff"_s, u"triggerHysteresis"_s, u"triggerClear"_s,
            // Math card (operation = picture buttons); FFT source under the spectrum.
            u"channelSwitch_mathCard"_s, u"mathScale"_s, u"mathPosition"_s, u"fftSource"_s,
            // Cursors panel.
            u"cursorsSwitch"_s, u"cursor1Trace"_s, u"cursor2Trace"_s, u"cursorStyle"_s,
            u"levelCursorsSwitch"_s, u"levelCursorTrace"_s,
            // Display and Measure panels.
            u"persistence"_s, u"averaging"_s, u"recordLength"_s, u"autoscaleSetsLevel"_s, u"rollMode"_s,
            u"frequencyMethod"_s, u"measureSource"_s, u"measureStatistics"_s,
        };
        expected.sort();
        if (names != expected)
            qWarning() << "setting controls:" << names;
        QCOMPARE(names, expected);
        QCOMPARE(QSet<QString>(names.begin(), names.end()).size(), names.size());

        // A few round trips: the control writes its setting.
        QObject* immediate = panel.find(u"triggerTypeImmediate"_s);
        QObject* edge = panel.find(u"triggerTypeEdge"_s);
        QVERIFY(immediate && edge);
        QVERIFY(QMetaObject::invokeMethod(immediate, "clicked"));
        QCOMPARE(scope.triggerType(), ScopeController::Immediate);
        QVERIFY(QMetaObject::invokeMethod(edge, "clicked"));
        QCOMPARE(scope.triggerType(), ScopeController::Edge);
        QObject* mode = panel.find(u"triggerMode"_s);   // Auto / Normal
        QVERIFY(mode);
        QVERIFY(QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 1)));
        QCOMPARE(scope.noTriggerMode(), ScopeController::KeepLastTrace);
        QVERIFY(QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 0)));
        QCOMPARE(scope.noTriggerMode(), ScopeController::ShowUntriggered);
        QCOMPARE(scope.triggerType(), ScopeController::Edge);
        QObject* fft = panel.find(u"fftSource"_s);       // CH0 / CH1 (Off = View YT)
        QVERIFY(QMetaObject::invokeMethod(fft, "activated", Q_ARG(int, 1)));
        QCOMPARE(scope.fftSource(), ScopeController::FftChannel1);
        QVERIFY(QMetaObject::invokeMethod(fft, "activated", Q_ARG(int, 0)));
        QCOMPARE(scope.fftSource(), ScopeController::FftChannel0);
        // View FFT turns the spectrum on, YT turns it off.
        QObject* view = panel.find(u"displayMode"_s);
        QVERIFY(QMetaObject::invokeMethod(view, "activated", Q_ARG(int, 0)));
        QCOMPARE(scope.fftSource(), ScopeController::FftOff);
        QVERIFY(QMetaObject::invokeMethod(view, "activated", Q_ARG(int, 2)));
        QVERIFY(scope.fftSource() != ScopeController::FftOff);
        // Math operation picture buttons.
        QObject* product = panel.find(u"mathOperation2"_s);
        QVERIFY(product);
        QVERIFY(QMetaObject::invokeMethod(product, "clicked"));
        QCOMPARE(int(scope.math()->operation()), 2);
        QVERIFY(product->property("checked").toBool());
        QVERIFY(!product->property("glyph").toString().isEmpty());
        QObject* position = panel.find(u"horizontalPosition"_s);
        QVERIFY(QMetaObject::invokeMethod(position, "valueModified", Q_ARG(double, 20.0)));
        QCOMPARE(scope.horizontalPosition(), 20.0);
    }

    // docs/UI.md §1 "Show what matters now": Immediate shows no Edge field (hidden, not disabled):
    // mode, source, slope, level (Trigger card) and the trigger position (bar); audio inputs hide
    // the input range; roll only from 50 ms/div.
    void immediateHidesEdgeFields()
    {
        ScopeController scope;
        scope.initialize();
        Panel panel;
        if (!createPanel(scope, panel))
            QSKIP("no graphics device");
        QObject* card = panel.find(u"triggerCard"_s);
        QVERIFY(card);
        card->setProperty("advanced", true);          // timeout, holdoff, hysteresis unfolded
        panel.root->setProperty("tool", u"display"_s);  // the Display panel (roll) open
        panel.window->renderFrame();
        // A new session opens in Auto: the trigger level knob is live from the start.
        QCOMPARE(scope.triggerType(), ScopeController::Edge);
        const QStringList edgeOnly{u"triggerMode"_s, u"triggerSource"_s, u"triggerSlope"_s, u"triggerLevel"_s,
                                   u"triggerTimeout"_s, u"triggerHoldoff"_s, u"triggerHysteresis"_s,
                                   u"horizontalPosition"_s, u"triggerAdvancedButton"_s};
        for (const QString& name : edgeOnly) {
            QVERIFY2(panel.shown(name), qPrintable(name + u" hidden with Edge"_s));
            QVERIFY2(panel.find(name)->property("enabled").toBool(), qPrintable(name + u" disabled"_s));
        }
        QVERIFY(panel.find(u"triggerLevelMarker"_s)->property("visible").toBool());

        scope.setTriggerType(ScopeController::Immediate);
        panel.window->renderFrame();
        for (const QString& name : edgeOnly) {
            QVERIFY2(panel.find(name), qPrintable(name));
            QVERIFY2(!panel.shown(name), qPrintable(name + u" shown with Immediate"_s));
        }
        QVERIFY(!panel.find(u"triggerLevelMarker"_s)->property("visible").toBool());
        // The type stays visible (the way back to Edge).
        QVERIFY(panel.shown(u"triggerTypeEdge"_s));
        QVERIFY(panel.shown(u"triggerTypeImmediate"_s));
        QVERIFY(panel.find(u"triggerTypeImmediate"_s)->property("checked").toBool());

        scope.setTriggerType(ScopeController::Edge);
        panel.window->renderFrame();
        QVERIFY(panel.shown(u"triggerLevel"_s));

        QVERIFY(panel.shown(u"inputRange0"_s));
        QVERIFY(scope.setChannelSource(0, ScopeChannel::AudioLeft));
        panel.window->renderFrame();
        QVERIFY(!panel.shown(u"inputRange0"_s));

        QVERIFY(!panel.shown(u"rollMode"_s));   // 5 ms/div
        scope.setTimePerDiv(50e-3);
        panel.window->renderFrame();
        QVERIFY(panel.shown(u"rollMode"_s));
        // Roll is untriggered: the edge settings hide, the type stays.
        scope.setRollMode(true);
        panel.window->renderFrame();
        QVERIFY(scope.rolling());
        for (const QString& name : {u"triggerLevel"_s, u"triggerSource"_s, u"triggerMode"_s, u"horizontalPosition"_s})
            QVERIFY2(!panel.shown(name), qPrintable(name + u" shown while rolling"_s));
        QVERIFY(panel.shown(u"triggerTypeEdge"_s));
    }

    // Timebase, trigger position,
    // type, mode, source, slope and level are on screen with no tool open; one control per action
    // (no Math/References cards beside their tools, no FFT or Trigger tool, no range chip beside
    // the range buttons); the column keeps one width; an open tool panel pushes the display aside
    // when there is room (the traces stay visible) and floats in a narrow pane.
    void horizontalAndTriggerAlwaysVisible()
    {
        ScopeController scope;
        scope.initialize();
        Panel panel;
        if (!createPanel(scope, panel))
            QSKIP("no graphics device");
        QCOMPARE(panel.root->property("tool").toString(), QString());
        for (const QString& name : {u"timeDiv"_s, u"horizontalPosition"_s, u"triggerCard"_s, u"triggerTypeImmediate"_s,
                                    u"triggerTypeEdge"_s, u"triggerMode"_s, u"triggerSource"_s, u"triggerSlope"_s,
                                    u"triggerLevel"_s, u"displayMode"_s, u"mathCard"_s, u"toolPalette"_s})
            QVERIFY2(panel.shown(name), qPrintable(name + u" not visible without a tool"_s));
        // The slope choices are pictograms.
        const QVariantList slopes = panel.find(u"triggerSlope"_s)->property("model").toList();
        QCOMPARE(slopes.size(), 2);
        for (const QVariant& slope : slopes)
            QVERIFY(!slope.toMap().value(u"icon"_s).toString().isEmpty());

        // No duplicates: the tools are what no card shows.
        QStringList tools;
        for (const QVariant& entry : panel.find(u"toolPalette"_s)->property("model").toList())
            tools.append(entry.toMap().value(u"id"_s).toString());
        // (Export and print are the pane's ⋯ menu entries.)
        QCOMPARE(tools, (QStringList{u"measure"_s, u"cursors"_s, u"references"_s, u"display"_s}));
        for (const QString& gone : {u"mathPanel"_s, u"fftPanel"_s, u"triggerPanel"_s, u"exportPanel"_s, u"mathSwitch"_s})
            QVERIFY2(!panel.find(gone), qPrintable(gone + u" still exists"_s));
        QVERIFY(panel.find(u"channelCard0"_s)->property("facts").toList().isEmpty());
        QVERIFY(scope.setChannelSource(0, ScopeChannel::AudioLeft));   // no range buttons: the card says it
        QCOMPARE(panel.find(u"channelCard0"_s)->property("facts").toList().size(), 1);
        QVERIFY(scope.setChannelSource(0, ScopeChannel::AI0));

        // One column width; the default screen (CH 1 off) needs no scrolling at 1440 × 900.
        auto* column = qobject_cast<QQuickItem*>(panel.find(u"scopeColumn"_s));
        auto* display = qobject_cast<QQuickItem*>(panel.find(u"ytPlot"_s));
        QVERIFY(column && display);
        QCOMPARE(column->width(), 260.0);
        QVERIFY(!column->property("scrolls").toBool());
        const double displayWidth = display->width();
        scope.setChannelEnabled(1, true);
        scope.math()->setEnabled(true);
        panel.window->renderFrame();
        QCOMPARE(column->width(), 260.0);
        QCOMPARE(display->width(), displayWidth);
        QVERIFY(!panel.find(u"channelCard1"_s)->property("facts").toList().size());

        // A tool panel beside the display, not over the traces.
        panel.root->setProperty("tool", u"measure"_s);
        panel.window->renderFrame();
        auto* measure = qobject_cast<QQuickItem*>(panel.find(u"measurePanel"_s));
        QVERIFY(measure && measure->isVisible());
        const auto left = [](QQuickItem* i) { return i->mapToScene(QPointF(0, 0)).x(); };
        QVERIFY2(left(display) + display->width() <= left(measure) + 0.5,
                 qPrintable(u"display right %1, panel left %2"_s.arg(left(display) + display->width()).arg(left(measure))));
        QVERIFY(display->width() < displayWidth);
        panel.root->setProperty("tool", QString());
        panel.window->renderFrame();
        QCOMPARE(display->width(), displayWidth);
    }

    void toolPanelFloatsWhenNarrow()
    {
        ScopeController scope;
        scope.initialize();
        Panel panel;
        if (!createPanel(scope, panel, QSize(900, 700)))
            QSKIP("no graphics device");
        auto* column = qobject_cast<QQuickItem*>(panel.find(u"scopeColumn"_s));
        auto* display = qobject_cast<QQuickItem*>(panel.find(u"ytPlot"_s));
        QCOMPARE(column->width(), 220.0);
        const double displayWidth = display->width();
        panel.root->setProperty("tool", u"measure"_s);
        panel.window->renderFrame();
        QCOMPARE(display->width(), displayWidth);
        // The narrow view controls fit the compact column.
        auto* view = qobject_cast<QQuickItem*>(panel.find(u"displayMode"_s));
        QVERIFY(view && view->isVisible());
        const double right = view->mapToScene(QPointF(view->width(), 0)).x();
        QVERIFY2(right <= column->mapToScene(QPointF(column->width(), 0)).x() + 0.5, qPrintable(QString::number(right)));
        // The Trigger card's header fits the compact column (the mode gets a row of its own).
        auto* card = qobject_cast<QQuickItem*>(panel.find(u"triggerCard"_s));
        auto* sliders = qobject_cast<QQuickItem*>(panel.find(u"triggerAdvancedButton"_s));
        auto* mode = qobject_cast<QQuickItem*>(panel.find(u"triggerMode"_s));
        QVERIFY(card && sliders && mode && mode->isVisible());
        const double cardRight = card->mapToScene(QPointF(card->width(), 0)).x();
        QVERIFY2(sliders->mapToScene(QPointF(sliders->width(), 0)).x() <= cardRight + 0.5, "sliders button outside the card");
        QVERIFY2(mode->mapToScene(QPointF(mode->width(), 0)).x() <= cardRight + 0.5, "trigger mode outside the card");
    }

    // The two cursors use different colors. C1 / C2 (and H1 / H2) are drawn in the two
    // cursor pair colors, the same in their card; the card reads Δt, 1/Δt and ΔV.
    void cursorPairColors()
    {
        ScopeController scope;
        scope.initialize();
        scope.setCursorsOn(true);
        scope.setLevelCursorsOn(true);
        scope.setChannelEnabled(1, true);   // C2 follows CH 1 (ELVISmx default)
        Panel panel;
        if (!createPanel(scope, panel))
            QSKIP("no graphics device");
        QSignalSpy frames(&scope, &ScopeController::recordChanged);
        scope.run();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 2, 20000);
        scope.stop();
        QTRY_VERIFY(scope.isIdle());
        panel.window->renderFrame();
        QObject* c1 = panel.find(u"cursorItem1"_s);
        QObject* c2 = panel.find(u"cursorItem2"_s);
        QObject* card = panel.find(u"cursorCard"_s);
        QVERIFY(c1 && c2 && card);
        const QColor color1 = c1->property("color").value<QColor>();
        const QColor color2 = c2->property("color").value<QColor>();
        QVERIFY2(color1 != color2, "C1 and C2 have the same color");
        QCOMPARE(card->property("color1").value<QColor>(), color1);
        QCOMPARE(card->property("color2").value<QColor>(), color2);
        QCOMPARE(panel.find(u"levelCursor1"_s)->property("color").value<QColor>(), color1);
        QCOMPARE(panel.find(u"levelCursor2"_s)->property("color").value<QColor>(), color2);
        // Readouts: Δt large, then 1/Δt, ΔV, C1, C2.
        const double dt = scope.cursor2()->position() - scope.cursor1()->position();
        QCOMPARE(card->property("value").toString(), scope.formatMeasurement(dt, u"s"_s));
        QStringList names;
        for (const QVariant& item : card->property("items").toList()) {
            names.append(item.toMap().value(u"name"_s).toString());
            QVERIFY2(!item.toMap().value(u"text"_s).toString().isEmpty(), qPrintable(names.last()));
        }
        QCOMPARE(names, (QStringList{u"1/Δt"_s, u"ΔV"_s, u"C1"_s, u"C2"_s}));
        QVERIFY(card->property("items").toList().first().toMap().value(u"text"_s).toString().endsWith(u"Hz"_s));
        QVERIFY(panel.shown(u"levelCursorCard"_s));
        // Δt is in the card: the plot's own Δ chip between C1 and C2 is off (follow-up, ui/plot switch).
        QVERIFY(!panel.find(u"ytPlot"_s)->property("cursorDeltaVisible").toBool());
        // The Trace Styles dialog offers the cursors in their own colors.
        bool found1 = false, found2 = false;
        const QVariantList styles = panel.root->property("panel").value<QObject*>()->property("traceStyles").toList();
        for (const QVariant& entry : styles) {
            const QVariantMap m = entry.toMap();
            if (m.value(u"name"_s).toString() == u"Cursor C1"_s)
                found1 = m.value(u"color"_s).value<QColor>() == color1;
            if (m.value(u"name"_s).toString() == u"Cursor C2"_s)
                found2 = m.value(u"color"_s).value<QColor>() == color2;
        }
        QVERIFY(found1 && found2);
    }

    // Screenshots of busy states for review (SCOPE_SHOTS=<dir>): everything on, FFT view.
    void reviewShots()
    {
        const QString dir = qEnvironmentVariable("SCOPE_SHOTS");
        if (dir.isEmpty())
            QSKIP("SCOPE_SHOTS not set");
        ScopeController scope;
        scope.initialize();
        scope.setChannelEnabled(1, true);
        scope.math()->setEnabled(true);
        scope.setCursorsOn(true);
        scope.setLevelCursorsOn(true);
        Panel panel;
        if (!createPanel(scope, panel))
            QSKIP("no graphics device");
        QSignalSpy frames(&scope, &ScopeController::recordChanged);
        scope.run();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 3, 20000);
        for (int i = 0; i < 3; ++i) {
            panel.window->renderFrame();
            QCoreApplication::processEvents();
        }
        QVERIFY(panel.window->grab().save(dir + u"/scope-all-on.png"_s));
        panel.root->setProperty("tool", u"cursors"_s);
        panel.find(u"triggerCard"_s)->setProperty("advanced", true);
        scope.math()->setEnabled(false);
        scope.setLevelCursorsOn(false);
        scope.setFftSource(ScopeController::FftChannel0);
        const int seen = frames.count();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= seen + 3, 20000);
        for (int i = 0; i < 3; ++i) {
            panel.window->renderFrame();
            QCoreApplication::processEvents();
        }
        QVERIFY(panel.window->grab().save(dir + u"/scope-fft-cursors.png"_s));
        panel.root->setProperty("tool", QString());
        for (int i = 0; i < 3; ++i) {
            panel.window->renderFrame();
            QCoreApplication::processEvents();
        }
        QVERIFY(panel.window->grab().save(dir + u"/scope-fft.png"_s));
        scope.stop();
        QTRY_VERIFY(scope.isIdle());
    }

    // The measurement strip is the only place to choose measurements: "+" adds a row for every
    // channel, Remove takes it away; the cursor rows (C1, C2, ΔV, Δt, 1/Δt) follow the cursors.
    void measurementStrip()
    {
        ScopeController scope;
        scope.initialize();
        scope.setChannelEnabled(1, true);
        Panel panel;
        if (!createPanel(scope, panel))
            QSKIP("no graphics device");
        QObject* table = panel.find(u"measurementCards"_s);
        QVERIFY(table);
        auto rowIds = [table] {
            QStringList ids;
            for (const QVariant& row : table->property("rows").toList())
                ids.append(row.toMap().value(u"id"_s).toString());
            return ids;
        };
        auto columns = [table] {
            QStringList titles;
            for (const QVariant& column : table->property("columns").toList())
                titles.append(column.toMap().value(u"title"_s).toString());
            return titles;
        };
        QCOMPARE(columns(), (QStringList{u"CH0"_s, u"CH1"_s}));
        QCOMPARE(rowIds(), (QStringList{u"m1"_s, u"m2"_s, u"m4"_s}));   // RMS, Frequency, Peak-to-peak

        const QString mean = u"m%1"_s.arg(int(ScopeChannel::Mean));
        QVERIFY(QMetaObject::invokeMethod(table, "addRequested", Q_ARG(QString, mean)));
        QVERIFY(scope.channel0()->shows(ScopeChannel::Mean));
        QVERIFY(scope.channel1()->shows(ScopeChannel::Mean));
        QVERIFY(rowIds().contains(mean));
        QVERIFY(QMetaObject::invokeMethod(table, "removeRequested", Q_ARG(QString, u"m1"_s)));
        QVERIFY(!scope.channel0()->shows(ScopeChannel::Rms));
        QVERIFY(!rowIds().contains(u"m1"_s));
        // Every catalog entry is in the "+" menu.
        QCOMPARE(table->property("available").toList().size(), qsizetype(ScopeChannel::kMeasurementCount));

        // Math adds a column; cursors add their rows and menu entries.
        scope.math()->setEnabled(true);
        QCOMPARE(columns(), (QStringList{u"CH0"_s, u"CH1"_s, u"Math"_s}));
        scope.setCursorsOn(true);
        QStringList ids = rowIds();
        for (const QString& id : {u"cursor1"_s, u"cursor2"_s, u"cursorDelta"_s, u"cursorDeltaT"_s, u"cursorRate"_s})
            QVERIFY2(ids.contains(id), qPrintable(id));
        QCOMPARE(table->property("available").toList().size(), qsizetype(ScopeChannel::kMeasurementCount + 3));
        QVERIFY(QMetaObject::invokeMethod(table, "removeRequested", Q_ARG(QString, u"cursorDeltaT"_s)));
        QVERIFY(!scope.showDeltaTime());
        ids = rowIds();
        QVERIFY(!ids.contains(u"cursorDelta"_s) && !ids.contains(u"cursorDeltaT"_s) && !ids.contains(u"cursorRate"_s));
        QVERIFY(QMetaObject::invokeMethod(table, "addRequested", Q_ARG(QString, u"cursorDelta"_s)));
        QVERIFY(scope.showDeltaTime());
        QVERIFY(QMetaObject::invokeMethod(table, "removeRequested", Q_ARG(QString, u"cursor1"_s)));
        QVERIFY(!scope.cursor1()->showValue());
        scope.setCursorsOn(false);
        QVERIFY(!rowIds().contains(u"cursor2"_s));
        // One summary line for short windows.
        QVERIFY(!table->property("summaryText").toString().isEmpty());
        // The Measure panel's picture buttons write the same set: Peak on, then off again.
        QObject* peak = panel.find(u"measureType%1"_s.arg(int(ScopeChannel::Peak)));
        QVERIFY(peak);
        QVERIFY(QMetaObject::invokeMethod(peak, "clicked"));
        QVERIFY(scope.channel0()->shows(ScopeChannel::Peak));
        QVERIFY(rowIds().contains(u"m%1"_s.arg(int(ScopeChannel::Peak))));
        QVERIFY(QMetaObject::invokeMethod(peak, "clicked"));
        QVERIFY(!scope.channel0()->shows(ScopeChannel::Peak));
    }

    // A session of an older version with "FFT of CH 1" as math opens with the FFT view of CH 1,
    // shown by the FFT section, and the math trace off.
    void migratedFftInPanel()
    {
        ScopeController scope;
        scope.initialize();
        scope.restoreSettings(QJsonObject{{u"mathTrace"_s, QJsonObject{{u"enabled"_s, true}, {u"operation"_s, u"FftB"_s}}}});
        Panel panel;
        if (!createPanel(scope, panel))
            QSKIP("no graphics device");
        QCOMPARE(panel.find(u"fftSource"_s)->property("currentIndex").toInt(), 1);   // CH0, CH1
        QVERIFY(!panel.find(u"mathCard"_s)->property("checked").toBool());
        QVERIFY(panel.find(u"fftPlot"_s)->property("visible").toBool());
    }
};

QTEST_MAIN(TestScopeQml)
#include "tst_scope_qml.moc"
