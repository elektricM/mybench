# mybench_offscreen: run a QML root normally, or render it offscreen to a PNG (--screenshot).
# See README.md.

set(QT_NO_PRIVATE_MODULE_WARNING ON)   # QRhi is semi-public API (GuiPrivate)
find_package(Qt6 REQUIRED COMPONENTS Gui GuiPrivate Qml Quick QuickControls2)

add_library(mybench_offscreen STATIC
    include/mybench/offscreen.h
    src/offscreen.cpp
)
target_include_directories(mybench_offscreen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mybench_offscreen
    PUBLIC Qt6::Gui Qt6::Qml Qt6::Quick
    PRIVATE Qt6::GuiPrivate Qt6::QuickControls2   # GuiPrivate: <rhi/qrhi.h>
)
target_compile_features(mybench_offscreen PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(mybench_offscreen PRIVATE /W4 /utf-8)
else()
    target_compile_options(mybench_offscreen PRIVATE -Wall -Wextra)
endif()

# Self-test: a scene with text, Shapes, MultiEffect and a custom QSGGeometryNode.
#   mybench-offscreen-test --screenshot out.png          (manual check)
#   ctest -R offscreen                                   (pixel checks, runs offscreen)
option(MYBENCH_OFFSCREEN_TESTS "Build the offscreen helper self-test" ON)
if(MYBENCH_OFFSCREEN_TESTS)
    add_subdirectory(test)
endif()
