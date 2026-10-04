# mybench — the MYBENCH application (docs/UI.md §7–§11): one window with the menu bar, the
# toolbar, the sidebar, one or two instrument panes or Overview, the inspector dock, the status
# bar, the command palette and the Preferences/About/Device Info/Simulator windows. Every
# instrument in MYBENCH_BUILT_INSTRUMENTS is linked and registered (instruments.cpp is generated
# from that list: no edit needed to add one).
#
# macOS: an application bundle named after MYBENCH_APP_NAME (e.g. MYBENCH.app).
#   mybench [--panel <id>] [--split <id>] [--page overview] [--sidebar full|compact|hidden]
#           [--inspector shown|hidden] [--run <ids|all>] [--open file.mybench] [--theme light]
#           [--screenshot out.png --size 1440x900 --delay 1500]   (mybench --help: all options)

qt_add_executable(mybench main.cpp ${CMAKE_CURRENT_BINARY_DIR}/instruments.cpp)

# Registration call list from the built instruments ("dmm" -> registerDmm(registry)).
set(MYBENCH_REGISTER_DECLS "")
set(MYBENCH_REGISTER_CALLS "")
foreach(id IN LISTS MYBENCH_BUILT_INSTRUMENTS)
    string(SUBSTRING ${id} 0 1 initial)
    string(TOUPPER ${initial} initial)
    string(SUBSTRING ${id} 1 -1 rest)
    set(name "${initial}${rest}")
    string(APPEND MYBENCH_REGISTER_DECLS "void register${name}(mybench::InstrumentRegistry& registry);\n")
    string(APPEND MYBENCH_REGISTER_CALLS "    mybench::instruments::register${name}(registry);\n")
    target_link_libraries(mybench PRIVATE mybench_instrument_${id} mybench_instrument_${id}plugin)
endforeach()
configure_file(instruments.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/instruments.cpp @ONLY)

# The shell's QML module MyBench.App (a static library, so tst_shell can load it too): the QML
# files of this directory and its C++ helpers (WindowChrome: the macOS window buttons and title;
# KeyText: shortcut texts).
file(GLOB app_qml CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.qml)
if(APPLE)
    set(app_window_chrome windowchrome.mm)
else()
    set(app_window_chrome windowchrome.cpp)
endif()
qt_add_library(mybench_app STATIC)
qt_add_qml_module(mybench_app
    URI MyBench.App
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/MyBench/App
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    DEPENDENCIES QtQuick MyBench.Framework MyBench.Controls
    QML_FILES ${app_qml}
    SOURCES windowchrome.h ${app_window_chrome} keytext.h keytext.cpp
)
target_link_libraries(mybench_app PUBLIC mybench_ui)
if(APPLE)
    target_link_libraries(mybench_app PRIVATE "-framework AppKit")
endif()
if(NOT MSVC)
    target_compile_options(mybench_app PRIVATE -Wall -Wextra)
endif()
target_link_libraries(mybench PRIVATE mybench_app mybench_appplugin mybench_ui)
# The SVG icons need Qt's svg image/icon plugins at runtime; linking Qt Svg makes the deploy
# tools (macdeployqt, windeployqt, linuxdeploy) ship them.
target_link_libraries(mybench PRIVATE Qt6::Svg)

set_target_properties(mybench PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "${MYBENCH_APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${MYBENCH_APP_ID}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
if(APPLE)
    set_target_properties(mybench PROPERTIES OUTPUT_NAME "${MYBENCH_APP_NAME}")
endif()
if(NOT MSVC)
    target_compile_options(mybench PRIVATE -Wall -Wextra)
endif()

add_subdirectory(tests)   # QuickTest of the shell QML (tst_shell)
