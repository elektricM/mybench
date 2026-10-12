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

# Panel (pane-header bar, right column, no inspector) loads and runs without QML
# warnings (binding loops, undefined assignments, missing properties) in the dark and light themes.
foreach(theme dark light)
    add_test(NAME dio_qml_clean_${theme}
             COMMAND mybench-dio --screenshot ${CMAKE_CURRENT_BINARY_DIR}/qml_clean_${theme}.png
                     --size 1024x700 --theme ${theme} --run --delay 400)
    set_tests_properties(dio_qml_clean_${theme} PROPERTIES TIMEOUT 60
                         ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen"
                         FAIL_REGULAR_EXPRESSION "Unable to assign|TypeError|ReferenceError|is not defined|Binding loop|Cannot assign|Invalid property|non-existent property|qml: .*[Ee]rror|\\.qml:[0-9]+:[0-9]*:? ")
endforeach()

# The panel QML in an offscreen window: pattern picture buttons, settings per pattern, header radix.
add_executable(tst_dio_qml tst_dio_qml.cpp)
target_link_libraries(tst_dio_qml PRIVATE mybench_instrument_dio mybench_instrument_dioplugin mybench_offscreen
                                          Qt6::Test Qt6::Quick)
target_include_directories(tst_dio_qml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tst_dio_qml PRIVATE MYBENCH_QML_DIR="${CMAKE_BINARY_DIR}/qml")
if(MSVC)
    target_compile_options(tst_dio_qml PRIVATE /W4)
else()
    target_compile_options(tst_dio_qml PRIVATE -Wall -Wextra)
endif()
target_link_options(tst_dio_qml PRIVATE $<$<CXX_COMPILER_ID:AppleClang>:LINKER:-no_warn_duplicate_libraries>)
add_test(NAME dio_qml COMMAND tst_dio_qml)
set_tests_properties(dio_qml PROPERTIES TIMEOUT 120 ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
