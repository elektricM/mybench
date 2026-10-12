# MyBench.Framework — C++ services shared by the shell and every instrument (docs/UI.md):
# DeviceHub, InstrumentController (+ worker), ResourceArbiter, InstrumentRegistry, SessionManager,
# DataExport, Units, Help, Notifications, EventLog, AppInfo, Launcher and the executable bootstrap
# (shell.h).
#
# Static library `mybench_framework` (C++ API, headers in include/mybench/) with the QML plugin
# `mybench_frameworkplugin`. Linking the plugin target registers the QML module automatically.

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

qt_add_library(mybench_framework STATIC)

# Project web site (About, Home > Resources, "Find more online"); packaging/ uses the same cache entry.
set(MYBENCH_HOMEPAGE "https://github.com/elektricM/mybench" CACHE STRING "Project homepage")

# End-user help pages (docs/help/<id>.md), embedded as :/mybench/help/<id>.md.
file(GLOB MYBENCH_HELP_PAGES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/docs/help/*.md)

qt_add_qml_module(mybench_framework
    URI MyBench.Framework
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/MyBench/Framework
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    DEPENDENCIES QtQuick
    SOURCES
        include/mybench/appinfo.h           src/appinfo.cpp
        include/mybench/dataexport.h        src/dataexport.cpp
        include/mybench/devicehub.h         src/devicehub.cpp
        include/mybench/eventlog.h          src/eventlog.cpp
        include/mybench/help.h              src/help.cpp
        include/mybench/instrumentcontroller.h src/instrumentcontroller.cpp
        include/mybench/instrumentregistry.h   src/instrumentregistry.cpp
        include/mybench/launcher.h          src/launcher.cpp
        include/mybench/notifications.h     src/notifications.cpp
        include/mybench/resourcearbiter.h   src/resourcearbiter.cpp
        include/mybench/sessionmanager.h    src/sessionmanager.cpp
        include/mybench/settingsjson.h      src/settingsjson.cpp
        include/mybench/shell.h             src/shell.cpp
        include/mybench/units.h             src/units.cpp
        include/mybench/worker.h            src/worker.cpp
        include/mybench/framework.h
)

if(MYBENCH_HELP_PAGES)
    qt_add_resources(mybench_framework "mybench_help"
        PREFIX /mybench/help
        BASE ${PROJECT_SOURCE_DIR}/docs/help
        FILES ${MYBENCH_HELP_PAGES}
    )
endif()

# The license shown by About > License.
set(MYBENCH_LICENSE_FILE ${PROJECT_SOURCE_DIR}/LICENSE)
set_source_files_properties(${MYBENCH_LICENSE_FILE} PROPERTIES QT_RESOURCE_ALIAS LICENSE)
qt_add_resources(mybench_framework "mybench_license"
    PREFIX /mybench
    FILES ${MYBENCH_LICENSE_FILE}
)

target_include_directories(mybench_framework
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/mybench   # qmltyperegistrar includes by file name
)
target_link_libraries(mybench_framework
    PUBLIC mydaq_core Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
    PRIVATE Qt6::QuickControls2 mybench_offscreen
)
target_compile_definitions(mybench_framework PRIVATE
    MYBENCH_APP_NAME="${MYBENCH_APP_NAME}"
    MYBENCH_VERSION="${PROJECT_VERSION}"
    MYBENCH_HOMEPAGE="${MYBENCH_HOMEPAGE}"
    MYBENCH_WITH_USB=$<BOOL:${MYBENCH_WITH_USB}>
)
target_compile_features(mybench_framework PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(mybench_framework PRIVATE /W4)
else()
    target_compile_options(mybench_framework PRIVATE -Wall -Wextra)
endif()

# Unit tests of the framework (Qt Test, run with ctest -R framework).
if(BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Direct printing (Print dialog equivalent: printer, copies, orientation) when Qt Print Support is
# available; without it the Print destination is hidden and PDF/PNG export still work.
option(MYBENCH_WITH_PRINT "Direct printing through Qt Print Support (pulls in Qt Widgets)" ON)
if(MYBENCH_WITH_PRINT)
    find_package(Qt6 QUIET COMPONENTS PrintSupport)
endif()
if(MYBENCH_WITH_PRINT AND TARGET Qt6::PrintSupport)
    target_link_libraries(mybench_framework PRIVATE Qt6::PrintSupport)
    target_compile_definitions(mybench_framework PRIVATE MYBENCH_HAVE_PRINT=1)
endif()
