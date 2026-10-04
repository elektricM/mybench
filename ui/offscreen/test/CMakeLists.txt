find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)   # QtQuick.Shapes/Effects are QML imports only

qt_add_executable(mybench-offscreen-test main.cpp)
qt_add_qml_module(mybench-offscreen-test
    URI MyBench.OffscreenTest
    VERSION 1.0
    QML_FILES Scene.qml WindowScene.qml
    SOURCES testgeometryitem.h testgeometryitem.cpp
)
target_link_libraries(mybench-offscreen-test PRIVATE mybench_offscreen Qt6::Quick)

# Pixel test: renders Scene.qml with OffscreenWindow and checks colors of each element.
add_test(NAME offscreen_screenshot
    COMMAND mybench-offscreen-test --screenshot ${CMAKE_CURRENT_BINARY_DIR}/offscreen-test.png
            --delay 200 --size 640x400 --self-check)
add_test(NAME offscreen_window_root
    COMMAND mybench-offscreen-test --window --screenshot ${CMAKE_CURRENT_BINARY_DIR}/offscreen-window.png
            --delay 100 --theme light --dpr 2)
# Style resolution end to end: "native" renders with Fusion offscreen, "mybench" asks for
# MyBench.Style (not linked here, so only the selection is checked with a Basic-only scene).
add_test(NAME offscreen_style_native
    COMMAND mybench-offscreen-test --screenshot ${CMAKE_CURRENT_BINARY_DIR}/offscreen-style.png
            --delay 50 --size 320x200 --style native --expect-style Fusion)
set_tests_properties(offscreen_screenshot offscreen_window_root offscreen_style_native PROPERTIES TIMEOUT 60)

# Unit test of the launch options (--style, --grab) and of the style resolution.
find_package(Qt6 REQUIRED COMPONENTS Test)
qt_add_executable(tst_launchoptions tst_launchoptions.cpp)
target_link_libraries(tst_launchoptions PRIVATE mybench_offscreen Qt6::Test)
add_test(NAME offscreen_launchoptions COMMAND tst_launchoptions)
set_tests_properties(offscreen_launchoptions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(NOT MSVC)
    target_compile_options(tst_launchoptions PRIVATE -Wall -Wextra)
endif()

if(NOT MSVC)
    target_compile_options(mybench-offscreen-test PRIVATE -Wall -Wextra)
endif()
