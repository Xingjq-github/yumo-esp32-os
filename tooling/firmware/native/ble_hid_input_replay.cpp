// Execute the production ESP-IDF adapter with allocation and notify failures.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <initializer_list>
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=0x101, ESP_ERR_INVALID_ARG=0x102,
    ESP_ERR_INVALID_STATE=0x103, ESP_ERR_NOT_FOUND=0x105, ESP_ERR_TIMEOUT=0x107;
constexpr int BLE_HS_ENOMEM=6, BLE_HS_EBUSY=15, BLE_HS_EAGAIN=1, BLE_HS_ENOTCONN=7;
constexpr int ESP_HID_REPORT_TYPE_INPUT=1;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) {std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);} } while(0)
struct os_mbuf {unsigned kind;};
struct hidd_le_report_item_t {uint16_t handle=91;};
struct map_t {uint16_t hid_protocol_handle=12;};
struct esp_ble_hidd_dev_t {
    bool connected=true; uint8_t protocol=1; uint16_t conn_id=3;
    size_t devices_len=1; map_t devices[1];
};
static esp_ble_hidd_dev_t device, *s_dev=&device;
static hidd_le_report_item_t report;
static int readError=0, flatError=0, notifyError=0;
static bool allocFail=false, reportAbsent=false;
static unsigned live=0, notifications=0, reads=0, protocolFrees=0, reportFrees=0;
static uint8_t nextProtocol=1;
static os_mbuf *allocate(unsigned kind){++live;return new os_mbuf{kind};}
static void os_mbuf_free_chain(os_mbuf *om){CHECK(om&&live);--live;(om->kind?reportFrees:protocolFrees)++;delete om;}
static int ble_att_svr_read_local(uint16_t handle,os_mbuf **out){
    CHECK(handle==12);++reads;if(readError)return readError;*out=allocate(0);return 0;
}
static int ble_hs_mbuf_to_flat(os_mbuf *om,uint8_t *out,size_t n,void*){
    CHECK(om&&om->kind==0&&n==1);*out=nextProtocol;return flatError;
}
static hidd_le_report_item_t *find_report(size_t id,int type,uint8_t *protocol){
    CHECK(id==2&&type==ESP_HID_REPORT_TYPE_INPUT&&*protocol==nextProtocol);return reportAbsent?nullptr:&report;
}
static os_mbuf *ble_hs_mbuf_from_flat(uint8_t *data,size_t n){CHECK(data&&n==4);return allocFail?nullptr:allocate(1);}
static int ble_gatts_notify_custom(uint16_t conn,uint16_t handle,os_mbuf *om){
    CHECK(conn==3&&handle==91&&om&&om->kind==1);++notifications;os_mbuf_free_chain(om);return notifyError;
}
#include "cmake/nimble_hid_input.inc"
static void reset(){CHECK(live==0);device={};s_dev=&device;readError=flatError=notifyError=0;
    allocFail=reportAbsent=false;notifications=reads=protocolFrees=reportFrees=0;nextProtocol=1;}
static int send(){uint8_t data[4]{};const int result=nimble_hidd_dev_input_set(&device,0,2,data,4);CHECK(live==0);return result;}
int main(){
    reset();CHECK(send()==ESP_OK);CHECK(notifications==1&&protocolFrees==1&&reportFrees==1);
    for(int rc:{BLE_HS_ENOMEM,BLE_HS_EBUSY,BLE_HS_EAGAIN,BLE_HS_ENOTCONN,99}){
        int expected=rc==BLE_HS_ENOMEM?ESP_ERR_NO_MEM:(rc==BLE_HS_EBUSY||rc==BLE_HS_EAGAIN)?ESP_ERR_TIMEOUT:
            rc==BLE_HS_ENOTCONN?ESP_ERR_INVALID_STATE:ESP_FAIL;
        reset();readError=rc;CHECK(send()==expected);CHECK(!notifications&&!live);
        reset();flatError=rc;nextProtocol=0;CHECK(send()==expected);CHECK(protocolFrees==1&&!notifications&&device.protocol==1);
        reset();notifyError=rc;CHECK(send()==expected);CHECK(protocolFrees==1&&reportFrees==1&&notifications==1);
    }
    reset();allocFail=true;CHECK(send()==ESP_ERR_NO_MEM);CHECK(protocolFrees==1&&!notifications);
    allocFail=false;CHECK(send()==ESP_OK);CHECK(notifications==1);
    reset();reportAbsent=true;CHECK(send()==ESP_ERR_NOT_FOUND);CHECK(protocolFrees==1&&!notifications);
    reset();device.connected=false;CHECK(send()==ESP_ERR_INVALID_STATE);CHECK(!reads);
    reset();s_dev=nullptr;CHECK(send()==ESP_ERR_INVALID_STATE);CHECK(!reads);
    reset();device.devices_len=0;CHECK(send()==ESP_ERR_INVALID_ARG);CHECK(!reads);
    reset();CHECK(nimble_hidd_dev_input_set(&device,0,2,nullptr,4)==ESP_ERR_INVALID_ARG);CHECK(!reads);
    reset();nextProtocol=0;CHECK(send()==ESP_OK);CHECK(device.protocol==0);
    std::printf("{\"passed\":true,\"checks\":%u,\"cases\":24,\"method\":\"production HID input adapter with allocation/protocol/notify failure injection\"}\n",checks);
}
