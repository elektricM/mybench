# MyBench.Plot: GPU scene-graph plotting (static QML module). See README.md.
#
# Consumers link `mybench_plot` (C++ API: TraceBuffer, LogicBuffer, WaterfallBuffer, ...) and
# `mybench_plotplugin` (registers the QML module; its static initializer is linked automatically).

set(QT_NO_PRIVATE_MODULE_WARNING ON)   # <rhi/qrhi.h> (waterfall texture uploads)
find_package(Qt6 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick ShaderTools)

qt_add_library(mybench_plot STATIC)
qt_add_qml_module(mybench_plot
    URI MyBench.Plot
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/MyBench/Plot
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    DEPENDENCIES QtQuick
    SOURCES
        include/mybench/plot/decimation.h
        include/mybench/plot/logicbuffer.h
        include/mybench/plot/plot.h
        include/mybench/plot/plotaxis.h
        include/mybench/plot/plotelements.h
        include/mybench/plot/scale.h
        include/mybench/plot/tracebuffer.h
        include/mybench/plot/waterfall.h
        src/decimation.cpp
        src/logicbuffer.cpp
        src/plot.cpp
        src/plot_p.h
        src/plotaxis.cpp
        src/plotelements.cpp
        src/plotmaterials.cpp
        src/plotmaterials.h
        src/plotrender.cpp
        src/scale.cpp
        src/tracebuffer.cpp
        src/waterfall.cpp
)

qt_add_shaders(mybench_plot "mybench_plot_shaders"
    BATCHABLE
    PRECOMPILE
    OPTIMIZED
    PREFIX "/mybench/plot"
    FILES
        shaders/line.vert
        shaders/line.frag
        shaders/shape.vert
        shaders/shape.frag
)

target_include_directories(mybench_plot
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/mybench/plot ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(mybench_plot PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick PRIVATE Qt6::GuiPrivate)
target_compile_features(mybench_plot PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(mybench_plot PRIVATE /W4 /utf-8)
else()
    target_compile_options(mybench_plot PRIVATE -Wall -Wextra)
endif()

option(MYBENCH_PLOT_TESTS "Build the MyBench.Plot unit tests" ON)
if(MYBENCH_PLOT_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()
option(MYBENCH_PLOT_GALLERY "Build mybench-plot-gallery" ON)
if(MYBENCH_PLOT_GALLERY AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gallery/CMakeLists.txt)
    add_subdirectory(gallery)
endif()
