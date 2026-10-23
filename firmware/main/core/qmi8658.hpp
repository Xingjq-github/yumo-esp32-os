#pragma once
#include <cstdint>

namespace sys {

struct Acceleration {
    float x;
    float y;
    float z;
};
struct AccelerationSample {
    Acceleration value{};
    uint32_t timeMs=0;
};

class Qmi8658 {
public:
    // CTRL2: self-test OFF, aFS=010 (+/-8 g), aODR=0101 (250 Hz).
    // Derive conversion from the same register value that initialization writes.
    static constexpr uint8_t AccelConfig=0x25;
    static constexpr float accelToG(uint8_t low,uint8_t high) {
        const auto raw=static_cast<int16_t>(uint16_t(low)|(uint16_t(high)<<8));
        return raw*(float(2u<<((AccelConfig>>4)&0x07))/32768.0f);
    }
    // Initialization and control: boot/system worker only.
    static bool init(void);
    static bool powerDownAtBoot(); // quiesce retained registers after a warm reset, no sampling/init
    static bool available(void);
    static bool setEnabled(bool enabled);
    static bool enabled(void);
    static void setInUse(bool value); // GUI publishes automatic orientation demand
    static bool inUse();
    // GUI-only lease: foreground game use, subject to the common motion switch.
    static uint32_t claimTemporaryUse();
    static void releaseTemporaryUse(uint32_t session);
    static bool requested(bool systemPermission);
    // The existing system worker samples; the GUI only copies a small cache.
    static void pollTemporaryAcceleration();
    static bool temporaryAcceleration(uint32_t session,AccelerationSample &sample);
    static bool cachedAcceleration(AccelerationSample &sample); // raw axes for orientation
    static bool suspendForStandby(); // paired worker APIs; do not persist preference
    static bool resumeFromStandby();
    static bool readAcceleration(Acceleration &value,int timeoutMs=100);
};

}// namespace sys
