# Packaging (docs/BUILDING.md "Packaging"): app icon, install rules, Qt runtime deployment, CPack.
#
#   macOS    MYBENCH.app (Info.plist, .icns), macdeployqt (via Qt's deploy script), ad-hoc
#            codesign, DMG (CPack DragNDrop)
#   Windows  bin/mybench.exe (+ icon resource), windeployqt, libusb-1.0.dll, MSVC runtime,
#            NSIS installer (optional WinUSB driver step with libwdi's wdi-simple)
#   Linux    FHS install (bin, .desktop, AppStream metainfo, hicolor icons, udev rule) for
#            DEB/RPM against the distribution's Qt (>= 6.9); AppImage via linuxdeploy on top
#            of the same install tree (packaging/linux/appimage/build-appimage.sh)
#
#   cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-release && cpack --config build-release/CPackConfig.cmake -B out

include(GNUInstallDirs)

set(MYBENCH_VENDOR "MYBENCH contributors" CACHE STRING "Package vendor / copyright holder")
set(MYBENCH_MAINTAINER "MYBENCH maintainers <maintainers@mybench.invalid>" CACHE STRING
    "Package maintainer (Debian control, RPM packager)")
set(MYBENCH_HOMEPAGE "https://github.com/mybench/mybench" CACHE STRING "Project homepage")
set(MYBENCH_SUMMARY "Instrument suite for the NI myDAQ (DMM, scope, generator, Bode, spectrum, DIO)")

if(APPLE OR WIN32)
    set(_deploy_default ON)
else()
    set(_deploy_default OFF)   # DEB/RPM use the distribution's Qt; the AppImage uses linuxdeploy
endif()
option(MYBENCH_DEPLOY_QT "Copy the Qt runtime into the install tree (macdeployqt/windeployqt)" ${_deploy_default})

# --- Application icon ------------------------------------------------------------------------
# One vector source (assets/app-icon.svg) rendered at build time by a small Qt tool into
# .icns (macOS), .ico (Windows) and hicolor PNGs (Linux).
set(MYBENCH_ICON_SVG ${PROJECT_SOURCE_DIR}/assets/app-icon.svg)
set(MYBENCH_ICON_DIR ${CMAKE_CURRENT_BINARY_DIR}/icons)
set(MYBENCH_ICON_NAME mybench)
find_package(Qt6 REQUIRED COMPONENTS Gui Svg)
qt_add_executable(mybench-icongen MANUAL_FINALIZATION icongen/main.cpp)
set_target_properties(mybench-icongen PROPERTIES MACOSX_BUNDLE FALSE WIN32_EXECUTABLE FALSE)
target_link_libraries(mybench-icongen PRIVATE Qt6::Gui Qt6::Svg)
qt_finalize_target(mybench-icongen)

set(MYBENCH_ICON_OUTPUTS ${MYBENCH_ICON_DIR}/${MYBENCH_ICON_NAME}.icns ${MYBENCH_ICON_DIR}/${MYBENCH_ICON_NAME}.ico)
foreach(size 16 22 24 32 48 64 128 256 512)
    list(APPEND MYBENCH_ICON_OUTPUTS ${MYBENCH_ICON_DIR}/png/${size}x${size}.png)
endforeach()
add_custom_command(
    OUTPUT ${MYBENCH_ICON_OUTPUTS}
    COMMAND mybench-icongen ${MYBENCH_ICON_SVG} ${MYBENCH_ICON_DIR} ${MYBENCH_ICON_NAME}
    DEPENDS mybench-icongen ${MYBENCH_ICON_SVG}
    COMMENT "Rendering the application icon"
    VERBATIM
)
add_custom_target(mybench_app_icon DEPENDS ${MYBENCH_ICON_OUTPUTS})
add_dependencies(mybench mybench_app_icon)

set(MYBENCH_ICNS ${MYBENCH_ICON_DIR}/${MYBENCH_ICON_NAME}.icns)
set(MYBENCH_ICO ${MYBENCH_ICON_DIR}/${MYBENCH_ICON_NAME}.ico)
set(_app_dir ${PROJECT_SOURCE_DIR}/app)   # source properties are per directory: the target's

if(APPLE)
    # Info.plist: every value is filled here (no ${MACOSX_BUNDLE_*} left for CMake to expand).
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(MYBENCH_MACOS_MIN ${CMAKE_OSX_DEPLOYMENT_TARGET})
    else()
        # The compiler targets the SDK version by default; the install step raises the value
        # to the newest minimum found in the deployed binaries (see macos/finalize-bundle.cmake).
        execute_process(COMMAND xcrun --show-sdk-version OUTPUT_VARIABLE MYBENCH_MACOS_MIN
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT MYBENCH_MACOS_MIN)
            set(MYBENCH_MACOS_MIN 13.0)
        endif()
    endif()
    string(TIMESTAMP MYBENCH_YEAR "%Y")
    get_target_property(MYBENCH_BUNDLE_EXECUTABLE mybench OUTPUT_NAME)
    configure_file(macos/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
    set_source_files_properties(${MYBENCH_ICNS} DIRECTORY ${_app_dir}
        PROPERTIES GENERATED TRUE MACOSX_PACKAGE_LOCATION Resources)
    target_sources(mybench PRIVATE ${MYBENCH_ICNS})
    set_target_properties(mybench PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
        MACOSX_BUNDLE_ICON_FILE ${MYBENCH_ICON_NAME}.icns
    )
elseif(WIN32)
    # Executable icon + version information.
    string(REPLACE "." "," MYBENCH_VERSION_COMMAS "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")
    string(TIMESTAMP MYBENCH_YEAR "%Y")
    file(TO_NATIVE_PATH "${MYBENCH_ICO}" MYBENCH_ICO_NATIVE)
    string(REPLACE "\\" "\\\\" MYBENCH_ICO_RC "${MYBENCH_ICO_NATIVE}")
    configure_file(windows/mybench.rc.in ${CMAKE_CURRENT_BINARY_DIR}/mybench.rc @ONLY)
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/mybench.rc DIRECTORY ${_app_dir}
        PROPERTIES OBJECT_DEPENDS ${MYBENCH_ICO})
    target_sources(mybench PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/mybench.rc)
endif()

# --- Install rules ---------------------------------------------------------------------------
if(APPLE)
    install(TARGETS mybench BUNDLE DESTINATION . COMPONENT app)
    set(MYBENCH_INSTALLED_APP "${MYBENCH_BUNDLE_EXECUTABLE}.app")
else()
    install(TARGETS mybench RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT app)
endif()

if(MYBENCH_DEPLOY_QT AND APPLE)
    # macdeployqt -qmldir=<shipped QML sources>, then pruning and a self-containment check
    # (macos/deploy-bundle.cmake).
    find_program(MYBENCH_MACDEPLOYQT macdeployqt
        HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${Qt6_DIR}/../../../bin" REQUIRED)
    set(_qml_sources ${PROJECT_SOURCE_DIR}/app ${PROJECT_SOURCE_DIR}/ui/controls ${PROJECT_SOURCE_DIR}/instruments)
    install(CODE "
        set(MYBENCH_BUNDLE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${MYBENCH_INSTALLED_APP}\")
        set(MYBENCH_MACDEPLOYQT \"${MYBENCH_MACDEPLOYQT}\")
        set(MYBENCH_QML_SOURCE_DIRS \"${_qml_sources}\")
        set(MYBENCH_QML_IMPORT_DIRS \"${CMAKE_BINARY_DIR}/qml\")
        set(MYBENCH_QMLIMPORTSCANNER \"$<TARGET_FILE:Qt6::qmlimportscanner>\")
        set(MYBENCH_QT_QML_DIR \"${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}\")
        set(MYBENCH_QT_PLUGINS_DIR \"${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}\")
        set(MYBENCH_EXTRA_PLUGINS \"platforms/libqoffscreen.dylib\")
        include(\"${CMAKE_CURRENT_SOURCE_DIR}/macos/deploy-bundle.cmake\")
    " COMPONENT app)
elseif(MYBENCH_DEPLOY_QT)
    set(_tool_options "")
    if(WIN32)
        set(_tool_options DEPLOY_TOOL_OPTIONS --no-opengl-sw)   # D3D11/Vulkan; no software GL
    endif()
    # Windows (and Linux with -DMYBENCH_DEPLOY_QT=ON): Qt's deploy script copies the QML imports
    # found by qmlimportscanner (our MyBench.* modules are static) and runs windeployqt (or
    # Qt's generic deployer on Linux).
    qt_generate_deploy_qml_app_script(
        TARGET mybench
        OUTPUT_SCRIPT MYBENCH_DEPLOY_SCRIPT
        NO_TRANSLATIONS
        NO_COMPILER_RUNTIME                 # Windows: InstallRequiredSystemLibraries below
        NO_UNSUPPORTED_PLATFORM_ERROR
        ${_tool_options}
    )
    install(SCRIPT ${MYBENCH_DEPLOY_SCRIPT} COMPONENT app)
endif()

if(APPLE)
    set(MYBENCH_CODESIGN_IDENTITY "-" CACHE STRING
        "codesign identity for the bundle ('-' = ad-hoc; a Developer ID also enables the hardened runtime)")
    install(CODE "
        set(MYBENCH_BUNDLE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${MYBENCH_INSTALLED_APP}\")
        set(MYBENCH_CODESIGN_IDENTITY \"${MYBENCH_CODESIGN_IDENTITY}\")
        include(\"${CMAKE_CURRENT_SOURCE_DIR}/macos/finalize-bundle.cmake\")
    " COMPONENT app)
elseif(WIN32)
    # libusb-1.0.dll next to the executable (windeployqt only copies Qt). Found next to the import
    # library (vcpkg: lib/ -> bin/) whether libusb came from pkg-config or the fallback search.
    if(MYBENCH_WITH_USB)
        set(_libusb_hints "")
        foreach(dir IN LISTS LIBUSB_LIBRARY_DIRS LIBUSB_LIBDIR)
            list(APPEND _libusb_hints "${dir}" "${dir}/../bin")
        endforeach()
        if(LIBUSB_LIBRARY)
            get_filename_component(_libusb_dir "${LIBUSB_LIBRARY}" DIRECTORY)
            list(APPEND _libusb_hints "${_libusb_dir}" "${_libusb_dir}/../bin")
        endif()
        if(LIBUSB_PREFIX)
            list(APPEND _libusb_hints "${LIBUSB_PREFIX}/bin")
        endif()
        find_file(MYBENCH_LIBUSB_DLL libusb-1.0.dll HINTS ${_libusb_hints} PATH_SUFFIXES bin)
        if(MYBENCH_LIBUSB_DLL)
            install(FILES ${MYBENCH_LIBUSB_DLL} DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT app)
        else()
            message(WARNING "libusb-1.0.dll not found: the installer will lack it (set MYBENCH_LIBUSB_DLL)")
        endif()
    endif()
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT app)
    include(InstallRequiredSystemLibraries)

    # Optional: libwdi's wdi-simple.exe, run by the installer on request to bind WinUSB to the
    # myDAQ (docs/INSTALL.md "Windows driver"). Not bundled unless provided.
    configure_file(windows/driver-help.url.in ${CMAKE_CURRENT_BINARY_DIR}/myDAQ-USB-driver-help.url @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/myDAQ-USB-driver-help.url DESTINATION . COMPONENT app)
    set(MYBENCH_WDI_SIMPLE "" CACHE FILEPATH "Path to libwdi's wdi-simple.exe to bundle (optional)")
    if(MYBENCH_WDI_SIMPLE)
        install(PROGRAMS ${MYBENCH_WDI_SIMPLE} DESTINATION driver RENAME wdi-simple.exe COMPONENT app)
    endif()
else()
    set(MYBENCH_LINUX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/linux)
    set(MYBENCH_EXECUTABLE mybench)
    configure_file(linux/mybench.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/${MYBENCH_APP_ID}.desktop @ONLY)
    configure_file(linux/mybench.metainfo.xml.in ${CMAKE_CURRENT_BINARY_DIR}/${MYBENCH_APP_ID}.metainfo.xml @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${MYBENCH_APP_ID}.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT app)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${MYBENCH_APP_ID}.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo COMPONENT app)
    foreach(size 16 22 24 32 48 64 128 256 512)
        install(FILES ${MYBENCH_ICON_DIR}/png/${size}x${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME ${MYBENCH_APP_ID}.png COMPONENT app)
    endforeach()
    install(FILES ${MYBENCH_ICON_SVG} DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME ${MYBENCH_APP_ID}.svg COMPONENT app)
    install(FILES linux/mybench-mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
            RENAME ${MYBENCH_APP_ID}.xml COMPONENT app)
    # udev rule: always below /usr/lib/udev (or <prefix>/lib/udev), never lib64/multiarch.
    set(MYBENCH_UDEV_RULES_DIR "lib/udev/rules.d" CACHE STRING "udev rules directory (relative to the prefix, or absolute)")
    install(FILES linux/60-mybench-mydaq.rules DESTINATION ${MYBENCH_UDEV_RULES_DIR} COMPONENT app)
endif()

# --- CPack -----------------------------------------------------------------------------------
set(CPACK_VERBATIM_VARIABLES ON)   # values reach the generators exactly as written here
set(CPACK_PACKAGE_NAME mybench)
set(CPACK_PACKAGE_VENDOR "${MYBENCH_VENDOR}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${MYBENCH_SUMMARY}")
set(CPACK_PACKAGE_DESCRIPTION "${MYBENCH_APP_NAME} replaces NI ELVISmx for the NI myDAQ: multimeter, oscilloscope, waveform generator, Bode and spectrum analyzers, digital I/O, counter/timer, data logger and audio equalizer in one window. It talks to the device directly over USB (libusb); no NI software is needed. A simulated myDAQ is built in.")
set(CPACK_PACKAGE_HOMEPAGE_URL "${MYBENCH_HOMEPAGE}")
set(CPACK_PACKAGE_CONTACT "${MYBENCH_MAINTAINER}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${MYBENCH_APP_NAME}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_STRIP_FILES ON)
if(EXISTS ${PROJECT_SOURCE_DIR}/LICENSE)
    set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE)
else()
    set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/license-pending.txt)
endif()

# Architecture tag for file names.
if(APPLE)
    if(CMAKE_OSX_ARCHITECTURES MATCHES ";")
        set(_arch universal)
    elseif(CMAKE_OSX_ARCHITECTURES)
        set(_arch ${CMAKE_OSX_ARCHITECTURES})
    else()
        set(_arch ${CMAKE_SYSTEM_PROCESSOR})
    endif()
    set(_os macOS)
elseif(WIN32)
    set(_os windows)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_arch x64)
    else()
        set(_arch x86)
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
        set(_arch arm64)
    endif()
else()
    set(_os linux)
    set(_arch ${CMAKE_SYSTEM_PROCESSOR})
endif()
set(CPACK_PACKAGE_FILE_NAME "${MYBENCH_APP_NAME}-${PROJECT_VERSION}-${_os}-${_arch}")

if(APPLE)
    set(CPACK_GENERATOR DragNDrop)
    set(CPACK_DMG_VOLUME_NAME "${MYBENCH_APP_NAME} ${PROJECT_VERSION}")
    set(CPACK_DMG_FORMAT UDZO)
    set(CPACK_DMG_FILESYSTEM APFS)
    set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE OFF)
    set(CPACK_PACKAGE_ICON ${MYBENCH_ICNS})   # volume icon
elseif(WIN32)
    set(CPACK_GENERATOR NSIS ZIP)
    set(CPACK_NSIS_PACKAGE_NAME "${MYBENCH_APP_NAME}")
    set(CPACK_NSIS_DISPLAY_NAME "${MYBENCH_APP_NAME} ${PROJECT_VERSION}")
    set(CPACK_NSIS_MUI_ICON ${MYBENCH_ICO})
    set(CPACK_NSIS_MUI_UNIICON ${MYBENCH_ICO})
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\mybench.exe")
    set(CPACK_NSIS_URL_INFO_ABOUT "${MYBENCH_HOMEPAGE}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY bin)
    set(CPACK_PACKAGE_EXECUTABLES mybench "${MYBENCH_APP_NAME}")   # Start menu shortcut
    set(CPACK_CREATE_DESKTOP_LINKS mybench)
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN mybench.exe)
    set(CPACK_NSIS_BRANDING_TEXT "${MYBENCH_APP_NAME} ${PROJECT_VERSION}")
    # Driver step (only when wdi-simple.exe is bundled): ask, default No, skipped in silent mode.
    if(MYBENCH_WDI_SIMPLE)
        file(READ windows/nsis-driver.nsh CPACK_NSIS_EXTRA_INSTALL_COMMANDS)
    endif()
else()
    set(CPACK_GENERATOR DEB RPM)
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
    # Debian / Ubuntu. Qt >= 6.9: Ubuntu 25.10+, Debian 14 (forky).
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${MYBENCH_MAINTAINER}")
    set(CPACK_DEBIAN_PACKAGE_SECTION science)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # QML modules are plugins (not seen by dpkg-shlibdeps).
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-templates, qml6-module-qtquick-layouts, qml6-module-qtquick-window, qml6-module-qtquick-dialogs, qml6-module-qtquick-effects, qml6-module-qtquick-shapes, qml6-module-qtqml-models, qml6-module-qtqml-workerscript, qml6-module-qtcore, qt6-svg-plugins, qt6-qpa-plugins")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA ${CMAKE_CURRENT_SOURCE_DIR}/linux/postinst)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
    # Fedora / openSUSE. Qt >= 6.9: Fedora 42+.
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_PACKAGE_LICENSE "TBD")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
    set(CPACK_RPM_PACKAGE_URL "${MYBENCH_HOMEPAGE}")
    set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative >= 6.9, qt6-qtsvg >= 6.9")
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/linux/postinst)
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/lib/udev /usr/lib/udev/rules.d /usr/share/metainfo /usr/share/applications
        /usr/share/icons /usr/share/icons/hicolor /usr/share/mime /usr/share/mime/packages)
    foreach(size 16x16 22x22 24x24 32x32 48x48 64x64 128x128 256x256 512x512 scalable)
        list(APPEND CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
             /usr/share/icons/hicolor/${size} /usr/share/icons/hicolor/${size}/apps)
    endforeach()
endif()

include(CPack)
