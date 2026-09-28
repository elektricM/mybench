// MYBENCH offscreen rendering helper.
//
// One call from main() loads a QML root (a module type or a URL) and either runs it normally in a
// window, or — when the command line contains --screenshot <file.png> — renders the scene
// OFFSCREEN (QQuickRenderControl + QRhi texture render target + readback; no window is ever
// shown) and writes a PNG, then exits. The GPU scene graph is used exactly as on screen (Metal on
// macOS, D3D11 on Windows, OpenGL/Vulkan on Linux), so custom QSGGeometryNodes, Shapes,
// MultiEffect, text etc. render like they do in the real app.
//
//     #include <mybench/offscreen.h>
//     int main(int argc, char *argv[])
//     {
//         return mybench::runQmlApp(argc, argv, {
//             .module = QStringLiteral("MyBench.Instruments.Scope"),
//             .type = QStringLiteral("ScopeDev"),
//             .applicationName = QStringLiteral("mybench-scope"),
//         });
//     }
//
// Options understood by runQmlApp() (everything else is passed through in
// LaunchOptions::arguments):
//   --screenshot <file.png>   render offscreen, write the PNG, exit (0 = ok, 1 = QML/render error)
//   --delay <ms>              time to run (animations, timers, data) before capturing (default 500)
//   --size <W>x<H>            logical window size (default: root's size, else QmlApp::defaultSize)
//   --theme dark|light        sets QStyleHints::colorScheme (+ LaunchOptions::theme for setup())
//   --dpr <ratio>             device pixel ratio of the screenshot (default 1; 2 = retina)
//   --samples <n>             MSAA sample count of the offscreen target (default 1, like a window)
//   --help-launch             print these options
//
// With --screenshot and no QT_QPA_PLATFORM / -platform given, the "offscreen" platform plugin is
// selected automatically, so nothing appears on screen (not even a Dock icon on macOS).
// The graphics API can be forced with QSG_RHI_BACKEND=metal|vulkan|opengl|d3d11|d3d12.
#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QQuickItem;
class QQuickWindow;
QT_END_NAMESPACE

namespace mybench {

// Parsed launch options (see the option list above).
struct LaunchOptions {
    QString screenshotPath;      // empty: normal interactive run
    int delayMs = 500;
    QSize size;                  // invalid: not given on the command line
    QString theme;               // "dark", "light" or empty (not given)
    qreal devicePixelRatio = 1.0;
    int samples = 1;
    bool help = false;
    QStringList arguments;       // arguments not consumed here (program name excluded)

    bool screenshot() const { return !screenshotPath.isEmpty(); }
};

// Parses `args` (program name excluded). Unknown arguments go to LaunchOptions::arguments.
// Returns false and sets *error on malformed options (e.g. "--size abc").
bool parseLaunchOptions(const QStringList &args, LaunchOptions *options, QString *error = nullptr);

// Usage text for the options above.
QString launchOptionsHelp();

// The options of the current process, as parsed by runQmlApp() (defaults before that). E.g. a
// Theme singleton can start in launchOptions().theme when it is not empty: the "offscreen"
// platform ignores QStyleHints::setColorScheme(), so styleHints alone is not enough there.
const LaunchOptions &launchOptions();

// What to load and how to set up the application.
struct QmlApp {
    // Either a QML module type (preferred; e.g. module "MyBench.Plot.Gallery", type "Main") ...
    QString module;
    QString type;
    // ... or a URL (qrc:/..., file:...).
    QUrl url;

    QString applicationName;          // QCoreApplication::applicationName / window title
    QString organizationName;         // for QSettings / QtCore.Settings
    QString organizationDomain;
    QString style = QStringLiteral("Basic");  // Qt Quick Controls style; empty = keep default
    QSize defaultSize{1280, 800};     // used when neither --size nor the root gives a size

    // Called after the engine exists and before the root is created. Register singletons, add
    // import paths, turn LaunchOptions::arguments (e.g. an instrument id) into initial (required)
    // properties of the root object. May be empty.
    std::function<void(QQmlEngine &engine, const LaunchOptions &options, QVariantMap &initialProperties)> setup;
};

// Creates the QGuiApplication, parses the options, loads the root and runs it (interactive) or
// renders it offscreen and writes the screenshot (--screenshot). Returns the process exit code.
// The root may be an Item (it is put in a window sized to --size / its own size) or a Window /
// ApplicationWindow (shown as is; in screenshot mode it is never shown and its content is
// rendered offscreen instead).
int runQmlApp(int argc, char *argv[], const QmlApp &app);

// Lower-level building block (also handy in tests): a QQuickWindow driven by
// QQuickRenderControl that renders into an RHI texture and can be read back to a QImage.
// Requires a QGuiApplication. Never shows anything on screen.
// QQuickItem::grabToImage() does not work in such (never visible) windows; code that needs a
// frame can find window->findChild<QObject *>("mybench_offscreen_grabber") and invoke its
// `QImage grab()` (Plot::saveImage does).
class OffscreenWindow
{
public:
    // `size` is in logical pixels; the texture is size * devicePixelRatio.
    explicit OffscreenWindow(QSize size = {800, 600}, qreal devicePixelRatio = 1.0, int samples = 1);
    ~OffscreenWindow();
    OffscreenWindow(const OffscreenWindow &) = delete;
    OffscreenWindow &operator=(const OffscreenWindow &) = delete;

    bool isValid() const;           // false if the graphics device could not be initialized
    QString errorString() const;
    QString graphicsApiName() const;  // "Metal", "Vulkan", "OpenGL", "D3D11", ...

    QQuickWindow *window() const;
    QSize size() const;
    void resize(QSize size);         // recreates the render target; also resizes the root item

    // Puts `item` into the window (it is not deleted by OffscreenWindow). With fillWindow, the
    // item is resized with the window (like QQuickView::SizeRootObjectToView).
    void setRootItem(QQuickItem *item, bool fillWindow = true);
    // Renders the contents of a QML Window/ApplicationWindow (which must not be visible): its
    // content item is moved into this window until the OffscreenWindow is destroyed or
    // releaseContent() is called. The QML window is resized to size().
    void adoptWindowContent(QQuickWindow *qmlWindow);
    void releaseContent();

    // Polishes, syncs and renders one frame (keeps animations/persistence effects progressing).
    void renderFrame();
    // Renders one frame and returns it (Format_ARGB32_Premultiplied, devicePixelRatio set).
    // Returns a null image on failure.
    QImage grab();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace mybench
