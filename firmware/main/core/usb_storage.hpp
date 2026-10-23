#pragma once
#include "esp_err.h"
namespace sys {
class UsbStorage {
public:
    static esp_err_t setEnabled(bool on); // GUI request, hardware work is asynchronous.
    static bool enabled();
    static bool idle();
    static bool transitioning();
    static const char *statusText();
};
}
