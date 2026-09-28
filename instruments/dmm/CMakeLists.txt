# MyBench.Instruments.Dmm — Multimeter (spec: docs/instruments/dmm.md, help: docs/help/dmm.md).
#
# Self-contained: every *.cpp/*.h and *.qml of this directory (and assets/*) is picked up
# automatically, so adding files needs no CMake change; QML files starting with
# "pragma Singleton" become singletons. Targets:
#   mybench_instrument_dmm (+ mybench_instrument_dmmplugin)  the QML module, linked by the app
#   mybench-dmm                                             dev executable: this panel alone
# See instruments/README.md.

set(instrument_id dmm)
set(instrument_name Dmm)
set(instrument_target mybench_instrument_${instrument_id})

file(GLOB instrument_sources CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.cpp *.h)
file(GLOB instrument_qml CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.qml)
file(GLOB_RECURSE instrument_assets CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} assets/*)
foreach(file IN LISTS instrument_qml)
    file(STRINGS ${file} singleton REGEX "^pragma[ \t]+Singleton")
    if(singleton)
        set_source_files_properties(${file} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    endif()
endforeach()

qt_add_library(${instrument_target} STATIC)
qt_add_qml_module(${instrument_target}
    URI MyBench.Instruments.${instrument_name}
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/MyBench/Instruments/${instrument_name}
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    DEPENDENCIES QtQuick MyBench.Framework MyBench.Controls
    SOURCES ${instrument_sources}
    QML_FILES ${instrument_qml}
    RESOURCES ${instrument_assets}
)
target_link_libraries(${instrument_target} PUBLIC mybench_ui)
target_include_directories(${instrument_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Continuity tone (1 kHz on the sound output). Optional: without Qt Multimedia the DMM is silent
# and keeps its visual continuity indicator.
find_package(Qt6 QUIET COMPONENTS Multimedia)
if(TARGET Qt6::Multimedia)
    target_link_libraries(${instrument_target} PRIVATE Qt6::Multimedia)
    target_compile_definitions(${instrument_target} PRIVATE MYBENCH_DMM_HAVE_AUDIO)
else()
    message(STATUS "DMM: Qt Multimedia not found, the continuity tone is disabled")
endif()
if(MSVC)
    target_compile_options(${instrument_target} PRIVATE /W4 /utf-8)
else()
    target_compile_options(${instrument_target} PRIVATE -Wall -Wextra)
endif()

# Dev executable: mybench-dmm [--tile] [--show-help] [--run] [--theme light]
#                 [--screenshot out.png --size 1280x800 --delay 1000]
qt_add_executable(mybench-${instrument_id} dev/main.cpp)
target_link_libraries(mybench-${instrument_id} PRIVATE ${instrument_target} ${instrument_target}plugin)
target_include_directories(mybench-${instrument_id} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Unit tests (logic tables, autoranging, formats, log file, controller against the simulator).
add_subdirectory(tests)
