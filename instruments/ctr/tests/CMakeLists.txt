# Counter/Timer tests: timing rules, terminals, statistics (pure) and the controller running
# against the simulator. Run: ctest --test-dir <build> -R ctr --output-on-failure
add_executable(tst_ctr tst_ctr.cpp)
target_link_libraries(tst_ctr PRIVATE mybench_instrument_ctr Qt6::Test)
target_include_directories(tst_ctr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
    target_compile_options(tst_ctr PRIVATE /W4 /utf-8)
else()
    target_compile_options(tst_ctr PRIVATE -Wall -Wextra)
endif()
add_test(NAME ctr COMMAND tst_ctr)
set_tests_properties(ctr PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
