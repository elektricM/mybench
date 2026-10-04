# Captures with sigrok-cli into a .sr file, reads the file back and checks every channel's
# samples: the count, and that each value is a number within ±limit volts.
#
#   cmake -DSIGROK_CLI=<sigrok-cli> -DDRIVER=ni-mydaq -DCHANNELS=AI0,AI1 -DSAMPLERATE=10k
#         -DSAMPLES=1000 -DLIMITS=11 -DOUT=<file.sr> [-DCONFIGS=channel_group=AI1:vdiv=400mV]
#         -P capture_check.cmake
#
# LIMITS: one value for all channels, or one per channel ("11,2.2"). CONFIGS: extra -c options,
# separated by '|'. The file is read back with the "analog" output ("AI0: -1.234 mV DC"), which
# prints every sample of every channel; the csv output warns that multi-channel analog data is
# untested in libsigrok.
cmake_minimum_required(VERSION 3.16)

foreach(_v SIGROK_CLI DRIVER CHANNELS SAMPLERATE SAMPLES LIMITS OUT)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "capture_check: -D${_v}=... is required")
    endif()
endforeach()

string(REPLACE "," ";" _channels "${CHANNELS}")
string(REPLACE "," ";" _limits "${LIMITS}")
list(LENGTH _channels _nch)
list(LENGTH _limits _nlim)
if(NOT _nlim EQUAL 1 AND NOT _nlim EQUAL _nch)
    message(FATAL_ERROR "capture_check: LIMITS needs one value or one per channel")
endif()

set(_extra)
if(DEFINED CONFIGS AND NOT CONFIGS STREQUAL "")
    string(REPLACE "|" ";" _configs "${CONFIGS}")
    foreach(_c IN LISTS _configs)
        list(APPEND _extra -c "${_c}")
    endforeach()
endif()

file(REMOVE "${OUT}")
set(_cmd "${SIGROK_CLI}" -d "${DRIVER}" -C "${CHANNELS}" -c "samplerate=${SAMPLERATE}" ${_extra}
    --samples "${SAMPLES}" -o "${OUT}")
string(REPLACE ";" " " _shown "${_cmd}")
message(STATUS "capture: ${_shown}")
execute_process(COMMAND ${_cmd} RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err
    TIMEOUT 120)
if(NOT _out STREQUAL "" OR NOT _err STREQUAL "")
    message(STATUS "sigrok-cli said:\n${_out}${_err}")
endif()
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "capture failed (exit ${_rc})")
endif()
if(_err MATCHES "[Ee]rror|[Cc]ritical|[Ff]ailed")
    message(FATAL_ERROR "capture reported an error")
endif()
if(NOT EXISTS "${OUT}")
    message(FATAL_ERROR "capture wrote no file ${OUT}")
endif()

execute_process(COMMAND "${SIGROK_CLI}" -i "${OUT}" -O analog RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _text ERROR_VARIABLE _err TIMEOUT 120)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "reading ${OUT} back failed (exit ${_rc}):\n${_err}")
endif()
string(REPLACE "\n" ";" _lines "${_text}")

set(_index 0)
foreach(_ch IN LISTS _channels)
    if(_nlim EQUAL 1)
        list(GET _limits 0 _limit)
    else()
        list(GET _limits ${_index} _limit)
    endif()
    math(EXPR _index "${_index} + 1")

    set(_mine ${_lines})
    list(FILTER _mine INCLUDE REGEX "^${_ch}: ")
    list(LENGTH _mine _count)
    if(NOT _count EQUAL SAMPLES)
        message(FATAL_ERROR "${_ch}: ${_count} samples in ${OUT}, expected ${SAMPLES}")
    endif()

    set(_min "")
    set(_max "")
    set(_small 0)
    foreach(_line IN LISTS _mine)
        if(NOT _line MATCHES "^${_ch}: (-?[0-9]+(\\.[0-9]+)?) ([^ ]*)V")
            message(FATAL_ERROR "${_ch}: not a voltage: '${_line}'")
        endif()
        set(_value "${CMAKE_MATCH_1}")
        set(_prefix "${CMAKE_MATCH_3}")
        if(NOT _prefix STREQUAL "")
            # SI prefix below 1 (m, u, n, p, or the micro sign): |value| < 1 V
            if(_prefix MATCHES "^[kMGT]$")
                message(FATAL_ERROR "${_ch}: out of range: '${_line}'")
            endif()
            math(EXPR _small "${_small} + 1")
            continue()
        endif()
        if(_value GREATER _limit OR _value LESS -${_limit})
            message(FATAL_ERROR "${_ch}: ${_value} V is outside ±${_limit} V")
        endif()
        if(_min STREQUAL "" OR _value LESS _min)
            set(_min "${_value}")
        endif()
        if(_max STREQUAL "" OR _value GREATER _max)
            set(_max "${_value}")
        endif()
    endforeach()
    if(_min STREQUAL "")
        set(_range "all below 1 V")
    else()
        set(_range "${_min} V .. ${_max} V, ${_small} below 1 V")
    endif()
    message(STATUS "${_ch}: ${_count} samples within ±${_limit} V (${_range})")
endforeach()
message(STATUS "capture_check: OK")
