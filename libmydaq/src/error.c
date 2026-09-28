#include "libmydaq/mydaq.h"

#define MD_STR2(x) #x
#define MD_STR(x) MD_STR2(x)

const char *mydaq_version(void)
{
    return MD_STR(LIBMYDAQ_VERSION_MAJOR) "." MD_STR(LIBMYDAQ_VERSION_MINOR) "." MD_STR(
        LIBMYDAQ_VERSION_PATCH);
}

const char *mydaq_strerror(int err)
{
    switch ((mydaq_error)err) {
    case MYDAQ_OK: return "Success";
    case MYDAQ_ERR_INVALID_ARG: return "Invalid argument or configuration";
    case MYDAQ_ERR_NO_MEM: return "Out of memory";
    case MYDAQ_ERR_NOT_FOUND: return "No matching myDAQ found";
    case MYDAQ_ERR_NO_DEVICE: return "The myDAQ was disconnected";
    case MYDAQ_ERR_ACCESS: return "Access to the USB device denied (permissions)";
    case MYDAQ_ERR_BUSY: return "Device or subsystem in use";
    case MYDAQ_ERR_TIMEOUT: return "Operation timed out";
    case MYDAQ_ERR_OVERFLOW: return "Acquisition overflow: samples were lost";
    case MYDAQ_ERR_UNDERFLOW: return "Generation underflow: ran out of samples";
    case MYDAQ_ERR_USB: return "USB transfer failed";
    case MYDAQ_ERR_PROTOCOL: return "Unexpected response from the device";
    case MYDAQ_ERR_DEVICE: return "The device rejected the command";
    case MYDAQ_ERR_NOT_SUPPORTED: return "Not supported (yet) by libmydaq";
    case MYDAQ_ERR_STATE: return "Operation not valid in the current state";
    case MYDAQ_ERR_CALIBRATION: return "Calibration memory is missing or inconsistent";
    }
    return "Unknown error";
}

const char *mydaq_error_name(int err)
{
    switch ((mydaq_error)err) {
    case MYDAQ_OK: return "MYDAQ_OK";
    case MYDAQ_ERR_INVALID_ARG: return "MYDAQ_ERR_INVALID_ARG";
    case MYDAQ_ERR_NO_MEM: return "MYDAQ_ERR_NO_MEM";
    case MYDAQ_ERR_NOT_FOUND: return "MYDAQ_ERR_NOT_FOUND";
    case MYDAQ_ERR_NO_DEVICE: return "MYDAQ_ERR_NO_DEVICE";
    case MYDAQ_ERR_ACCESS: return "MYDAQ_ERR_ACCESS";
    case MYDAQ_ERR_BUSY: return "MYDAQ_ERR_BUSY";
    case MYDAQ_ERR_TIMEOUT: return "MYDAQ_ERR_TIMEOUT";
    case MYDAQ_ERR_OVERFLOW: return "MYDAQ_ERR_OVERFLOW";
    case MYDAQ_ERR_UNDERFLOW: return "MYDAQ_ERR_UNDERFLOW";
    case MYDAQ_ERR_USB: return "MYDAQ_ERR_USB";
    case MYDAQ_ERR_PROTOCOL: return "MYDAQ_ERR_PROTOCOL";
    case MYDAQ_ERR_DEVICE: return "MYDAQ_ERR_DEVICE";
    case MYDAQ_ERR_NOT_SUPPORTED: return "MYDAQ_ERR_NOT_SUPPORTED";
    case MYDAQ_ERR_STATE: return "MYDAQ_ERR_STATE";
    case MYDAQ_ERR_CALIBRATION: return "MYDAQ_ERR_CALIBRATION";
    }
    return "MYDAQ_ERR_UNKNOWN";
}
