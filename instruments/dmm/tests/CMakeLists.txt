# Multimeter unit tests (ctest -R dmm). They run against the simulator only (MYBENCH_NO_USB=1,
# offscreen platform: no window, no sound).
foreach(name dmmlogic dmmcontroller)
    add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE mybench_instrument_dmm Qt6::Test)
    if(MSVC)
        target_compile_options(tst_${name} PRIVATE /W4)
    else()
        target_compile_options(tst_${name} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME dmm_${name} COMMAND tst_${name})
    set_tests_properties(dmm_${name} PROPERTIES TIMEOUT 120
                         ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
endforeach()

# The panel QML in an offscreen window: the trend's time-range selector vs the plot toolbar.
add_executable(tst_dmm_qml tst_dmm_qml.cpp)
target_link_libraries(tst_dmm_qml PRIVATE mybench_instrument_dmm mybench_instrument_dmmplugin mybench_offscreen
                                          Qt6::Test Qt6::Quick)
target_compile_definitions(tst_dmm_qml PRIVATE MYBENCH_QML_DIR="${CMAKE_BINARY_DIR}/qml")
if(MSVC)
    target_compile_options(tst_dmm_qml PRIVATE /W4)
else()
    target_compile_options(tst_dmm_qml PRIVATE -Wall -Wextra)
endif()
# Static QML plugin + its backing library appear twice on the link line; Apple ld warns about that.
target_link_options(tst_dmm_qml PRIVATE $<$<CXX_COMPILER_ID:AppleClang>:LINKER:-no_warn_duplicate_libraries>)
add_test(NAME dmm_qml COMMAND tst_dmm_qml)
set_tests_properties(dmm_qml PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
