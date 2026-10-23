// Check the generated ESP32 configuration actually delivered in the image.
#include "sdkconfig.h"

#if !defined(CONFIG_RTC_CLK_SRC_INT_8MD256) || !CONFIG_RTC_CLK_SRC_INT_8MD256
#error "Long-sleep timekeeping requires the internal RC_FAST / 256 clock"
#endif
#if defined(CONFIG_RTC_CLK_SRC_INT_RC) || defined(CONFIG_RTC_CLK_SRC_EXT_CRYS) || defined(CONFIG_RTC_CLK_SRC_EXT_OSC)
#error "Do not fall back to RC_SLOW or select an unpopulated external RTC clock"
#endif
#if !defined(CONFIG_LIBC_TIME_SYSCALL_USE_RTC_HRT) || !CONFIG_LIBC_TIME_SYSCALL_USE_RTC_HRT
#error "Keep high-resolution awake time and retained RTC sleep time together"
#endif
#if !defined(CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL) || !CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL
#error "The RTC change must preserve the existing Bluetooth main-XTAL clock"
#endif

static_assert(CONFIG_RTC_CLK_CAL_CYCLES == 3000,
              "Calibrate RC_FAST / 256 using the SDK-recommended 3000 cycles");
static_assert(CONFIG_PM_LIGHTSLEEP_RTC_OSC_CAL_INTERVAL == 1,
              "Keep calibration on every light-sleep entry");
