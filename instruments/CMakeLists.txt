# One QML module per instrument. Build a subset with -DMYBENCH_INSTRUMENTS="scope;dmm"
# (useful while other instruments are mid-change); the app registers whatever is built.
set(MYBENCH_INSTRUMENTS "all" CACHE STRING "Instruments to build (all, or a ;-list of ids)")

file(GLOB _children LIST_DIRECTORIES true RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
set(MYBENCH_BUILT_INSTRUMENTS "")
foreach(id IN LISTS _children)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${id}/CMakeLists.txt)
        continue()
    endif()
    if(NOT MYBENCH_INSTRUMENTS STREQUAL "all" AND NOT id IN_LIST MYBENCH_INSTRUMENTS)
        continue()
    endif()
    add_subdirectory(${id})
    list(APPEND MYBENCH_BUILT_INSTRUMENTS ${id})
endforeach()
set(MYBENCH_BUILT_INSTRUMENTS "${MYBENCH_BUILT_INSTRUMENTS}" CACHE INTERNAL "Instrument ids that are built")
