# Bode Analyzer tests, against the simulated myDAQ (no hardware, no window).
find_package(Qt6 REQUIRED COMPONENTS Test)

foreach(name bodemath bodesweep bodecontroller)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE mybench_instrument_bode mybench_framework Qt6::Test)
    target_include_directories(tst_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    if(MSVC)
        target_compile_options(tst_${name} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(tst_${name} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${name} COMMAND tst_${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120 LABELS bode
                         ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_USB=1")
endforeach()
