# Counter/Timer tests: timing rules, terminals, statistics (pure) and the controller running
# against the simulator. Run: ctest --test-dir <build> -R ctr --output-on-failure
add_executable(tst_ctr tst_ctr.cpp)
target_link_libraries(tst_ctr PRIVATE mybench_instrument_ctr Qt6::Test)
target_include_directories(tst_ctr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
    target_compile_options(tst_ctr PRIVATE /W4)
else()
    target_compile_options(tst_ctr PRIVATE -Wall -Wextra)
endif()
add_test(NAME ctr COMMAND tst_ctr)
set_tests_properties(ctr PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")

# Panel (pane-header bar, right column, no inspector) loads and runs without QML
# warnings (binding loops, undefined assignments, missing properties) in the dark and light themes.
foreach(theme dark light)
    add_test(NAME ctr_qml_clean_${theme}
             COMMAND mybench-ctr --screenshot ${CMAKE_CURRENT_BINARY_DIR}/qml_clean_${theme}.png
                     --size 1024x700 --theme ${theme} --run --delay 400)
    set_tests_properties(ctr_qml_clean_${theme} PROPERTIES TIMEOUT 60
                         ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen"
                         FAIL_REGULAR_EXPRESSION "Unable to assign|TypeError|ReferenceError|is not defined|Binding loop|Cannot assign|Invalid property|non-existent property|qml: .*[Ee]rror|\\.qml:[0-9]+:[0-9]*:? ")
endforeach()

# The panel QML in an offscreen window: mode picture buttons, cards per mode, FREQOUT knob.
add_executable(tst_ctr_qml tst_ctr_qml.cpp)
target_link_libraries(tst_ctr_qml PRIVATE mybench_instrument_ctr mybench_instrument_ctrplugin mybench_offscreen
                                          Qt6::Test Qt6::Quick)
target_include_directories(tst_ctr_qml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tst_ctr_qml PRIVATE MYBENCH_QML_DIR="${CMAKE_BINARY_DIR}/qml")
if(MSVC)
    target_compile_options(tst_ctr_qml PRIVATE /W4)
else()
    target_compile_options(tst_ctr_qml PRIVATE -Wall -Wextra)
endif()
target_link_options(tst_ctr_qml PRIVATE $<$<CXX_COMPILER_ID:AppleClang>:LINKER:-no_warn_duplicate_libraries>)
add_test(NAME ctr_qml COMMAND tst_ctr_qml)
set_tests_properties(ctr_qml PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
