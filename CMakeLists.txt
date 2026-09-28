cmake_minimum_required(VERSION 3.21)
project(MYBENCH VERSION 0.1.0 LANGUAGES C CXX)

set(MYBENCH_APP_NAME "MYBENCH" CACHE STRING "Application display name")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg Test QuickTest)
qt_standard_project_setup(REQUIRES 6.8)   # AUTOMOC, AUTORCC, QML module defaults

option(MYBENCH_WITH_USB "Build the native libusb backend" ON)
if(MYBENCH_WITH_USB)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBUSB IMPORTED_TARGET libusb-1.0)
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
