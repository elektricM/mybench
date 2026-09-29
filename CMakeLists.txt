cmake_minimum_required(VERSION 3.21)
project(MYBENCH VERSION 0.1.0 LANGUAGES C CXX)

set(MYBENCH_APP_NAME "MYBENCH" CACHE STRING "Application display name")
# Reverse-DNS id: macOS bundle identifier, Linux desktop-file / AppStream id (packaging/).
set(MYBENCH_APP_ID "org.mybench.${MYBENCH_APP_NAME}" CACHE STRING "Application id (reverse DNS)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg Test QuickTest)
qt_standard_project_setup(REQUIRES 6.9)   # AUTOMOC, AUTORCC, QML module defaults
# Optional (DMM continuity tone); found here so the app target, which links it through the DMM
# module, sees Qt6::Multimedia at finalization and deployment.
find_package(Qt6 QUIET COMPONENTS Multimedia)

option(MYBENCH_WITH_USB "Build the native libusb backend" ON)
if(MYBENCH_WITH_USB)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBUSB IMPORTED_TARGET libusb-1.0)
    endif()
    if(NOT LIBUSB_FOUND)
        # No pkg-config (typical on Windows, e.g. libusb from vcpkg or the libusb binary release):
        # look for the header and library directly (CMAKE_PREFIX_PATH / LIBUSB_ROOT) and provide
        # the same PkgConfig::LIBUSB target that core/ and libmydaq/ link.
        find_path(LIBUSB_INCLUDE_DIR libusb.h PATH_SUFFIXES libusb-1.0 include/libusb-1.0 include)
        find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0 PATH_SUFFIXES lib VS2022/MS64/dll MS64/dll)
        if(LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARY)
            add_library(PkgConfig::LIBUSB INTERFACE IMPORTED)
            set_target_properties(PkgConfig::LIBUSB PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB_INCLUDE_DIR}"
                INTERFACE_LINK_LIBRARIES "${LIBUSB_LIBRARY}")
            set(LIBUSB_FOUND TRUE)
            message(STATUS "libusb-1.0: ${LIBUSB_LIBRARY}")
        endif()
    endif()
    if(NOT LIBUSB_FOUND)
        message(WARNING "libusb-1.0 not found: building with the simulated device only")
        set(MYBENCH_WITH_USB OFF)
    endif()
endif()

enable_testing()

# Plain-C protocol library (libusb only), used by core's USB backend and reusable by other projects.
if(MYBENCH_WITH_USB AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libmydaq/CMakeLists.txt)
    add_subdirectory(libmydaq)
endif()
add_subdirectory(core)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ui/CMakeLists.txt)
    add_subdirectory(ui)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/instruments/CMakeLists.txt)
    add_subdirectory(instruments)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt)
    add_subdirectory(app)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()

# Test executables are console applications on Windows (Qt's qt_add_executable makes GUI-subsystem
# ones), so QTest output reaches ctest and the CI log.
function(mybench_console_tests dir)
    get_property(_targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "EXECUTABLE" AND (_t MATCHES "^tst_" OR _t STREQUAL "mybench-offscreen-test"))
            set_target_properties(${_t} PROPERTIES WIN32_EXECUTABLE FALSE)
        endif()
    endforeach()
    get_property(_subdirs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
    foreach(_d IN LISTS _subdirs)
        mybench_console_tests(${_d})
    endforeach()
endfunction()
mybench_console_tests(${CMAKE_CURRENT_SOURCE_DIR})

# App icon, install rules, Qt runtime deployment and installers (CPack): see docs/BUILDING.md.
option(MYBENCH_PACKAGING "App icon, install rules and CPack packaging" ON)
if(MYBENCH_PACKAGING AND TARGET mybench AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/CMakeLists.txt)
    add_subdirectory(packaging)
endif()
