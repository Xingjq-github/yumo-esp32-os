#include "usb_storage.hpp"
#include "usb_serial_phy.hpp"
#include "usb_storage_io.hpp"
#include "sd_media.hpp"
#include "power_mgr.hpp"
#include "display_cast.hpp"
#include "bsp/esp-bsp.h"
#include "sdmmc_cmd.h"
#include "sd_protocol_defs.h"
#include "driver/sdmmc_host.h"
#include "esp_private/usb_phy.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

void displayCastReserveConsole(bool on);
namespace {
enum class Mode {Off,Preparing,Ready,Stopping,Failed};
std::atomic<Mode> mode{Mode::Off};
// USB disk mode is deliberately volatile: every boot starts with Serial/JTAG.
std::atomic<bool> running{false},stop{false},attached{false},ejected{false},admit{false};
std::atomic<esp_err_t> lastError{ESP_OK};
std::mutex ioMutex;
// All callbacks serialize through ioMutex. SD DMA never receives PSRAM or an
// unaligned USB buffer. Synchronous writes report actual card errors to SCSI.
alignas(4) uint8_t sector[512];
sdmmc_card_t card{};

char serialName[20];
const char *strings[]={"\x09\x04","YUMO","Yumo SD Card",serialName,"SD Card"};
const tusb_desc_device_t device={
    .bLength=sizeof(tusb_desc_device_t),.bDescriptorType=TUSB_DESC_DEVICE,.bcdUSB=0x0200,
    .bDeviceClass=0,.bDeviceSubClass=0,.bDeviceProtocol=0,.bMaxPacketSize0=64,
    .idVendor=0x303a,.idProduct=0x4002,.bcdDevice=0x0100,.iManufacturer=1,.iProduct=2,.iSerialNumber=3,.bNumConfigurations=1
};
const uint8_t configuration[]={
    TUD_CONFIG_DESCRIPTOR(1,1,0,TUD_CONFIG_DESC_LEN+TUD_MSC_DESC_LEN,0,100),
    TUD_MSC_DESCRIPTOR(0,4,0x01,0x81,64)
};
void usbEvent(tinyusb_event_t *event,void *) {
    attached.store(event->id==TINYUSB_EVENT_ATTACHED);
    // Do not auto-remount on disconnect. SD ownership remains explicit even
    // with an unconnected VBUS sense pin or a host temporarily suspending USB.
}
bool ready(uint8_t lun) {
    if(lun==0&&admit.load()&&!ejected.load()&&!stop.load())return true;
    tud_msc_set_sense(lun,SCSI_SENSE_NOT_READY,0x3a,0);return false;
}
int32_t transfer(uint8_t lun,uint32_t lba,uint32_t offset,void *buffer,uint32_t size,bool write) {
    const std::lock_guard<std::mutex> lock(ioMutex);
    if(!ready(lun))return -1;
    int error=0;
    const auto result=core::usb_disk::transfer(card.csd.capacity,lba,offset,buffer,size,write,sector,error,
        [](uint64_t lba,uint8_t *data){return sdmmc_read_sectors(&card,data,lba,1);},
        [](uint64_t lba,const uint8_t *data){return sdmmc_write_sectors(&card,data,lba,1);});
    if(result<0) {
        if(error){lastError.store(error);tud_msc_set_sense(lun,SCSI_SENSE_MEDIUM_ERROR,write?0x0c:0x11,0);}
        else tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x21,0);
    }
    return result;
}
esp_err_t flushCard() {
    // A successful SEND_STATUS alone is insufficient: wait for programming to
    // finish and reject R1 card errors before acknowledging flush/eject.
    const auto deadline=esp_timer_get_time()+1000000;
    do {
        sdmmc_command_t cmd{};cmd.opcode=MMC_SEND_STATUS;cmd.arg=MMC_ARG_RCA(card.rca);
        cmd.flags=SCF_CMD_AC|SCF_RSP_R1;cmd.timeout_ms=100;
        auto error=card.host.do_transaction(card.host.slot,&cmd);
        if(error==ESP_OK)error=cmd.error;
        if(error!=ESP_OK)return error;
        if(cmd.response[0]&0xFDFFE008u)return ESP_FAIL;
        if((cmd.response[0]&MMC_R1_READY_FOR_DATA)&&MMC_R1_CURRENT_STATE_STATUS(cmd.response[0])==MMC_R1_CURRENT_STATE_TRAN)return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(1));
    }while(esp_timer_get_time()<deadline);
    return ESP_ERR_TIMEOUT;
}
void execute() {
    sys::Power::WorkGuard work;
    bool raw=false,usb=false,reserved=false,claimed=false,cardReady=false;
    esp_err_t error=ESP_OK;
    const auto deadline=esp_timer_get_time()+12000000;
    // A cancelled switch still waits for the existing filesystem eject. Never
    // disable SD signals underneath the VFS or overwrite its failure reason.
    while(core::sd::state()==core::sd::State::Ejecting&&esp_timer_get_time()<deadline)
        vTaskDelay(pdMS_TO_TICKS(20));
    if(!stop.load()) {
        claimed=core::sd::claimUsb();
        if(!claimed)error=ESP_ERR_INVALID_STATE;
        else {
            sdmmc_host_t host;sdmmc_slot_config_t slot;
            bsp_sdcard_get_sdmmc_host(SDMMC_HOST_SLOT_0,&host);
            bsp_sdcard_sdmmc_get_slot(host.slot,&slot);
            host.max_freq_khz=SDMMC_FREQ_DEFAULT;host.command_timeout_ms=1000;
            error=sdmmc_host_init();raw=error==ESP_OK;
            if(error==ESP_OK)error=sdmmc_host_init_slot(host.slot,&slot);
            if(error==ESP_OK){error=sdmmc_card_init(&host,&card);cardReady=error==ESP_OK;}
            if(error==ESP_OK&&card.csd.sector_size!=512)error=ESP_ERR_NOT_SUPPORTED;
            if(error==ESP_OK) {
                uint8_t mac[6]{};error=esp_read_mac(mac,ESP_MAC_BASE);
                std::snprintf(serialName,sizeof(serialName),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
            }
            if(error==ESP_OK&&!stop.load()) {
                displayCastReserveConsole(true);reserved=true;
                error=yumoReleaseSerialPhy();
                tinyusb_config_t cfg=TINYUSB_DEFAULT_CONFIG();cfg.task.xCoreID=0;cfg.task.size=6144;
                cfg.descriptor.device=&device;cfg.descriptor.string=strings;cfg.descriptor.string_count=5;
                cfg.descriptor.full_speed_config=configuration;cfg.event_cb=usbEvent;
                admit.store(true);
                if(error==ESP_OK)error=tinyusb_driver_install(&cfg);
                usb=error==ESP_OK;
                if(usb) {
                    mode.store(Mode::Ready);
                    while(!stop.load())vTaskDelay(pdMS_TO_TICKS(20));
                }
            }
        }
    }
    mode.store(Mode::Stopping);admit.store(false);
    // Device-confirmed detach does not depend on the host sending SCSI eject.
    // Fence new commands, finish the in-flight write and wait for card programming.
    // Host-side data not sent over USB cannot be flushed by this device.
    bool mediaClean=true;
    {
        const std::lock_guard<std::mutex> lock(ioMutex);
        mediaClean=lastError.load()==ESP_OK;
        if(cardReady) {
            const auto rc=flushCard();
            if(rc!=ESP_OK){error=rc;mediaClean=false;}
        }
    }
    if(!mediaClean&&error==ESP_OK)error=lastError.load();
    bool clean=true;
    if(usb){tud_disconnect();const auto rc=tinyusb_driver_uninstall();if(rc!=ESP_OK){error=rc;clean=false;}}
    if(raw&&clean){const auto rc=sdmmc_host_deinit();if(rc!=ESP_OK){error=rc;clean=false;}}
    if(claimed&&clean&&bsp_sdcard_quiesce()!=ESP_OK){error=ESP_FAIL;clean=false;}
    if(reserved&&clean) {
        const auto rc=yumoRestoreSerialPhy();if(rc!=ESP_OK){error=rc;clean=false;}
        else displayCastReserveConsole(false);
    }
    if(claimed)core::sd::releaseUsb(clean);
    else {
        core::sd::cancelUsbRequest();
        if(core::sd::state()!=core::sd::State::Ejected&&error==ESP_OK)error=ESP_ERR_INVALID_STATE;
    }
    // Restore the flashing interface even after a card error, but leave that
    // card ejected until an explicit retry. Teardown failure retains ownership.
    if(clean&&mediaClean&&core::sd::state()==core::sd::State::Ejected) {
        core::sd::Session mount(core::sd::requestAccess(true));
        if(!mount&&error==ESP_OK)error=ESP_FAIL;
    }
    if(error!=ESP_OK)lastError.store(error);
    attached.store(false);mode.store(error==ESP_OK?Mode::Off:Mode::Failed);
}
void worker(void *) {
    execute(); // All C++ guards (including the PM lock) are destroyed here.
    running.store(false);
    vTaskDeleteWithCaps(nullptr);
}
}
namespace sys {
esp_err_t UsbStorage::setEnabled(bool on) {
    if(!on) {
        if(!running.load())return ESP_OK;
        stop.store(true);return ESP_OK;
    }
    if(Power::sleepPending()||!DisplayCast::idle()||core::sd::usbExclusive()||running.exchange(true))return ESP_ERR_INVALID_STATE;
    if(!core::sd::requestUsb()){running.store(false);return ESP_ERR_INVALID_STATE;}
    mode.store(Mode::Preparing);stop.store(false);attached.store(false);ejected.store(false);lastError.store(ESP_OK);
    if(xTaskCreatePinnedToCoreWithCaps(worker,"usb_sd",8192,nullptr,3,nullptr,0,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
        // No raw owner exists; the original state/epoch still fence the pending
        // eject. An allocation failure must not turn into permanent SD damage.
        stop.store(true);mode.store(Mode::Failed);running.store(false);lastError.store(ESP_ERR_NO_MEM);
        core::sd::cancelUsbRequest();return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
bool UsbStorage::enabled(){return running.load()&&!stop.load();}
bool UsbStorage::idle(){return !running.load()&&!core::sd::usbExclusive();}
bool UsbStorage::transitioning(){return running.load()&&(mode.load()!=Mode::Ready||stop.load());}
const char *UsbStorage::statusText(){
    if(mode.load()==Mode::Preparing)return "正在结束SD任务并连接电脑…";
    if(mode.load()==Mode::Stopping)return "正在恢复开发接口与SD卡…";
    if(mode.load()==Mode::Failed)return core::sd::usbExclusive()?"USB关闭失败，请关机后重试":"开发接口已恢复，请检查SD卡后重试";
    if(mode.load()==Mode::Ready){
        if(lastError.load()!=ESP_OK)return "SD读写出错，请停止复制并切回开发模式";
        if(ejected.load())return "电脑已弹出，可切回开发与烧录";
        return attached.load()?"电脑独占SD卡 · 可在此切回开发模式":"等候电脑连接 · SD卡由USB独占";
    }
    return "开发与烧录 · 支持电脑画面投放";
}
}
// Strong TinyUSB callbacks replace its weak defaults. Supplying both mount
// hooks keeps esp_tinyusb's automatic VFS/deferred-write glue out of the link.
// This adapter is pinned to esp_tinyusb 2.0.1; the USB transport is unmodified.
extern "C" {
void msc_storage_mount_to_usb() {}
void msc_storage_mount_to_app() {}
void tud_msc_inquiry_cb(uint8_t,uint8_t vendor[8],uint8_t product[16],uint8_t revision[4]) {
    std::memcpy(vendor,"YUMO    ",8);std::memcpy(product,"SD Card Reader  ",16);std::memcpy(revision,"1.00",4);
}
uint8_t tud_msc_get_maxlun_cb(){return 1;}
bool tud_msc_test_unit_ready_cb(uint8_t lun){return ready(lun);}
bool tud_msc_is_writable_cb(uint8_t lun){return ready(lun);}
void tud_msc_capacity_cb(uint8_t lun,uint32_t *count,uint16_t *size) {
    *count=lun==0?card.csd.capacity:0;*size=512;
}
bool tud_msc_start_stop_cb(uint8_t lun,uint8_t,bool start,bool load) {
    if(lun)return false;
    if(load&&!start) {
        const std::lock_guard<std::mutex> lock(ioMutex);
        if(!ready(lun))return ejected.load();
        const auto error=flushCard();
        if(error!=ESP_OK){lastError.store(error);tud_msc_set_sense(lun,SCSI_SENSE_MEDIUM_ERROR,0x0c,0);return false;}
        ejected.store(true);
    }
    return true;
}
int32_t tud_msc_read10_cb(uint8_t lun,uint32_t lba,uint32_t offset,void *buf,uint32_t size){return transfer(lun,lba,offset,buf,size,false);}
int32_t tud_msc_write10_cb(uint8_t lun,uint32_t lba,uint32_t offset,uint8_t *buf,uint32_t size){return transfer(lun,lba,offset,buf,size,true);}
int32_t tud_msc_scsi_cb(uint8_t lun,const uint8_t command[16],void *,uint16_t) {
    if(command[0]==0x35) {
        const std::lock_guard<std::mutex> lock(ioMutex);if(!ready(lun))return -1;
        const auto error=flushCard();if(error==ESP_OK)return 0;
        lastError.store(error);tud_msc_set_sense(lun,SCSI_SENSE_MEDIUM_ERROR,0x0c,0);return -1;
    }
    if(command[0]==0x1e)return 0;
    tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x20,0);return -1;
}
}
