#pragma once
#include "ble_hid.hpp"
#include "ble_peer_store.hpp"

namespace sys {
enum class BluetoothOperation : uint8_t { None, Discover, Saved, Keyboard };

// Owned exclusively by SystemService. One value-only request survives the
// asynchronous disconnect; never keep App pointers or start a background retry.
class BluetoothConnectionFlow {
public:
    BluetoothOperation operation=BluetoothOperation::None;
    BleBond target{};
    int error=0;
    bool switching=false;

    esp_err_t begin(BluetoothOperation next,const BleBond &peer,
                    const BleScanDevice &device,bool authenticated,int64_t now) {
        if(!BleHid::enabled() || BleHid::state()==BleHidState::Starting) return ESP_ERR_INVALID_STATE;
        BleBond selected=peer;
        if(next==BluetoothOperation::Saved) {
            BleBond saved[BlePeerStore::Capacity]{};const int count=BleHid::bonds(saved,BlePeerStore::Capacity);bool found=false;
            for(int i=0;i<count;++i) if(BlePeerStore::same(saved[i],peer)) {selected=saved[i];found=true;break;}
            if(!found) return ESP_ERR_NOT_FOUND; // don't disconnect a working host for a stale row
            if(BleHid::connected() && BlePeerStore::same(BleHid::peer(),selected)) return ESP_OK;
        }
        operation=next;target=selected;error=0;
        device_=device;authenticated_=authenticated;
        switching=!BleHid::connectionIdle();pending_=true;deadline_=now+10000000;
        const auto result=BleHid::disconnect();
        if(result!=ESP_OK) finish(result);
        return result;
    }
    void cancel() {
        finish(0);target={};
        (void)BleHid::stopDiscoverable();BleHid::cancelUserScan();
    }
    void cancelFor(const BleBond &peer) {
        if(operation!=BluetoothOperation::None && BlePeerStore::same(target,peer)) cancel();
    }
    void poll(int64_t now,bool stopping) {
        if(operation==BluetoothOperation::None) return;
        if(stopping || !BleHid::enabled()) {cancel();return;}
        if(pending_) {
            if(!BleHid::connectionIdle()) {
                if(now>=deadline_) {(void)BleHid::stopDiscoverable();finish(ESP_ERR_TIMEOUT);}
                return;
            }
            pending_=false;switching=false;deadline_=now+120000000;
            // reconnectBond revalidates the bond after the old link has gone.
            const auto result=operation==BluetoothOperation::Saved?BleHid::reconnectBond(target):
                operation==BluetoothOperation::Keyboard?BleHid::connectKeyboard(device_):
                BleHid::makeDiscoverable(authenticated_);
            if(result!=ESP_OK) finish(result);
            return;
        }
        if(BleHid::connected()) {finish(0);return;}
        if(BleHid::state()==BleHidState::Failed) {finish(BleHid::lastError());return;}
        const bool ended=BleHid::state()==BleHidState::Ready && !BleHid::discoverableSecondsRemaining();
        if(ended || now>=deadline_) {
            const bool discovery=operation==BluetoothOperation::Discover;
            (void)BleHid::stopDiscoverable();finish(discovery?0:ESP_ERR_TIMEOUT);
        }
    }
private:
    BleScanDevice device_{};
    bool pending_=false,authenticated_=false;
    int64_t deadline_=0;
    void finish(int result) {operation=BluetoothOperation::None;switching=pending_=false;error=result;device_={};}
};
} // namespace sys
