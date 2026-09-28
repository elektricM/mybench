# libmydaq tests. Unit/replay/emulator tests run without hardware (mock transport).
# Hardware tests (label "hw") are opt-in: MYDAQ_HW_TESTS=1 ctest -L hw  (they go through
# tools/devlock.sh, which lends the shared myDAQ to this Mac for the duration of one run).

# Mock transport, also used by the C++ backend tests (tests/tst_usbdevice.cpp).
add_library(mydaq_mock STATIC mock/mock.c mock/mydaq_mock.h)
target_include_directories(mydaq_mock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mydaq_mock PUBLIC mydaq)
set_target_properties(mydaq_mock PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_options(mydaq_mock PRIVATE ${LIBMYDAQ_C_FLAGS})
target_compile_definitions(mydaq_mock PUBLIC
    MYDAQ_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

add_executable(libmydaq_tests test_main.c test_units.c test_replay.c test_emulator.c test.h)
target_include_directories(libmydaq_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(libmydaq_tests PRIVATE mydaq_mock mydaq Threads::Threads)
if(UNIX)
    target_link_libraries(libmydaq_tests PRIVATE m)
endif()
set_target_properties(libmydaq_tests PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_options(libmydaq_tests PRIVATE ${LIBMYDAQ_C_FLAGS})

foreach(group unit replay emu)
    add_test(NAME libmydaq_${group} COMMAND libmydaq_tests ${group}.)
    set_tests_properties(libmydaq_${group} PROPERTIES TIMEOUT 120 LABELS "libmydaq")
endforeach()

# Hardware test: one short device hold through the lock script (UNIX dev bench only).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/hw_test.c)
add_executable(libmydaq_hwtest hw_test.c)
target_include_directories(libmydaq_hwtest PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(libmydaq_hwtest PRIVATE mydaq Threads::Threads)
if(UNIX)
    target_link_libraries(libmydaq_hwtest PRIVATE m)
endif()
set_target_properties(libmydaq_hwtest PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_options(libmydaq_hwtest PRIVATE ${LIBMYDAQ_C_FLAGS})
target_compile_definitions(libmydaq_hwtest PRIVATE
    MYDAQ_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

set(LIBMYDAQ_DEVLOCK "${PROJECT_SOURCE_DIR}/../tools/devlock.sh" CACHE FILEPATH
    "Script that lends the shared myDAQ to this host for one command")
if(UNIX AND EXISTS ${LIBMYDAQ_DEVLOCK})
    # Skipped (exit 77) unless MYDAQ_HW_TESTS=1: never touch the device from a plain ctest run.
    add_test(NAME libmydaq_hw
        COMMAND sh -c "if [ \"$MYDAQ_HW_TESTS\" = 1 ]; then exec \"$0\" native libmydaq -- \"$1\"; else echo 'hardware test skipped (set MYDAQ_HW_TESTS=1)'; exit 77; fi"
                ${LIBMYDAQ_DEVLOCK} $<TARGET_FILE:libmydaq_hwtest>)
    set_tests_properties(libmydaq_hw PROPERTIES LABELS "hw" SKIP_RETURN_CODE 77 TIMEOUT 300)
endif()
endif()
