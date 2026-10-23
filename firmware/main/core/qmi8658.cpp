#include "qmi8658.hpp"

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdint>
#include <atomic>
#include <mutex>

namespace sys {
namespace {

constexpr uint16_t kAddress = 0x6B;
constexpr uint8_t kWhoAmI = 0x00;
constexpr uint8_t kCtrl1 = 0x02;
constexpr uint8_t kCtrl2 = 0x03;
constexpr uint8_t kCtrl3 = 0x04;
constexpr uint8_t kCtrl7 = 0x08;
constexpr uint8_t kAccelEnable = 0x01; // CTRL7 bit0 accel; bit1 gyro unused by this firmware
constexpr uint8_t kAxLow = 0x35;
constexpr uint8_t kReset = 0x60;
constexpr char kTag[] = "qmi8658";

i2c_master_dev_handle_t s_device = nullptr;
std::atomic<bool> s_ready{false};
std::atomic<bool> s_enabled{false}, s_inUse{false};
std::atomic<uint32_t> s_temporarySession{0};
std::mutex s_sampleMutex;
AccelerationSample s_sample;
uint32_t s_sampleSession=0;
bool s_sampleValid=false; // protected by s_sampleMutex
std::recursive_mutex s_ioMutex;
bool s_suspended=false, s_restoreEnabled=false; // worker-only

bool writeRegister(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(s_device, data, sizeof(data), 100) == ESP_OK;
}

bool readRegisters(uint8_t reg, uint8_t *data, size_t length,int timeoutMs=100)
{
    return i2c_master_transmit_receive(
               s_device, &reg, 1, data, length, timeoutMs) == ESP_OK;
}

}// namespace

bool Qmi8658::powerDownAtBoot()
{
    const std::lock_guard<std::recursive_mutex> lock(s_ioMutex);
    if(s_ready.load()) return false; // startup-only, never disturb an active consumer
    auto bus=bsp_i2c_get_handle();if(!bus) return false;
    i2c_device_config_t config={};
    config.dev_addr_length=I2C_ADDR_BIT_LEN_7;config.device_address=kAddress;config.scl_speed_hz=400000;
    i2c_master_dev_handle_t temporary=nullptr;
    if(i2c_master_bus_add_device(bus,&config,&temporary)!=ESP_OK) return false;
    uint8_t reg=kWhoAmI,id=0;
    bool ok=i2c_master_transmit_receive(temporary,&reg,1,&id,1,100)==ESP_OK && id==0x05;
    if(ok) {
        const uint8_t off[]={kCtrl7,0};
        ok=i2c_master_transmit(temporary,off,sizeof(off),100)==ESP_OK;
    }
    if(ok) {
        reg=kCtrl1;uint8_t ctrl1=0;
        ok=i2c_master_transmit_receive(temporary,&reg,1,&ctrl1,1,100)==ESP_OK;
        if(ok) {
            const uint8_t idle[]={kCtrl1,static_cast<uint8_t>(ctrl1|0x02)};
            ok=i2c_master_transmit(temporary,idle,sizeof(idle),100)==ESP_OK;
        }
    }
    (void)i2c_master_bus_rm_device(temporary);
    return ok; // do not publish ready or retain an initialized sensor object
}

bool Qmi8658::init(void)
{
    const std::lock_guard<std::recursive_mutex> lock(s_ioMutex);
    if (s_ready) return true;

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGW(kTag, "Shared I2C bus unavailable");
        return false;
    }

    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = kAddress;
    config.scl_speed_hz = 400000;
    esp_err_t err = i2c_master_bus_add_device(bus, &config, &s_device);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Unable to attach device: %s", esp_err_to_name(err));
        s_device = nullptr;
        return false;
    }

    uint8_t id = 0;
    if (!readRegisters(kWhoAmI, &id, 1) || id != 0x05) {
        ESP_LOGW(kTag, "Sensor not found (WHO_AM_I=0x%02x)", id);
        i2c_master_bus_rm_device(s_device);
        s_device = nullptr;
        return false;
    }

    if (!writeRegister(kReset, 0xB0)) goto failed;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!writeRegister(kCtrl1, 0x40) ||
        !writeRegister(kCtrl7, 0x00) ||
        !writeRegister(kCtrl2, AccelConfig) ||
        !writeRegister(kCtrl3, 0x55)) { // Gyro stays disabled; self-test also OFF.
        goto failed;
    }
    {
        uint8_t configured=0;
        // Do not publish ready if the sensor would use a different conversion.
        if(!readRegisters(kCtrl2,&configured,1) || configured!=AccelConfig) goto failed;
    }

    s_ready = true;
    s_enabled = false;
    ESP_LOGI(kTag, "QMI8658 ready");
    return true;

failed:
    ESP_LOGW(kTag, "Sensor initialization failed");
    i2c_master_bus_rm_device(s_device);
    s_device = nullptr;
    return false;
}

bool Qmi8658::available(void)
{
    return s_ready;
}

bool Qmi8658::setEnabled(bool enabled)
{
    const std::lock_guard<std::recursive_mutex> lock(s_ioMutex);
    if(!enabled && !s_ready) return true; // do not load an unused sensor
    if(!enabled && s_suspended) { s_restoreEnabled=false;s_enabled.store(false);return true; }
    if (!s_ready && !init()) return false;
    if (s_suspended && !resumeFromStandby()) return false;
    if (s_enabled == enabled) return true;
    if (!writeRegister(kCtrl7, enabled ? kAccelEnable : 0x00)) return false;
    { const std::lock_guard<std::mutex> sampleLock(s_sampleMutex);s_sampleValid=false; }
    s_enabled.store(enabled);
    return true;
}

bool Qmi8658::suspendForStandby()
{
    const std::lock_guard<std::recursive_mutex> lock(s_ioMutex);
    if(!s_ready || s_suspended) return true;
    s_restoreEnabled=s_enabled.load();
    s_suspended=true; // allow rollback even if a later register write fails
    s_enabled.store(false); // GUI must not read a partially suspended sensor
    { const std::lock_guard<std::mutex> sampleLock(s_sampleMutex);s_sampleValid=false; }
    if(!writeRegister(kCtrl7,0x00)) return false;
    uint8_t ctrl1=0;
    return readRegisters(kCtrl1,&ctrl1,1) && writeRegister(kCtrl1,ctrl1|0x02);
}

bool Qmi8658::resumeFromStandby()
{
    const std::lock_guard<std::recursive_mutex> lock(s_ioMutex);
    if(!s_ready || !s_suspended) return true;
    uint8_t ctrl1=0;
    if(!readRegisters(kCtrl1,&ctrl1,1) || !writeRegister(kCtrl1,ctrl1 & ~0x02)) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    // Write unconditionally: a failed suspend must not leave cached CTRL7 stale.
    if(!writeRegister(kCtrl7,s_restoreEnabled?kAccelEnable:0x00)) return false;
    s_enabled.store(s_restoreEnabled);
    s_suspended=false;
    return true;
}

void Qmi8658::setInUse(bool value) { s_inUse.store(value); }
bool Qmi8658::inUse() { return s_inUse.load(); }

uint32_t Qmi8658::claimTemporaryUse()
{
    static uint32_t generation=0; // GUI task only.
    if(++generation==0) ++generation;
    s_temporarySession.store(generation,std::memory_order_release);
    return generation;
}
void Qmi8658::releaseTemporaryUse(uint32_t session)
{
    if(session) s_temporarySession.compare_exchange_strong(session,0,std::memory_order_acq_rel);
}
bool Qmi8658::requested(bool systemPermission)
{
    return systemPermission && (s_inUse.load() || s_temporarySession.load(std::memory_order_acquire)!=0);
}
void Qmi8658::pollTemporaryAcceleration()
{
    const uint32_t session=s_temporarySession.load(std::memory_order_acquire);
    if((!session && !s_inUse.load()) || !enabled()) return;
    AccelerationSample sample;
    // This runs at the existing service cadence (normally 20 Hz), never in
    // LVGL or the emulator. Bound a failed/shared-bus transaction to short waits.
    if(!readAcceleration(sample.value,3)) return;
    sample.timeMs=static_cast<uint32_t>(esp_timer_get_time()/1000);
    const std::lock_guard<std::mutex> lock(s_sampleMutex);
    if(session!=s_temporarySession.load(std::memory_order_acquire) || !enabled()) return;
    s_sample=sample;s_sampleSession=session;s_sampleValid=true;
}
bool Qmi8658::temporaryAcceleration(uint32_t session,AccelerationSample &sample)
{
    if(!session || !enabled() || session!=s_temporarySession.load(std::memory_order_acquire)) return false;
    const std::unique_lock<std::mutex> lock(s_sampleMutex,std::try_to_lock);
    if(!lock.owns_lock() || !s_sampleValid || !enabled() || s_sampleSession!=session) return false;
    sample=s_sample;
    return true;
}

bool Qmi8658::cachedAcceleration(AccelerationSample &sample)
{
    if(!enabled() || !s_inUse.load()) return false;
    const std::unique_lock<std::mutex> lock(s_sampleMutex,std::try_to_lock);
    if(!lock.owns_lock() || !s_sampleValid || !enabled()) return false;
    sample=s_sample;
    return true;
}

bool Qmi8658::enabled(void)
{
    return s_ready && s_enabled;
}

bool Qmi8658::readAcceleration(Acceleration &value,int timeoutMs)
{
    if (!s_ready || !s_enabled) return false;

    const std::unique_lock<std::recursive_mutex> lock(s_ioMutex,std::try_to_lock);
    if(!lock.owns_lock() || !s_enabled.load() || (!s_inUse.load() && !s_temporarySession.load())) return false;
    uint8_t data[6] = {};
    if (!readRegisters(kAxLow, data, sizeof(data),timeoutMs)) return false;

    value.x = accelToG(data[0], data[1]);
    value.y = accelToG(data[2], data[3]);
    value.z = accelToG(data[4], data[5]);
    return true;
}

}// namespace sys
