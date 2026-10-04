# mybench-scopehal-bridge: the NI myDAQ as an oscilloscope in ngscopeclient (README.md).
# Not installed with the app (yet): the macOS bundle and the Windows installer would need
# QtNetwork deployed and the bundle re-signed for a second executable.

find_package(Qt6 QUIET COMPONENTS Network)
if(NOT TARGET Qt6::Network)
    message(WARNING "Qt6 Network not found: mybench-scopehal-bridge is not built")
    return()
endif()

add_library(mybench_scopehal_bridge STATIC
    src/acquisition.cpp
    src/acquisition.h
    src/bridge_server.cpp
    src/bridge_server.h
    src/protocol.cpp
    src/protocol.h
)
target_include_directories(mybench_scopehal_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mybench_scopehal_bridge PUBLIC mydaq_core Qt6::Core Qt6::Network)

add_executable(mybench-scopehal-bridge src/main.cpp)
target_link_libraries(mybench-scopehal-bridge PRIVATE mybench_scopehal_bridge)
target_compile_definitions(mybench-scopehal-bridge PRIVATE MYBENCH_BRIDGE_VERSION="${PROJECT_VERSION}")

foreach(_t mybench_scopehal_bridge mybench-scopehal-bridge)
    if(MSVC)
        target_compile_options(${_t} PRIVATE /W4)
    else()
        target_compile_options(${_t} PRIVATE -Wall -Wextra)
    endif()
endforeach()

add_subdirectory(tests)
