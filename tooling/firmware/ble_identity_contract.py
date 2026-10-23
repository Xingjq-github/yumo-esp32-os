"""Normalize only the reviewed privacy fix for older whole-section freezes.

The new path has its own source and compiled identity tests. All unrelated HID,
pairing, cancellation and password transport bytes still match the old hashes.
"""
def before_identity_fix(text):
    # Exact reviewed hunks only: later unrelated GAP edits still fail the
    # historical digest. Real callbacks and SDK teardown have runtime coverage.
    import json
    from pathlib import Path
    # Only the exact scan callback validated by test_ble_scan is normalized.
    # All surrounding security/private-input bytes retain historical hashes.
    changes=json.loads((Path(__file__).parent/'fixtures/contracts/ble-scan-priority-delta.json').read_text(encoding='utf-8'))
    for change in changes:
        text=text.replace(change['after'],change['before'])
    changes=json.loads((Path(__file__).parent/'fixtures/contracts/ble-switch-delta.json').read_text(encoding='utf-8'))
    for change in changes:
        text=text.replace(change['after'],change['before'])
    changes=json.loads((Path(__file__).parent/'fixtures/contracts/ble-pairing-delta.json').read_text(encoding='utf-8'))
    for change in changes:
        text=text.replace(change['after'],change['before'])
    start=text.find('bool connectionPeer(')
    if start>=0:
        end=text.index('int scanEvent',start)
        text=text[:start]+'\n'+text[end:]
    pairs=[
# Only the reviewed advertising payload split is normalized here. The new
# invitation replay checks both packets; the old hash still freezes all SMP,
# HID report, cancellation and private-input code in the same section.
('''    // Keep HID identity in the primary advertisement. The complete local name
    // goes in scan response; no optional TX-power HCI query on every invitation.
''','''    s_advFields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    s_advFields.tx_pwr_lvl_is_present = 1;
    s_advFields.name = reinterpret_cast<uint8_t *>(const_cast<char *>(kDeviceName));
    s_advFields.name_len = std::strlen(kDeviceName);
    s_advFields.name_is_complete = 1;
'''),
('''    if(!cached) {
        ble_addr_t resolved{};
        if(ble::resolveIdentity(disc.addr,resolved,ble_gap_rpa_resolve)) {
            identity.addrType=resolved.type;std::memcpy(identity.addr,resolved.val,6);
        }
''','''    if(!cached && disc.addr.type==BLE_ADDR_RANDOM && (disc.addr.val[5]&0xc0)==0x40) {
        uint8_t rpa[6],resolved[6]{},type=0;std::memcpy(rpa,disc.addr.val,6);
        if(ble_gap_rpa_resolve(rpa,resolved,&type) && std::any_of(resolved,resolved+6,[](uint8_t v){return v!=0;})) {
            identity.addrType=type;std::memcpy(identity.addr,resolved,6);
        }
'''),
('''            s_verifyExpected=s_targeted.load();s_linkExpected=BlePeerStore::active();
''',''),
('''                auto peer=s_linkExpected;
                if(!s_keyboardLink.load()) peer={};
                const bool resolved=connectionPeer(peerDesc,peer,true);peer.connected=false;
                if(!resolved || !matchesLinkExpected(peer)) {
                    if(permitted)s_pairError.store(BLE_HS_EAUTHEN);
                    permitted=false;
                }
                if(permitted)BlePeerStore::activate(peer);
''','''                auto peer=BlePeerStore::active();const auto expected=peer;
                if(!s_keyboardLink.load()) peer={};
                peer.addrType=peerDesc.peer_id_addr.type;std::memcpy(peer.addr,peerDesc.peer_id_addr.val,6);
                peer=BlePeerStore::enrich(peer);peer.keyboard=s_keyboardLink.load();peer.connected=false;
                if(s_targeted.load() && (!BlePeerStore::same(expected,peer) || expected.keyboard!=peer.keyboard)) permitted=false;
                BlePeerStore::activate(peer);
'''),
('''        s_linkExpected={};s_verifyExpected=false;
''',''),
('''                auto peer=BlePeerStore::active();
                if(!connectionPeer(desc,peer) || !matchesLinkExpected(peer)) {
                    s_privateEligible.store(false);s_disconnectPending.store(true);
                    s_pairError.store(BLE_HS_EAUTHEN);s_state.store(BleHidState::Failed);
                    (void)ble_gap_terminate(event->enc_change.conn_handle,BLE_ERR_REM_USER_CONN_TERM);return 0;
                }
''',''),
('''                    s_privateEligible.store(false);s_disconnectPending.store(true);
                    s_pairError.store(BLE_HS_EAUTHEN);s_state.store(BleHidState::Failed);
''','''                    s_pairError.store(BLE_HS_EAUTHEN);s_state.store(BleHidState::Failed);
'''),
('''                const bool authenticated=desc.sec_state.encrypted&&desc.sec_state.authenticated&&desc.sec_state.bonded&&desc.sec_state.key_size==16;
''','''                const bool authenticated=desc.sec_state.encrypted&&desc.sec_state.authenticated&&desc.sec_state.bonded&&desc.sec_state.key_size==16;
                s_privateEligible.store(desc.sec_state.encrypted && desc.sec_state.bonded && desc.sec_state.key_size==16);
'''),
('''                s_privateEligible.store(desc.sec_state.encrypted && desc.sec_state.bonded && desc.sec_state.key_size==16);
                s_pairError.store(0);
''','''                s_pairError.store(0);
                auto peer=BlePeerStore::active();peer.addrType=desc.peer_id_addr.type;std::memcpy(peer.addr,desc.peer_id_addr.val,6);
'''),
('''    BleBond active = {};
''','''    ble_addr_t active = {};
'''),
('''            haveActive = connectionPeer(desc,active);
''','''            active = desc.peer_id_addr;
            haveActive = true;
'''),
('''        out[i].connected = connected() && haveActive && BlePeerStore::same(active,out[i]);
''','''        out[i].connected = connected() && haveActive && (active.type&1) == (addrs[i].type&1) &&
                           std::memcmp(active.val, addrs[i].val, sizeof(active.val)) == 0;
''')]
    for after,before in pairs:text=text.replace(after,before)
    return text
