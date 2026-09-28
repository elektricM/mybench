# libmydaq: native NI myDAQ driver library. Plain C11, depends only on libusb-1.0.
# Builds standalone (cmake -S libmydaq -B build) or as part of MYBENCH.
cmake_minimum_required(VERSION 3.16)
project(libmydaq VERSION 0.1.0 LANGUAGES C)

# Plain C: no Qt code generators even when built inside a Qt project.
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

option(LIBMYDAQ_BUILD_TESTS "Build the libmydaq unit tests (mock transport) and hw tests" ON)
option(LIBMYDAQ_BUILD_CLI "Build the mydaq-cli tool" ON)

if(NOT TARGET PkgConfig::LIBUSB)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBUSB IMPORTED_TARGET libusb-1.0)
    endif()
    if(NOT LIBUSB_FOUND)
        # No pkg-config (typical on Windows: vcpkg or the libusb binary release): find the header
        # and library directly (CMAKE_PREFIX_PATH / LIBUSB_ROOT), same target name as pkg-config.
        find_path(LIBUSB_INCLUDE_DIR libusb.h PATH_SUFFIXES libusb-1.0 include/libusb-1.0 include)
        find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0 PATH_SUFFIXES lib VS2022/MS64/dll MS64/dll)
        if(NOT LIBUSB_INCLUDE_DIR OR NOT LIBUSB_LIBRARY)
            message(FATAL_ERROR "libusb-1.0 not found (set CMAKE_PREFIX_PATH or LIBUSB_ROOT)")
        endif()
        add_library(PkgConfig::LIBUSB INTERFACE IMPORTED)
        set_target_properties(PkgConfig::LIBUSB PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${LIBUSB_LIBRARY}")
        message(STATUS "libusb-1.0: ${LIBUSB_LIBRARY}")
    endif()
endif()
find_package(Threads REQUIRED)

set(LIBMYDAQ_SOURCES
    src/ai.c
    src/ai_task.c
    src/ao.c
    src/ao_task.c
    src/counter.c
    src/calibration.c
    src/command.c
    src/context.c
    src/device.c
    src/dio.c
    src/dmm.c
    src/error.c
    src/es51981.c
    src/events.c
    src/frame.c
    src/usb_transport.c
)
if(WIN32)
    list(APPEND LIBMYDAQ_SOURCES src/platform_win32.c)
else()
    list(APPEND LIBMYDAQ_SOURCES src/platform_posix.c)
endif()

add_library(mydaq STATIC ${LIBMYDAQ_SOURCES})
target_include_directories(mydaq
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(mydaq PRIVATE PkgConfig::LIBUSB Threads::Threads)
if(UNIX)
    target_link_libraries(mydaq PRIVATE m)
endif()
set_target_properties(mydaq PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# Warnings as the project's quality bar; no FMA contraction (calibration must stay bit-exact).
set(LIBMYDAQ_C_FLAGS)
if(MSVC)
    list(APPEND LIBMYDAQ_C_FLAGS /W4 /fp:precise)
else()
    list(APPEND LIBMYDAQ_C_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes
         -Wmissing-prototypes -Wformat=2 -ffp-contract=off)
endif()
target_compile_options(mydaq PRIVATE ${LIBMYDAQ_C_FLAGS})
if(NOT MSVC)
    target_compile_options(mydaq PRIVATE -Wconversion -Wsign-conversion)   # library only
endif()

if(LIBMYDAQ_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()
if(LIBMYDAQ_BUILD_CLI AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cli/CMakeLists.txt)
    add_subdirectory(cli)
endif()
