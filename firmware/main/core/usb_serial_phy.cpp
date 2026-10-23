#include "usb_serial_phy.hpp"
#include "esp_private/usb_phy.h"
namespace {
usb_phy_handle_t serialPhy=nullptr;
}
esp_err_t yumoReleaseSerialPhy() {
    if(!serialPhy)return ESP_OK;
    const auto rc=usb_del_phy(serialPhy);if(rc==ESP_OK)serialPhy=nullptr;return rc;
}
esp_err_t yumoRestoreSerialPhy() {
    if(serialPhy)return ESP_OK;
    usb_phy_config_t cfg{};cfg.controller=USB_PHY_CTRL_SERIAL_JTAG;cfg.target=USB_PHY_TARGET_INT;
    return usb_new_phy(&cfg,&serialPhy);
}
