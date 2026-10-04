# QuickTest of the shell's QML (the MyBench.App module) with fake instruments.
qt_add_executable(tst_shell tst_shell.cpp)
target_link_libraries(tst_shell PRIVATE mybench_app mybench_appplugin mybench_ui Qt6::QuickTest Qt6::QuickControls2)
target_compile_definitions(tst_shell PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT MSVC)
    target_compile_options(tst_shell PRIVATE -Wall -Wextra)
endif()
add_test(NAME shell_qml COMMAND tst_shell -import ${CMAKE_BINARY_DIR}/qml)
set_tests_properties(shell_qml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_USB=1")
