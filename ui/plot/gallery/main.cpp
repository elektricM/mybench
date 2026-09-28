// mybench-plot-gallery: demo pages of MyBench.Plot (scope, deep memory, Bode, spectrum, waterfall,
// DMM trend, XY, recorder, logic).
//
//   mybench-plot-gallery [--page scope|deep|bode|spectrum|waterfall|trend|xy|recorder|logic]
//   mybench-plot-gallery --page bode --screenshot bode.png --size 1280x800 [--theme light]
//   mybench-plot-gallery --bench [--frames 600] [--samples 200000]   (offscreen frame timing)
#include "gallerydata.h"

#include <mybench/offscreen.h>
#include <mybench/plot/plot.h>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

QString argValue(const QStringList &args, const QString &name, const QString &fallback = {})
{
    const qsizetype i = args.indexOf(name);
    return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
}

// Renders the deep-memory page offscreen as fast as possible, pushing 2 new records per frame.
int runBench(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    const QStringList args = app.arguments();
    const int frames = argValue(args, QStringLiteral("--frames"), QStringLiteral("600")).toInt();
    const int samples = argValue(args, QStringLiteral("--samples"), QStringLiteral("200000")).toInt();
    const QString page = argValue(args, QStringLiteral("--page"), QStringLiteral("deep"));

    mybench::OffscreenWindow offscreen(QSize(1280, 800), 2.0);  // retina-size target
    if (!offscreen.isValid()) {
        std::fprintf(stderr, "bench: %s\n", qPrintable(offscreen.errorString()));
        return 1;
    }
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule("MyBench.Plot.Gallery", "Main");
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"page", page}, {"benchmark", true}}));
    if (!root) {
        std::fprintf(stderr, "%s\n", qPrintable(component.errorString()));
        return 1;
    }
    auto *item = qobject_cast<QQuickItem *>(root.get());
    offscreen.setRootItem(item);
    auto *data = root->findChild<GalleryData *>();
    data->setDeepSamples(samples);
    // Recorder: wait for the 100 s pre-recording (2e7 samples), then pan/zoom through it.
    auto *recorder = root->findChild<mybench::plot::Plot *>(QStringLiteral("recorderMain"));
    if (recorder) {
        while (data->recordedSamples() < 20000000)
            app.processEvents(QEventLoop::AllEvents, 50);
        recorder->xAxis()->setFollow(false);
    }
    data->setRunning(false);
    const double recStart = data->recording()->xMinimum();
    auto step = [&](int i) {
        if (!recorder) {
            data->pushDeepFrame();
            return;
        }
        // Alternate: a 20 s window panning through, and the whole recording (2e7 samples visible).
        if ((i / 60) % 2 == 0) {
            const double t = recStart + std::fmod(i * 0.25, 80.0);
            recorder->xAxis()->setViewRange(t, t + 20.0);
        } else {
            recorder->xAxis()->setViewRange(recStart - (i % 2) * 1e-3, recStart + 100.0);
        }
    };
    for (int i = 0; i < 30; ++i) {  // warm up
        step(i);
        app.processEvents();
        offscreen.renderFrame();
    }
    std::vector<double> times;
    std::vector<double> polish, sync;
    QList<mybench::plot::Plot *> plots = root->findChildren<mybench::plot::Plot *>();
    QElapsedTimer total;
    total.start();
    for (int i = 0; i < frames; ++i) {
        QElapsedTimer t;
        t.start();
        step(i);
        app.processEvents();
        offscreen.renderFrame();
        times.push_back(double(t.nsecsElapsed()) / 1e6);
        double p = 0, s = 0;
        for (auto *plot : plots) {
            p += plot->frameStats().polishMs;
            s += plot->frameStats().syncMs;
        }
        polish.push_back(p);
        sync.push_back(s);
    }
    const double elapsed = double(total.nsecsElapsed()) / 1e6;
    auto stats = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        double sum = 0;
        for (double x : v)
            sum += x;
        return std::make_tuple(sum / double(v.size()), v[v.size() / 2], v[size_t(double(v.size()) * 0.95)]);
    };
    const auto [mean, median, p95] = stats(times);
    const auto [pMean, pMed, pP95] = stats(polish);
    const auto [sMean, sMed, sP95] = stats(sync);
    if (recorder)
        std::printf("bench: page recorder, %d frames, %lld-sample recording, view 20 s (4 M samples) panning / 100 s (all), 1280x800 @2x, %s\n",
                    frames, qlonglong(data->recordedSamples()), qPrintable(offscreen.graphicsApiName()));
    else
        std::printf("bench: page %s, %d frames, 2 x %d samples replaced every frame, 1280x800 @2x, %s\n",
                    qPrintable(page), frames, samples, qPrintable(offscreen.graphicsApiName()));
    std::printf("  frame (data copy + polish + sync + render + GPU wait): mean %.2f ms, median %.2f, p95 %.2f -> %.0f fps\n",
                mean, median, p95, 1000.0 * frames / elapsed);
    std::printf("  plot polish (decimation + geometry, GUI thread):       mean %.2f ms, median %.2f, p95 %.2f\n", pMean, pMed, pP95);
    std::printf("  plot sync (upload to scene graph):                     mean %.2f ms, median %.2f, p95 %.2f\n", sMean, sMed, sP95);
    offscreen.setRootItem(nullptr);
    return 0;
}

}  // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) == "--bench")
            return runBench(argc, argv);
    }
    return mybench::runQmlApp(argc, argv, {
        .module = QStringLiteral("MyBench.Plot.Gallery"),
        .type = QStringLiteral("Main"),
        .applicationName = QStringLiteral("mybench-plot-gallery"),
        .organizationName = QStringLiteral("MYBENCH"),
        .setup = [](QQmlEngine &, const mybench::LaunchOptions &options, QVariantMap &properties) {
            const QString page = argValue(options.arguments, QStringLiteral("--page"));
            if (!page.isEmpty())
                properties.insert(QStringLiteral("page"), page);
            if (!options.theme.isEmpty())
                properties.insert(QStringLiteral("dark"), options.theme == u"dark");
        },
    });
}
