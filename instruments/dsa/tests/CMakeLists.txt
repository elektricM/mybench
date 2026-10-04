# Unit tests of the Spectrum Analyzer: display conversions, measurements, peak search, the
# third-octave filter bank, and the controller against the simulated myDAQ (no hardware, no window).
find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_dsa tst_dsa.cpp)
target_link_libraries(tst_dsa PRIVATE mybench_instrument_dsa mybench_framework Qt6::Test)
target_include_directories(tst_dsa PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
    target_compile_options(tst_dsa PRIVATE /W4)
else()
    target_compile_options(tst_dsa PRIVATE -Wall -Wextra)
endif()
add_test(NAME dsa COMMAND tst_dsa)
set_tests_properties(dsa PROPERTIES TIMEOUT 120 LABELS dsa ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_USB=1")

# The panel QML in an offscreen window: no sentence-style display title or info line.
add_executable(tst_dsa_qml tst_dsa_qml.cpp)
target_link_libraries(tst_dsa_qml PRIVATE mybench_instrument_dsa mybench_instrument_dsaplugin mybench_offscreen
                                          Qt6::Test Qt6::Quick)
target_include_directories(tst_dsa_qml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tst_dsa_qml PRIVATE MYBENCH_QML_DIR="${CMAKE_BINARY_DIR}/qml")
if(MSVC)
    target_compile_options(tst_dsa_qml PRIVATE /W4)
else()
    target_compile_options(tst_dsa_qml PRIVATE -Wall -Wextra)
endif()
# Static QML plugin + its backing library appear twice on the link line; Apple ld warns about that.
target_link_options(tst_dsa_qml PRIVATE $<$<CXX_COMPILER_ID:AppleClang>:LINKER:-no_warn_duplicate_libraries>)
add_test(NAME dsa_qml COMMAND tst_dsa_qml)
set_tests_properties(dsa_qml PROPERTIES TIMEOUT 120 LABELS dsa ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
