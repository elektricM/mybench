# MyBench.Plot unit tests (Qt Test). Run: ctest --test-dir <build> -R plot --output-on-failure
find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_plot tst_plot.cpp)
target_link_libraries(tst_plot PRIVATE mybench_plot mybench_plotplugin mybench_offscreen Qt6::Test Qt6::Quick)
if(MSVC)
    target_compile_options(tst_plot PRIVATE /W4 /utf-8)
else()
    target_compile_options(tst_plot PRIVATE -Wall -Wextra)
endif()
add_test(NAME plot COMMAND tst_plot)
# The render test uses the GPU through the offscreen platform (no window).
set_tests_properties(plot PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
