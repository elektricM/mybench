# Qt Test unit tests of the Waveform Generator. Run: ctest --test-dir <build> -R gen_
set(GEN_TESTS genexpression genwaveform genfileio genengine gencontroller)
foreach(name IN LISTS GEN_TESTS)
    add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE mybench_instrument_gen Qt6::Test)
    target_include_directories(tst_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    if(MSVC)
        target_compile_options(tst_${name} PRIVATE /W4)
    else()
        target_compile_options(tst_${name} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME gen_${name} COMMAND tst_${name})
    set_tests_properties(gen_${name} PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
endforeach()

# The panel loads without QML binding warnings (e.g. "Unable to assign [undefined] to QColor").
if(TARGET mybench-gen)
    add_test(NAME gen_qml_clean
             COMMAND mybench-gen --screenshot ${CMAKE_CURRENT_BINARY_DIR}/qml_clean.png --delay 300)
    set_tests_properties(gen_qml_clean PROPERTIES TIMEOUT 60
                         ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen"
                         FAIL_REGULAR_EXPRESSION "Unable to assign|TypeError|ReferenceError|is not defined|Binding loop")
endif()

# The round-2 panel layout and its picture buttons / tool palette in an offscreen window.
add_executable(tst_gen_qml tst_gen_qml.cpp)
target_link_libraries(tst_gen_qml PRIVATE mybench_instrument_gen mybench_instrument_genplugin mybench_offscreen
                                          Qt6::Test Qt6::Quick)
target_include_directories(tst_gen_qml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tst_gen_qml PRIVATE MYBENCH_QML_DIR="${CMAKE_BINARY_DIR}/qml")
if(MSVC)
    target_compile_options(tst_gen_qml PRIVATE /W4)
else()
    target_compile_options(tst_gen_qml PRIVATE -Wall -Wextra)
endif()
target_link_options(tst_gen_qml PRIVATE $<$<CXX_COMPILER_ID:AppleClang>:LINKER:-no_warn_duplicate_libraries>)
add_test(NAME gen_qml COMMAND tst_gen_qml)
set_tests_properties(gen_qml PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
