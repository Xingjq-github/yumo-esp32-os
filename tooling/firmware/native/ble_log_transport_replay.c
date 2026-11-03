// Actual SDK log macros, notification call sites, tag filter/cache and USB TX.
// Simulated platform boundary: clock, FIFO and uncontended RTOS log lock.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "esp_log.h"
#define MYNEWT_VAL(x) MYNEWT_VAL_ ## x
#define MYNEWT_VAL_BLE_HS_LOG_LVL CONFIG_BT_NIMBLE_LOG_LEVEL
#include "logcfg/logcfg.h"
#define BLE_HS_LOG(level, ...) BLE_HS_LOG_ ## level(__VA_ARGS__)

static unsigned checks, messages, chars, polls, locks;
static int64_t now_us=1000000;
static bool fifo_writable;
static struct { int64_t last_tx_ts; } s_ctx;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)

int64_t esp_timer_get_time(void) { ++now_us;return now_us; }
uint32_t esp_log_timestamp(void) { return (uint32_t)(now_us/1000); }
void esp_log_impl_lock(void) { ++locks; }
bool esp_log_impl_lock_timeout(void) { ++locks;return true; }
void esp_log_impl_unlock(void) { CHECK(locks>0);--locks; }
static bool usb_serial_jtag_ll_txfifo_writable(void) { ++polls;return fifo_writable; }
static void usb_serial_jtag_ll_write_txfifo(const uint8_t *c, unsigned n) { (void)c;CHECK(n==1);++chars; }
static void usb_serial_jtag_ll_txfifo_flush(void) {}
#include "usb_tx.inc"
#include "notify_log.inc"

static int sink(const char *format, va_list args) {
    char line[512];
    int n=vsnprintf(line,sizeof(line),format,args);
    CHECK(n>0 && (size_t)n<sizeof(line));
    ++messages;
    for(int i=0;i<n;++i)usb_serial_jtag_tx_char_no_driver(0,line[i]);
    return n;
}
static void reset_sink(bool writable) {
    messages=chars=polls=0;fifo_writable=writable;
    s_ctx.last_tx_ts=now_us;
}
static void apply_production_policy(void) {
#include "policy.inc"
}
int main(void) {
    esp_log_set_vprintf(sink);
    esp_log_level_set("*",ESP_LOG_INFO);
    reset_sink(true);
    ble_gattc_log_notify(44);
    CHECK(messages==2 && chars>0 && locks==0);
    unsigned old_bytes=chars;
    reset_sink(false);
    int64_t start=now_us;
    ble_gattc_log_notify(44);
    int64_t old_stall=now_us-start;
    CHECK(messages==2 && old_stall>=50000 && polls>=50000 && chars==0);

    apply_production_policy();
    CHECK(esp_log_level_get("NimBLE")==ESP_LOG_WARN);
    CHECK(esp_log_level_get("ble_hid")==ESP_LOG_INFO);
    reset_sink(false);
    start=now_us;
    for(unsigned i=0;i<10000;++i)ble_gattc_log_notify(44);
    CHECK(messages==0 && chars==0 && polls==0 && now_us==start && locks==0);
    reset_sink(true);
    BLE_HS_LOG(WARN,"warning=%u",1u);
    BLE_HS_LOG(ERROR,"error=%u",2u);
    ESP_LOGI("ble_hid","GAP interval update status=%u",0u);
    CHECK(messages==3 && chars>0);

    // Exercise actual 31-slot binary cache eviction: the tag setting must
    // survive falling out of cache and a same-text/different-address lookup.
    char tags[80][32];
    for(unsigned i=0;i<80;++i) {
        snprintf(tags[i],sizeof(tags[i]),"unrelated-%u",i);
        CHECK(esp_log_level_get(tags[i])==ESP_LOG_INFO);
    }
    char alias[]="NimBLE";
    CHECK(esp_log_level_get(alias)==ESP_LOG_WARN);
    reset_sink(false);start=now_us;
    ble_gattc_log_notify(44);
    CHECK(messages==0 && polls==0 && now_us==start);
    apply_production_policy();
    CHECK(esp_log_level_get(alias)==ESP_LOG_WARN);
    CHECK(esp_log_level_get("unrelated")==ESP_LOG_INFO && locks==0);
    printf("{\"passed\":true,\"checks\":%u,\"notifications_suppressed\":10001,\"old_log_lines_per_report\":2,\"old_example_bytes\":%u,\"old_simulated_fifo_stall_us\":%lld,\"new_fifo_polls\":0,\"warn_error_and_app_info_preserved\":true,\"cache_eviction_preserved\":true}\n",
           checks,old_bytes,(long long)old_stall);
    return 0;
}
