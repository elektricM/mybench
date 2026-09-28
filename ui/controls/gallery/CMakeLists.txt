# mybench-controls-gallery: every MyBench.Controls component on one page (light/dark), with a
# small demo instrument (DMM readings from the simulator) to drive RunBar, Readout and toasts.
#   mybench-controls-gallery [--theme light] [--screenshot gallery.png --size 1440x1000]

qt_add_executable(mybench-controls-gallery main.cpp democontroller.h democontroller.cpp)
qt_add_qml_module(mybench-controls-gallery
    URI MyBench.Gallery
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/MyBench/Gallery
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    DEPENDENCIES QtQuick MyBench.Framework MyBench.Controls
    QML_FILES Gallery.qml
)
target_link_libraries(mybench-controls-gallery PRIVATE mybench_ui)
target_include_directories(mybench-controls-gallery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(mybench-controls-gallery PROPERTIES MACOSX_BUNDLE FALSE WIN32_EXECUTABLE TRUE)
if(NOT MSVC)
    target_compile_options(mybench-controls-gallery PRIVATE -Wall -Wextra)
endif()
