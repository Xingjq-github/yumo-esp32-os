/*
 * Physical button manager implementation.
 */
#include "button_mgr.hpp"
#include "game_key_state.hpp"
#include "event_bus.hpp"
#include "settings_store.hpp"
#include "system_service.hpp"
#include "power_mgr.hpp"
#include "screenshot.hpp"
#include "driver/gpio.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <climits>
#include <initializer_list>

namespace sys {

static const char *TAG = "buttons";

// Board key mapping; keep independent callbacks and emergency PWR handling.
static constexpr gpio_num_t kBtnBoot = GPIO_NUM_0;
static constexpr gpio_num_t kBtnKey  = GPIO_NUM_4;
static constexpr gpio_num_t kBtnPwr  = GPIO_NUM_5;

static button_handle_t s_boot = nullptr;
static button_handle_t s_key  = nullptr;
static button_handle_t s_pwr  = nullptr;

static std::atomic<uint32_t> s_pageSession{0};
static uint32_t s_pressedPageBoot=0,s_pressedPageKey=0; // Button task only, until PRESS_END.
static std::atomic<uint32_t> s_viewSession{0},s_viewExit{0};
static uint32_t s_pressedView=0; // Only touched by the button task.
static std::atomic<uint32_t> s_bootBackSession{0},s_bootBackPending{0};
static uint32_t s_pressedBootBack=0; // Button task captures the owner at press down.
static std::atomic<bool> s_systemPanel{false};
static bool s_panelBoot=false,s_panelKey=false,s_panelPwr=false; // button task only
static portMUX_TYPE s_gameLock=portMUX_INITIALIZER_UNLOCKED;
static GameKeyState s_gameKeys;
static portMUX_TYPE s_timerLock=portMUX_INITIALIZER_UNLOCKED;
static TimerKeyState s_timerKeys;

uint32_t Buttons::claimTimerKeys() {
    portENTER_CRITICAL(&s_timerLock);const auto token=s_timerKeys.claim();portEXIT_CRITICAL(&s_timerLock);
    return token;
}
void Buttons::releaseTimerKeys(uint32_t session) {
    portENTER_CRITICAL(&s_timerLock);s_timerKeys.release(session);portEXIT_CRITICAL(&s_timerLock);
}
bool Buttons::takeTimerKey(uint32_t session,TimerKey &key) {
    portENTER_CRITICAL(&s_timerLock);const bool found=s_timerKeys.take(session,key);portEXIT_CRITICAL(&s_timerLock);
    return found;
}
static bool routeTimerKey(button_event_t event,TimerKeyState::Key key) {
    TimerKeyState::Event input;
    switch(event) {
    case BUTTON_PRESS_DOWN: input=TimerKeyState::Down;break;
    case BUTTON_PRESS_UP: input=TimerKeyState::Up;break;
    case BUTTON_PRESS_END: input=TimerKeyState::End;break;
    case BUTTON_SINGLE_CLICK: input=TimerKeyState::Single;break;
    case BUTTON_DOUBLE_CLICK: input=TimerKeyState::Double;break;
    case BUTTON_LONG_PRESS_START: input=TimerKeyState::Long;break;
    default:return false;
    }
    const auto now=esp_timer_get_time();
    const bool interactive=!s_systemPanel.load(std::memory_order_acquire) && Power::isScreenOn() && !Power::sleepPending();
    portENTER_CRITICAL(&s_timerLock);const bool consumed=s_timerKeys.route(key,input,now,interactive);portEXIT_CRITICAL(&s_timerLock);
    return consumed;
}

uint32_t Buttons::claimGameKeys() {
    portENTER_CRITICAL(&s_gameLock); const auto session=s_gameKeys.claim(); portEXIT_CRITICAL(&s_gameLock);
    return session;
}
void Buttons::releaseGameKeys(uint32_t session) {
    portENTER_CRITICAL(&s_gameLock); s_gameKeys.release(session); portEXIT_CRITICAL(&s_gameLock);
}
void Buttons::gameMode(uint32_t session,bool gameplay) {
    portENTER_CRITICAL(&s_gameLock); s_gameKeys.mode(session,gameplay); portEXIT_CRITICAL(&s_gameLock);
}
void Buttons::gameRunning(uint32_t session,bool running) {
    portENTER_CRITICAL(&s_gameLock); s_gameKeys.running(session,running); portEXIT_CRITICAL(&s_gameLock);
}
void Buttons::clearGameKeys(uint32_t session) {
    portENTER_CRITICAL(&s_gameLock); s_gameKeys.clear(session); portEXIT_CRITICAL(&s_gameLock);
}
uint8_t Buttons::takeGameKeys(uint32_t session) {
    portENTER_CRITICAL(&s_gameLock); const auto keys=s_gameKeys.take(session); portEXIT_CRITICAL(&s_gameLock);
    return keys;
}
bool Buttons::blockHomeShortcut() {
    portENTER_CRITICAL(&s_gameLock); const bool blocked=s_gameKeys.blockHome(); portEXIT_CRITICAL(&s_gameLock);
    return blocked;
}
static bool routeGameKey(button_event_t event,uint8_t key) {
    bool consumed=true;
    portENTER_CRITICAL(&s_gameLock);
    switch(event) {
    case BUTTON_PRESS_DOWN: s_gameKeys.down(key); break;
    case BUTTON_PRESS_UP: s_gameKeys.up(key); break;
    case BUTTON_PRESS_END: s_gameKeys.end(key); break;
    case BUTTON_SINGLE_CLICK: consumed=s_gameKeys.consumeClick(key); break;
    case BUTTON_DOUBLE_CLICK: consumed=key==GameKeyState::Boot && s_gameKeys.blockHome(); break;
    default: consumed=false; break;
    }
    portEXIT_CRITICAL(&s_gameLock);
    return consumed;
}

uint32_t Buttons::claimPageKeys()
{
    static uint32_t generation=0; // GUI task only; Event::i32 stays positive.
    generation=generation>=INT32_MAX?1:generation+1;
    s_pageSession.store(generation,std::memory_order_release);
    return generation;
}

void Buttons::releasePageKeys(uint32_t session)
{
    if(session) s_pageSession.compare_exchange_strong(session,0,std::memory_order_acq_rel);
}

static void capturePageKey(button_event_t event,uint32_t &pressed)
{
    if(event==BUTTON_PRESS_DOWN) {
        const bool interactive=!s_systemPanel.load(std::memory_order_acquire) && Power::isScreenOn() && !Power::sleepPending();
        pressed=interactive?s_pageSession.load(std::memory_order_acquire):0;
    } else if(event==BUTTON_PRESS_END) pressed=0;
}

static bool routePageKey(EventType type)
{
    const auto session=s_pageSession.load(std::memory_order_acquire);
    const auto pressed=type==EventType::PreviousPage?s_pressedPageBoot:s_pressedPageKey;
    // Consume an old page's delayed click, including after returning home.
    if(pressed && pressed!=session) return true;
    if(!session) return false;
    if(pressed==session && Power::isScreenOn() && !Power::sleepPending())
        EventBus::publish({type,static_cast<int32_t>(session)});
    return true; // Still consume a click if the GUI event queue is full.
}

uint32_t Buttons::claimViewExit() {
    static uint32_t generation=0; // GUI-only generation.
    generation=generation>=INT32_MAX?1:generation+1;
    s_viewExit.store(0,std::memory_order_release);
    s_viewSession.store(generation,std::memory_order_release);
    return generation;
}
void Buttons::releaseViewExit(uint32_t session) {
    if(session) s_viewSession.compare_exchange_strong(session,0,std::memory_order_acq_rel);
}
bool Buttons::takeViewExit(uint32_t session) {
    const auto exit=s_viewExit.exchange(0,std::memory_order_acq_rel);
    return session && !s_systemPanel.load(std::memory_order_acquire) && exit==session;
}
uint32_t Buttons::claimBootBack() {
    static uint32_t generation=0; // GUI-only generation.
    generation=generation>=INT32_MAX?1:generation+1;
    s_bootBackPending.store(0,std::memory_order_release);
    s_bootBackSession.store(generation,std::memory_order_release);
    return generation;
}
void Buttons::releaseBootBack(uint32_t session) {
    if(session)s_bootBackSession.compare_exchange_strong(session,0,std::memory_order_acq_rel);
}
bool Buttons::takeBootBack(uint32_t session) {
    const auto pending=s_bootBackPending.exchange(0,std::memory_order_acq_rel);
    return session && pending==session && !s_systemPanel.load(std::memory_order_acquire);
}
static bool routeBootBack(button_event_t event) {
    const auto session=s_bootBackSession.load(std::memory_order_acquire);
    const bool interactive=!s_systemPanel.load(std::memory_order_acquire) && Power::isScreenOn() && !Power::sleepPending();
    if(event==BUTTON_PRESS_DOWN)s_pressedBootBack=interactive?session:0;
    else if(event==BUTTON_PRESS_END)s_pressedBootBack=0;
    else if(event==BUTTON_SINGLE_CLICK){
        if(interactive && session && s_pressedBootBack==session)s_bootBackPending.store(session,std::memory_order_release);
        // An old press cannot affect a new App, nor fall through to volume control.
        return session || s_pressedBootBack;
    }
    return false;
}
void Buttons::setSystemPanel(bool visible) {
    s_systemPanel.store(visible,std::memory_order_release);
    s_viewExit.store(0,std::memory_order_release);
    s_bootBackPending.store(0,std::memory_order_release);
}

static void changeVolume(int delta)
{
    int v = Settings::volume() + delta;
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    SystemService::setVolume(v);
}

static bool routePanelKey(button_event_t event,bool &began,int delta) {
    const bool panel=s_systemPanel.load(std::memory_order_acquire);
    if(event==BUTTON_PRESS_DOWN)began=panel;
    if(event==BUTTON_PRESS_END)began=false;
    if(event!=BUTTON_SINGLE_CLICK && event!=BUTTON_DOUBLE_CLICK)return false;
    if(!panel && !began)return false;
    // A delayed click from a dismissed panel never reaches the underlying App.
    if(panel) {
        if(event==BUTTON_SINGLE_CLICK)changeVolume(delta);
        else if(delta<0)EventBus::publish({EventType::BackRequested});
        else Screenshot::request();
    }
    return true;
}

static void bootCb(void *arg, void *)
{
    button_event_t ev = iot_button_get_event((button_handle_t)arg);
    capturePageKey(ev,s_pressedPageBoot);
    if(ev==BUTTON_PRESS_DOWN || ev==BUTTON_PRESS_UP)Power::activity();
    const bool game=routeGameKey(ev,GameKeyState::Boot); // always drain held-key releases
    const bool timer=routeTimerKey(ev,TimerKeyState::Boot);
    const bool back=routeBootBack(ev);
    if(routePanelKey(ev,s_panelBoot,-5) || game || timer || back) return;
    switch (ev) {
    case BUTTON_SINGLE_CLICK:
        if(!routePageKey(EventType::PreviousPage)) changeVolume(-5);
        break;
    case BUTTON_DOUBLE_CLICK:
        EventBus::publish({EventType::HomeRequested});
        break;
    default:
        break;
    }
}

static void keyCb(void *arg, void *)
{
    button_event_t ev = iot_button_get_event((button_handle_t)arg);
    capturePageKey(ev,s_pressedPageKey);
    if(ev==BUTTON_PRESS_DOWN || ev==BUTTON_PRESS_UP)Power::activity();
    const bool game=routeGameKey(ev,GameKeyState::Key);
    const bool timer=routeTimerKey(ev,TimerKeyState::Start);
    if(routePanelKey(ev,s_panelKey,5) || game || timer) return;
    if (ev == BUTTON_SINGLE_CLICK) {
        if(!routePageKey(EventType::NextPage)) changeVolume(5);
    } else if(ev==BUTTON_DOUBLE_CLICK) {
        Screenshot::request(); // Driver resolves double vs single before dispatch.
    }
}

static void pwrCb(void *arg, void *)
{
    button_event_t ev = iot_button_get_event((button_handle_t)arg);
    if(ev==BUTTON_PRESS_DOWN || ev==BUTTON_PRESS_UP)Power::activity();
    if(routeTimerKey(ev,TimerKeyState::Pause))return;
    switch (ev) {
    case BUTTON_PRESS_DOWN:
        s_panelPwr=s_systemPanel.load(std::memory_order_acquire);
        s_pressedView=Power::isScreenOn()?s_viewSession.load(std::memory_order_acquire):0;
        break;
    case BUTTON_PRESS_END:
        s_panelPwr=false;
        s_pressedView=0;
        break;
    case BUTTON_SINGLE_CLICK:
        if(Power::sleepPending()) Power::toggleScreen(); // Keep deep-sleep cancellation/recovery.
        else if(!Power::isScreenOn()) Power::screenOn();
        else if(s_systemPanel.load(std::memory_order_acquire)) EventBus::publish({EventType::BackRequested});
        else if(s_panelPwr) {} // panel dismissed before this delayed click
        else if(s_pressedView) s_viewExit.store(s_pressedView,std::memory_order_release);
        else if(!s_viewSession.load(std::memory_order_acquire)) Power::toggleScreen();
        // A press begun on an old page stays consumed after that page closes.
        break;
    case BUTTON_LONG_PRESS_START:
        Power::powerOff();
        break;
    default:
        break;
    }
}

// Emergency PWR long-press watchdog.
//
// The normal PWR handling above goes through iot_button, whose detection runs
// on a software-timer task at a modest priority. If a busy render or a
// deadlock starves that task (the classic "screen frozen, PWR does nothing"
// symptom), the long-press is never detected and the user cannot force the
// device off. This independent high-priority task reads GPIO5 directly, so as
// long as the scheduler is alive at all it can still force a power-off after a
// sustained hold -- giving the user a guaranteed hardware escape from a freeze.
static void pwrWatchdogTask(void *)
{
    constexpr int kPollMs   = 50;
    constexpr int kHoldMs   = 3000;  // sustained hold to force power-off
    int64_t pressedAt=0;
    for (;;) {
        // active-low: level 0 == pressed.
        if (gpio_get_level(kBtnPwr) == 0) {
            const auto now=esp_timer_get_time();
            if(!pressedAt)pressedAt=now;
            const auto held=int((now-pressedAt)/1000);
            if (held >= kHoldMs) {
                ESP_LOGW(TAG, "[pwr-wdt] PWR held %dms -> forced power off", held);
                Power::powerOff();  // pure-GPIO path, safe even while frozen
            }
        } else {
            pressedAt=0;
        }
        // Independent GPIO fallback remains live even if the GUI/button timer
        // stalls; low-power idle polls at 500ms, held-key detection stays 50ms.
        vTaskDelay(pdMS_TO_TICKS(pressedAt?kPollMs:Power::slowIdle()?500:kPollMs));
    }
}

static esp_err_t createButton(gpio_num_t gpio, const char *name,
                              button_handle_t *handle, button_cb_t callback)
{
    button_config_t btn_cfg = {};
    button_gpio_config_t gpio_cfg = {};
    gpio_cfg.gpio_num = gpio;
    gpio_cfg.active_level = 0;
    gpio_cfg.enable_power_save = true;

    esp_err_t result = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, handle);
    if(result!=ESP_OK) ESP_LOGE(TAG, "Cannot create GPIO%d(%s): %s",
                               (int)gpio, name, esp_err_to_name(result));
    if (result == ESP_OK) {
        for(auto event:{BUTTON_PRESS_DOWN,BUTTON_PRESS_UP,BUTTON_PRESS_END})
            iot_button_register_cb(*handle,event,nullptr,callback,nullptr);
        iot_button_register_cb(*handle, BUTTON_SINGLE_CLICK, nullptr,
                               callback, nullptr);
        iot_button_register_cb(*handle, BUTTON_DOUBLE_CLICK, nullptr,
                               callback, nullptr);
        iot_button_register_cb(*handle, BUTTON_LONG_PRESS_START, nullptr,
                               callback, nullptr);
    }
    return result;
}

void Buttons::init(void)
{
    // Independent high-priority PWR long-press escape. Priority 11 sits above
    // the touch/LVGL task (7), so a sustained PWR
    // hold can force a power-off even if everything else is starved/deadlocked.
    if (xTaskCreate(pwrWatchdogTask, "pwr_wdt", 3072, nullptr, 11, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Emergency PWR watchdog could not start");
    }
    // Start the emergency watchdog first. A wake key held through startup must
    // be released before iot_button begins tracking it, not count as a new click.
    if(Power::wokeFromSleep()) {
        while(gpio_get_level(kBtnPwr)==0) vTaskDelay(pdMS_TO_TICKS(20));
    }
    createButton(kBtnBoot, "boot", &s_boot, bootCb);
    createButton(kBtnKey, "key", &s_key, keyCb);
    createButton(kBtnPwr, "pwr", &s_pwr, pwrCb);
}

}// namespace sys
