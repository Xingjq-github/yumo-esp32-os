// Execute the installed SDK's actual HCI disconnect dispatcher with both
// configurations. No keys, controller, radio or SDK file modifications.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define MYNEWT_VAL(name) CFG_##name
#define CFG_BLE_ENABLE_CONN_REATTEMPT RETRY_ENABLED
#define CFG_BLE_ROLE_PERIPHERAL 1
#define CFG_BLE_ROLE_CENTRAL 1
#define NIMBLE_BLE_CONNECT 1
#define BLE_HS_LOG(...) ((void)0)
constexpr int BLE_HS_ECONTROLLER=12,BLE_HS_ENOMEM=6;
constexpr unsigned BLE_HS_CONN_F_MASTER=1,MAX_REATTEMPT_ALLOWED=3;
constexpr uint8_t BLE_ERR_CONN_ESTABLISHMENT=0x3e,BLE_ERR_CONN_SPVN_TMO=8;
struct ble_hci_ev_disconn_cmp {uint8_t status;uint16_t conn_handle;uint8_t reason;};
struct ble_hs_conn {unsigned bhc_flags=0,bhc_outstanding_pkts=0;int slave_conn=1;};
struct ble_gap_reattempt_ctxt {unsigned count=0;} reattempt_conn;
ble_hs_conn connection;bool present=true;
unsigned delivered=0,restarts=0,deleted=0,woken=0;
uint16_t le16toh(uint16_t v){return v;}
void ble_hs_lock(){}void ble_hs_unlock(){}
const ble_hs_conn *ble_hs_conn_find(uint16_t){return present?&connection:nullptr;}
void ble_hs_hci_add_avail_pkts(unsigned){}
void ble_gap_reattempt_count(uint16_t,unsigned){}
int ble_gap_master_connect_reattempt(uint16_t){++restarts;return 0;}
void ble_l2cap_sig_conn_broken(uint16_t,int){}
void ble_sm_connection_broken(uint16_t){}
void ble_hs_flow_connection_broken(uint16_t){}
int ble_hs_atomic_conn_delete(uint16_t){++deleted;present=false;return 0;}
int ble_gap_slave_adv_reattempt(){++restarts;return 0;}
void ble_gap_rx_disconn_complete(const ble_hci_ev_disconn_cmp *){++delivered;present=false;}
void ble_hs_wakeup_tx(){++woken;}
#include "sdk_disconnect.inc"
void check(bool v){if(!v)std::abort();}
void reset(){present=true;connection={};reattempt_conn={};delivered=restarts=deleted=woken=0;}
int main(){
    reset();ble_hci_ev_disconn_cmp event{0,7,0x3e};
    check(ble_hs_hci_evt_disconn_complete(0,&event,sizeof(event))==0);
    if(RETRY_ENABLED){check(delivered==0&&restarts==1&&deleted==1);}
    else{check(delivered==1&&restarts==0&&deleted==0);}
    reset();event.reason=0x13;check(!ble_hs_hci_evt_disconn_complete(0,&event,sizeof(event)));
    check(delivered==1&&restarts==0);
    reset();connection.slave_conn=0;event.reason=8;
    check(!ble_hs_hci_evt_disconn_complete(0,&event,sizeof(event)));
    check(RETRY_ENABLED?(delivered==0&&restarts==1):(delivered==1&&restarts==0));
    reset();event.reason=0x3e;connection.bhc_flags=BLE_HS_CONN_F_MASTER;
    check(!ble_hs_hci_evt_disconn_complete(0,&event,sizeof(event)));
    check(delivered==1);check(restarts==(RETRY_ENABLED?1u:0u));
    reset();check(ble_hs_hci_evt_disconn_complete(0,&event,0)==BLE_HS_ECONTROLLER);
    check(delivered==0&&restarts==0);
    std::printf("SDK retry=%d: 0x3E, pre-CONNECT 0x08, manual disconnect, Central failure and malformed event passed%s\n",
        RETRY_ENABLED,RETRY_ENABLED?"; reproduced swallowed Peripheral disconnect":"; disconnect delivered, no hidden retries");
}
