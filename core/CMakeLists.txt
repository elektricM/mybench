# mydaq_core: hardware API, device discovery, simulated myDAQ and the DSP library.

add_library(mydaq_core STATIC
    include/mydaq/device.h
    include/mydaq/device_manager.h
    include/mydaq/sim_device.h
    include/mydaq/dsp/decimate.h
    include/mydaq/dsp/fft.h
    include/mydaq/dsp/measure.h
    include/mydaq/dsp/spectrum.h
    include/mydaq/dsp/trigger.h
    include/mydaq/dsp/units.h
    include/mydaq/dsp/waveform.h
    include/mydaq/dsp/window.h

    src/device.cpp
    src/device_manager.cpp

    src/dsp/decimate.cpp
    src/dsp/fft.cpp
    src/dsp/measure.cpp
    src/dsp/spectrum.cpp
    src/dsp/trigger.cpp
    src/dsp/units.cpp
    src/dsp/waveform.cpp
    src/dsp/window.cpp

    src/sim/ao_timeline.cpp
    src/sim/ao_timeline.h
    src/sim/digital_timeline.cpp
    src/sim/digital_timeline.h
    src/sim/sim_core.cpp
    src/sim/sim_core.h
    src/sim/sim_counter.cpp
    src/sim/sim_counter.h
    src/sim/sim_device.cpp
    src/sim/sim_provider.cpp
    src/sim/sim_provider.h
    src/sim/sim_tasks.cpp
    src/sim/sim_tasks.h
)

target_include_directories(mydaq_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(mydaq_core PUBLIC Qt6::Core)
target_compile_features(mydaq_core PUBLIC cxx_std_20)

if(MSVC)
    # Sources and headers are UTF-8 (µ, Ω in comments and literals).
    target_compile_options(mydaq_core PUBLIC /utf-8 PRIVATE /W4)
else()
    target_compile_options(mydaq_core PRIVATE -Wall -Wextra)
endif()

if(MYBENCH_WITH_USB)
    file(GLOB MYDAQ_USB_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/usb/*.cpp)
    target_sources(mydaq_core PRIVATE ${MYDAQ_USB_SOURCES})
    target_link_libraries(mydaq_core PRIVATE PkgConfig::LIBUSB mydaq)
    target_compile_definitions(mydaq_core PRIVATE MYDAQ_HAVE_USB=1)
endif()
