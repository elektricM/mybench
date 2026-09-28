# Data Logger tests: file writer, naming, autoranging (pure) and the controller recording
# against the simulator. Run: ctest --test-dir <build> -R log --output-on-failure
add_executable(tst_log tst_log.cpp)
target_link_libraries(tst_log PRIVATE mybench_instrument_log Qt6::Test)
target_include_directories(tst_log PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
    target_compile_options(tst_log PRIVATE /W4 /utf-8)
else()
    target_compile_options(tst_log PRIVATE -Wall -Wextra)
endif()
add_test(NAME log COMMAND tst_log)
set_tests_properties(log PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
