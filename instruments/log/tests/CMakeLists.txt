# Data Logger tests: file writer, naming, autoranging (pure) and the controller recording
# against the simulator. Run: ctest --test-dir <build> -R log --output-on-failure
add_executable(tst_log tst_log.cpp)
target_link_libraries(tst_log PRIVATE mybench_instrument_log Qt6::Test)
target_include_directories(tst_log PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
    target_compile_options(tst_log PRIVATE /W4)
else()
    target_compile_options(tst_log PRIVATE -Wall -Wextra)
endif()
add_test(NAME log COMMAND tst_log)
set_tests_properties(log PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")

# The panel QML in an offscreen window: layout at several sizes, Log File format choice.
add_executable(tst_log_qml tst_log_qml.cpp)
target_link_libraries(tst_log_qml PRIVATE mybench_instrument_log mybench_instrument_logplugin mybench_offscreen
                                          Qt6::Test Qt6::Quick)
target_include_directories(tst_log_qml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tst_log_qml PRIVATE MYBENCH_QML_DIR="${CMAKE_BINARY_DIR}/qml")
if(MSVC)
    target_compile_options(tst_log_qml PRIVATE /W4)
else()
    target_compile_options(tst_log_qml PRIVATE -Wall -Wextra)
endif()
# Static QML plugin + its backing library appear twice on the link line; Apple ld warns about that.
target_link_options(tst_log_qml PRIVATE $<$<CXX_COMPILER_ID:AppleClang>:LINKER:-no_warn_duplicate_libraries>)
add_test(NAME log_qml COMMAND tst_log_qml)
set_tests_properties(log_qml PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
