# Audio Equalizer tests: Bessel filter design, band analysis (pure) and the controller streaming
# against the simulator. Run: ctest --test-dir <build> -R aeq --output-on-failure
add_executable(tst_aeq tst_aeq.cpp)
target_link_libraries(tst_aeq PRIVATE mybench_instrument_aeq Qt6::Test)
target_include_directories(tst_aeq PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
    target_compile_options(tst_aeq PRIVATE /W4)
else()
    target_compile_options(tst_aeq PRIVATE -Wall -Wextra)
endif()
add_test(NAME aeq COMMAND tst_aeq)
set_tests_properties(aeq PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")

# Panel QML in an offscreen window: layout, routing kinds, faders as the only EQ editors.
add_executable(tst_aeq_qml tst_aeq_qml.cpp)
target_link_libraries(tst_aeq_qml PRIVATE mybench_instrument_aeq mybench_instrument_aeqplugin mybench_offscreen
                                         Qt6::Test Qt6::Quick)
target_include_directories(tst_aeq_qml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tst_aeq_qml PRIVATE MYBENCH_QML_DIR="${CMAKE_BINARY_DIR}/qml")
if(MSVC)
    target_compile_options(tst_aeq_qml PRIVATE /W4)
else()
    target_compile_options(tst_aeq_qml PRIVATE -Wall -Wextra)
endif()
# Static QML plugin + its backing library appear twice on the link line; Apple ld warns about that.
target_link_options(tst_aeq_qml PRIVATE $<$<CXX_COMPILER_ID:AppleClang>:LINKER:-no_warn_duplicate_libraries>)
add_test(NAME aeq_qml COMMAND tst_aeq_qml)
set_tests_properties(aeq_qml PROPERTIES TIMEOUT 120 LABELS aeq ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
