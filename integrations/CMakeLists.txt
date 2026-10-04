# Integrations with other tools (docs/integrations.md):
#   scopehal/  mybench-scopehal-bridge: the myDAQ as an oscilloscope in ngscopeclient
#   sigrok/    libsigrok driver ni-mydaq (PulseView, sigrok-cli); built in a libsigrok tree,
#              here only its tests
foreach(_dir scopehal sigrok)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt)
        add_subdirectory(${_dir})
    endif()
endforeach()
