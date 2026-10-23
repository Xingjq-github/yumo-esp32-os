#pragma once
#include "esp_err.h"
#include <cstdint>
namespace sys::lanweb {
// Called by hotspot's system worker; HTTP shutdown drains asynchronously.
void enable(uint32_t address);
void stop();
bool idle();
bool ready();
esp_err_t error();
}
