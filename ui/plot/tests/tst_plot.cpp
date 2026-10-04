// Unit tests of MyBench.Plot: ticks (lin/log/divisions/time), labels, value<->pixel mapping,
// PlotAxis zoom/pan/limits, min/max pyramid, decimation, TraceBuffer thread safety and
// recording, LogicBuffer, WaterfallBuffer, and an offscreen render of a Plot.
#include <mybench/offscreen.h>
#include <mybench/plot/decimation.h>
#include <mybench/plot/logicbuffer.h>
#include <mybench/plot/plot.h>
#include <mybench/plot/plotaxis.h>
#include <mybench/plot/scale.h>
#include <mybench/plot/tracebuffer.h>
#include <mybench/plot/tracestyle.h>
#include <mybench/plot/waterfall.h>

#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <cmath>
#include <thread>

using namespace mybench::plot;

namespace {

QString minus(QString s)
{
    return s.replace(u'-', QChar(0x2212));
}

std::vector<double> sine(size_t n, double cycles, double amplitude = 1.0)
{
    std::vector<double> y(n);
    for (size_t i = 0; i < n; ++i)
        y[i] = amplitude * std::sin(2 * M_PI * cycles * double(i) / double(n));
    return y;
}

void mouse(QQuickWindow *w, QEvent::Type type, QPointF pos, Qt::MouseButton button = Qt::LeftButton,
           Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons(button);
    QMouseEvent e(type, pos, pos, pos, type == QEvent::MouseMove ? Qt::NoButton : button,
                  type == QEvent::MouseMove ? Qt::MouseButtons(button) : buttons, mods,
                  QPointingDevice::primaryPointingDevice());
    QCoreApplication::sendEvent(w, &e);
}

void drag(QQuickWindow *w, QPointF from, QPointF to, Qt::MouseButton button = Qt::LeftButton,
          Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    mouse(w, QEvent::MouseButtonPress, from, button, mods);
    for (int i = 1; i <= 5; ++i)
        mouse(w, QEvent::MouseMove, from + (to - from) * (i / 5.0), button, mods);
    mouse(w, QEvent::MouseButtonRelease, to, button, mods);
}

void wheel(QQuickWindow *w, QPointF pos, QPoint angle, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QWheelEvent e(pos, pos, QPoint(), angle, Qt::NoButton, mods, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &e);
}

// A 400 x 300 Plot (body given as QML) in an offscreen window; buffer "buf" if declared.
struct PlotFixture {
    std::unique_ptr<mybench::OffscreenWindow> offscreen;
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    Plot *plot = nullptr;
    TraceBuffer *buf = nullptr;

    bool create(const QByteArray &body)
    {
        offscreen = std::make_unique<mybench::OffscreenWindow>(QSize(400, 300));
        if (!offscreen->isValid())
            return false;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport MyBench.Plot\nPlot {\nwidth: 400; height: 300\n" + body + "}\n", QUrl());
        root.reset(component.create());
        if (!root)
            qFatal("%s", qPrintable(component.errorString()));
        plot = qobject_cast<Plot *>(root.get());
        buf = root->findChild<TraceBuffer *>("buf");
        offscreen->setRootItem(plot, false);
        offscreen->renderFrame();
        return true;
    }
    QQuickWindow *window() const { return offscreen->window(); }
    void render() { offscreen->renderFrame(); }
    QImage grab() { return offscreen->grab().convertToFormat(QImage::Format_RGB32); }
    void setData(const std::vector<double> &y, double x0, double dx)
    {
        buf->setUniform(x0, dx, std::span<const double>(y));
        QCoreApplication::processEvents();
    }
    void release() { offscreen->setRootItem(nullptr); }
};

}  // namespace

class TestPlot : public QObject
{
    Q_OBJECT

private slots:
    // ---- scale / ticks ------------------------------------------------------------------------
    void niceSteps()
    {
        QCOMPARE(niceStep(0.3), 0.5);
        QCOMPARE(niceStep(1.0), 1.0);
        QCOMPARE(niceStep(1.1), 2.0);
        QCOMPARE(niceStep(2.5), 5.0);
        QCOMPARE(niceStep(7.0), 10.0);
        QCOMPARE(niceStep(0.0012), 0.002);
        QCOMPARE(niceStepBelow(7.0), 5.0);
        QCOMPARE(niceStepBelow(1.9), 1.0);
        QCOMPARE(niceStepBelow(0.25), 0.2);
    }

    void linearTickSets()
    {
        TickSet t = linearTicks(0, 10, 5);
        QCOMPARE(t.step, 2.0);
        QCOMPARE(t.major, (std::vector<double>{0, 2, 4, 6, 8, 10}));
        QCOMPARE(t.minor.size(), size_t(5 * 3 + 0));  // 2-steps split in 4: 3 minors per interval
        QCOMPARE(t.minor.front(), 0.5);

        t = linearTicks(-1, 1, 4);
        QCOMPARE(t.step, 0.5);
        QCOMPARE(t.major.size(), size_t(5));
        QCOMPARE(t.major[2], 0.0);
        QVERIFY(!std::signbit(t.major[2]));
        QCOMPARE(formatTick(-0.5, t, ScaleType::Linear, LabelStyle::Engineering), minus("-0.5"));
        QCOMPARE(formatTick(0.0, t, ScaleType::Linear, LabelStyle::Engineering), QStringLiteral("0"));
        QCOMPARE(formatTick(1.0, t, ScaleType::Linear, LabelStyle::Engineering), QStringLiteral("1.0"));

        t = linearTicks(0, 20000, 4);
        QCOMPARE(t.step, 5000.0);
        QCOMPARE(t.exponent, 3);
        QStringList labels;
        for (double v : t.major)
            labels << formatTick(v, t, ScaleType::Linear, LabelStyle::Engineering);
        QCOMPARE(labels, (QStringList{"0", "5 k", "10 k", "15 k", "20 k"}));
        QCOMPARE(formatTick(10000, t, ScaleType::Linear, LabelStyle::Engineering, "Hz"), QStringLiteral("10 kHz"));

        t = linearTicks(-40, 0, 4);
        QCOMPARE(formatTick(-20, t, ScaleType::Linear, LabelStyle::Engineering), minus("-20"));

        t = linearTicks(0, 0.01, 5);
        QCOMPARE(t.step, 0.002);
        QCOMPARE(formatTick(0.004, t, ScaleType::Linear, LabelStyle::Engineering), QStringLiteral("4 m"));
        QCOMPARE(formatTick(0.004, t, ScaleType::Linear, LabelStyle::Engineering, "s"), QStringLiteral("4 ms"));

        // Many significant digits stay plain numbers.
        t = linearTicks(1000.0, 1000.5, 5);
        QCOMPARE(t.exponent, 0);
        QCOMPARE(formatTick(1000.2, t, ScaleType::Linear, LabelStyle::Engineering), QStringLiteral("1000.2"));

        // Degenerate input.
        QVERIFY(linearTicks(1, 1, 5).major.empty());
        QVERIFY(linearTicks(qQNaN(), 1, 5).major.empty());
    }

    void divisionTickSets()
    {
        const TickSet t = divisionTicks(-5e-3, 5e-3, 10, 5);
        QCOMPARE(t.major.size(), size_t(11));
        QCOMPARE(t.minor.size(), size_t(40));
        QCOMPARE(t.major[5], 0.0);
        QCOMPARE(formatTick(-4e-3, t, ScaleType::Linear, LabelStyle::Engineering, "s"), minus("-4 ms"));
        const TickSet v = divisionTicks(-4, 4, 8, 5);
        QCOMPARE(v.major.front(), -4.0);
        QCOMPARE(v.major.back(), 4.0);
    }

    void logTickSets()
    {
        // 3 decades over 300 px: decades are majors, 2..9 minors.
        TickSet t = logTicks(20, 20000, 300, 60);
        QVERIFY(t.logDecades);
        QCOMPARE(t.major, (std::vector<double>{100, 1000, 10000}));
        QVERIFY(std::find(t.minor.begin(), t.minor.end(), 20.0) != t.minor.end());
        QVERIFY(std::find(t.minor.begin(), t.minor.end(), 20000.0) != t.minor.end());
        QStringList labels;
        for (double v : t.major)
            labels << formatTick(v, t, ScaleType::Log, LabelStyle::Engineering);
        QCOMPARE(labels, (QStringList{"100", "1 k", "10 k"}));

        // Plenty of room: 1-2-5 labels.
        t = logTicks(10, 1000, 800, 60);
        QCOMPARE(t.major, (std::vector<double>{10, 20, 50, 100, 200, 500, 1000}));

        // Crowded: every n-th decade.
        t = logTicks(1, 1e12, 200, 60);
        QVERIFY(t.major.size() >= 2 && t.major.size() <= 4);
        for (double v : t.major)
            QCOMPARE(std::fmod(std::log10(v), double(t.step)), 0.0);

        // Less than a decade: linear steps.
        t = logTicks(100, 300, 500, 60);
        QVERIFY(!t.logDecades);
        QVERIFY(t.major.size() >= 3);
        QCOMPARE(formatTick(150, t, ScaleType::Log, LabelStyle::Engineering), QStringLiteral("150"));

        QVERIFY(logTicks(-1, 10, 100).major.empty());
    }

    void timeTickSets()
    {
        const double t0 = 1.7e9;  // seconds since the epoch
        const TickSet t = timeTicks(t0, t0 + 300, 5);
        QCOMPARE(t.step, 60.0);
        QVERIFY(!t.major.empty());
        for (double v : t.major)
            QVERIFY(v >= t0 && v <= t0 + 300);
        QCOMPARE(formatTick(t.major.front(), t, ScaleType::Linear, LabelStyle::Time).size(), 5);  // HH:mm
    }

    void engineeringFormat()
    {
        QCOMPARE(formatEngineering(0.001234, "s"), QStringLiteral("1.234 ms"));
        QCOMPARE(formatEngineering(1234.6, "Hz"), QStringLiteral("1.235 kHz"));
        QCOMPARE(formatEngineering(999.96, "V", 4), QStringLiteral("1.000 kV"));
        QCOMPARE(formatEngineering(-20, "dB"), minus("-20.00 dB"));
        QCOMPARE(formatEngineering(0, "V", 3), QStringLiteral("0.00 V"));
        QCOMPARE(formatEngineering(2.5e-7, "A"), QStringLiteral("250.0 nA"));
        QCOMPARE(formatEngineering(qQNaN(), "V"), QStringLiteral("— V"));
    }

    // ---- mapping ------------------------------------------------------------------------------
    void scaleMapping()
    {
        ScaleMap m;
        m.min = 0;
        m.max = 10;
        m.pixelStart = 100;
        m.pixelLength = 200;
        QCOMPARE(m.toPixel(5), 200.0);
        QCOMPARE(m.fromPixel(300), 10.0);
        m.inverted = true;
        QCOMPARE(m.toPixel(0), 300.0);
        QCOMPARE(m.fromPixel(100), 10.0);

        ScaleMap y;  // y axis: pixel of min at the bottom (400), growing up
        y.min = -1;
        y.max = 1;
        y.pixelStart = 400;
        y.pixelLength = -200;
        QCOMPARE(y.toPixel(1), 200.0);
        QCOMPARE(y.toPixel(0), 300.0);

        ScaleMap lg;
        lg.type = ScaleType::Log;
        lg.min = 10;
        lg.max = 1000;
        lg.pixelStart = 0;
        lg.pixelLength = 200;
        QCOMPARE(lg.toPixel(100), 100.0);
        QVERIFY(std::abs(lg.fromPixel(50) - std::sqrt(1000.0)) < 1e-9);
        QVERIFY(std::isnan(lg.toPixel(0)));
        for (double v : {12.5, 99.0, 777.0})
            QVERIFY(std::abs(lg.fromPixel(lg.toPixel(v)) - v) < 1e-9 * v);
    }

    void axisZoomPanLimits()
    {
        PlotAxis axis;
        axis.setRange(0, 10);
        QVERIFY(!axis.zoomed());
        axis.zoom(2.0, 5.0);
        QCOMPARE(axis.min(), 2.5);
        QCOMPARE(axis.max(), 7.5);
        QVERIFY(axis.zoomed());
        axis.pan(0.2);
        QCOMPARE(axis.min(), 3.5);
        QCOMPARE(axis.max(), 8.5);
        axis.reset();
        QCOMPARE(axis.min(), 0.0);
        QCOMPARE(axis.max(), 10.0);
        QVERIFY(!axis.zoomed());

        axis.setLowerLimit(0);
        axis.setUpperLimit(10);
        axis.setViewRange(-5, 3);
        QCOMPARE(axis.min(), 0.0);
        QCOMPARE(axis.max(), 8.0);
        axis.setViewRange(-50, 50);  // wider than the limits: clamp to them
        QCOMPARE(axis.min(), 0.0);
        QCOMPARE(axis.max(), 10.0);

        PlotAxis lg;
        lg.setScale(PlotAxis::Log);
        lg.setRange(10, 1e5);
        lg.zoom(2.0, 1000);  // 4 decades -> 2 decades around 1 k
        QVERIFY(std::abs(lg.min() - 100) < 1e-9);
        QVERIFY(std::abs(lg.max() - 1e4) < 1e-6);
        QVERIFY(std::abs(lg.normalized(1000) - 0.5) < 1e-12);

        // Binding-style updates reset the view and become home.
        axis.setViewRange(2, 4);
        axis.setMax(20);
        QCOMPARE(axis.min(), 0.0);
        QCOMPARE(axis.max(), 20.0);
        QVERIFY(!axis.zoomed());
    }

    // ---- storage / pyramid ----------------------------------------------------------------------
    void pyramidRangeMinMax()
    {
        const size_t n = 300000;  // spans 5 chunks
        std::vector<double> y(n);
        QRandomGenerator rng(42);
        for (double &v : y)
            v = double(int(rng.bounded(200000)) - 100000) / 64.0;  // exactly representable in float
        TraceBuffer buffer;
        buffer.setUniform(0, 1, std::span<const double>(y));
        const TraceSnapshot s = buffer.snapshot();
        QCOMPARE(s->size(), n);
        for (int k = 0; k < 400; ++k) {
            size_t i = rng.bounded(quint32(n)), j = rng.bounded(quint32(n));
            if (k % 5 == 0)
                j = i + rng.bounded(40);  // short ranges
            if (i > j)
                std::swap(i, j);
            j = std::min(j + 1, n);
            double mn, mx;
            QVERIFY(s->rangeMinMax(i, j, &mn, &mx));
            const auto [a, b] = std::minmax_element(y.begin() + qsizetype(i), y.begin() + qsizetype(j));
            QCOMPARE(mn, *a);
            QCOMPARE(mx, *b);
        }
    }

    void recordingRollingAndGrowth()
    {
        TraceBuffer buffer;
        buffer.setMaximumCount(100000);
        buffer.startUniform(0.0, 0.001);
        std::vector<double> block(7777);
        size_t total = 0;
        while (total < 1000000) {
            for (size_t i = 0; i < block.size(); ++i)
                block[i] = double(total + i);
            buffer.appendSamples(std::span<const double>(block));
            total += block.size();
        }
        const TraceSnapshot s = buffer.snapshot();
        QCOMPARE(s->size(), size_t(100000));
        QCOMPARE(s->yAt(0), double(total - 100000));
        QCOMPARE(s->yAt(s->size() - 1), double(total - 1));
        QVERIFY(std::abs(s->xAt(0) - double(total - 100000) * 0.001) < 1e-9);
        double mn, mx;
        QVERIFY(s->rangeMinMax(10, 60000, &mn, &mx));
        QCOMPARE(mn, s->yAt(10));
        QCOMPARE(mx, s->yAt(59999));

        // Explicit append converts, stays sorted, interpolates.
        TraceBuffer trend;
        for (int i = 0; i < 1000; ++i)
            trend.append(i * 0.5, i);
        const TraceSnapshot t = trend.snapshot();
        QVERIFY(!t->uniform);
        QVERIFY(t->sorted);
        QCOMPARE(t->interpolate(10.25), 20.5);
        QCOMPARE(t->lowerBound(10.0), size_t(20));
        QCOMPARE(t->upperBound(10.0), size_t(21));
        trend.append(-1, 0);  // going back in x: unsorted (XY)
        QVERIFY(!trend.snapshot()->sorted);
    }

    // ---- decimation -----------------------------------------------------------------------------
    void decimationEnvelope()
    {
        const size_t n = 1000000;
        std::vector<double> y = sine(n, 50.0);
        y[500000] = 10.0;   // a single-sample spike must survive
        y[123457] = -7.0;
        TraceBuffer buffer;
        buffer.setUniform(0, 1.0 / double(n), std::span<const double>(y));
        const TraceSnapshot s = buffer.snapshot();
        ScaleMap xm;
        xm.min = 0;
        xm.max = 1;
        xm.pixelStart = 0;
        xm.pixelLength = 1000;
        ScaleMap ym;
        ym.min = -10;
        ym.max = 10;
        ym.pixelStart = 500;
        ym.pixelLength = -500;
        std::vector<PixelPoint> out;
        const PolylineInfo info = buildPolyline(*s, xm, ym, {}, false, 1.0, out);
        QVERIFY(info.decimated);
        QVERIFY(out.size() <= 2 * 1000 + 4);
        QVERIFY(out.size() >= 1000);
        float top = 1e9f, bottom = -1e9f;
        for (const PixelPoint &p : out) {
            QVERIFY(!p.isGap());
            top = std::min(top, p.y);
            bottom = std::max(bottom, p.y);
        }
        QCOMPARE(top, float(ym.toPixel(10.0)));
        QCOMPARE(bottom, float(ym.toPixel(-7.0)));
        QCOMPARE(info.yMax, 10.0);
        QCOMPARE(info.yMin, -7.0);

        // Per column, the emitted pair equals the brute-force min/max of that column's samples.
        for (int column : {0, 1, 123, 500, 999}) {
            const size_t a = s->lowerBound(xm.fromPixel(column)), b = column == 999 ? n : s->lowerBound(xm.fromPixel(column + 1));
            const auto [lo, hi] = std::minmax_element(y.begin() + qsizetype(a), y.begin() + qsizetype(b));
            const float cx = float(column) + 0.5f;
            float pmin = 1e9f, pmax = -1e9f;
            for (const PixelPoint &p : out) {
                if (std::abs(p.x - cx) < 1e-3f) {
                    pmin = std::min(pmin, p.y);
                    pmax = std::max(pmax, p.y);
                }
            }
            QCOMPARE(pmin, float(ym.toPixel(*hi)));
            QCOMPARE(pmax, float(ym.toPixel(*lo)));
        }

        // Zoomed in to 300 samples: no decimation, one point per sample (+1 each side).
        xm.min = 0.2;
        xm.max = 0.2 + 299.0 / double(n);
        const PolylineInfo zoomed = buildPolyline(*s, xm, ym, {}, false, 1.0, out);
        QVERIFY(!zoomed.decimated);
        QVERIFY(out.size() >= 301 && out.size() <= 302);  // 300 samples + one beyond each edge
    }

    void decimationLogAndExplicit()
    {
        // Spectrum-like: 100k uniform bins on a log axis over 500 px.
        const size_t n = 100001;
        std::vector<double> y(n);
        for (size_t i = 0; i < n; ++i)
            y[i] = -100 + 10 * std::sin(double(i) * 0.37);
        TraceBuffer buffer;
        buffer.setUniform(0, 0.2, std::span<const double>(y));  // 0 .. 20 kHz
        ScaleMap xm;
        xm.type = ScaleType::Log;
        xm.min = 10;
        xm.max = 20000;
        xm.pixelStart = 0;
        xm.pixelLength = 500;
        ScaleMap ym;
        ym.min = -120;
        ym.max = 0;
        ym.pixelStart = 300;
        ym.pixelLength = -300;
        std::vector<PixelPoint> out;
        const PolylineInfo info = buildPolyline(*buffer.snapshot(), xm, ym, {}, false, 1.0, out);
        QVERIFY(info.decimated);
        QVERIFY(out.size() <= 2 * 500 + 4);
        for (size_t i = 1; i < out.size(); ++i)
            QVERIFY(out[i].x >= out[i - 1].x - 1e-3f);

        // Explicit x identical to uniform gives the same polyline.
        std::vector<double> x(n);
        for (size_t i = 0; i < n; ++i)
            x[i] = 0.2 * double(i);
        TraceBuffer explicitBuffer;
        explicitBuffer.setXY(std::span<const double>(x), std::span<const double>(y));
        std::vector<PixelPoint> out2;
        buildPolyline(*explicitBuffer.snapshot(), xm, ym, {}, false, 1.0, out2);
        QCOMPARE(out2.size(), out.size());
        for (size_t i = 0; i < out.size(); ++i) {
            QVERIFY(std::abs(out[i].x - out2[i].x) < 1e-3f);
            QCOMPARE(out[i].y, out2[i].y);
        }

        // Sparse data with step mode: 2 points per sample (except the first).
        TraceBuffer sparse;
        std::vector<double> sy{1, 2, 3, 4};
        sparse.setUniform(0, 1, std::span<const double>(sy));
        ScaleMap lx;
        lx.min = 0;
        lx.max = 3;
        lx.pixelLength = 300;
        buildPolyline(*sparse.snapshot(), lx, ym, {}, true, 1.0, out);
        QCOMPARE(out.size(), size_t(7));
        QCOMPARE(out[1].x, out[2].x);
        QCOMPARE(out[1].y, out[0].y);
    }

    void decimationTransformAndGaps()
    {
        std::vector<double> y{0, 1, qQNaN(), 1, 0};
        TraceBuffer buffer;
        buffer.setUniform(0, 1, std::span<const double>(y));
        ScaleMap xm;
        xm.min = 0;
        xm.max = 4;
        xm.pixelLength = 400;
        ScaleMap ym;
        ym.min = -4;
        ym.max = 4;
        ym.pixelStart = 400;
        ym.pixelLength = -400;
        std::vector<PixelPoint> out;
        TraceTransform tf;
        tf.yScale = 2.0;  // 0.5 V/div
        tf.yOffset = -1.0;
        buildPolyline(*buffer.snapshot(), xm, ym, tf, false, 1.0, out);
        QCOMPARE(out.size(), size_t(5));
        QVERIFY(out[2].isGap());
        QCOMPARE(out[1].y, float(ym.toPixel(1.0)));  // 1 * 2 - 1
    }

    // ---- thread safety ----------------------------------------------------------------------------
    void traceBufferThreadSafety()
    {
        TraceBuffer buffer;
        std::atomic<bool> stop{false};
        std::atomic<int> errors{0}, reads{0};
        QSignalSpy spy(&buffer, &TraceBuffer::changed);

        std::thread writer([&] {
            for (int gen = 1; !stop.load(); ++gen) {
                const size_t n = 1000 + size_t(gen % 97) * 700;
                std::vector<double> y(n, double(gen));
                buffer.setUniform(double(gen), 1.0, std::span<const double>(y));
            }
        });
        std::vector<std::thread> readers;
        for (int r = 0; r < 3; ++r) {
            readers.emplace_back([&] {
                while (!stop.load()) {
                    const TraceSnapshot s = buffer.snapshot();
                    if (s->empty())
                        continue;
                    const double gen = s->yAt(0);
                    if (s->size() != 1000 + size_t(int(gen) % 97) * 700 || s->x0 != gen || s->yAt(s->size() - 1) != gen)
                        ++errors;
                    double mn, mx;
                    if (!s->rangeMinMax(0, s->size(), &mn, &mx) || mn != gen || mx != gen)
                        ++errors;
                    ++reads;
                }
            });
        }
        QTest::qWait(300);
        stop = true;
        writer.join();
        for (auto &t : readers)
            t.join();
        QCOMPARE(errors.load(), 0);
        QVERIFY(reads.load() > 100);
        QTRY_VERIFY(spy.count() >= 1);
        // Coalesced: far fewer signals than updates.
        QVERIFY(quint64(spy.count()) < buffer.version());
    }

    void recordingThreadSafety()
    {
        TraceBuffer buffer;
        buffer.startUniform(0, 1);
        std::atomic<bool> stop{false};
        std::atomic<int> errors{0};
        std::thread writer([&] {
            std::vector<double> block(333);
            double next = 0;
            while (!stop.load() && next < 3e6) {
                for (double &v : block)
                    v = next++;
                buffer.appendSamples(std::span<const double>(block));
            }
        });
        std::thread reader([&] {
            QRandomGenerator rng(7);
            while (!stop.load()) {
                const TraceSnapshot s = buffer.snapshot();
                const size_t n = s->size();
                if (n < 2)
                    continue;
                const size_t i = rng.bounded(quint32(n)), j = std::min(n, i + 1 + rng.bounded(100000));
                double mn, mx;
                if (s->yAt(i) != double(i) || !s->rangeMinMax(i, j, &mn, &mx) || mn != double(i) || mx != double(j - 1))
                    ++errors;
            }
        });
        QTest::qWait(300);
        stop = true;
        writer.join();
        reader.join();
        QCOMPARE(errors.load(), 0);
    }

    // ---- logic / waterfall ------------------------------------------------------------------------
    void logicBuffer()
    {
        LogicBuffer b;
        b.setLaneCount(4);
        b.append(0.0, 0b0001);
        b.append(1.0, 0b0011);
        b.append(2.0, 0b0010);
        b.append(3.0, 0b0010);  // no change: no edge
        b.setEnd(5.0);
        const LogicSnapshot s = b.snapshot();
        QCOMPARE(s->startX, 0.0);
        QCOMPARE(s->endX, 5.0);
        QCOMPARE(s->edgeCount(0), size_t(1));
        QCOMPARE(s->edgeCount(1), size_t(1));
        QCOMPARE(s->edgeCount(2), size_t(0));
        QVERIFY(s->levelAt(0, 0.5));
        QVERIFY(!s->levelAt(0, 2.5));
        QVERIFY(s->levelAt(1, 1.0));   // edge exactly at x: level after it
        QVERIFY(!s->levelAt(1, 0.99));
        QVERIFY(s->levelAt(1, 4.0));

        // Uniform samples + rolling window.
        LogicBuffer u;
        u.setMaximumSpan(100.0);
        std::vector<quint8> states(1000);
        for (size_t i = 0; i < states.size(); ++i)
            states[i] = quint8((i / 10) & 1);  // lane 0 toggles every 10 samples
        u.appendSamples(0.0, 1.0, std::span<const quint8>(states));
        const LogicSnapshot us = u.snapshot();
        QCOMPARE(us->endX, 1000.0);
        QCOMPARE(us->startX, 900.0);
        QCOMPARE(us->edgeCount(0), size_t(10));
        QCOMPARE(us->levelAt(0, 905.0), bool((905 / 10) & 1));
        QCOMPARE(us->levelAt(0, 999.0), bool((999 / 10) & 1));
    }

    void waterfallRing()
    {
        WaterfallBuffer w;
        w.setCapacity(4);
        w.setColumns(0, 10, 3);
        for (int r = 0; r < 6; ++r) {
            std::vector<float> row{float(r), float(r), float(r)};
            w.appendRow(std::span<const float>(row));
        }
        QCOMPARE(w.rowCount(), qint64(6));
        std::vector<float> out;
        int columns = 0;
        const qint64 first = w.readRows(0, 10, out, &columns);
        QCOMPARE(first, qint64(2));   // rows 0, 1 dropped
        QCOMPARE(columns, 3);
        QCOMPARE(out.size(), size_t(12));
        QCOMPARE(out.front(), 2.0f);
        QCOMPARE(out.back(), 5.0f);
    }

    // ---- interaction ------------------------------------------------------------------------------
    void interaction()
    {
        mybench::OffscreenWindow offscreen(QSize(400, 300));
        if (!offscreen.isValid())
            QSKIP("no GPU / graphics device for offscreen rendering");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(
            "import QtQuick\n"
            "import MyBench.Plot\n"
            "Plot {\n"
            "    width: 400; height: 300\n"
            "    xAxis { min: 0; max: 10 }\n"
            "    yAxis { min: -1; max: 1 }\n"
            "    PlotCursor { objectName: \"cursor\"; position: 7.5; readoutVisible: false }\n"
            "    PlotLine { objectName: \"level\"; value: 0.5; handle: PlotLine.Start; label: \"T\" }\n"
            "}\n",
            QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *plot = qobject_cast<Plot *>(root.get());
        auto *cursor = root->findChild<PlotCursor *>("cursor");
        auto *level = root->findChild<PlotLine *>("level");
        QVERIFY(plot && cursor && level);
        offscreen.setRootItem(plot, false);
        QQuickWindow *w = offscreen.window();
        offscreen.renderFrame();
        PlotAxis *x = plot->xAxis();
        const QRectF area = plot->plotArea();
        QVERIFY(area.width() > 300 && area.height() > 200);

        // Wheel up at the center zooms x in by 1.2, anchored at the pointer.
        const QPointF center = area.center();
        const double anchor = plot->pixelToX(center.x());
        wheel(w, center, QPoint(0, 120));
        QVERIFY(std::abs((x->max() - x->min()) - 10.0 / 1.2) < 1e-9);
        QVERIFY(std::abs(plot->pixelToX(center.x()) - anchor) < 1e-9);
        QVERIFY(x->zoomed());
        // Shift+wheel zooms y only.
        const double xSpan = x->span();
        wheel(w, center, QPoint(0, 120), Qt::ShiftModifier);
        QCOMPARE(x->span(), xSpan);
        QVERIFY(std::abs(plot->yAxis()->span() - 2.0 / 1.2) < 1e-9);

        // Trackpad pinch zooms x.
        {
            const double span = x->span();
            QNativeGestureEvent pinch(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2, center, center,
                                      center, 0.25, QPointF(), 0);
            QCoreApplication::sendEvent(w, &pinch);
            QVERIFY2(std::abs(x->span() - span / 1.25) < 1e-9, qPrintable(QString::number(x->span())));
        }

        // Double-click resets to the home ranges.
        // Qt's sequence for a double click: press, release, press, double-click, release.
        mouse(w, QEvent::MouseButtonPress, center);
        mouse(w, QEvent::MouseButtonRelease, center);
        mouse(w, QEvent::MouseButtonPress, center);
        mouse(w, QEvent::MouseButtonDblClick, center);
        mouse(w, QEvent::MouseButtonRelease, center);
        QCOMPARE(x->min(), 0.0);
        QCOMPARE(x->max(), 10.0);
        QCOMPARE(plot->yAxis()->max(), 1.0);
        offscreen.renderFrame();

        // Drag pans (content follows the pointer).
        const double before = plot->pixelToX(area.left() + 100);
        drag(w, QPointF(area.left() + 100, center.y() + 40), QPointF(area.left() + 150, center.y() + 40));
        QVERIFY(std::abs(plot->pixelToX(area.left() + 150) - before) < 1e-9);
        plot->resetView();
        offscreen.renderFrame();

        // Shift+drag: rubber-band zoom to the box.
        const double a = plot->pixelToX(area.left() + 50), b = plot->pixelToX(area.left() + 150);
        drag(w, QPointF(area.left() + 50, area.top() + 60), QPointF(area.left() + 150, area.top() + 120), Qt::LeftButton,
             Qt::ShiftModifier);
        QVERIFY(std::abs(x->min() - a) < 1e-9 && std::abs(x->max() - b) < 1e-9);
        plot->resetView();
        offscreen.renderFrame();

        // Drag the cursor from 7.5 to 5.0.
        QSignalSpy moved(cursor, &PlotCursor::moved);
        const QPointF c0(plot->xToPixel(7.5), center.y() + 30);
        drag(w, c0, QPointF(plot->xToPixel(5.0), center.y() + 30));
        QVERIFY(std::abs(cursor->position() - 5.0) < 0.05);
        QVERIFY(moved.count() > 0);
        QCOMPARE(x->min(), 0.0);  // no pan while dragging a cursor
        offscreen.renderFrame();

        // The cursor is selected: arrow keys nudge it by one pixel.
        const double px = plot->xToPixel(cursor->position());
        QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &right);
        QVERIFY(std::abs(plot->xToPixel(cursor->position()) - (px + 1)) < 1e-6);
        offscreen.renderFrame();

        // Drag the level handle (left margin) up to y = 0.8.
        const QPointF handle(area.left() - 12, plot->yToPixel(0.5));
        drag(w, handle, QPointF(area.left() - 12, plot->yToPixel(0.8)));
        QVERIFY2(std::abs(level->value() - 0.8) < 0.02, qPrintable(QString::number(level->value())));

        offscreen.setRootItem(nullptr);
    }

    // ---- autoscale honours minimumSpan -----------------------------------------------------------
    void autoscaleMinimumSpan()
    {
        mybench::OffscreenWindow offscreen(QSize(400, 300));
        if (!offscreen.isValid())
            QSKIP("no GPU / graphics device for offscreen rendering");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(
            "import QtQuick\n"
            "import MyBench.Plot\n"
            "Plot {\n"
            "    width: 400; height: 300\n"
            "    xAxis { min: 0; max: 100 }\n"
            "    yAxis { min: -1; max: 1; minimumSpan: 0.001 }\n"
            "    PlotTrace { buffer: TraceBuffer { objectName: \"buf\" } }\n"
            "}\n",
            QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *plot = qobject_cast<Plot *>(root.get());
        auto *buf = root->findChild<TraceBuffer *>("buf");
        QVERIFY(plot && buf);
        // A DMM reading of 4.99850 V with 2 µV of noise.
        std::vector<double> y(101);
        for (size_t i = 0; i < y.size(); ++i)
            y[i] = 4.9985 + ((i % 2) ? 1e-6 : -1e-6);
        buf->setUniform(0, 1, std::span<const double>(y));
        QCoreApplication::processEvents();
        offscreen.setRootItem(plot, false);
        offscreen.renderFrame();
        PlotAxis *ya = plot->yAxis();

        // One-shot autoscale: at least 1 mV, centred on the data (within the float precision of the
        // min/max pyramid, ~0.3 µV at 5 V).
        plot->autoscaleY();
        QVERIFY2(std::abs(ya->span() - 0.001) < 1e-12, qPrintable(QString::number(ya->span(), 'g', 12)));
        QVERIFY2(std::abs(0.5 * (ya->min() + ya->max()) - 4.9985) < 1e-6, qPrintable(QString::number(ya->min(), 'g', 12) + " " + QString::number(ya->max(), 'g', 12)));

        // Continuous autoScale (the DMM trend path) too.
        ya->setRange(-1, 1);
        ya->setAutoScale(true);
        offscreen.renderFrame();
        QVERIFY2(std::abs(ya->span() - 0.001) < 1e-12, qPrintable(QString::number(ya->span(), 'g', 12)));
        QVERIFY(std::abs(0.5 * (ya->min() + ya->max()) - 4.9985) < 1e-6);

        // Data wider than minimumSpan: normal padded fit (not clamped).
        for (size_t i = 0; i < y.size(); ++i)
            y[i] = double(i) * 0.01;  // 0 .. 1 V
        buf->setUniform(0, 1, std::span<const double>(y));
        QCoreApplication::processEvents();
        offscreen.renderFrame();
        QVERIFY(ya->min() < 0.0 && ya->max() > 1.0 && ya->span() < 1.5);

        // x autoscale honours the x axis' minimumSpan as well.
        plot->xAxis()->setMinimumSpan(1000);
        plot->autoscaleX();
        QVERIFY(std::abs(plot->xAxis()->span() - 1000) < 1e-9);
        QVERIFY(std::abs(0.5 * (plot->xAxis()->min() + plot->xAxis()->max()) - 50) < 1e-9);
        offscreen.setRootItem(nullptr);
    }

    // ---- rendering --------------------------------------------------------------------------------
    void offscreenRender()
    {
        mybench::OffscreenWindow offscreen(QSize(400, 300));
        if (!offscreen.isValid())
            QSKIP("no GPU / graphics device for offscreen rendering");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(
            "import QtQuick\n"
            "import MyBench.Plot\n"
            "Plot {\n"
            "width: 400; height: 300\n"
            "colorScheme: Plot.Dark\n"
            "xAxis { min: 0; max: 10 }\n"
            "yAxis { min: -1; max: 1 }\n"
            "PlotTrace {\n"
            "    buffer: TraceBuffer { objectName: \"buf\" }\n"
            "    color: \"#ff0000\"; width: 3\n"
            "}\n"
            "PlotCursor { position: 7.5; color: \"#00ff00\"; readoutVisible: false }\n"
            "}\n"
            , QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *plot = qobject_cast<Plot *>(root.get());
        QVERIFY(plot);
        auto *buf = root->findChild<TraceBuffer *>("buf");
        QVERIFY(buf);
        std::vector<double> flat(1001, 0.25);
        buf->setUniform(0, 0.01, std::span<const double>(flat));
        QCoreApplication::processEvents();
        offscreen.setRootItem(plot, false);
        offscreen.grab();
        const QImage image = offscreen.grab().convertToFormat(QImage::Format_RGB32);
        QVERIFY(!image.isNull());

        // Mapping round trip after layout.
        const QPointF p = plot->mapFromValue(QPointF(5, 0.25));
        QVERIFY(plot->plotArea().contains(p));
        const QPointF v = plot->mapToValue(p);
        QVERIFY(std::abs(v.x() - 5) < 1e-9 && std::abs(v.y() - 0.25) < 1e-9);

        const QRgb trace = image.pixel(int(p.x()), int(std::round(p.y())));
        QVERIFY2(qRed(trace) > 200 && qGreen(trace) < 80, qPrintable(QString::number(trace, 16)));
        const QPointF c = plot->mapFromValue(QPointF(7.5, -0.5));
        int green = 0;
        for (int dx = -1; dx <= 1; ++dx)
            green = std::max(green, qGreen(image.pixel(int(c.x()) + dx, int(c.y()))));
        QVERIFY2(green > 200, qPrintable(QString::number(green)));
        if (qEnvironmentVariableIsSet("PLOT_TEST_IMAGE"))
            image.save(qEnvironmentVariable("PLOT_TEST_IMAGE"));
        const QPointF bg = plot->mapFromValue(QPointF(2.3, -0.7));
        const QRgb area = image.pixel(int(bg.x()), int(bg.y()));
        QVERIFY(qRed(area) < 40 && qGreen(area) < 40);

        // CSV export of the visible range.
        const QString csv = plot->toCsv(true);
        QVERIFY(csv.startsWith(QStringLiteral("\"x\",\"trace\"")));
        QCOMPARE(csv.count(u'\n'), 1 + 1001);

        // Hover readout + legend (visual check: PLOT_TEST_HOVER_IMAGE=path).
        plot->setHoverReadout(true);
        plot->setLegendVisible(true);
        auto *plotTrace = root->findChild<PlotTrace *>();
        plotTrace->setName(QStringLiteral("CH0"));
        plotTrace->setUnit(QStringLiteral("V"));
        const QPointF hover = plot->mapFromValue(QPointF(3.3, 0.6));
        QMouseEvent move(QEvent::MouseMove, hover, hover, hover, Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                         QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(offscreen.window(), &move);
        QTRY_VERIFY(plot->containsMouse());
        QVERIFY(std::abs(plot->hoverValue().x() - 3.3) < 1e-6);
        offscreen.renderFrame();
        const QImage hovered = offscreen.grab();
        if (qEnvironmentVariableIsSet("PLOT_TEST_HOVER_IMAGE"))
            hovered.save(qEnvironmentVariable("PLOT_TEST_HOVER_IMAGE"));
        plot->setHoverReadout(false);

        // PNG export (asynchronous grab, completes with the next rendered frame).
        QSignalSpy saved(plot, &Plot::imageSaved);
        const QString png = QDir::temp().filePath(QStringLiteral("tst_plot_export.png"));
        QVERIFY(plot->saveImage(QUrl::fromLocalFile(png)));
        for (int i = 0; i < 20 && saved.isEmpty(); ++i) {
            offscreen.renderFrame();
            QCoreApplication::processEvents();
        }
        QCOMPARE(saved.count(), 1);
        QVERIFY(saved.first().at(1).toBool());
        QCOMPARE(QImage(png).size(), QSize(400, 300));
        QFile::remove(png);

        // Performance smoke test: 2 x 200k samples re-uploaded every frame.
        std::vector<double> big = sine(200000, 37.0, 0.8);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 20; ++i) {
            buf->setUniform(0, 10.0 / 200000, std::span<const double>(big));
            QCoreApplication::processEvents();
            offscreen.renderFrame();
        }
        qInfo("200k-sample trace: %.2f ms/frame (update + polish + sync + GPU)", double(timer.elapsed()) / 20.0);
        offscreen.setRootItem(nullptr);
    }

    // ---- plot tools: rubber band modifiers, context menu click ------------------------------------
    void rubberBandModifiersAndContextClick()
    {
        PlotFixture f;
        if (!f.create("xAxis { min: 0; max: 10 }\nyAxis { min: -1; max: 1 }\n"))
            QSKIP("no GPU / graphics device for offscreen rendering");
        Plot *plot = f.plot;
        PlotAxis *x = plot->xAxis();
        const QRectF area = plot->plotArea();
        QQuickWindow *w = f.window();

        // Ctrl+drag (⌘ on macOS) and Meta+drag (Ctrl key on macOS) zoom to the box; Shift stays.
        for (Qt::KeyboardModifier mod : {Qt::ControlModifier, Qt::MetaModifier, Qt::ShiftModifier}) {
            const double a = plot->pixelToX(area.left() + 40), b = plot->pixelToX(area.left() + 140);
            drag(w, QPointF(area.left() + 40, area.top() + 50), QPointF(area.left() + 140, area.top() + 110),
                 Qt::LeftButton, mod);
            QVERIFY2(std::abs(x->min() - a) < 1e-9 && std::abs(x->max() - b) < 1e-9, qPrintable(QString::number(mod, 16)));
            plot->resetView();
            f.render();
        }

        // Right click (no movement): context menu request at the pointer, no zoom.
        QSignalSpy menu(plot, &Plot::contextMenuRequested);
        const QPointF c = area.center();
        mouse(w, QEvent::MouseButtonPress, c, Qt::RightButton);
        mouse(w, QEvent::MouseButtonRelease, c, Qt::RightButton);
        QCOMPARE(menu.count(), 1);
        QCOMPARE(menu.first().at(0).toPointF(), c);
        QVERIFY(!x->zoomed());
        // Right drag: rubber band zoom, no menu.
        drag(w, QPointF(area.left() + 40, area.top() + 50), QPointF(area.left() + 140, area.top() + 110), Qt::RightButton);
        QCOMPARE(menu.count(), 1);
        QVERIFY(x->zoomed());
        plot->resetView();

        // zoomAxes: X only ignores the vertical extent of the box, Y only the horizontal one.
        plot->setZoomAxes(Plot::ZoomX);
        drag(w, QPointF(area.left() + 40, area.top() + 50), QPointF(area.left() + 140, area.top() + 110), Qt::LeftButton,
             Qt::ControlModifier);
        QVERIFY(x->zoomed() && !plot->yAxis()->zoomed());
        plot->resetView();
        plot->setZoomAxes(Plot::ZoomY);
        drag(w, QPointF(area.left() + 40, area.top() + 50), QPointF(area.left() + 140, area.top() + 110), Qt::LeftButton,
             Qt::ControlModifier);
        QVERIFY(!x->zoomed() && plot->yAxis()->zoomed());
        f.release();
    }

    // ---- Shift-zoom axis lock ---------------------------------------------------------------------
    void zoomDragAxisLock()
    {
        PlotFixture f;
        if (!f.create("xAxis { min: 0; max: 10 }\nyAxis { min: -1; max: 1 }\n"))
            QSKIP("no GPU / graphics device for offscreen rendering");
        Plot *plot = f.plot;
        PlotAxis *x = plot->xAxis();
        PlotAxis *y = plot->yAxis();
        const QRectF area = plot->plotArea();
        QQuickWindow *w = f.window();
        const QPointF p0(area.left() + 40, area.top() + 50);

        // Near-horizontal Shift-drag (dy small): x only.
        drag(w, p0, p0 + QPointF(120, 6), Qt::LeftButton, Qt::ShiftModifier);
        QVERIFY(x->zoomed());
        QVERIFY(!y->zoomed());
        plot->resetView();
        f.render();
        // Near-vertical: y only.
        drag(w, p0, p0 + QPointF(5, 100), Qt::LeftButton, Qt::ShiftModifier);
        QVERIFY(!x->zoomed());
        QVERIFY(y->zoomed());
        plot->resetView();
        f.render();
        // Diagonal: both.
        drag(w, p0, p0 + QPointF(100, 80), Qt::LeftButton, Qt::ShiftModifier);
        QVERIFY(x->zoomed() && y->zoomed());
        plot->resetView();
        f.release();
    }

    // ---- axis limits: max span, limit to data ------------------------------------------------------
    void axisMaximumSpanAndDataLimits()
    {
        PlotAxis axis;
        axis.setRange(0, 10);
        axis.setMaximumSpan(20);
        axis.setViewRange(-10, 30);          // 40 wide: cut to 20 around the centre
        QCOMPARE(axis.max() - axis.min(), 20.0);
        QCOMPARE(axis.min() + axis.max(), 20.0);
        axis.setMaximumSpan(qQNaN());

        // limitToData: the view may not leave the extent + 25 %.
        axis.setRange(0, 10);
        axis.setLimitToData(true);
        axis.setDataExtent(0, 100);
        axis.setViewRange(500, 510);
        QCOMPARE(axis.max(), 125.0);
        QCOMPARE(axis.min(), 115.0);
        axis.setViewRange(-900, 900);
        QCOMPARE(axis.min(), -25.0);
        QCOMPARE(axis.max(), 125.0);
        // An explicit limit wins for its side.
        axis.setLowerLimit(0);
        axis.setViewRange(-900, 900);
        QCOMPARE(axis.min(), 0.0);
        QCOMPARE(axis.max(), 125.0);

        PlotAxis lg;
        lg.setScale(PlotAxis::Log);
        lg.setRange(10, 1e4);
        lg.setMaximumSpan(2);                 // decades
        lg.setViewRange(1, 1e6);
        QVERIFY(std::abs(std::log10(lg.max() / lg.min()) - 2.0) < 1e-9);
    }

    // ---- plot tools: zoom history -----------------------------------------------------------------
    void zoomHistory()
    {
        PlotFixture f;
        if (!f.create("xAxis { min: 0; max: 10 }\nyAxis { min: -1; max: 1 }\n"
                      "PlotTrace { buffer: TraceBuffer { objectName: \"buf\" } }\n"))
            QSKIP("no GPU / graphics device for offscreen rendering");
        Plot *plot = f.plot;
        PlotAxis *x = plot->xAxis();
        PlotAxis *y = plot->yAxis();
        QQuickWindow *w = f.window();
        const QRectF area = plot->plotArea();
        const QPointF c = area.center();
        QSignalSpy historyChanged(plot, &Plot::zoomHistoryChanged);
        QVERIFY(!plot->canUndoZoom());
        plot->undoZoom();  // no-op
        QCOMPARE(x->min(), 0.0);

        // A burst of wheel steps is one history entry.
        wheel(w, c, QPoint(0, 120));
        wheel(w, c, QPoint(0, 120));
        QVERIFY(plot->canUndoZoom());
        QVERIFY(historyChanged.count() >= 1);
        plot->undoZoom();
        QCOMPARE(x->min(), 0.0);
        QCOMPARE(x->max(), 10.0);
        QVERIFY(!plot->canUndoZoom());

        // Rubber band, then pan, then undo twice.
        drag(w, QPointF(area.left() + 40, area.top() + 50), QPointF(area.left() + 140, area.top() + 110), Qt::LeftButton,
             Qt::ControlModifier);
        const double zx0 = x->min(), zx1 = x->max(), zy0 = y->min(), zy1 = y->max();
        f.render();
        drag(w, QPointF(area.left() + 100, c.y()), QPointF(area.left() + 160, c.y() + 20));
        QVERIFY(x->min() != zx0);
        plot->undoZoom();
        QCOMPARE(x->min(), zx0);
        QCOMPARE(x->max(), zx1);
        QCOMPARE(y->min(), zy0);
        QCOMPARE(y->max(), zy1);
        plot->undoZoom();
        QCOMPARE(x->min(), 0.0);
        QCOMPARE(y->max(), 1.0);
        QVERIFY(!plot->canUndoZoom());

        // Reset pushes the zoomed view (undo returns to it); autoscale pushes once.
        plot->zoomAt(c, 2.0, 1.0);
        const double zoomedMin = x->min();
        plot->resetView();
        QCOMPARE(x->min(), 0.0);
        plot->undoZoom();
        QCOMPARE(x->min(), zoomedMin);
        plot->resetView();
        f.setData(std::vector<double>{0.1, 0.5, 0.3}, 2.0, 1.0);
        plot->autoscale();
        QCOMPARE(x->min(), 2.0);
        QCOMPARE(x->max(), 4.0);
        plot->undoZoom();
        QCOMPARE(x->min(), 0.0);
        QCOMPARE(y->min(), -1.0);
        plot->clearZoomHistory();
        QVERIFY(!plot->canUndoZoom());
        f.release();
    }

    // ---- plot tools: autoscale locks ---------------------------------------------------------------
    void autoscaleLocks()
    {
        PlotFixture f;
        if (!f.create("xAxis { min: 0; max: 10 }\nyAxis { min: -1; max: 1 }\n"
                      "PlotTrace { buffer: TraceBuffer { objectName: \"buf\" } }\n"))
            QSKIP("no GPU / graphics device for offscreen rendering");
        Plot *plot = f.plot;
        PlotAxis *x = plot->xAxis();
        PlotAxis *y = plot->yAxis();
        QSignalSpy lockSpy(plot, &Plot::autoscaleLockChanged);
        f.setData(std::vector<double>{2, 3, 4, 5}, 0.0, 1.0);  // x 0..3, y 2..5

        plot->setYAutoscaleLock(true);
        QCOMPARE(lockSpy.count(), 1);
        f.render();
        QVERIFY2(y->min() < 2.0 && y->min() > 1.0 && y->max() > 5.0 && y->max() < 6.0,
                 qPrintable(QString("%1 %2").arg(y->min()).arg(y->max())));
        QCOMPARE(x->max(), 10.0);  // x not locked
        // New data: y re-fits (no hysteresis), even to a smaller range.
        f.setData(std::vector<double>{10, 10.5, 11}, 0.0, 1.0);
        f.render();
        QVERIFY2(y->min() > 9.0 && y->max() < 12.0, qPrintable(QString("%1 %2").arg(y->min()).arg(y->max())));
        f.setData(std::vector<double>{10, 10.1, 10.2}, 0.0, 1.0);
        f.render();
        QVERIFY2(y->max() < 10.4, qPrintable(QString::number(y->max())));
        // The one-shot autoscale buttons do not unlock.
        plot->autoscaleY();
        QVERIFY(plot->yAutoscaleLock());

        plot->setXAutoscaleLock(true);
        f.render();
        QCOMPARE(x->min(), 0.0);
        QCOMPARE(x->max(), 2.0);
        f.setData(std::vector<double>{1, 2, 3, 4, 5}, 1.0, 0.5);
        f.render();
        QCOMPARE(x->min(), 1.0);
        QCOMPARE(x->max(), 3.0);

        // A purely horizontal pan keeps the y lock (scrolling through time while y follows the data).
        plot->setXAutoscaleLock(false);
        {
            const QPointF c = plot->plotArea().center();
            drag(f.window(), c, c + QPointF(40, 0));
        }
        QVERIFY(plot->yAutoscaleLock());
        plot->setXAutoscaleLock(true);
        f.render();
        // A user zoom of one axis unlocks that axis only.
        wheel(f.window(), plot->plotArea().center(), QPoint(0, 120), Qt::ShiftModifier);
        QVERIFY(!plot->yAutoscaleLock());
        QVERIFY(plot->xAutoscaleLock());
        wheel(f.window(), plot->plotArea().center(), QPoint(0, 120));
        QVERIFY(!plot->xAutoscaleLock());
        // Reset unlocks both.
        plot->setXAutoscaleLock(true);
        plot->setYAutoscaleLock(true);
        plot->resetView();
        QVERIFY(!plot->xAutoscaleLock() && !plot->yAutoscaleLock());
        f.release();
    }

    // ---- plot tools: copy image --------------------------------------------------------------------
    void copyImageToClipboard()
    {
        PlotFixture f;
        if (!f.create("xAxis { min: 0; max: 10 }\nyAxis { min: -1; max: 1 }\n"))
            QSKIP("no GPU / graphics device for offscreen rendering");
        QGuiApplication::clipboard()->clear();
        QSignalSpy copied(f.plot, &Plot::imageCopied);
        QVERIFY(f.plot->copyImage());
        for (int i = 0; i < 20 && copied.isEmpty(); ++i) {
            f.render();
            QCoreApplication::processEvents();
        }
        QCOMPARE(copied.count(), 1);
        QVERIFY(copied.first().at(0).toBool());
        const QImage image = QGuiApplication::clipboard()->image();
        QCOMPARE(image.size(), QSize(400, 300));
        f.release();
    }

    // ---- trace styles ------------------------------------------------------------------------------
    void stepModes()
    {
        TraceBuffer sparse;
        std::vector<double> sy{0, 10};
        sparse.setUniform(0, 1, std::span<const double>(sy));
        ScaleMap xm;
        xm.min = 0;
        xm.max = 1;
        xm.pixelLength = 100;
        ScaleMap ym;
        ym.min = 0;
        ym.max = 10;
        ym.pixelStart = 100;
        ym.pixelLength = -100;
        std::vector<PixelPoint> out;
        auto pts = [&](StepMode mode) {
            buildPolyline(*sparse.snapshot(), xm, ym, {}, mode, 1.0, out);
            QStringList s;
            for (const PixelPoint &p : out)
                s << QString("%1,%2").arg(p.x).arg(p.y);
            return s.join(u' ');
        };
        QCOMPARE(pts(StepMode::None), QStringLiteral("0,100 100,0"));
        QCOMPARE(pts(StepMode::Hold), QStringLiteral("0,100 100,100 100,0"));
        QCOMPARE(pts(StepMode::Vertical), QStringLiteral("0,100 0,0 100,0"));
        QCOMPARE(pts(StepMode::CenterHorizontal), QStringLiteral("0,100 50,100 50,0 100,0"));
        QCOMPARE(pts(StepMode::CenterVertical), QStringLiteral("0,100 0,50 100,50 100,0"));
    }

    void traceStyleObject()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import MyBench.Plot\n"
                          "TraceStyle { lineWidth: 2; pointStyle: TraceStyle.Diamond }\n",
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *style = qobject_cast<TraceStyle *>(root.get());
        QVERIFY(style);
        // The declared values are the defaults.
        QVERIFY(style->isDefault());
        QSignalSpy changed(style, &TraceStyle::changed);
        style->setLineColor(QColor("#ff8800"));
        style->setLineStyle(TraceStyle::DashDotDot);
        style->setInterpolation(TraceStyle::CenterStep);
        style->setFillTo(TraceStyle::PlusInfinity);
        style->setLineWidth(0);
        QCOMPARE(changed.count(), 5);
        QVERIFY(!style->isDefault());

        const QVariantMap json = style->toJson();
        QCOMPARE(json.value("lineColor").toString(), QStringLiteral("#ff8800"));
        QCOMPARE(json.value("fillColor").toString(), QString());
        QCOMPARE(json.value("lineStyle").toString(), QStringLiteral("dashDotDot"));
        QCOMPARE(json.value("pointStyle").toString(), QStringLiteral("diamond"));
        QCOMPARE(json.value("interpolation").toString(), QStringLiteral("centerStep"));
        QCOMPARE(json.value("fillTo").toString(), QStringLiteral("plusInfinity"));
        QCOMPARE(json.value("lineWidth").toDouble(), 0.0);

        style->reset();
        QVERIFY(style->isDefault());
        QCOMPARE(style->lineWidth(), 2.0);
        QCOMPARE(style->pointStyle(), TraceStyle::Diamond);
        QVERIFY(!style->lineColor().isValid());

        style->fromJson(json);
        QCOMPARE(style->toJson(), json);
        // Unknown keys ignored, missing keys = defaults, bad values coerced.
        style->fromJson(QVariantMap{{"lineWidth", 99}, {"lineStyle", "wavy"}, {"interpolation", "step"},
                                    {"future", true}, {"lineColor", "not a color"}});
        QCOMPARE(style->lineWidth(), 10.0);
        QCOMPARE(style->lineStyle(), TraceStyle::Solid);
        QCOMPARE(style->interpolation(), TraceStyle::Step);
        QCOMPARE(style->pointStyle(), TraceStyle::Diamond);  // missing -> default
        QVERIFY(!style->lineColor().isValid());

        TraceStyle copy;
        copy.copyFrom(style);
        QCOMPARE(copy.toJson(), style->toJson());
    }

    // QML subclasses of PlotTrace (instruments/scope/ScopeTrace.qml) declare their own `traceStyle`
    // and `interpolation` properties: the new PlotTrace properties must not clash with them.
    void traceSubclassPropertyNames()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport MyBench.Plot\n"
                          "PlotTrace { property var traceStyle: null; readonly property int interpolation: 2;"
                          " readonly property int fillTo: 0; property color defaultColor; step: interpolation === 2 }\n",
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QCOMPARE(qobject_cast<PlotTrace *>(root.get())->interpolationMode(), PlotTrace::Step);
    }

    // Colors come from the host (MyBench's Theme through BenchPlot): seriesColors drives the
    // default trace colors, panelColor the in-plot panels; the built-in palette is only a fallback.
    void seriesColorsAndPanel()
    {
        Plot plot;
        const QColor fallback0 = plot.paletteColor(0);
        QVERIFY(fallback0.isValid());
        QVERIFY(plot.seriesColors().isEmpty());
        QSignalSpy style(&plot, &Plot::styleChanged);
        plot.setSeriesColors({QColor("#112233"), QColor("#445566")});
        QCOMPARE(style.count(), 1);
        QCOMPARE(plot.paletteColor(0), QColor("#112233"));
        QCOMPARE(plot.paletteColor(1), QColor("#445566"));
        QCOMPARE(plot.paletteColor(2), QColor("#112233"));
        QCOMPARE(plot.paletteColor(-1), QColor("#445566"));
        plot.setSeriesColors({QColor("#112233"), QColor("#445566")});
        QCOMPARE(style.count(), 1);   // unchanged: no signal
        auto *first = new PlotTrace(&plot);
        auto *second = new PlotTrace(&plot);
        plot.addElement(first);
        plot.addElement(second);
        QCOMPARE(plot.traceColor(first), QColor("#112233"));
        QCOMPARE(plot.traceColor(second), QColor("#445566"));
        plot.resetSeriesColors();
        QCOMPARE(plot.paletteColor(0), fallback0);

        const QColor defaultPanel = plot.panelColor();
        plot.setPanelColor(QColor("#202020"));
        QCOMPARE(plot.panelColor(), QColor("#202020"));
        plot.resetPanelColor();
        QCOMPARE(plot.panelColor(), defaultPanel);
    }

    void traceStyleOnTrace()
    {
        PlotFixture f;
        if (!f.create("colorScheme: Plot.Dark\n"
                      "xAxis { min: 0; max: 10 }\nyAxis { min: -1; max: 1 }\n"
                      "PlotTrace { objectName: \"t\"; color: \"#00ff00\"; buffer: TraceBuffer { objectName: \"buf\" }\n"
                      "            userStyle: TraceStyle { objectName: \"s\" } }\n"))
            QSKIP("no GPU / graphics device for offscreen rendering");
        auto *trace = f.root->findChild<PlotTrace *>("t");
        auto *style = f.root->findChild<TraceStyle *>("s");
        QVERIFY(trace && style);
        // An untouched style keeps the trace's own look (color, width, linear, no points).
        PlotTrace::Appearance a = trace->appearance();
        QCOMPARE(a.color, QColor("#00ff00"));
        QVERIFY(a.lineVisible && !a.pointsVisible && !a.fill);
        QCOMPARE(a.interpolation, PlotTrace::Linear);

        QSignalSpy traceChanged(trace, &PlotElement::changed);
        style->setLineColor(QColor("#ff0000"));
        QVERIFY(traceChanged.count() > 0);
        style->setLineStyle(TraceStyle::DashDot);
        style->setPointStyle(TraceStyle::FilledSquare);
        style->setInterpolation(TraceStyle::StepVertical);
        style->setFillTo(TraceStyle::PlusInfinity);
        a = trace->appearance();
        QCOMPARE(a.color, QColor("#ff0000"));
        QCOMPARE(a.fillColor, QColor("#ff0000"));
        QCOMPARE(a.style, PlotElement::DashDot);
        QVERIFY(a.pointsVisible);
        QCOMPARE(a.pointShape, PlotTrace::FilledSquare);
        QCOMPARE(a.interpolation, PlotTrace::StepVertical);
        QVERIFY(a.fill);
        QVERIFY(std::isinf(a.fillBaseline) && a.fillBaseline > 0);
        QCOMPARE(a.fillOpacity, 0.5);
        QVERIFY(!a.fillGradient);
        QCOMPARE(f.plot->traceColor(trace), QColor("#ff0000"));
        style->setFillTo(TraceStyle::Zero);
        QCOMPARE(trace->appearance().fillBaseline, trace->toDisplayY(0.0));
        style->setLineWidth(0);
        QVERIFY(!trace->appearance().lineVisible);
        style->setLineWidth(1.5);
        style->setInterpolation(TraceStyle::PointsOnly);
        style->setPointStyle(TraceStyle::NoPoints);
        a = trace->appearance();
        QVERIFY(!a.lineVisible && a.pointsVisible && a.pointsOnly);

        // Render: fill to +∞ paints the area above a flat trace at y = -0.5 in the fill color.
        style->reset();
        style->setLineColor(QColor("#ff0000"));
        style->setFillColor(QColor("#0000ff"));
        style->setFillTo(TraceStyle::PlusInfinity);
        f.setData(std::vector<double>(11, -0.5), 0.0, 1.0);
        const QImage image = f.grab();
        const QPointF above = f.plot->mapFromValue(QPointF(4.7, 0.45));
        const QPointF below = f.plot->mapFromValue(QPointF(4.7, -0.85));
        QVERIFY2(qBlue(image.pixel(above.toPoint())) > 80, qPrintable(QString::number(image.pixel(above.toPoint()), 16)));
        QVERIFY2(qBlue(image.pixel(below.toPoint())) < 40, qPrintable(QString::number(image.pixel(below.toPoint()), 16)));
        // Every point style / line style / interpolation renders.
        for (int p = 0; p <= int(TraceStyle::X); ++p) {
            style->setPointStyle(TraceStyle::PointStyle(p));
            style->setLineStyle(TraceStyle::LineStyle(p % 5));
            style->setInterpolation(TraceStyle::Interpolation(p % 6));
            QVERIFY(!f.grab().isNull());
        }
        trace->setUserStyle(nullptr);
        QCOMPARE(trace->appearance().color, QColor("#00ff00"));
        f.release();
    }

    // par1 bode #7: decade-only labels on log axes (minors 2..9 unlabeled).
    void logDecadeLabels()
    {
        PlotAxis axis;
        axis.setScale(PlotAxis::Log);
        axis.setRange(10, 10000);
        TickSet ticks = axis.ticks(1200);
        QVERIFY(std::find(ticks.major.begin(), ticks.major.end(), 20.0) != ticks.major.end());   // default: 1-2-5
        axis.setLogLabels(PlotAxis::Decades);
        ticks = axis.ticks(1200);
        QCOMPARE(ticks.major, (std::vector<double>{10, 100, 1000, 10000}));
        QVERIFY(std::find(ticks.minor.begin(), ticks.minor.end(), 20.0) != ticks.minor.end());
        QVERIFY(std::find(ticks.minor.begin(), ticks.minor.end(), 5000.0) != ticks.minor.end());
        QVERIFY(std::is_sorted(ticks.minor.begin(), ticks.minor.end()));
    }

    // par1 scope #4: a cursor without its line (chip and drag stay) and a line width.
    void cursorLineOptions()
    {
        PlotFixture f;
        if (!f.create("colorScheme: Plot.Dark\n"
                      "xAxis { min: 0; max: 10 }\nyAxis { min: -1; max: 1 }\n"
                      "PlotCursor { objectName: \"c\"; position: 5; color: \"#ff0000\"; readoutVisible: false }\n"))
            QSKIP("no GPU / graphics device for offscreen rendering");
        auto *cursor = f.root->findChild<PlotCursor *>("c");
        QVERIFY(cursor);
        QVERIFY(cursor->lineVisible());
        QCOMPARE(cursor->lineWidth(), 1.0);
        const QPointF mid = f.plot->mapFromValue(QPointF(5, 0));
        // The line's exact pixel depends on the backend's rounding: look at a small neighbourhood.
        const auto maxRed = [&](const QImage &img, int halfWidth) {
            int best = 0;
            for (int dy = -8; dy <= 8; dy += 4)
                for (int dx = -halfWidth; dx <= halfWidth; ++dx)
                    best = qMax(best, qRed(img.pixel(QPoint(qRound(mid.x()) + dx, qRound(mid.y()) + dy))));
            return best;
        };
        QImage image = f.grab();
        QVERIFY2(maxRed(image, 2) > 150, qPrintable(QString::number(maxRed(image, 2))));
        cursor->setLineWidth(5);
        image = f.grab();
        QVERIFY(maxRed(image, 3) > 150);
        // Width 5: the pixels 2 px to each side are covered too (both, whatever the rounding).
        QVERIFY(qRed(image.pixel(QPoint(qRound(mid.x()) - 1, qRound(mid.y())))) > 150
                || qRed(image.pixel(QPoint(qRound(mid.x()) + 1, qRound(mid.y())))) > 150);
        cursor->setLineVisible(false);
        image = f.grab();
        QVERIFY2(maxRed(image, 3) < 100, qPrintable(QString::number(maxRed(image, 3))));
        f.release();
    }
};

QTEST_MAIN(TestPlot)
#include "tst_plot.moc"
