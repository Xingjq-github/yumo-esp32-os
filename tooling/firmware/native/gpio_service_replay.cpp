#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

extern "C" esp_err_t __wrap_gpio_install_isr_service(int);
static std::atomic<int> calls{0},active{0},overlaps{0},injected{ESP_ERR_NO_MEM};
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
extern "C" esp_err_t __real_gpio_install_isr_service(int flags) {
    ++calls;
    if(active.fetch_add(1))++overlaps;
    if(flags!=0)std::abort(); // preserve the existing non-IRAM board policy
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    --active;
    return injected.load();
}
int main() {
    CHECK(__wrap_gpio_install_isr_service(0x400000)==ESP_ERR_NOT_SUPPORTED);
    CHECK(calls==0);
    CHECK(__wrap_gpio_install_isr_service(0)==ESP_ERR_NO_MEM);
    CHECK(calls==1);
    injected=ESP_ERR_INVALID_STATE;
    CHECK(__wrap_gpio_install_isr_service(ESP_INTR_FLAG_IRAM)==ESP_ERR_INVALID_STATE);
    CHECK(calls==2); // unknown external ownership is not cached as our success
    injected=ESP_FAIL;
    CHECK(__wrap_gpio_install_isr_service(ESP_INTR_FLAG_LOWMED)==ESP_FAIL);
    CHECK(calls==3);
    injected=ESP_OK;
    std::atomic<int> successes{0},reused{0},unexpected{0};
    std::vector<std::thread> threads;
    for(int i=0;i<24;++i)threads.emplace_back([&,i] {
        const int policy=i%3==0?0:i%3==1?ESP_INTR_FLAG_IRAM:ESP_INTR_FLAG_LOWMED;
        for(int j=0;j<100;++j) {
            const auto r=__wrap_gpio_install_isr_service(policy);
            if(r==ESP_OK)++successes;
            else if(r==ESP_ERR_INVALID_STATE)++reused;
            else ++unexpected;
        }
    });
    for(auto &t:threads)t.join();
    CHECK(calls==4&&successes==1&&reused==2399&&unexpected==0&&overlaps==0);
    CHECK(__wrap_gpio_install_isr_service(0x400000)==ESP_ERR_NOT_SUPPORTED);
    CHECK(calls==4);
    std::printf("%u GPIO checks; 2400 concurrent client calls, failure retries and one shared install\n",checks);
}
