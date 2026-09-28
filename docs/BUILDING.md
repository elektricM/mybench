# Building MYBENCH

## Requirements

| | Version | Notes |
|---|---|---|
| CMake | ≥ 3.21 | Ninja recommended |
| Compiler | C11 + C++20 | AppleClang 15+, GCC 12+, Clang 16+, MSVC 2022 |
| Qt | ≥ 6.9 | Core, Gui (+ GuiPrivate headers), Qml, Quick, QuickControls2, ShaderTools, Svg, Test; Multimedia optional (DMM continuity tone) |
| libusb-1.0 | ≥ 1.0.26 | optional: without it only the simulated device is built |
| pkg-config | any | finds libusb; on Windows libusb is found without it (see below) |

Qt 6.9 is the minimum (`qt_standard_project_setup(REQUIRES 6.9)`): the sources use `QImage::flip`
and the `ContextMenu` edit actions, both new in 6.9.

## macOS

```sh
brew install cmake ninja qt libusb pkgconf
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
open build/app/MYBENCH.app
```

## Linux

Debian 14 / Ubuntu 25.10+ (distribution Qt ≥ 6.9):

```sh
sudo apt install cmake ninja-build g++ pkg-config libusb-1.0-0-dev \
    qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-declarative-private-dev \
    qt6-shadertools-dev qt6-svg-dev qt6-multimedia-dev libgl-dev libvulkan-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/app/mybench
```

Fedora 42+:

```sh
sudo dnf install cmake ninja-build gcc-c++ libusb1-devel qt6-qtbase-devel \
    qt6-qtbase-private-devel qt6-qtdeclarative-devel qt6-qtshadertools-devel qt6-qtsvg-devel \
    qt6-qtmultimedia-devel
```

Older distributions: install Qt from the Qt online installer or with
[aqtinstall](https://github.com/miurahr/aqtinstall), then pass
`-DCMAKE_PREFIX_PATH=<qt>/gcc_64`:

```sh
aqt install-qt linux desktop 6.11.3 linux_gcc_64 -m qtshadertools qtmultimedia
```

To use a real myDAQ you need the udev rule; see [INSTALL.md](INSTALL.md#usb-permission-udev-rule).

## Windows (MSVC)

In a *Developer PowerShell for VS 2022* (it provides `cl`, `rc` and `fxc`; `qsb` uses `fxc` to
precompile the plot shaders):

```powershell
# Qt: online installer or  aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -m qtshadertools qtmultimedia
vcpkg install libusb:x64-windows
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.10.3/msvc2022_64;$env:VCPKG_ROOT/installed/x64-windows"
cmake --build build
```

If pkg-config is not available, the top-level `CMakeLists.txt` finds libusb by its header
(`libusb-1.0/libusb.h`) and library (`libusb-1.0.lib`) under `CMAKE_PREFIX_PATH`. This works
for vcpkg and for the libusb binary release.

Before running tests or the build-tree executable, put Qt's `bin` and libusb's `bin`
(`libusb-1.0.dll`) on `PATH`. To use a real myDAQ you need the WinUSB driver; see
[INSTALL.md](INSTALL.md#windows-usb-driver).

## Options

| CMake option | Default | |
|---|---|---|
| `MYBENCH_APP_NAME` | `MYBENCH` | display name, bundle/executable name on macOS |
| `MYBENCH_APP_ID` | `org.mybench.<name>` | macOS bundle id, Linux desktop/AppStream id |
| `MYBENCH_WITH_USB` | `ON` | native libusb backend (+ libmydaq, its tests and `mydaq-cli`) |
| `MYBENCH_INSTRUMENTS` | `all` | build a subset, e.g. `"scope;dmm"` |
| `MYBENCH_PACKAGING` | `ON` | app icon, install rules, CPack (`packaging/`) |
| `MYBENCH_DEPLOY_QT` | `ON` on macOS/Windows | copy the Qt runtime into the install tree |
| `MYBENCH_CODESIGN_IDENTITY` | `-` | macOS: ad-hoc signature, or a Developer ID identity |
| `MYBENCH_WDI_SIMPLE` | — | Windows: bundle libwdi's `wdi-simple.exe` for the optional driver step |
| `MYBENCH_VENDOR`, `MYBENCH_MAINTAINER`, `MYBENCH_HOMEPAGE` | placeholders | package metadata |

Every executable (the app `mybench` and the per-instrument dev executables `mybench-<id>`,
e.g. `mybench-scope`) accepts `--screenshot out.png [--size WxH] [--delay ms]` and renders
offscreen. This works with `QT_QPA_PLATFORM=offscreen` and needs no display.

## Tests

```sh
MYBENCH_NO_USB=1 QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -LE hw
```

- `MYBENCH_NO_USB=1` keeps the USB backend idle, so a test run never opens a real myDAQ.
- libmydaq's C tests use its mock transport (capture replay + emulator).
- Hardware tests are opt-in (`MYDAQ_HW_TESTS=1 ctest -L hw`, see [libmydaq.md](libmydaq.md#tests)).
- On headless Linux, run ctest under `xvfb-run -a`: the offscreen render tests need an OpenGL
  context.

## Packaging

`packaging/CMakeLists.txt` holds the app icon, the install rules and the CPack configuration.

The icon is a single vector source, [`assets/app-icon.svg`](../assets/app-icon.svg). A small
host tool (`packaging/icongen`, Qt Svg) renders it at build time into:
- `mybench.icns` (macOS)
- `mybench.ico` (Windows)
- hicolor PNGs, 16–512 px (Linux)

No binary icon files live in the repository.

### macOS: app bundle and DMG

```sh
packaging/macos/build-dmg.sh                 # = configure Release + build + cpack
# or by hand:
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build-release
cmake --install build-release --prefix /tmp/mybench      # deployed /tmp/mybench/MYBENCH.app
(cd build-release && cpack -B package)                    # package/MYBENCH-<v>-macOS-<arch>.dmg
```

The install step:

1. **Bundle**: `MYBENCH.app` with `packaging/macos/Info.plist.in` configured:
   - bundle id, version from `project()`
   - `NSHighResolutionCapable`, dark-mode support, category
   - the `.icns` icon
2. **Deploy**, in `packaging/macos/deploy-bundle.cmake`:
   - `macdeployqt -qmldir=app -qmldir=ui/controls -qmldir=instruments -qmlimport=<build>/qml`.
     This copies the Qt frameworks, the QML modules the sources import and the Qt plugins. It
     also copies non-Qt dylibs, including **libusb**, and rewrites their install names.
   - The offscreen platform plugin is added, so headless `--screenshot` works.
   - The bundle is pruned to what qmlimportscanner reports. Removed are: our static
     `MyBench.*` modules, the unused Controls styles, and unused plugin types (SQL, TLS,
     input methods; image formats except SVG). Then any framework or dylib that is no longer
     reachable goes too.
   - Every Mach-O file is checked: all its dependencies must resolve inside the bundle or in
     `/System` and `/usr/lib`. The install **fails** if anything points to `/opt/homebrew`,
     `/usr/local` or the build tree.
3. **Finalize**, in `packaging/macos/finalize-bundle.cmake`:
   - `LSMinimumSystemVersion` is raised to the newest `minos` found in the bundled binaries.
     Homebrew's libusb, for example, is built for the build machine's macOS.
   - Every nested binary is signed, then the bundle, ad hoc by default
     (`MYBENCH_CODESIGN_IDENTITY=-`).
   - With a Developer ID identity, the hardened runtime and a timestamp are added, ready for
     notarization with `xcrun notarytool submit … --wait` and `xcrun stapler staple`.
4. **DMG**: CPack DragNDrop, a compressed APFS image with an Applications link.

The DMG must run on older Macs and on Intel. For that, build like the release workflow:
- Qt from the Qt installer or aqtinstall (universal binaries).
- libusb built by `packaging/macos/build-libusb.sh <prefix>` (universal, minimum macOS
  13), used via `PKG_CONFIG_PATH=<prefix>/lib/pkgconfig`.
- `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0`.

A bundle deployed from Homebrew's Qt only runs on the build machine's macOS version (or
newer), and is arm64 only.

### Linux: DEB, RPM, AppImage

The install tree (`cmake --install build --prefix /usr`) contains:

| File | Destination |
|---|---|
| `mybench` | `bin/` |
| `<app-id>.desktop` | `share/applications/` |
| `<app-id>.metainfo.xml` (AppStream) | `share/metainfo/` |
| icons | `share/icons/hicolor/{16…512,scalable}/apps/` |
| `.mybench` MIME type | `share/mime/packages/` |
| udev rule `60-mybench-mydaq.rules` | `lib/udev/rules.d/` |

The sources are in `packaging/linux/`.

- **DEB / RPM** (`cpack -G DEB` / `cpack -G RPM`) link against the distribution's Qt; they do
  not bundle it.
  - DEB: dependencies come from `dpkg-shlibdeps`, plus the QML module packages that `Depends:`
    lists explicitly (plugins are invisible to shlibdeps).
  - RPM: dependencies come from rpm's automatic requires.
  - Post-install, both reload udev and update the desktop, MIME and icon caches.
  - CI builds them in `ubuntu:26.04` and `fedora:43` containers, and installs each package
    once as a check.
- **AppImage**: `packaging/linux/appimage/build-appimage.sh [build-dir]` uses linuxdeploy and
  linuxdeploy-plugin-qt on the same install tree:
  - Deploy step: `QML_SOURCES_PATHS` = the shipped QML, `EXTRA_QT_MODULES=svg`, plus the
    offscreen and Wayland platform plugins.
  - Then it removes the copied `MyBench.*` qmldirs and runs a smoke test (offscreen
    screenshot).
  - Build it on the oldest distribution you want to support. CI uses ubuntu-22.04 with Qt
    from aqtinstall.
- Checks: `desktop-file-validate`, `appstreamcli validate` and `udevadm verify` run in CI.

### Windows: NSIS installer

```powershell
cmake --build build --config Release
cd build; cpack -C Release -B package      # package/MYBENCH-<v>-windows-x64.exe and .zip
```

The install tree has these parts:

- **`bin/mybench.exe`**: the icon and version info come from `packaging/windows/mybench.rc.in`.
- **Qt runtime**: deployed by Qt's `qt_generate_deploy_qml_app_script`, which runs
  windeployqt with the QML imports from qmlimportscanner and `--no-opengl-sw`.
- **`libusb-1.0.dll`**: found next to the import library or in `<prefix>/bin`.
- **MSVC runtime**: installed by `InstallRequiredSystemLibraries`.
- **Help link**: an internet shortcut to the WinUSB driver instructions.

NSIS adds:
- Start menu and desktop shortcuts
- an uninstaller
- removal of a previous version before install
- an option to launch the app from the finish page

**Optional driver step**: configure with `-DMYBENCH_WDI_SIMPLE=C:/path/wdi-simple.exe`. The
installer then carries `driver/wdi-simple.exe`. At the end of setup it asks, default *No*,
whether to bind WinUSB to the myDAQ (`packaging/windows/nsis-driver.nsh`). See
[INSTALL.md](INSTALL.md#windows-usb-driver) for why this replaces NI's driver and how to go
back.

## Continuous integration

`.github/workflows/ci.yml` runs on every push and pull request.

| Job | Qt | Notes |
|---|---|---|
| macOS | 6.11 | Homebrew libusb |
| Linux (Ubuntu 24.04) | 6.11 | apt libusb; tests under Xvfb |
| Windows | 6.10 (MSVC) | vcpkg libusb |
| Linux (minimum) | 6.9 | the declared minimum |

Qt comes from aqtinstall. aqtinstall 3.3 cannot yet install Windows Qt 6.11+, whose online
repository layout changed. That is why the Windows jobs use 6.10.

Each job:
- configures, builds and runs `ctest -LE hw` with `MYBENCH_NO_USB=1` and
  `QT_QPA_PLATFORM=offscreen`
- renders a screenshot of Home as a smoke test and uploads it
- on Linux, also validates the desktop, AppStream and udev files

`.github/workflows/release.yml` runs on tags `v*`. It builds the DMG (universal), the AppImage,
the DEB, the RPM and the Windows installer, checks each one (mount/install and start
offscreen), and attaches all of them to a draft GitHub release.
