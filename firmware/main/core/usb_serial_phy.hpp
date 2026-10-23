#pragma once
#include "esp_err.h"
// Worker-owned Serial/JTAG restoration shared with SD USB device mode.
esp_err_t yumoReleaseSerialPhy();
esp_err_t yumoRestoreSerialPhy();
