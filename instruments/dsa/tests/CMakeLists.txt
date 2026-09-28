# Unit tests of the Spectrum Analyzer: display conversions, measurements, peak search, the
# third-octave filter bank, and the controller against the simulated myDAQ (no hardware, no window).
find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_dsa tst_dsa.cpp)
target_link_libraries(tst_dsa PRIVATE mybench_instrument_dsa mybench_framework Qt6::Test)
target_include_directories(tst_dsa PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
    target_compile_options(tst_dsa PRIVATE /W4 /utf-8)
else()
    target_compile_options(tst_dsa PRIVATE -Wall -Wextra)
endif()
add_test(NAME dsa COMMAND tst_dsa)
set_tests_properties(dsa PROPERTIES TIMEOUT 120 LABELS dsa ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_USB=1")
