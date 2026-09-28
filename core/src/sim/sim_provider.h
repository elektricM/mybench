// DeviceProvider exposing one simulated myDAQ ("sim:0") while it is "plugged in".
#pragma once

#include "mydaq/device_manager.h"

#include <memory>

namespace mydaq {

std::unique_ptr<DeviceProvider> makeSimProvider();

}  // namespace mydaq
