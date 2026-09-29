# Digital I/O unit tests (ctest -R dio). They run against the simulator only (MYBENCH_NO_USB=1,
# offscreen platform: no window, no sound).
foreach(name diologic diocontroller)
    add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE mybench_instrument_dio Qt6::Test)
    if(MSVC)
        target_compile_options(tst_${name} PRIVATE /W4)
    else()
        target_compile_options(tst_${name} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME dio_${name} COMMAND tst_${name})
    set_tests_properties(dio_${name} PROPERTIES TIMEOUT 120
                         ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
endforeach()
