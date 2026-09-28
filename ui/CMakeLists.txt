# UI libraries, each a QML module (see docs/UI.md). Order matters: later ones may use earlier ones.
foreach(sub offscreen plot framework controls)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${sub}/CMakeLists.txt)
        add_subdirectory(${sub})
    endif()
endforeach()
