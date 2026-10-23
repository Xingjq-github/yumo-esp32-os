#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include <mutex>

// One board-wide service for touch, display TE and wake buttons. The touch
// driver previously installed it first with flags=0. Keep that non-IRAM
// policy: not every callback/data path is safe while the flash cache is off.
// IRAM on a later button install never upgraded the already-running service.
// Link wrapping keeps upstream sources untouched and covers every component.
namespace {
std::mutex serviceMutex;
bool installed=false;
}
extern "C" esp_err_t __real_gpio_install_isr_service(int flags);
extern "C" esp_err_t __wrap_gpio_install_isr_service(int flags)
{
    // Only the pinned board clients' policies are supported. A new driver
    // requiring a different priority/edge/shared mode needs an explicit audit.
    if(flags!=0 && flags!=ESP_INTR_FLAG_IRAM && flags!=ESP_INTR_FLAG_LOWMED) {
        ESP_LOGE("gpio_service","Unsupported GPIO interrupt policy: 0x%x",flags);
        return ESP_ERR_NOT_SUPPORTED;
    }
    const std::lock_guard<std::mutex> lock(serviceMutex);
    if(installed) return ESP_ERR_INVALID_STATE; // preserve the public API contract
    const auto result=__real_gpio_install_isr_service(0);
    if(result==ESP_OK) installed=true;
    // Allocation/registration errors remain visible and the next call can retry.
    return result;
}

// Service lifetime is the boot lifetime. Board clients remove only their own
// pin handler. Leave IDF's failed-install cleanup entirely under IDF ownership.
