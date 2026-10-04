# MyBench.Controls — the QML design system (docs/UI.md v2): Theme, Section, FormRow, Inspector,
# SiField, StepSelector, SegmentedControl, Readout, StatusIndicator, ChannelBadge, tables, TrendView,
# Banner/Toast, BenchPlot (themed MyBench.Plot), and the shell building blocks shared by the app and
# the dev executables (InstrumentPanel, PaneHeader, InspectorDock, PanelRegistry, HelpView, DevShell…).
# Every *.qml of this directory is part of the module (glob).
#
# MyBench.Style (style/) is the Qt Quick Controls style that gives the standard controls
# (Button, ComboBox, CheckBox, Slider, SpinBox, TabBar, ToolTip, Menu, ...) the same look; the
# executables select it with QQuickStyle (mybench::runShell does).
#
# `mybench_ui` (INTERFACE) links every UI QML plugin; executables and instrument modules link it.

find_package(Qt6 REQUIRED COMPONENTS Gui Qml Quick QuickControls2)

qt_add_library(mybench_controls STATIC)

file(GLOB MYBENCH_CONTROLS_QML CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*.qml)
# QML files starting with "pragma Singleton" become singletons (Theme, ShellActions, PanelRegistry…):
# adding a file needs no CMake change.
foreach(file IN LISTS MYBENCH_CONTROLS_QML)
    file(STRINGS ${file} singleton REGEX "^pragma[ \t]+Singleton")
    if(singleton)
        set_source_files_properties(${file} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    endif()
endforeach()

# Our icons (assets/icons/*.svg) as qrc:/qt/qml/MyBench/Controls/icons/<name>.svg (Theme.icon(name)).
file(GLOB MYBENCH_ICONS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/assets/icons/*.svg)
foreach(icon IN LISTS MYBENCH_ICONS)
    get_filename_component(icon_name ${icon} NAME)
    set_source_files_properties(${icon} PROPERTIES QT_RESOURCE_ALIAS icons/${icon_name})
endforeach()

qt_add_qml_module(mybench_controls
    URI MyBench.Controls
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/MyBench/Controls
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    DEPENDENCIES QtQuick MyBench.Framework MyBench.Plot
    SOURCES fonts.h fonts.cpp wheelguard.h wheelguard.cpp
    QML_FILES ${MYBENCH_CONTROLS_QML}
    RESOURCES ${MYBENCH_ICONS}
)
target_link_libraries(mybench_controls PUBLIC mybench_framework mybench_plot Qt6::Gui Qt6::Quick)
if(MSVC)
    target_compile_options(mybench_controls PRIVATE /W4)
else()
    target_compile_options(mybench_controls PRIVATE -Wall -Wextra)
endif()

add_subdirectory(style)

add_library(mybench_ui INTERFACE)
target_link_libraries(mybench_ui INTERFACE
    mybench_framework mybench_frameworkplugin
    mybench_controls mybench_controlsplugin
    mybench_styleplugin
    mybench_plot mybench_plotplugin
    mybench_offscreen
    Qt6::QuickControls2
)

add_subdirectory(gallery)

if(BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    add_subdirectory(tests)
endif()
