# Bridge tests. tst_scopehal_bridge starts the bridge executable and talks to it over TCP like
# ngscopeclient's "digilent" driver does (it parses the replies and frames on its own, not with
# the bridge's code). Simulator by default; with MYBENCH_BRIDGE_HW=1 against the real myDAQ.

add_executable(tst_scopehal_protocol tst_scopehal_protocol.cpp)
target_link_libraries(tst_scopehal_protocol PRIVATE mybench_scopehal_bridge Qt6::Test)

add_executable(tst_scopehal_bridge tst_scopehal_bridge.cpp)
target_link_libraries(tst_scopehal_bridge PRIVATE Qt6::Core Qt6::Network Qt6::Test)
target_compile_definitions(tst_scopehal_bridge PRIVATE
    MYBENCH_BRIDGE_EXECUTABLE="$<TARGET_FILE:mybench-scopehal-bridge>")
add_dependencies(tst_scopehal_bridge mybench-scopehal-bridge)

foreach(_t tst_scopehal_protocol tst_scopehal_bridge)
    if(MSVC)
        target_compile_options(${_t} PRIVATE /W4)
    else()
        target_compile_options(${_t} PRIVATE -Wall -Wextra)
    endif()
endforeach()

add_test(NAME scopehal_protocol COMMAND tst_scopehal_protocol)
add_test(NAME scopehal_bridge COMMAND tst_scopehal_bridge)
set_tests_properties(scopehal_protocol scopehal_bridge PROPERTIES
    TIMEOUT 180 LABELS scopehal ENVIRONMENT MYBENCH_NO_USB=1)

# Real myDAQ (AI only, nothing wired): opt-in with MYDAQ_HW_TESTS=1 ctest -L hw; holds the shared
# device through tools/devlock.sh for the run.
set(_devlock ${PROJECT_SOURCE_DIR}/tools/devlock.sh)
if(UNIX AND EXISTS ${_devlock})
    add_test(NAME scopehal_bridge_hw
        COMMAND sh -c "if [ \"$MYDAQ_HW_TESTS\" = 1 ]; then exec \"$0\" native scopehal -- \"$1\"; else echo 'hardware test skipped (set MYDAQ_HW_TESTS=1)'; exit 77; fi"
                ${_devlock} $<TARGET_FILE:tst_scopehal_bridge>)
    set_tests_properties(scopehal_bridge_hw PROPERTIES LABELS "hw" SKIP_RETURN_CODE 77 TIMEOUT 300
                         ENVIRONMENT MYBENCH_BRIDGE_HW=1)
endif()
