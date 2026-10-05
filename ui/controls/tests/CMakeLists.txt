# QML tests of MyBench.Controls (Qt Quick Test, offscreen, simulated device): tst_*.qml in this
# directory. Also loads app/WorkspaceState.qml by file URL (plain QML) for the workspace logic.
find_package(Qt6 REQUIRED COMPONENTS QuickTest QuickControls2)

qt_add_executable(tst_controls_qml main.cpp)
target_link_libraries(tst_controls_qml PRIVATE mybench_ui Qt6::QuickTest Qt6::QuickControls2)
target_compile_definitions(tst_controls_qml PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT MSVC)
    target_compile_options(tst_controls_qml PRIVATE -Wall -Wextra)
endif()
add_test(NAME controls_qml COMMAND tst_controls_qml -input ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(controls_qml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_USB=1")
# Theme metrics under the native macOS style (controls follow its 24 px control height).
if(APPLE)
    add_test(NAME controls_qml_macos_metrics
             COMMAND tst_controls_qml -input ${CMAKE_CURRENT_SOURCE_DIR}/tst_nativemetrics.qml)
    set_tests_properties(controls_qml_macos_metrics PROPERTIES
                         ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_USB=1;MYBENCH_TEST_STYLE=macOS")
endif()
