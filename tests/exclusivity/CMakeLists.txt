# Cross-instrument resource exclusivity (docs/device.md: one AI task, one AO task; AI and audio
# inputs share the converters). Runs the real controllers against the simulator.
foreach(_id scope log dsa aeq bode gen)
    if(NOT TARGET mybench_instrument_${_id})
        return()
    endif()
endforeach()

add_executable(tst_exclusivity tst_exclusivity.cpp)
target_link_libraries(tst_exclusivity PRIVATE mybench_instrument_scope mybench_instrument_log
    mybench_instrument_dsa mybench_instrument_aeq mybench_instrument_bode mybench_instrument_gen Qt6::Test)
foreach(_id scope log dsa aeq bode gen)
    target_include_directories(tst_exclusivity PRIVATE ${PROJECT_SOURCE_DIR}/instruments/${_id})
endforeach()
if(MSVC)
    target_compile_options(tst_exclusivity PRIVATE /W4)
else()
    target_compile_options(tst_exclusivity PRIVATE -Wall -Wextra)
endif()
add_test(NAME exclusivity COMMAND tst_exclusivity)
set_tests_properties(exclusivity PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
