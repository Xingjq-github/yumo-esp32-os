#pragma once
#include "app.hpp"
#include "core/lan_hotspot.hpp"
#include <array>

namespace apps {
class Lan : public ui::App {
public:
    const char *name() const override { return "局域网"; }
    const char *icon() const override { return LV_SYMBOL_WIFI; }
    void onCreate(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onDestroy() override;
    bool onBack() override;
private:
    struct Row { lv_obj_t *panel=nullptr,*name=nullptr,*mac=nullptr,*ip=nullptr,*upload=nullptr,*download=nullptr; };
    static void toggleCb(lv_event_t *event);
    static void fieldCb(lv_event_t *event);
    static void timerCb(lv_timer_t *timer);
    void refresh();
    bool saveFields(bool report);
    uint32_t session=0;
    lv_timer_t *timer=nullptr;
    lv_obj_t *body=nullptr,*toggle=nullptr,*status=nullptr,*ssid=nullptr,*password=nullptr,*hint=nullptr,*heading=nullptr,*empty=nullptr;
    std::array<Row,sys::lan::MaxClients> rows{};
    bool fieldsLocked=false;
};
} // namespace apps