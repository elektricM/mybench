# mybench-plot-gallery: demo pages for MyBench.Plot (uses mybench_offscreen for --screenshot).
find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)

qt_add_executable(mybench-plot-gallery main.cpp)
qt_add_qml_module(mybench-plot-gallery
    URI MyBench.Plot.Gallery
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/MyBench/Plot/Gallery
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    DEPENDENCIES QtQuick MyBench.Plot
    SOURCES gallerydata.h gallerydata.cpp
    QML_FILES
        Main.qml
        GalleryTheme.qml
        Chip.qml
        PageHeader.qml
        Card.qml
        Readout.qml
        ScopePage.qml
        DeepPage.qml
        BodePage.qml
        SpectrumPage.qml
        WaterfallPage.qml
        TrendPage.qml
        XYPage.qml
        RecorderPage.qml
        LogicPage.qml
        StylesPage.qml
)
target_link_libraries(mybench-plot-gallery PRIVATE
    mybench_plot mybench_plotplugin mybench_offscreen mydaq_core Qt6::Quick Qt6::QuickControls2)
if(MSVC)
    target_compile_options(mybench-plot-gallery PRIVATE /W4 /utf-8)
else()
    target_compile_options(mybench-plot-gallery PRIVATE -Wall -Wextra)
endif()
