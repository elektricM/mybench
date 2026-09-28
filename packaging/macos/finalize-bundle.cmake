# Install-time finishing of MYBENCH.app (included by an install(CODE) rule of packaging/):
#   1. LSMinimumSystemVersion = newest `minos` of every Mach-O file in the bundle (a Homebrew
#      libusb, for instance, is built for the build machine's macOS), so Finder refuses cleanly
#      instead of the app crashing at launch on an older system;
#   2. code signature: every nested Mach-O file, then the bundle (ad-hoc by default: Apple
#      Silicon requires a signature, and macdeployqt's install_name_tool edits break the
#      original ones).
# Inputs: MYBENCH_BUNDLE (absolute path of the .app), MYBENCH_CODESIGN_IDENTITY ("-" = ad-hoc).

if(NOT IS_DIRECTORY "${MYBENCH_BUNDLE}")
    message(FATAL_ERROR "finalize-bundle: ${MYBENCH_BUNDLE} not found")
endif()

file(GLOB_RECURSE _files LIST_DIRECTORIES false "${MYBENCH_BUNDLE}/Contents/*")
set(_macho "")
foreach(f IN LISTS _files)
    get_filename_component(_name "${f}" NAME)
    # Mach-O candidates: *.dylib/*.so and extension-less files (executables, frameworks).
    if(IS_SYMLINK "${f}" OR NOT (_name MATCHES "\\.(dylib|so)$" OR NOT _name MATCHES "\\."))
        continue()
    endif()
    execute_process(COMMAND file -b "${f}" OUTPUT_VARIABLE _type ERROR_QUIET)
    if(_type MATCHES "Mach-O")
        list(APPEND _macho "${f}")
    endif()
endforeach()

# 1. Minimum macOS version.
set(_min "0")
foreach(f IN LISTS _macho)
    execute_process(COMMAND otool -l "${f}" OUTPUT_VARIABLE _lc ERROR_QUIET)
    string(REGEX MATCHALL "minos [0-9.]+" _mins "${_lc}")
    string(REGEX MATCHALL "LC_VERSION_MIN_MACOSX[^\n]*\n[^\n]*\n *version [0-9.]+" _old "${_lc}")
    foreach(m IN LISTS _mins _old)
        string(REGEX MATCH "[0-9]+(\\.[0-9]+)*$" v "${m}")
        if(v VERSION_GREATER _min)
            set(_min "${v}")
        endif()
    endforeach()
endforeach()
set(_plist "${MYBENCH_BUNDLE}/Contents/Info.plist")
execute_process(COMMAND /usr/libexec/PlistBuddy -c "Print :LSMinimumSystemVersion" "${_plist}"
                OUTPUT_VARIABLE _declared OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(_min VERSION_GREATER _declared)
    message(STATUS "Bundle: LSMinimumSystemVersion ${_declared} -> ${_min} (newest minos of the bundled binaries)")
    execute_process(COMMAND /usr/libexec/PlistBuddy -c "Set :LSMinimumSystemVersion ${_min}" "${_plist}")
else()
    message(STATUS "Bundle: LSMinimumSystemVersion ${_declared}")
endif()

# 2. Signature: nested code first (deepest paths first), then the bundle.
set(_sign_opts --force --sign "${MYBENCH_CODESIGN_IDENTITY}")
if(NOT MYBENCH_CODESIGN_IDENTITY STREQUAL "-")
    list(APPEND _sign_opts --options runtime --timestamp)   # Developer ID: notarization-ready
endif()
list(SORT _macho ORDER DESCENDING)
set(_main "${MYBENCH_BUNDLE}/Contents/MacOS")
foreach(f IN LISTS _macho)
    string(FIND "${f}" "${_main}/" _pos)
    if(_pos EQUAL 0)
        continue()   # the main executable is signed with the bundle
    endif()
    execute_process(COMMAND codesign ${_sign_opts} "${f}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
    if(rc)
        message(FATAL_ERROR "codesign failed for ${f}: ${err}")
    endif()
endforeach()
file(GLOB _frameworks "${MYBENCH_BUNDLE}/Contents/Frameworks/*.framework")
foreach(fw IN LISTS _frameworks)
    execute_process(COMMAND codesign ${_sign_opts} "${fw}" RESULT_VARIABLE rc ERROR_VARIABLE err)
    if(rc)
        message(FATAL_ERROR "codesign failed for ${fw}: ${err}")
    endif()
endforeach()
execute_process(COMMAND codesign ${_sign_opts} "${MYBENCH_BUNDLE}" RESULT_VARIABLE rc ERROR_VARIABLE err)
if(rc)
    message(FATAL_ERROR "codesign failed for ${MYBENCH_BUNDLE}: ${err}")
endif()
execute_process(COMMAND codesign --verify --deep --strict "${MYBENCH_BUNDLE}" RESULT_VARIABLE rc ERROR_VARIABLE err)
if(rc)
    message(FATAL_ERROR "codesign --verify failed: ${err}")
endif()
message(STATUS "Bundle: signed (${MYBENCH_CODESIGN_IDENTITY}) ${MYBENCH_BUNDLE}")
