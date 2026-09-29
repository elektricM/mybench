# App-level hardware tests: the instrument controllers run headless on the real myDAQ through the
# USB backend (ctest label "hw"). Skipped (exit 77) unless MYDAQ_HW_TESTS=1; each test holds the
# shared device only for its own run (tools/devlock.sh native hwtests). Jumper tests need
# MYDAQ_JUMPERS=ao0-ai0,dio4-dio1. To develop a test against the simulator, run its executable with
# MYBENCH_HW_DEVICE=sim:0 (hardware facts are skipped there).
if(NOT TARGET mybench_framework)
    return()
endif()

add_library(mybench_hwfixture STATIC hwfixture.cpp hwfixture.h)
target_link_libraries(mybench_hwfixture PUBLIC mybench_framework mydaq_core Qt6::Test)
target_include_directories(mybench_hwfixture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT MSVC)
    target_compile_options(mybench_hwfixture PRIVATE -Wall -Wextra)
endif()

set(MYBENCH_HW_DEVLOCK ${PROJECT_SOURCE_DIR}/tools/devlock.sh)

# mybench_hw_test(<name> <instrument>...): tst_hw_<name>.cpp against the given instruments.
function(mybench_hw_test name)
    foreach(instrument IN LISTS ARGN)
        if(NOT TARGET mybench_instrument_${instrument})
            return()
        endif()
    endforeach()
    add_executable(tst_hw_${name} tst_hw_${name}.cpp)
    target_link_libraries(tst_hw_${name} PRIVATE mybench_hwfixture)
    foreach(instrument IN LISTS ARGN)
        target_link_libraries(tst_hw_${name} PRIVATE mybench_instrument_${instrument})
        target_include_directories(tst_hw_${name} PRIVATE ${PROJECT_SOURCE_DIR}/instruments/${instrument})
    endforeach()
    if(MSVC)
        target_compile_options(tst_hw_${name} PRIVATE /W4)
    else()
        target_compile_options(tst_hw_${name} PRIVATE -Wall -Wextra)
    endif()
    if(UNIX AND EXISTS ${MYBENCH_HW_DEVLOCK})
        add_test(NAME hw_${name}
            COMMAND sh -c "if [ \"$MYDAQ_HW_TESTS\" = 1 ]; then exec \"$0\" native hwtests -- \"$1\"; else echo 'hardware test skipped (set MYDAQ_HW_TESTS=1)'; exit 77; fi"
                    ${MYBENCH_HW_DEVLOCK} $<TARGET_FILE:tst_hw_${name}>)
        set_tests_properties(hw_${name} PROPERTIES LABELS "hw" SKIP_RETURN_CODE 77 TIMEOUT 600
                             ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_SIM=1")
    endif()
endfunction()

mybench_hw_test(device)
mybench_hw_test(dmm dmm)
mybench_hw_test(scope scope gen)
mybench_hw_test(gen gen)
mybench_hw_test(dio dio)
mybench_hw_test(ctr ctr)
mybench_hw_test(bode bode)
mybench_hw_test(dsa dsa)
mybench_hw_test(log log)
mybench_hw_test(aeq aeq)
mybench_hw_test(hotplug scope)
