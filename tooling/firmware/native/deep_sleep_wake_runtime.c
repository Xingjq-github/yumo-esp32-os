// Execute the real wake stub with simulated GPIO, sleep and a retained RTC WDT.
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "fake_sdk.h"

enum { ASLEEP=1, WATCHDOG=2, BOOTED=3 };
static unsigned checks, feeds, watchdog_us, max_watchdog_us;
static unsigned elapsed_us, wall_us;
static bool protected=true, ignore_feed, pressed, wake_on_high;
static esp_deep_sleep_wake_stub_fn_t wake_entry;
static jmp_buf outcome;
typedef struct { unsigned us; bool down; } edge_t;
static const edge_t *edges;
static unsigned edge_count;
rtc_cntl_dev_t RTCCNTL;
#define CHECK(x) do { ++checks; if(!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)

void rwdt_ll_write_protect_disable(rtc_cntl_dev_t *dev) {
    CHECK(dev==&RTCCNTL && protected); protected=false;
}
void rwdt_ll_feed(rtc_cntl_dev_t *dev) {
    CHECK(dev==&RTCCNTL && !protected); ++feeds;
    if(!ignore_feed) watchdog_us=0;
}
void rwdt_ll_write_protect_enable(rtc_cntl_dev_t *dev) {
    CHECK(dev==&RTCCNTL && !protected); protected=true;
}
uint32_t mock_read(unsigned reg) {
    CHECK(reg==RTC_GPIO_IN_REG);
    return pressed ? 0 : (1u<<(RTC_GPIO_IN_NEXT_S+5));
}
void mock_level(unsigned reg,unsigned bit,bool high) {
    CHECK(reg==RTC_CNTL_EXT_WAKEUP_CONF_REG && bit==RTC_CNTL_EXT_WAKEUP0_LV);
    wake_on_high=high;
}
void esp_rom_delay_us(unsigned us) {
    elapsed_us+=us;wall_us+=us;watchdog_us+=us;
    if(watchdog_us>max_watchdog_us)max_watchdog_us=watchdog_us;
    if(watchdog_us>=1000000u)longjmp(outcome,WATCHDOG);
    for(unsigned i=0;i<edge_count;++i)if(elapsed_us>=edges[i].us)pressed=edges[i].down;
}
void esp_default_wake_deep_sleep(void) {}
void esp_set_deep_sleep_wake_stub(esp_deep_sleep_wake_stub_fn_t fn) {wake_entry=fn;}
void esp_wake_stub_sleep(esp_deep_sleep_wake_stub_fn_t fn) {
    CHECK(protected);wake_entry=fn;longjmp(outcome,ASLEEP);
}

#include "core/deep_sleep_wake.c"

static int invoke(const edge_t *wave,unsigned count) {
    CHECK(count>0 && wave[0].us==0);
    edges=wave;edge_count=count;elapsed_us=0;pressed=wave[0].down;
    CHECK(pressed!=wake_on_high); // Only a configured EXT0 level can wake us.
    const int result=setjmp(outcome);
    if(result)return result;
    // Retained watchdog also runs during ROM initialization before the stub.
    watchdog_us+=10000;wall_us+=10000;
    if(watchdog_us>=1000000u)return WATCHDOG;
    wake_entry();CHECK(protected);return BOOTED;
}
static void begin(bool omit_feed) {
    ignore_feed=omit_feed;watchdog_us=0;max_watchdog_us=0;
    protected=true;wake_on_high=false;deep_sleep_arm_double_click();
}
static void sleep_ms(unsigned ms) {
    // RTC WDT pause-in-sleep preserves, rather than clears, its counter.
    wall_us+=ms*1000u;
}
static int single(void) {
    const edge_t wave[]={{0,true},{120000,false}};
    return invoke(wave,2);
}
static int double_click(unsigned press_ms,unsigned gap_ms) {
    const edge_t wave[]={{0,true},{press_ms*1000,false},
        {(press_ms+gap_ms)*1000,true},{(2*press_ms+gap_ms)*1000,false}};
    return invoke(wave,4);
}
int main(void) {
    // Control: omitting feed reproduces a reset on the second isolated click.
    begin(true);CHECK(single()==ASLEEP);CHECK(watchdog_us>500000u);
    sleep_ms(5000);CHECK(single()==WATCHDOG);

    for(unsigned gap_ms=3000;gap_ms<=5000;gap_ms+=2000) {
        begin(false);
        for(unsigned i=0;i<100;++i) {
            CHECK(single()==ASLEEP);CHECK(!wake_on_high);
            CHECK(watchdog_us==0 && protected);sleep_ms(gap_ms);
        }
        CHECK(double_click(80,100)==BOOTED);
        CHECK(max_watchdog_us<=10000u);
    }
    // The full valid gesture can exceed one watchdog period.
    begin(false);CHECK(double_click(340,360)==BOOTED);
    begin(true);CHECK(double_click(340,360)==WATCHDOG);
    begin(false);CHECK(double_click(60,500)==ASLEEP);

    // Holding either press must never boot; a release only rearms EXT0.
    const edge_t held[]={{0,true}}, released[]={{0,false}};
    begin(false);CHECK(invoke(held,1)==ASLEEP);CHECK(wake_on_high);
    sleep_ms(5000);CHECK(invoke(released,1)==ASLEEP);CHECK(!wake_on_high);
    CHECK(double_click(60,60)==BOOTED);
    const edge_t held_second[]={{0,true},{60000,false},{120000,true}};
    begin(false);CHECK(invoke(held_second,3)==ASLEEP);CHECK(wake_on_high);
    CHECK(invoke(released,1)==ASLEEP);CHECK(!wake_on_high);
    CHECK(single()==ASLEEP);

    const edge_t noise[]={{0,true},{10000,false}};
    begin(false);
    for(unsigned i=0;i<100;++i) {
        CHECK(invoke(noise,2)==ASLEEP);sleep_ms(3000);
    }
    CHECK(double_click(60,60)==BOOTED);
    printf("Wake stub: %u checks; 200 isolated clicks, 100 noise wakes, holds, slow/valid doubles and RTC WDT regression passed (%u protected feeds)\n",checks,feeds);
    return 0;
}
