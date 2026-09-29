# MyBench.Instruments.Scope — Oscilloscope (spec: docs/instruments/scope.md, help: docs/help/scope.md).
#
# Self-contained: every *.cpp/*.h and *.qml of this directory (and assets/*) is picked up
# automatically, so adding files needs no CMake change; QML files starting with
# "pragma Singleton" become singletons. Targets:
#   mybench_instrument_scope (+ mybench_instrument_scopeplugin)  the QML module, linked by the app
#   mybench-scope                                             dev executable: this panel alone
# See instruments/README.md.

set(instrument_id scope)
set(instrument_name Scope)
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
target_include_directories(${instrument_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(${instrument_target} PRIVATE /W4)
else()
    target_compile_options(${instrument_target} PRIVATE -Wall -Wextra)
endif()

# Dev executable: mybench-scope [--tile] [--show-help] [--run] [--theme light]
#                 [--screenshot out.png --size 1280x800 --delay 1000]
qt_add_executable(mybench-${instrument_id} dev/main.cpp)
target_link_libraries(mybench-${instrument_id} PRIVATE ${instrument_target} ${instrument_target}plugin)
target_include_directories(mybench-${instrument_id} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Unit tests (algorithms against the simulator, controller settings): ctest -R scope
if(BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    add_subdirectory(tests)
endif()
