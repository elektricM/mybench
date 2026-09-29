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
set_tests_properties(offscreen_screenshot offscreen_window_root PROPERTIES TIMEOUT 60)

if(NOT MSVC)
    target_compile_options(mybench-offscreen-test PRIVATE -Wall -Wextra)
endif()
