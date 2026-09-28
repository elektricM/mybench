# mybench — the MYBENCH application (docs/UI.md): one window with the nav rail, top bar, Home
# dashboard, workspace (tabs, side-by-side split, pinned dashboard column), help drawer, toasts,
# status bar and command palette. Every instrument in MYBENCH_BUILT_INSTRUMENTS is linked and
# registered (instruments.cpp is generated from that list: no edit needed to add one).
#
# macOS: an application bundle named after MYBENCH_APP_NAME (e.g. MYBENCH.app).
#   mybench [--instrument <id>] [--split <id>] [--dashboard] [--show-help] [--run <ids|all>]
#           [--open file.mybench] [--theme light] [--screenshot out.png --size 1440x900 --delay 1500]

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

file(GLOB app_qml CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.qml)
qt_add_qml_module(mybench
    URI MyBench.App
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/MyBench/App
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    DEPENDENCIES QtQuick MyBench.Framework MyBench.Controls
    QML_FILES ${app_qml}
)
target_link_libraries(mybench PRIVATE mybench_ui)
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
