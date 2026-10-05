// MYBENCH offscreen rendering helper — see include/mybench/offscreen.h.
#include "mybench/offscreen.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QResizeEvent>
#include <QFont>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QLibraryInfo>
#include <QRegularExpression>
#include <QStyleHints>
#include <QTimer>

#include <rhi/qrhi.h>

#include <cstdio>

namespace mybench {

// ---------------------------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------------------------

QString launchOptionsHelp()
{
    return QStringLiteral(
        "Launch options:\n"
        "  --screenshot <file.png>  render offscreen (no window), write a PNG and exit\n"
        "  --delay <ms>             run this long before capturing (default 500)\n"
        "  --size <W>x<H>           logical window size, e.g. 1280x800\n"
        "  --theme dark|light       color scheme (QStyleHints::colorScheme)\n"
        "  --dpr <ratio>            screenshot device pixel ratio (default 1, 2 = retina)\n"
        "  --samples <n>            MSAA samples of the offscreen target (default 1)\n"
        "  --style <name>           controls style: native (default: macOS, FluentWinUI3 or Fusion;\n"
        "                           Fusion offscreen), org.kde.desktop, mybench, Fusion, Basic, ...\n"
        "  --grab <file.png>        real window on the real platform, grabbed after --delay, then exit\n"
        "  --help-launch            show this help\n"
        "Environment: QSG_RHI_BACKEND=metal|vulkan|opengl|d3d11|d3d12 forces the graphics API.\n");
}

bool parseLaunchOptions(const QStringList &args, LaunchOptions *options, QString *error)
{
    LaunchOptions result;
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    for (qsizetype i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        // Accept "--opt value" and "--opt=value".
        auto takeValue = [&](const QString &name, QString *value) -> int {
            if (arg == name) {
                if (i + 1 >= args.size())
                    return -1;
                *value = args.at(++i);
                return 1;
            }
            if (arg.startsWith(name + u'=')) {
                *value = arg.mid(name.size() + 1);
                return 1;
            }
            return 0;
        };

        QString value;
        int r = 0;
        if ((r = takeValue(QStringLiteral("--screenshot"), &value)) != 0) {
            if (r < 0 || value.isEmpty())
                return fail(QStringLiteral("--screenshot needs a file name"));
            result.screenshotPath = value;
        } else if ((r = takeValue(QStringLiteral("--delay"), &value)) != 0) {
            bool ok = false;
            const int ms = value.toInt(&ok);
            if (r < 0 || !ok || ms < 0)
                return fail(QStringLiteral("--delay needs a non-negative number of milliseconds"));
            result.delayMs = ms;
        } else if ((r = takeValue(QStringLiteral("--size"), &value)) != 0) {
            const QStringList parts = value.split(QRegularExpression(QStringLiteral("[xX*,]")));
            bool okW = false, okH = false;
            const int w = parts.size() == 2 ? parts[0].toInt(&okW) : 0;
            const int h = parts.size() == 2 ? parts[1].toInt(&okH) : 0;
            if (r < 0 || !okW || !okH || w <= 0 || h <= 0 || w > 16384 || h > 16384)
                return fail(QStringLiteral("--size needs <width>x<height>, e.g. 1280x800"));
            result.size = QSize(w, h);
        } else if ((r = takeValue(QStringLiteral("--theme"), &value)) != 0) {
            value = value.toLower();
            if (r < 0 || (value != u"dark" && value != u"light"))
                return fail(QStringLiteral("--theme must be dark or light"));
            result.theme = value;
        } else if ((r = takeValue(QStringLiteral("--dpr"), &value)) != 0) {
            bool ok = false;
            const double dpr = value.toDouble(&ok);
            if (r < 0 || !ok || dpr < 0.25 || dpr > 8)
                return fail(QStringLiteral("--dpr needs a ratio between 0.25 and 8"));
            result.devicePixelRatio = dpr;
        } else if ((r = takeValue(QStringLiteral("--samples"), &value)) != 0) {
            bool ok = false;
            const int samples = value.toInt(&ok);
            if (r < 0 || !ok || samples < 1 || samples > 16)
                return fail(QStringLiteral("--samples needs 1..16"));
            result.samples = samples;
        } else if ((r = takeValue(QStringLiteral("--style"), &value)) != 0) {
            if (r < 0 || value.isEmpty())
                return fail(QStringLiteral("--style needs a style name (native, mybench, Fusion, ...)"));
            result.style = value;
        } else if ((r = takeValue(QStringLiteral("--grab"), &value)) != 0) {
            if (r < 0 || value.isEmpty())
                return fail(QStringLiteral("--grab needs a file name"));
            result.grabPath = value;
        } else if (arg == u"--help-launch") {
            result.help = true;
        } else {
            result.arguments.append(arg);
        }
    }
    *options = result;
    return true;
}

// ---------------------------------------------------------------------------------------------
// OffscreenWindow
// ---------------------------------------------------------------------------------------------

namespace {

// The "offscreen" (and "minimal") platform plugins do not advertise RHI-based rendering, so Qt
// Quick would pick the software adaptation, which cannot render custom materials. Force the RHI
// adaptation with QSG_RHI_BACKEND or the platform's default graphics API. Must run before the
// first QQuickWindow is created.
void ensureRhiAdaptation()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    if (!qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        return;  // explicitly chosen by the user
    const QString platform = QGuiApplication::platformName();
    if (platform != u"offscreen" && platform != u"minimal")
        return;
    const QByteArray backend = qgetenv("QSG_RHI_BACKEND").toLower();
    QSGRendererInterface::GraphicsApi api;
    if (backend == "vulkan")
        api = QSGRendererInterface::Vulkan;
    else if (backend == "opengl" || backend == "gl")
        api = QSGRendererInterface::OpenGL;
    else if (backend == "metal")
        api = QSGRendererInterface::Metal;
    else if (backend == "d3d11")
        api = QSGRendererInterface::Direct3D11;
    else if (backend == "d3d12")
        api = QSGRendererInterface::Direct3D12;
    else {
#if defined(Q_OS_DARWIN)
        api = QSGRendererInterface::Metal;
#elif defined(Q_OS_WIN)
        api = QSGRendererInterface::Direct3D11;
#else
        api = QSGRendererInterface::OpenGL;
#endif
    }
    QQuickWindow::setSceneGraphBackend(QStringLiteral("rhi"));  // "rhi" = the default adaptation
    QQuickWindow::setGraphicsApi(api);
}

}  // namespace

// Lets code that only knows the QQuickWindow (e.g. Plot::saveImage) grab an offscreen frame:
// window->findChild<QObject *>("mybench_offscreen_grabber") and invoke grab() -> QImage.
class OffscreenGrabber : public QObject
{
    Q_OBJECT
public:
    OffscreenGrabber(OffscreenWindow *owner, QObject *parent) : QObject(parent), m_owner(owner)
    {
        setObjectName(QStringLiteral("mybench_offscreen_grabber"));
    }
    Q_INVOKABLE QImage grab() { return m_owner->grab(); }

private:
    OffscreenWindow *m_owner;
};

struct OffscreenWindow::Impl {
    QSize size;
    qreal dpr = 1.0;
    int samples = 1;
    QString error;

    std::unique_ptr<QQuickRenderControl> renderControl;
    std::unique_ptr<QQuickWindow> window;

    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiRenderBuffer> msaaColor;
    std::unique_ptr<QRhiRenderBuffer> depthStencil;
    std::unique_ptr<QRhiTextureRenderTarget> renderTarget;
    std::unique_ptr<QRhiRenderPassDescriptor> renderPass;

    QPointer<QQuickItem> rootItem;
    bool fillWindow = true;

    QPointer<QQuickWindow> adoptedWindow;
    QPointer<QQuickItem> adoptedContent;
    QPointer<QQuickItem> adoptedOldParent;

    bool initialized = false;

    QSize pixelSize() const
    {
        return QSize(qMax(1, qRound(size.width() * dpr)), qMax(1, qRound(size.height() * dpr)));
    }

    void releaseTarget()
    {
        if (window)
            window->setRenderTarget(QQuickRenderTarget());
        renderTarget.reset();
        renderPass.reset();
        depthStencil.reset();
        msaaColor.reset();
        texture.reset();
    }

    bool createTarget()
    {
        QRhi *rhi = renderControl->rhi();
        if (!rhi) {
            error = QStringLiteral("no QRhi");
            return false;
        }
        releaseTarget();
        const QSize px = pixelSize();
        int sampleCount = samples;
        if (sampleCount > 1) {
            const QList<int> supported = rhi->supportedSampleCounts();
            if (!supported.contains(sampleCount))
                sampleCount = 1;
        }
        texture.reset(rhi->newTexture(QRhiTexture::RGBA8, px, 1,
                                      QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!texture->create()) {
            error = QStringLiteral("cannot create the offscreen color texture");
            return false;
        }
        depthStencil.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, px, sampleCount));
        if (!depthStencil->create()) {
            error = QStringLiteral("cannot create the depth-stencil buffer");
            return false;
        }
        QRhiTextureRenderTargetDescription desc;
        if (sampleCount > 1) {
            msaaColor.reset(rhi->newRenderBuffer(QRhiRenderBuffer::Color, px, sampleCount, {},
                                                 QRhiTexture::RGBA8));
            if (!msaaColor->create()) {
                error = QStringLiteral("cannot create the MSAA color buffer");
                return false;
            }
            QRhiColorAttachment color(msaaColor.get());
            color.setResolveTexture(texture.get());
            desc.setColorAttachments({color});
        } else {
            desc.setColorAttachments({QRhiColorAttachment(texture.get())});
        }
        desc.setDepthStencilBuffer(depthStencil.get());
        renderTarget.reset(rhi->newTextureRenderTarget(desc));
        renderPass.reset(renderTarget->newCompatibleRenderPassDescriptor());
        renderTarget->setRenderPassDescriptor(renderPass.get());
        if (!renderTarget->create()) {
            error = QStringLiteral("cannot create the offscreen render target");
            return false;
        }
        QQuickRenderTarget quickTarget = QQuickRenderTarget::fromRhiRenderTarget(renderTarget.get());
        quickTarget.setDevicePixelRatio(dpr);
        window->setRenderTarget(quickTarget);
        return true;
    }

    void layoutContent()
    {
        window->setGeometry(0, 0, size.width(), size.height());
        window->contentItem()->setSize(size);
        if (rootItem && fillWindow)
            rootItem->setSize(size);
        if (adoptedWindow) {
            // The QML window has no platform window, so it gets no resize event by itself;
            // send one so e.g. ApplicationWindow lays out header/footer/contentItem.
            const QSize old = adoptedWindow->size();
            adoptedWindow->setGeometry(0, 0, size.width(), size.height());
            QResizeEvent event(size, old);
            QCoreApplication::sendEvent(adoptedWindow, &event);
            if (adoptedContent)
                adoptedContent->setSize(size);
        }
    }
};

OffscreenWindow::OffscreenWindow(QSize size, qreal devicePixelRatio, int samples)
    : d(std::make_unique<Impl>())
{
    d->size = size.isValid() && !size.isEmpty() ? size : QSize(800, 600);
    d->dpr = devicePixelRatio > 0 ? devicePixelRatio : 1.0;
    d->samples = qMax(1, samples);
    ensureRhiAdaptation();
    d->renderControl = std::make_unique<QQuickRenderControl>();
    d->window = std::make_unique<QQuickWindow>(d->renderControl.get());
    d->window->setGeometry(0, 0, d->size.width(), d->size.height());
    new OffscreenGrabber(this, d->window.get());

    if (!d->renderControl->initialize()) {
        d->error = QStringLiteral("QQuickRenderControl::initialize() failed (graphics API %1)")
                       .arg(graphicsApiName());
        return;
    }
    if (!d->createTarget())
        return;
    d->layoutContent();
    d->initialized = true;
}

OffscreenWindow::~OffscreenWindow()
{
    releaseContent();
    if (d->rootItem && d->rootItem->parentItem() == d->window->contentItem())
        d->rootItem->setParentItem(nullptr);
    d->releaseTarget();
    d->window.reset();
    d->renderControl.reset();
}

bool OffscreenWindow::isValid() const { return d->initialized; }
QString OffscreenWindow::errorString() const { return d->error; }
QQuickWindow *OffscreenWindow::window() const { return d->window.get(); }
QSize OffscreenWindow::size() const { return d->size; }

QString OffscreenWindow::graphicsApiName() const
{
    if (QRhi *rhi = d->renderControl ? d->renderControl->rhi() : nullptr)
        return QString::fromLatin1(rhi->backendName());
    switch (d->window->rendererInterface()->graphicsApi()) {
    case QSGRendererInterface::Software: return QStringLiteral("Software");
    case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("D3D11");
    case QSGRendererInterface::Direct3D12: return QStringLiteral("D3D12");
    case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal: return QStringLiteral("Metal");
    default: return QStringLiteral("unknown");
    }
}

void OffscreenWindow::resize(QSize size)
{
    if (!size.isValid() || size.isEmpty() || size == d->size)
        return;
    d->size = size;
    if (d->initialized && !d->createTarget())
        d->initialized = false;
    d->layoutContent();
}

void OffscreenWindow::setRootItem(QQuickItem *item, bool fillWindow)
{
    releaseContent();
    if (d->rootItem && d->rootItem != item && d->rootItem->parentItem() == d->window->contentItem())
        d->rootItem->setParentItem(nullptr);
    d->rootItem = item;
    d->fillWindow = fillWindow;
    if (item)
        item->setParentItem(d->window->contentItem());
    d->layoutContent();
}

void OffscreenWindow::adoptWindowContent(QQuickWindow *qmlWindow)
{
    releaseContent();
    if (!qmlWindow)
        return;
    d->adoptedWindow = qmlWindow;
    d->adoptedContent = qmlWindow->contentItem();
    d->adoptedOldParent = d->adoptedContent->parentItem();
    d->window->setColor(qmlWindow->color());
    d->adoptedContent->setParentItem(d->window->contentItem());
    d->layoutContent();
}

void OffscreenWindow::releaseContent()
{
    if (d->adoptedContent && d->adoptedContent->parentItem() == d->window->contentItem())
        d->adoptedContent->setParentItem(d->adoptedOldParent);
    d->adoptedContent = nullptr;
    d->adoptedWindow = nullptr;
    d->adoptedOldParent = nullptr;
}

void OffscreenWindow::renderFrame()
{
    if (!d->initialized)
        return;
    d->renderControl->polishItems();
    d->renderControl->beginFrame();
    d->renderControl->sync();
    d->renderControl->render();
    d->renderControl->endFrame();
}

QImage OffscreenWindow::grab()
{
    if (!d->initialized)
        return {};
    QRhi *rhi = d->renderControl->rhi();

    d->renderControl->polishItems();
    d->renderControl->beginFrame();
    d->renderControl->sync();
    d->renderControl->render();

    QRhiReadbackResult readback;
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->readBackTexture(QRhiReadbackDescription(d->texture.get()), &readback);
    d->renderControl->commandBuffer()->resourceUpdate(batch);
    d->renderControl->endFrame();  // offscreen frames complete synchronously: data is ready

    if (readback.data.isEmpty()) {
        d->error = QStringLiteral("texture readback returned no data");
        return {};
    }
    const QSize px = readback.pixelSize;
    QImage image;
    if (readback.format == QRhiTexture::BGRA8) {
        image = QImage(reinterpret_cast<const uchar *>(readback.data.constData()), px.width(),
                       px.height(), px.width() * 4, QImage::Format_ARGB32_Premultiplied)
                    .copy();
    } else {
        image = QImage(reinterpret_cast<const uchar *>(readback.data.constData()), px.width(),
                       px.height(), px.width() * 4, QImage::Format_RGBA8888_Premultiplied)
                    .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    if (rhi->isYUpInFramebuffer())
        image.flip(Qt::Vertical);
    image.setDevicePixelRatio(d->dpr);
    return image;
}

// ---------------------------------------------------------------------------------------------
// runQmlApp
// ---------------------------------------------------------------------------------------------

namespace {

LaunchOptions g_launchOptions;

// The offscreen platform plugin uses a generic 9 pt "Sans Serif" default font; use the native
// platform's default UI font instead so screenshots match what users see.
void useNativeDefaultFont()
{
    if (QGuiApplication::platformName() != u"offscreen" && QGuiApplication::platformName() != u"minimal")
        return;
#if defined(Q_OS_MACOS)
    QGuiApplication::setFont(QFont(QStringLiteral(".AppleSystemUIFont"), 13));
#elif defined(Q_OS_WIN)
    QGuiApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));
#endif
}

void printErrors(const QList<QQmlError> &errors)
{
    for (const QQmlError &e : errors)
        std::fprintf(stderr, "%s\n", qPrintable(e.toString()));
}

bool argvHasPlatform(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        if (a == "-platform" || a == "--platform" || a.startsWith("-platform="))
            return true;
    }
    return false;
}

QSize rootSize(QObject *root, const QSize &fallback)
{
    if (auto *item = qobject_cast<QQuickItem *>(root)) {
        const QSizeF s(item->width() > 0 ? item->width() : item->implicitWidth(),
                       item->height() > 0 ? item->height() : item->implicitHeight());
        if (s.width() >= 1 && s.height() >= 1)
            return s.toSize();
    } else if (auto *window = qobject_cast<QQuickWindow *>(root)) {
        if (window->width() > 0 && window->height() > 0)
            return window->size();
    }
    return fallback;
}

// Offscreen screenshot run: keeps rendering at ~60 Hz for delayMs, then grabs and saves.
int runScreenshot(QGuiApplication &app, QObject *root, const LaunchOptions &options, const QSize &size)
{
    OffscreenWindow offscreen(size, options.devicePixelRatio, options.samples);
    if (!offscreen.isValid()) {
        std::fprintf(stderr, "offscreen: %s\n", qPrintable(offscreen.errorString()));
        return 1;
    }
    if (auto *item = qobject_cast<QQuickItem *>(root))
        offscreen.setRootItem(item, true);
    else if (auto *window = qobject_cast<QQuickWindow *>(root))
        offscreen.adoptWindowContent(window);

    int exitCode = 0;
    bool done = false;
    QTimer frameTimer;
    frameTimer.setInterval(16);
    QObject::connect(&frameTimer, &QTimer::timeout, &app, [&] { offscreen.renderFrame(); });

    auto capture = [&] {
        if (done)
            return;
        done = true;
        frameTimer.stop();
        const QImage image = offscreen.grab();
        const QString path = QFileInfo(options.screenshotPath).absoluteFilePath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (image.isNull() || !image.save(path)) {
            std::fprintf(stderr, "offscreen: cannot write screenshot %s: %s\n", qPrintable(path),
                         qPrintable(offscreen.errorString()));
            exitCode = 1;
        } else {
            std::fprintf(stderr, "offscreen: wrote %s (%dx%d px, %s)\n", qPrintable(path),
                         image.width(), image.height(), qPrintable(offscreen.graphicsApiName()));
        }
        QCoreApplication::exit(exitCode);
    };

    offscreen.renderFrame();
    frameTimer.start();
    QTimer::singleShot(options.delayMs, &app, capture);
    const int code = app.exec();
    offscreen.releaseContent();
    if (auto *item = qobject_cast<QQuickItem *>(root))
        item->setParentItem(nullptr);
    return done ? exitCode : code;
}

}  // namespace

const LaunchOptions &launchOptions()
{
    return g_launchOptions;
}

namespace {
QString g_quickStyle;

}  // namespace

QString nativeQuickStyle()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("macOS");
#elif defined(Q_OS_WIN)
    return QStringLiteral("FluentWinUI3");
#else
    // Fusion (Qt's desktop style; it follows the KDE/GNOME palette through the platform theme).
    // KDE's org.kde.desktop is not the default: it needs Kirigami, adds its own dialog headers and
    // control metrics, and broke the instrument bars (Alex, round 4); `--style org.kde.desktop`
    // still selects it.
    return QStringLiteral("Fusion");
#endif
}

QString resolveQuickStyle(const QString &name, bool offscreenRender)
{
    if (name.compare(u"native", Qt::CaseInsensitive) == 0)
        return offscreenRender ? QStringLiteral("Fusion") : nativeQuickStyle();
    if (name.compare(u"mybench", Qt::CaseInsensitive) == 0)
        return QStringLiteral("MyBench.Style");
    return name;
}

QString quickStyle()
{
    return g_quickStyle;
}

namespace {
// Real-platform capture (--grab): the window is shown (fully transparent, without taking focus and
// transparent for mouse input, so the user's desktop keeps working), rendered for delayMs and grabbed with QQuickWindow::grabWindow().
int runGrab(QGuiApplication &app, QQuickWindow *window, const LaunchOptions &options)
{
    window->setOpacity(0.0);
    window->setFlag(Qt::WindowDoesNotAcceptFocus, true);
    window->setFlag(Qt::WindowTransparentForInput, true);   // clicks and hover go to what lies below
    window->show();
    int exitCode = 1;
    QTimer::singleShot(options.delayMs, &app, [&] {
        const QImage image = window->grabWindow();
        const QString path = QFileInfo(options.grabPath).absoluteFilePath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (!image.isNull() && image.save(path)) {
            std::fprintf(stderr, "grab: wrote %s (%dx%d px, %s, style %s)\n", qPrintable(path), image.width(),
                         image.height(), qPrintable(QGuiApplication::platformName()), qPrintable(g_quickStyle));
            exitCode = 0;
        } else {
            std::fprintf(stderr, "grab: cannot write %s\n", qPrintable(path));
        }
        QCoreApplication::exit(exitCode);
    });
    app.exec();
    return exitCode;
}
}  // namespace

int runQmlApp(int argc, char *argv[], const QmlApp &spec)
{
    // Pre-parse to decide on the platform plugin before QGuiApplication exists. --style is taken
    // from here: QGuiApplication consumes "-style"/"--style" (its style override) from the arguments.
    QString requestedStyle;
    {
        QStringList raw;
        for (int i = 1; i < argc; ++i)
            raw.append(QString::fromLocal8Bit(argv[i]));
        LaunchOptions pre;
        const bool parsed = parseLaunchOptions(raw, &pre);
        requestedStyle = pre.style;
        if (parsed && pre.screenshot() && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !argvHasPlatform(argc, argv))
            qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    if (!spec.applicationName.isEmpty())
        QCoreApplication::setApplicationName(spec.applicationName);
    if (!spec.organizationName.isEmpty())
        QCoreApplication::setOrganizationName(spec.organizationName);
    if (!spec.organizationDomain.isEmpty())
        QCoreApplication::setOrganizationDomain(spec.organizationDomain);

    QGuiApplication app(argc, argv);
    ensureRhiAdaptation();  // before any QQuickWindow (a QML Window root creates one)
    useNativeDefaultFont();

    LaunchOptions options;
    QString error;
    if (!parseLaunchOptions(app.arguments().mid(1), &options, &error)) {
        std::fprintf(stderr, "%s\n\n%s", qPrintable(error), qPrintable(launchOptionsHelp()));
        return 2;
    }
    if (options.style.isEmpty())
        options.style = requestedStyle;
    g_launchOptions = options;
    if (options.help) {
        std::fprintf(stdout, "%s", qPrintable(launchOptionsHelp()));
        return 0;
    }

    {
        // --style overrides QmlApp::style. "native" means Fusion when rendering offscreen
        // (screenshots, tests on the offscreen platform); name "macOS" etc. to force one.
        const QString platform = QGuiApplication::platformName();
        const bool offscreenRender = options.screenshot() || platform == u"offscreen" || platform == u"minimal";
        const QString style = resolveQuickStyle(options.style.isEmpty() ? spec.style : options.style, offscreenRender);
        if (!style.isEmpty()) {
            QQuickStyle::setStyle(style);
            g_quickStyle = style;
        }
    }
    if (!options.theme.isEmpty())
        QGuiApplication::styleHints()->setColorScheme(options.theme == u"dark" ? Qt::ColorScheme::Dark
                                                                                : Qt::ColorScheme::Light);

    QQmlEngine engine;
    QObject::connect(&engine, &QQmlEngine::quit, &app, &QCoreApplication::quit, Qt::QueuedConnection);
    QObject::connect(&engine, &QQmlEngine::exit, &app, &QCoreApplication::exit, Qt::QueuedConnection);

    QVariantMap initialProperties;
    if (spec.setup)
        spec.setup(engine, options, initialProperties);

    QQmlComponent component(&engine);
    if (!spec.module.isEmpty())
        component.loadFromModule(spec.module, spec.type);
    else
        component.loadUrl(spec.url);
    if (component.isError()) {
        printErrors(component.errors());
        return 1;
    }

    std::unique_ptr<QObject> root(component.beginCreate(engine.rootContext()));
    if (!root) {
        printErrors(component.errors());
        return 1;
    }
    if (!initialProperties.isEmpty())
        component.setInitialProperties(root.get(), initialProperties);
    if (options.capture() && qobject_cast<QQuickWindow *>(root.get()))
        root->setProperty("visible", false);  // never shown (--screenshot) or shown by runGrab()
    component.completeCreate();
    if (component.isError()) {
        printErrors(component.errors());
        return 1;
    }

    const QSize size = options.size.isValid() ? options.size : rootSize(root.get(), spec.defaultSize);

    if (options.screenshot()) {
        if (auto *window = qobject_cast<QQuickWindow *>(root.get())) {
            window->setVisible(false);
            window->setGeometry(0, 0, size.width(), size.height());
        } else if (!qobject_cast<QQuickItem *>(root.get())) {
            std::fprintf(stderr, "offscreen: the root object is neither an Item nor a Window\n");
            return 1;
        }
        const int code = runScreenshot(app, root.get(), options, size);
        root.reset();
        return code;
    }

    // Interactive run (or a --grab of the real window).
    std::unique_ptr<QQuickWindow> ownWindow;
    if (auto *window = qobject_cast<QQuickWindow *>(root.get())) {
        if (options.size.isValid())
            window->resize(size);
        if (window->title().isEmpty() && !spec.applicationName.isEmpty())
            window->setTitle(spec.applicationName);
        if (!window->isVisible() && !options.grab())
            window->show();
    } else if (auto *item = qobject_cast<QQuickItem *>(root.get())) {
        ownWindow = std::make_unique<QQuickWindow>();
        ownWindow->setTitle(spec.applicationName.isEmpty() ? QCoreApplication::applicationName()
                                                           : spec.applicationName);
        item->setParentItem(ownWindow->contentItem());
        QQuickWindow *w = ownWindow.get();
        auto fit = [w, item] { item->setSize(QSizeF(w->width(), w->height())); };
        QObject::connect(w, &QWindow::widthChanged, item, fit);
        QObject::connect(w, &QWindow::heightChanged, item, fit);
        ownWindow->resize(size);
        fit();
        if (!options.grab())
            ownWindow->show();
    } else {
        std::fprintf(stderr, "the root object is neither an Item nor a Window\n");
        return 1;
    }

    int code = 0;
    if (options.grab()) {
        QQuickWindow *window = ownWindow ? ownWindow.get() : qobject_cast<QQuickWindow *>(root.get());
        code = runGrab(app, window, options);
    } else {
        code = app.exec();
    }
    if (auto *item = qobject_cast<QQuickItem *>(root.get()))
        item->setParentItem(nullptr);
    root.reset();
    ownWindow.reset();
    return code;
}

}  // namespace mybench

#include "offscreen.moc"
