# mybench_offscreen — run a QML root, or render it offscreen to a PNG

Static library `mybench_offscreen`, header `<mybench/offscreen.h>`. One call from `main()` loads a
QML root (module type or URL) and either runs it in a window, or with `--screenshot` renders the
scene **offscreen** and writes a PNG, then exits. No window is ever shown (not even a Dock icon:
the `offscreen` platform plugin is selected automatically), and the real GPU scene graph is used
(QQuickRenderControl + QRhi texture render target + readback: Metal on macOS, D3D11 on Windows,
OpenGL on Linux), so custom `QSGGeometryNode`s, Shapes, MultiEffect and text render exactly as in
the app.

## Use it

```cmake
target_link_libraries(mybench-scope PRIVATE mybench_offscreen)
```

```cpp
#include <mybench/offscreen.h>

int main(int argc, char *argv[])
{
    return mybench::runQmlApp(argc, argv, {
        .module = QStringLiteral("MyBench.Instruments.Scope"),   // or .url = QUrl("qrc:/...")
        .type = QStringLiteral("ScopeDev"),
        .applicationName = QStringLiteral("mybench-scope"),
        .organizationName = QStringLiteral("MYBENCH"),
        // optional: register things, map extra args to initial (required) properties
        .setup = [](QQmlEngine &engine, const mybench::LaunchOptions &options, QVariantMap &props) {
            if (!options.arguments.isEmpty())
                props.insert(QStringLiteral("instrumentId"), options.arguments.first());
        },
    });
}
```

`QmlApp` fields: `module`+`type` or `url`; `applicationName`, `organizationName`,
`organizationDomain`; `style` (Qt Quick Controls style, default `"Basic"`, empty = untouched;
`"native"` and `"mybench"` are resolved as described under *Controls style*);
`defaultSize` (1280×800); `setup` callback (engine, options, initial properties).

The root may be an **Item** (put in a window that it fills) or a **Window/ApplicationWindow**
(shown as is; in screenshot mode it is kept invisible and its content item is rendered offscreen,
header/footer laid out to `--size`).

Static QML modules linked into the executable still need their plugin imported, e.g.
`Q_IMPORT_QML_PLUGIN(MyBench_PlotPlugin)` in `main.cpp` and linking `mybench_plotplugin`.

## Command line

| Option | Meaning |
|---|---|
| `--screenshot <file.png>` | render offscreen, write the PNG, exit (0 ok, 1 QML/render error, 2 bad option) |
| `--delay <ms>` | run (animations, timers, simulated data) this long before capturing; default 500 |
| `--size <W>x<H>` | logical size; default: the root's width/height, else `defaultSize` |
| `--theme dark\|light` | sets `QStyleHints::colorScheme` and `mybench::launchOptions().theme` |
| `--dpr <ratio>` | device pixel ratio of the PNG (2 = retina-sharp; image is W·dpr × H·dpr) |
| `--samples <n>` | MSAA samples of the offscreen target (default 1, like a normal window) |
| `--style <name>` | Qt Quick Controls style for this run, overriding `QmlApp::style`: `native`, `mybench`, or any style name (`Fusion`, `Basic`, `macOS`, …) |
| `--grab <file.png>` | show the **real** window on the real platform (cocoa, windows, xcb/wayland) and grab it after `--delay`, write the PNG, exit |
| `--help-launch` | print the options |

Every other argument is passed through in `LaunchOptions::arguments` (e.g. an instrument id).
Qt's own options (`-platform`, `-qmljsdebugger`, …) are handled by QGuiApplication as usual.

```sh
build/ui/offscreen/test/mybench-offscreen-test --screenshot /tmp/shot.png --size 640x400 --dpr 2
QT_QPA_PLATFORM=offscreen build/app/mybench --screenshot shot.png --delay 1500 --theme light scope
```

Environment: `QSG_RHI_BACKEND=metal|vulkan|opengl|d3d11|d3d12` forces the graphics API;
`QT_QPA_PLATFORM` (or `-platform`) given explicitly is respected (e.g. `cocoa` also works and still
shows no window). On headless Linux CI use a platform with GL (EGL/`QT_QPA_PLATFORM=offscreen`
with Mesa) or `QSG_RHI_BACKEND=vulkan` with a software Vulkan driver (lavapipe).

### Controls style

`style` / `--style` accept two aliases besides plain style names:

| Value | Real window | Offscreen (`--screenshot`, `QT_QPA_PLATFORM=offscreen`/`minimal`) |
|---|---|---|
| `native` | the platform style: `macOS`, `FluentWinUI3` (Windows), `org.kde.desktop` (Linux, when its QML module is installed), else `Fusion` | `Fusion` (the macOS and Windows styles draw no bezels without a real window) |
| `mybench` | `MyBench.Style` | `MyBench.Style` |
| any other (`Fusion`, `macOS`, …) | that style | that style (forcing `macOS` offscreen is allowed, for debugging) |

`mybench::nativeQuickStyle()` and `mybench::resolveQuickStyle(name, offscreen)` expose the rule,
`mybench::quickStyle()` returns the style `runQmlApp()` selected. The MYBENCH executables default
to `native`; the preference *Appearance > Controls* (`appearance/controls` = `mybench`) selects
MyBench.Style. `--fresh`, `--screenshot` and `--grab` ignore the stored preference.

### Real-window grab (`--grab`)

`--screenshot` cannot show the platform look (offscreen there is no macOS/Windows style). `--grab`
runs on the real platform instead: the window is shown fully transparent, without taking focus
and transparent for mouse input (clicks and hover reach the windows below, so a grab never opens
a tooltip or changes the session; on macOS the window may flash for a frame), renders for `--delay` ms and
is captured with `QQuickWindow::grabWindow()` (the scene graph, at the screen's device pixel
ratio of the screen it lands on: 1x or 2x), then the program exits (0 ok, 1 error). The window is
never the key window, so native controls draw their inactive-window state (macOS: no focus ring,
default buttons not tinted). Use it to verify what the native style really draws:

```sh
MYBENCH_NO_USB=1 build/app/MYBENCH.app/Contents/MacOS/MYBENCH --simulated --panel scope --split gen \
    --run scope,gen --size 1440x900 --delay 2500 --grab /tmp/scope-native.png
```

The MYBENCH shell treats `--grab` like `--screenshot` for settings (fresh, temporary) and USB
(never opened unless `--device usb…` is given). Do not set `QT_QPA_PLATFORM=offscreen` for a grab.

### Theme

The `offscreen` platform plugin ignores `QStyleHints::setColorScheme()`, so a Theme singleton
should start from `mybench::launchOptions().theme` when it is non-empty (a C++ helper can expose
it to QML) and otherwise follow `Application.styleHints.colorScheme`.

## Lower level: `mybench::OffscreenWindow`

A `QQuickWindow` driven by `QQuickRenderControl`, for tests and tools:

```cpp
mybench::OffscreenWindow offscreen(QSize(800, 480), /*dpr*/ 1.0);
offscreen.setRootItem(item);            // or adoptWindowContent(qmlWindow)
offscreen.renderFrame();                // polish + sync + render (drive animations)
QImage image = offscreen.grab();        // render + read back (ARGB32 premultiplied)
```

`isValid()`/`errorString()` report graphics initialization failures; `graphicsApiName()` gives
the backend ("Metal", …); `resize()` recreates the render target.

## Self-test

`ctest -R offscreen` runs `mybench-offscreen-test` (scene with text, Shapes, MultiEffect, a custom
QSGGeometryNode and an animation) offscreen and checks pixel colors of each element, plus an
ApplicationWindow root at `--dpr 2 --theme light`.
