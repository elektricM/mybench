# Oscilloscope unit tests: value tables, rate policy, trigger, measurements, formatting,
# autoscale, acquisition against the simulated myDAQ, settings persistence.
add_executable(tst_scope tst_scope.cpp)
target_link_libraries(tst_scope PRIVATE mybench_instrument_scope Qt6::Test)
target_include_directories(tst_scope PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
    target_compile_options(tst_scope PRIVATE /W4)
else()
    target_compile_options(tst_scope PRIVATE -Wall -Wextra)
endif()
add_test(NAME scope COMMAND tst_scope)
set_tests_properties(scope PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")

# The panel loads without QML binding warnings (e.g. "Unable to assign [undefined] to QColor").
add_test(NAME scope_qml_clean
         COMMAND mybench-scope --screenshot ${CMAKE_CURRENT_BINARY_DIR}/qml_clean.png --delay 300)
set_tests_properties(scope_qml_clean PROPERTIES TIMEOUT 60
                     ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen"
                     FAIL_REGULAR_EXPRESSION "Unable to assign|TypeError|ReferenceError|is not defined|Binding loop")

# The display QML in an offscreen window: FFT zoom persistence.
add_executable(tst_scope_qml tst_scope_qml.cpp)
target_link_libraries(tst_scope_qml PRIVATE mybench_instrument_scope mybench_instrument_scopeplugin mybench_offscreen
                                            Qt6::Test Qt6::Quick)
target_include_directories(tst_scope_qml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tst_scope_qml PRIVATE MYBENCH_QML_DIR="${CMAKE_BINARY_DIR}/qml")
if(MSVC)
    target_compile_options(tst_scope_qml PRIVATE /W4)
else()
    target_compile_options(tst_scope_qml PRIVATE -Wall -Wextra)
endif()
# Static QML plugin + its backing library appear twice on the link line; Apple ld warns about that.
target_link_options(tst_scope_qml PRIVATE $<$<CXX_COMPILER_ID:AppleClang>:LINKER:-no_warn_duplicate_libraries>)
add_test(NAME scope_qml COMMAND tst_scope_qml)
set_tests_properties(scope_qml PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
