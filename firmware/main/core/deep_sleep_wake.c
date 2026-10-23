#include "deep_sleep_wake.h"
#include "wake_click.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_wake_stub.h"
#include "esp_rom_sys.h"
#include "hal/rwdt_ll.h"
#include "soc/rtc_io_reg.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_channel.h"
#include "soc/soc.h"
#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "Double-click wake uses ESP32-S3 GPIO5 / RTC IO5"
#endif
_Static_assert(RTCIO_GPIO5_CHANNEL == 5, "Check PWR RTC channel before changing target");
static RTC_DATA_ATTR bool release_only;

static inline __attribute__((always_inline)) void feed_rtc_watchdog(void) {
    // IDF arms a 1 s RTC watchdog before deep sleep. Sleep pauses its counter;
    // it does not reset it between rejected clicks. Keep that protection enabled
    // and feed during the bounded input check, otherwise later SINGLE clicks
    // can cause an RTC watchdog reset and bypass this wake stub altogether.
    rwdt_ll_write_protect_disable(&RTCCNTL);
    rwdt_ll_feed(&RTCCNTL);
    rwdt_ll_write_protect_enable(&RTCCNTL);
}

static inline __attribute__((always_inline)) bool pwr_down(void) {
    return (REG_READ(RTC_GPIO_IN_REG) & (1U << (RTC_GPIO_IN_NEXT_S + 5))) == 0;
}
static void RTC_IRAM_ATTR double_click_wake(void);
static void RTC_IRAM_ATTR sleep_again(void) {
    // If held, sleep until RELEASE instead of repeatedly waking on the same
    // low level. A release wake only rearms: it never boots the application.
    release_only=pwr_down();
    if(release_only) REG_SET_BIT(RTC_CNTL_EXT_WAKEUP_CONF_REG,RTC_CNTL_EXT_WAKEUP0_LV);
    else REG_CLR_BIT(RTC_CNTL_EXT_WAKEUP_CONF_REG,RTC_CNTL_EXT_WAKEUP0_LV);
    feed_rtc_watchdog(); // A fresh budget for the next ROM wake and input check.
    esp_wake_stub_sleep(double_click_wake);
}
// Inherit the RTC section from the forward declaration; repeating RTC_IRAM_ATTR
// expands a different __COUNTER__ section name and produces a conflict.
static void double_click_wake(void) {
    esp_default_wake_deep_sleep();
    feed_rtc_watchdog();
    if(release_only) {
        // Let release bounce settle before selecting the next wake level.
        esp_rom_delay_us(20000);
        sleep_again();return;
    }
    wake_click_t click={WAKE_FIRST_DOWN,0,0};
    while(click.phase != WAKE_ACCEPT && click.phase != WAKE_REJECT) {
        feed_rtc_watchdog();
        wake_click_sample(&click,pwr_down());
        esp_rom_delay_us(2000);
    }
    if(click.phase == WAKE_ACCEPT) {
        // Both clicks have been released; no held wake key reaches iot_button.
        // Display/amplifier holds stay low until normal Power::init().
        release_only=false;return;
    }
    sleep_again();
}
void deep_sleep_arm_double_click(void) {
    release_only=false;
    esp_set_deep_sleep_wake_stub(double_click_wake);
}
