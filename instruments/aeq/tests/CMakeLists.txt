# Audio Equalizer tests: Bessel filter design, band analysis (pure) and the controller streaming
# against the simulator. Run: ctest --test-dir <build> -R aeq --output-on-failure
add_executable(tst_aeq tst_aeq.cpp)
target_link_libraries(tst_aeq PRIVATE mybench_instrument_aeq Qt6::Test)
target_include_directories(tst_aeq PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
    target_compile_options(tst_aeq PRIVATE /W4)
else()
    target_compile_options(tst_aeq PRIVATE -Wall -Wextra)
endif()
add_test(NAME aeq COMMAND tst_aeq)
set_tests_properties(aeq PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
