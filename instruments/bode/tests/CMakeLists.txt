# Bode Analyzer tests, against the simulated myDAQ (no hardware, no window).
find_package(Qt6 REQUIRED COMPONENTS Test)

foreach(name bodemath bodesweep bodecontroller)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE mybench_instrument_bode mybench_framework Qt6::Test)
    target_include_directories(tst_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    if(MSVC)
        target_compile_options(tst_${name} PRIVATE /W4)
    else()
        target_compile_options(tst_${name} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${name} COMMAND tst_${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120 LABELS bode
                         ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_USB=1")
endforeach()

# The panel QML in an offscreen window: no sentence-style display title or info line.
add_executable(tst_bode_qml tst_bode_qml.cpp)
target_link_libraries(tst_bode_qml PRIVATE mybench_instrument_bode mybench_instrument_bodeplugin mybench_offscreen
                                          Qt6::Test Qt6::Quick)
target_include_directories(tst_bode_qml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tst_bode_qml PRIVATE MYBENCH_QML_DIR="${CMAKE_BINARY_DIR}/qml")
if(MSVC)
    target_compile_options(tst_bode_qml PRIVATE /W4)
else()
    target_compile_options(tst_bode_qml PRIVATE -Wall -Wextra)
endif()
# Static QML plugin + its backing library appear twice on the link line; Apple ld warns about that.
target_link_options(tst_bode_qml PRIVATE $<$<CXX_COMPILER_ID:AppleClang>:LINKER:-no_warn_duplicate_libraries>)
add_test(NAME bode_qml COMMAND tst_bode_qml)
set_tests_properties(bode_qml PROPERTIES TIMEOUT 120 LABELS bode ENVIRONMENT "MYBENCH_NO_USB=1;QT_QPA_PLATFORM=offscreen")
