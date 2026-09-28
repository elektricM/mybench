# Qt Test unit tests for mydaq_core, one executable per area. Run: ctest --test-dir <build> --output-on-failure

set(MYDAQ_CORE_TESTS
    decimate
    device
    devicemanager
    digitaltimeline
    fft
    measure
    simcounter
    simdevice
    spectrum
    trigger
    units
    waveform
    window
)

foreach(name IN LISTS MYDAQ_CORE_TESTS)
    add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE mydaq_core Qt6::Test)
    if(MSVC)
        target_compile_options(tst_${name} PRIVATE /W4)
    else()
        target_compile_options(tst_${name} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${name} COMMAND tst_${name})
    # MYBENCH_NO_USB: unit tests never open the shared hardware (the USB provider stays idle).
    set_tests_properties(${name} PROPERTIES TIMEOUT 120 ENVIRONMENT MYBENCH_NO_USB=1)
endforeach()

# Native USB backend against libmydaq's mock transport (no hardware).
if(MYBENCH_WITH_USB AND TARGET mydaq_mock)
    add_executable(tst_usbdevice tst_usbdevice.cpp)
    target_link_libraries(tst_usbdevice PRIVATE mydaq_core mydaq_mock Qt6::Test)
    target_include_directories(tst_usbdevice PRIVATE ${PROJECT_SOURCE_DIR}/core/src)
    if(NOT MSVC)
        target_compile_options(tst_usbdevice PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME usbdevice COMMAND tst_usbdevice)
    set_tests_properties(usbdevice PROPERTIES TIMEOUT 120)

    # Real-hardware check through DeviceManager: opt-in (MYDAQ_HW_TESTS=1 ctest -L hw), run
    # through tools/devlock.sh, which lends the shared myDAQ to this host for one run.
    add_executable(usb_hw_check usb_hw_check.cpp)
    target_link_libraries(usb_hw_check PRIVATE mydaq_core)
    set(MYBENCH_DEVLOCK ${PROJECT_SOURCE_DIR}/tools/devlock.sh)
    if(UNIX AND EXISTS ${MYBENCH_DEVLOCK})
        add_test(NAME usb_hw
            COMMAND sh -c "if [ \"$MYDAQ_HW_TESTS\" = 1 ]; then exec \"$0\" native libmydaq -- \"$1\"; else echo 'hardware test skipped (set MYDAQ_HW_TESTS=1)'; exit 77; fi"
                    ${MYBENCH_DEVLOCK} $<TARGET_FILE:usb_hw_check>)
        set_tests_properties(usb_hw PROPERTIES LABELS "hw" SKIP_RETURN_CODE 77 TIMEOUT 300)
    endif()
endif()

# White-box test of a simulator internal.
target_include_directories(tst_digitaltimeline PRIVATE ${PROJECT_SOURCE_DIR}/core/src)
