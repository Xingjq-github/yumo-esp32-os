#pragma once
#include "compact_ui.hpp"
#include "theme.hpp"
#include "core/ble_hid.hpp"
#include <cstring>
namespace ui {
struct BluetoothIdentity {
    lv_obj_t *name=nullptr,*address=nullptr;
    void create(lv_obj_t *parent,int width=238,int x=1,int y=0) {
        name=compact::label(parent,"",width);lv_obj_set_pos(name,x,y);
        address=compact::label(parent,"",width);lv_obj_set_pos(address,x,y+20);
        lv_obj_set_style_text_font(address,&lv_font_montserrat_10,0);
        lv_obj_set_style_text_color(address,theme::muted(),0);
    }
    void update(const sys::BleBond &peer,const char *empty="未连接设备") {
        bool valid=false;for(auto b:peer.addr) valid|=b!=0;
        compact::setText(name,valid?(peer.name[0]?peer.name:"未提供名称"):empty);
        char text[18]{};if(valid) sys::BleHid::formatAddr(peer,text,sizeof(text));
        compact::setText(address,text);
    }
};
}
