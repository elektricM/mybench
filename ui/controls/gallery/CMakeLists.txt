# mybench-controls-gallery: the living specification of the design system (docs/UI.md v2), one
# page per family, light and dark, with a small demo instrument (DMM readings from the simulator)
# for live readouts and toasts. The page is chosen by a positional argument:
#   mybench-controls-gallery [foundations|buttons|forms|readouts|indicators|plots|instrument]
#                            [--theme light] [--screenshot gallery.png --size 1440x900]

qt_add_executable(mybench-controls-gallery main.cpp democontroller.h democontroller.cpp)
file(GLOB MYBENCH_GALLERY_QML CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*.qml)
qt_add_qml_module(mybench-controls-gallery
    URI MyBench.Gallery
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/MyBench/Gallery
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    DEPENDENCIES QtQuick MyBench.Framework MyBench.Controls
    QML_FILES ${MYBENCH_GALLERY_QML}
)
target_link_libraries(mybench-controls-gallery PRIVATE mybench_ui)
target_include_directories(mybench-controls-gallery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(mybench-controls-gallery PROPERTIES MACOSX_BUNDLE FALSE WIN32_EXECUTABLE TRUE)
if(NOT MSVC)
    target_compile_options(mybench-controls-gallery PRIVATE -Wall -Wextra)
endif()

# Native look: every gallery page under the macOS style must load without "The current
# style does not support customization" (a component that overrides background/contentItem/
# indicator of a natively drawn control). Offscreen still evaluates the style, so it runs in CI.
if(APPLE)
    foreach(page foundations buttons forms readouts indicators plots instrument)
        add_test(NAME controls_gallery_macos_style_${page}
            COMMAND mybench-controls-gallery ${page} --style macOS --delay 200 --size 1440x1000
                    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/gallery-macos-style-${page}.png)
        set_tests_properties(controls_gallery_macos_style_${page} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_USB=1"
            FAIL_REGULAR_EXPRESSION "does not support customization"
            TIMEOUT 60)
    endforeach()
endif()
