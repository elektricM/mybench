# Checks of the libsigrok driver ni-mydaq. The driver is not built here: apply.sh adds it to a
# libsigrok tree and installs that libsigrok (README.md). These tests run only when a sigrok-cli
# whose libsigrok has the driver is found; the hardware test (label "hw") also needs
# MYDAQ_HW_TESTS=1 and goes through tools/devlock.sh like the other hardware tests.

set(_sigrok_cli_default "")
if(EXISTS "$ENV{HOME}/projects/pulseview-pico/prefix/bin/sigrok-cli")
    set(_sigrok_cli_default "$ENV{HOME}/projects/pulseview-pico/prefix/bin/sigrok-cli")
endif()
set(MYBENCH_SIGROK_CLI "${_sigrok_cli_default}" CACHE FILEPATH
    "sigrok-cli whose libsigrok includes the ni-mydaq driver (integrations/sigrok/apply.sh)")

if(NOT MYBENCH_SIGROK_CLI OR NOT EXISTS "${MYBENCH_SIGROK_CLI}")
    message(STATUS "sigrok driver tests: off (no sigrok-cli; set MYBENCH_SIGROK_CLI)")
    return()
endif()
execute_process(COMMAND "${MYBENCH_SIGROK_CLI}" -L RESULT_VARIABLE _rc OUTPUT_VARIABLE _drivers
    ERROR_QUIET TIMEOUT 30)
if(NOT _rc EQUAL 0 OR NOT _drivers MATCHES "\n  ni-mydaq ")
    message(STATUS "sigrok driver tests: off (${MYBENCH_SIGROK_CLI} has no ni-mydaq driver; "
                   "run integrations/sigrok/apply.sh)")
    return()
endif()
message(STATUS "sigrok driver tests: ${MYBENCH_SIGROK_CLI}")

set(_check ${CMAKE_CURRENT_SOURCE_DIR}/capture_check.cmake)
set(_out ${CMAKE_CURRENT_BINARY_DIR})

# The driver is registered in libsigrok.
add_test(NAME sigrok_ni_mydaq_listed COMMAND "${MYBENCH_SIGROK_CLI}" -L)
set_tests_properties(sigrok_ni_mydaq_listed PROPERTIES LABELS sigrok
    PASS_REGULAR_EXPRESSION "\n  ni-mydaq +NI myDAQ\n")

# A scan never opens a device: clean exit and no driver message, with or without a myDAQ.
add_test(NAME sigrok_ni_mydaq_scan COMMAND "${MYBENCH_SIGROK_CLI}" --driver ni-mydaq --scan)
set_tests_properties(sigrok_ni_mydaq_scan PROPERTIES LABELS sigrok TIMEOUT 60
    FAIL_REGULAR_EXPRESSION "ni-mydaq:|libmydaq:|[Ee]rror|[Cc]ritical|[Ff]ailed")

# The capture checker itself, on libsigrok's demo device (no hardware).
add_test(NAME sigrok_capture_check_demo
    COMMAND ${CMAKE_COMMAND} -DSIGROK_CLI=${MYBENCH_SIGROK_CLI} -DDRIVER=demo -DCHANNELS=A0,A1
            -DSAMPLERATE=10k -DSAMPLES=1000 -DLIMITS=10.5 -DOUT=${_out}/demo.sr -P ${_check})
set_tests_properties(sigrok_capture_check_demo PROPERTIES LABELS sigrok TIMEOUT 120)

# Real myDAQ: skipped (exit 77) unless MYDAQ_HW_TESTS=1, never touches the device otherwise.
set(MYBENCH_SIGROK_DEVLOCK "${CMAKE_SOURCE_DIR}/tools/devlock.sh" CACHE FILEPATH
    "Script that lends the shared myDAQ to this host for one command")
if(UNIX AND EXISTS "${MYBENCH_SIGROK_DEVLOCK}")
    set(_gate "if [ \"$MYDAQ_HW_TESTS\" = 1 ]; then exec \"$0\" native sigrok -- \"$@\"; else echo 'hardware test skipped (set MYDAQ_HW_TESTS=1)'; exit 77; fi")
    # AI0 + AI1, ±10 V, 10 kS/s, 1000 samples (inputs floating: only the range is checked).
    add_test(NAME sigrok_ni_mydaq_hw
        COMMAND sh -c "${_gate}" "${MYBENCH_SIGROK_DEVLOCK}"
                ${CMAKE_COMMAND} -DSIGROK_CLI=${MYBENCH_SIGROK_CLI} -DDRIVER=ni-mydaq
                -DCHANNELS=AI0,AI1 -DSAMPLERATE=10k -DSAMPLES=1000 -DLIMITS=11
                -DOUT=${_out}/ni-mydaq-hw.sr -P ${_check})
    # AI1 on ±2 V (vdiv 400 mV), both channels at the 200 kS/s maximum.
    add_test(NAME sigrok_ni_mydaq_hw_range
        COMMAND sh -c "${_gate}" "${MYBENCH_SIGROK_DEVLOCK}"
                ${CMAKE_COMMAND} -DSIGROK_CLI=${MYBENCH_SIGROK_CLI} -DDRIVER=ni-mydaq
                -DCHANNELS=AI0,AI1 -DSAMPLERATE=200k -DSAMPLES=100000
                -DCONFIGS=channel_group=AI1:vdiv=400mV -DLIMITS=11,2.2
                -DOUT=${_out}/ni-mydaq-hw-range.sr -P ${_check})
    set_tests_properties(sigrok_ni_mydaq_hw sigrok_ni_mydaq_hw_range PROPERTIES
        LABELS "hw;sigrok" SKIP_RETURN_CODE 77 TIMEOUT 300 RUN_SERIAL TRUE)
endif()
