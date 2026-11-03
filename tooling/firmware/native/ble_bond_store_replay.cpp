// All identities/keys are synthetic. Links the unmodified official NVS page,
// storage and garbage collector plus the actual production persistence adapter.
#include "syscfg/syscfg.h"
#include "host/ble_hs.h"
#include "ble_store_config_priv.h"
#include "yumo_ble_store.h"
#include "nvs_capacity_host.hpp"
#include "nvs.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static unsigned checks = 0, scenarios = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
extern "C" {
ble_store_config_vars_t *ble_store_config_vars = nullptr;
void __wrap_ble_store_config_conf_init(void);
int __wrap_ble_store_config_persist_our_secs(void);
int __wrap_ble_store_config_persist_peer_secs(void);
int __wrap_ble_store_config_persist_cccds(void);
int __wrap_ble_store_config_persist_rpa_recs(void);
int __wrap_ble_store_config_persist_csfcs(void);
}
static ble_store_config_vars_t ram{};
static std::array<ble_store_value_sec,16> our{}, peer{};
static std::array<ble_store_value_rpa_rec,16> rpa{};
static std::array<ble_store_value_csfc,16> csfc{};
static std::array<ble_store_value_cccd,128> cccds{};
static ble_store_value_local_irk irk{};
static bool maximumProfile=true;

template<class T> std::vector<uint8_t> bytes(const T &v) {
    const auto *p = reinterpret_cast<const uint8_t *>(&v);
    return {p, p + sizeof(v)};
}
void blob(const char *ns, const char *key, const void *p, size_t n) {
    nvs_handle_t h; CHECK(nvs_open(ns,NVS_READWRITE,&h)==ESP_OK);
    CHECK(nvs_set_blob(h,key,p,n)==ESP_OK); CHECK(nvs_commit(h)==ESP_OK); nvs_close(h);
}
template<class T> void blob(const char *ns, const char *key, const T &v) { blob(ns,key,&v,sizeof(v)); }
std::vector<uint8_t> read(const char *ns, const char *key) {
    nvs_handle_t h; if(nvs_open(ns,NVS_READONLY,&h)!=ESP_OK) return {};
    size_t size=0; auto err=nvs_get_blob(h,key,nullptr,&size);
    if(err==ESP_ERR_NVS_NOT_FOUND){nvs_close(h);return {};}
    CHECK(err==ESP_OK); std::vector<uint8_t> b(size);
    CHECK(nvs_get_blob(h,key,b.data(),&size)==ESP_OK); nvs_close(h); return b;
}
void init() {
    ram={}; ble_store_config_vars=&ram;
    __wrap_ble_store_config_conf_init();
}
void seedObjects() {
    for(unsigned i=0;i<16;++i) {
        auto &v=our[i]; v={}; v.peer_addr.type=i%2; v.peer_addr.val[0]=uint8_t(i+1);
        v.peer_addr.val[5]=0xc0; v.bond_count=uint16_t(i+10); v.key_size=16;
        v.ediv=uint16_t(1000+i);v.rand_num=0xfedcba9876543210ull+i;
        v.ltk_present=v.irk_present=v.csrk_present=v.authenticated=v.sc=1;
        v.sign_counter=0x10203040u+i;
        for(unsigned j=0;j<16;++j) {v.ltk[j]=uint8_t(i*7+j);v.irk[j]=uint8_t(i*13+j);v.csrk[j]=uint8_t(i*19+j);}
        peer[i]=v;peer[i].ltk[0]^=0x5a;peer[i].rand_num^=0xf0f0;
        rpa[i].peer_addr=v.peer_addr;rpa[i].peer_rpa_addr=v.peer_addr;
        rpa[i].peer_rpa_addr.type=1;rpa[i].peer_rpa_addr.val[5]=0x40;
        csfc[i].peer_addr=v.peer_addr;csfc[i].csfc[0]=uint8_t(i&7);
        for(unsigned c=0;c<8;++c) {auto &x=cccds[i*8+c];x={};x.peer_addr=v.peer_addr;
            x.chr_val_handle=uint16_t(20+c*3);x.flags=c&1?2:1;x.value_changed=c&1;}
    }
    irk.addr=our[0].peer_addr;for(unsigned j=0;j<16;++j)irk.irk[j]=uint8_t(0xa0+j);
}
void legacy(unsigned peers,unsigned perPeer) {
    char key[16];
    for(unsigned i=0;i<peers;++i) {
        std::snprintf(key,sizeof key,"our_sec_%u",i+1);blob("nimble_bond",key,our[i]);
        std::snprintf(key,sizeof key,"peer_sec_%u",i+1);blob("nimble_bond",key,peer[i]);
        std::snprintf(key,sizeof key,"rpa_rec_%u",i+1);blob("nimble_bond",key,rpa[i]);
        std::snprintf(key,sizeof key,"csfc_sec_%u",i+1);blob("nimble_bond",key,csfc[i]);
        for(unsigned j=0;j<perPeer;++j) {
            std::snprintf(key,sizeof key,"cccd_sec_%u",i*perPeer+j+1);blob("nimble_bond",key,cccds[i*8+j]);
        }
    }
    blob("nimble_bond","local_irk_1",irk);
}
void checkObjects(unsigned peers,unsigned perPeer) {
    CHECK(ble_store_config_num_our_secs==int(peers));CHECK(ble_store_config_num_peer_secs==int(peers));
    CHECK(ble_store_config_num_rpa_recs==int(peers));CHECK(ble_store_config_num_csfcs==int(peers));
    CHECK(ble_store_config_num_cccds==int(peers*perPeer));
    for(unsigned i=0;i<peers;++i) {
        CHECK(!std::memcmp(&ble_store_config_our_secs[i],&our[i],sizeof(our[i])));
        CHECK(!std::memcmp(&ble_store_config_peer_secs[i],&peer[i],sizeof(peer[i])));
        CHECK(!std::memcmp(&ble_store_config_rpa_recs[i],&rpa[i],sizeof(rpa[i])));
        CHECK(!std::memcmp(&ble_store_config_csfcs[i],&csfc[i],sizeof(csfc[i])));
        for(unsigned j=0;j<perPeer;++j) {
            const auto &a=ble_store_config_cccds[i*perPeer+j],&b=cccds[i*8+j];
            CHECK(!std::memcmp(&a.peer_addr,&b.peer_addr,7));CHECK(a.flags==b.flags);
            CHECK(a.chr_val_handle==b.chr_val_handle);CHECK(a.value_changed==b.value_changed);
        }
    }
    CHECK(ble_store_config_num_local_irks==1);
    CHECK(!std::memcmp(&ble_store_config_local_irks[0],&irk,sizeof irk));
    CHECK(read("nimble_bond","local_irk_1")==bytes(irk));
}
void systemProfile() {
    nvs_handle_t h;CHECK(nvs_open("settings",NVS_READWRITE,&h)==ESP_OK);
    const char *keys[]={"brightness","volume","sysinfo","cast_dark","theme","dino_best"};
    for(const char *key:keys)CHECK(nvs_set_u32(h,key,42)==ESP_OK);
    CHECK(nvs_set_i64(h,"clock",1793520000)==ESP_OK);
    const std::string ssid(maximumProfile?32:16,'s'),pass(maximumProfile?64:16,'p'),track(maximumProfile?1023:127,'m');
    CHECK(nvs_set_str(h,"ssid",ssid.c_str())==ESP_OK);CHECK(nvs_set_str(h,"pass",pass.c_str())==ESP_OK);
    CHECK(nvs_set_str(h,"lan_ssid",ssid.c_str())==ESP_OK);CHECK(nvs_set_str(h,"lan_pass",pass.c_str())==ESP_OK);
    std::string history;for(unsigned i=0;i<16;++i)history+=ssid+'\t'+pass+'\n';
    CHECK(nvs_set_str(h,"wifihist",history.c_str())==ESP_OK);CHECK(nvs_set_str(h,"mp3_track",track.c_str())==ESP_OK);
    nvs_close(h);
    const std::array<uint8_t,16> drift{};const std::array<uint8_t,22> power{};const std::array<uint8_t,140> order{};
    blob("settings","rtc_drift_v1",drift);blob("settings","power_v2",power);blob("settings","app_order_v1",order);
    CHECK(nvs_open("yumo_xiaozhi",NVS_READWRITE,&h)==ESP_OK);
    CHECK(nvs_set_str(h,"client_id","00000000-0000-4000-8000-000000000000")==ESP_OK);nvs_close(h);
}
void fill(unsigned n) {
    std::memcpy(ble_store_config_our_secs,our.data(),n*sizeof(our[0]));ble_store_config_num_our_secs=int(n);
    ble_store_config_our_bond_count=n?our[n-1].bond_count:0;CHECK(!__wrap_ble_store_config_persist_our_secs());
    std::memcpy(ble_store_config_peer_secs,peer.data(),n*sizeof(peer[0]));ble_store_config_num_peer_secs=int(n);
    ble_store_config_peer_bond_count=n?peer[n-1].bond_count:0;CHECK(!__wrap_ble_store_config_persist_peer_secs());
    std::memcpy(ble_store_config_rpa_recs,rpa.data(),n*sizeof(rpa[0]));ble_store_config_num_rpa_recs=int(n);
    CHECK(!__wrap_ble_store_config_persist_rpa_recs());
    std::memcpy(ble_store_config_csfcs,csfc.data(),n*sizeof(csfc[0]));ble_store_config_num_csfcs=int(n);
    CHECK(!__wrap_ble_store_config_persist_csfcs());
    std::memcpy(ble_store_config_cccds,cccds.data(),n*8*sizeof(cccds[0]));ble_store_config_num_cccds=int(n*8);
    CHECK(!__wrap_ble_store_config_persist_cccds());
}

int main(int argc,char **argv) {
    maximumProfile=!(argc>1&&!std::strcmp(argv[1],"typical"));
    seedObjects();CHECK(nvs_host::reset()==ESP_OK);legacy(8,3);systemProfile();
    std::array<uint8_t,652> oldMeta{};oldMeta[0]=1;blob("ble_peers","v1",oldMeta);
    const auto before=nvs_host::snapshot();nvs_host::clearIo();
    if(maximumProfile) {
        nvs_stats_t beforeStats{};CHECK(nvs_get_stats(nullptr,&beforeStats)==ESP_OK);
        nvs_handle_t h;CHECK(nvs_open("settings",NVS_READWRITE,&h)==ESP_OK);
        const std::string otherHistory(1568,'q');
        const auto legacyHistoryErr=nvs_set_str(h,"wifihist",otherHistory.c_str());nvs_close(h);
        std::fprintf(stderr,"legacy8 max-profile used=%zu available=%zu history-replacement-rc=%d\n",
            beforeStats.used_entries,beforeStats.available_entries,int(legacyHistoryErr));
        CHECK(nvs_host::restore(before)==ESP_OK);nvs_host::clearIo();
    }
    ++scenarios;init();CHECK(yumo_ble_store_status()==ESP_OK);checkObjects(8,3);
    const auto migrationOperations=nvs_host::operations();
    CHECK(!migrationOperations.empty());
    const auto migrated=nvs_host::snapshot();
    CHECK(read("nimble_bond","our_sec_1").empty());CHECK(read("nimble_bond","cccd_sec_1").empty());
    CHECK(read("ble_peers","v1")==bytes(oldMeta));
    CHECK(nvs_host::reboot()==ESP_OK);init();CHECK(yumo_ble_store_status()==ESP_OK);checkObjects(8,3);

    // Every physical migration boundary, before and after its NOR operation.
    // A boot either uses the fully verified new object or the intact old keys.
    for(size_t cut=0;cut<migrationOperations.size();++cut) {
        for(size_t prefix: {size_t(0),nvs_host::EntireOperation}) {
            ++scenarios;CHECK(nvs_host::restore(before)==ESP_OK);nvs_host::armPowerCut(cut,prefix);
            try { init(); } catch(const nvs_host::PowerCut &) {}
            CHECK(nvs_host::reboot()==ESP_OK);init();CHECK(yumo_ble_store_status()==ESP_OK);
            checkObjects(8,3);CHECK(read("ble_peers","v1")==bytes(oldMeta));
        }
    }

    // The full supported setting profile is present alongside 16 dual-security
    // bonds, all eight current CCCDs per bond, CSFC, RPA and 64-byte names.
    CHECK(nvs_host::restore(migrated)==ESP_OK);init();CHECK(yumo_ble_store_status()==ESP_OK);
    std::array<uint8_t,1228> meta{};meta[0]=2;blob("ble_peers","v1",meta);
    for(unsigned count=8;count<=16;++count){++scenarios;fill(count);}
    checkObjects(16,8);nvs_stats_t stats{};CHECK(nvs_get_stats(nullptr,&stats)==ESP_OK);
    const auto fullUsed=stats.used_entries,fullAvailable=stats.available_entries;
    std::fprintf(stderr,"full used=%zu available=%zu\n",fullUsed,fullAvailable);
    CHECK(stats.available_entries>=51);
    CHECK(read("yumo_bonds","our_v1").size()==16+16*88);
    CHECK(read("yumo_bonds","cccd_v1").size()==16+16*7+128*6);
    const auto full=nvs_host::snapshot();

    // Exercise repeated replacement/GC with both the largest BLE object and
    // the largest existing system value, not just an empty image's byte count.
    for(unsigned iteration=0;iteration<128;++iteration) {
        ++scenarios;our[0].sign_counter++;
        ble_store_config_our_secs[0]=our[0];CHECK(!__wrap_ble_store_config_persist_our_secs());
        cccds[0].value_changed^=1;ble_store_config_cccds[0]=cccds[0];
        CHECK(!__wrap_ble_store_config_persist_cccds());
        meta[4]=uint8_t(iteration);blob("ble_peers","v1",meta);
        nvs_handle_t h;CHECK(nvs_open("settings",NVS_READWRITE,&h)==ESP_OK);
        std::string history(maximumProfile?1568:16*34,char('a'+iteration%20));
        const auto historyErr=(!maximumProfile||iteration==0)?nvs_set_str(h,"wifihist",history.c_str()):ESP_OK;
        if(historyErr!=ESP_OK){
            nvs_get_stats(nullptr,&stats);std::fprintf(stderr,"history replacement iteration=%u rc=%d used=%zu available=%zu\n",iteration,int(historyErr),stats.used_entries,stats.available_entries);
            const auto image=nvs_host::snapshot();
            for(unsigned page=0;page<4;++page){unsigned written=0,erased=0,empty=0;const auto base=page*4096;
                for(unsigned item=0;item<126;++item){auto state=(image[base+32+item/4]>>((item%4)*2))&3;if(state==2)++written;else if(!state)++erased;else if(state==3)++empty;}
                uint32_t state=0;std::memcpy(&state,&image[base],4);std::fprintf(stderr,"page %u state=%08x written=%u erased=%u empty=%u\n",page,state,written,erased,empty);
            }
        }
        if(!maximumProfile)CHECK(historyErr==ESP_OK);
        nvs_close(h);
        if(iteration%8==0){CHECK(nvs_host::reboot()==ESP_OK);init();CHECK(yumo_ble_store_status()==ESP_OK);checkObjects(16,8);}
    }
    CHECK(nvs_host::ioStats().erases>0);CHECK(!nvs_host::ioStats().forbiddenBitTransitions);
    seedObjects();CHECK(nvs_host::restore(full)==ESP_OK);init();CHECK(yumo_ble_store_status()==ESP_OK);

    // Updating an existing aggregate uses alternating NVS blob chunk versions.
    // Sweep both transitions with the generated production recovery fix, using
    // actual secret/flag/counter objects rather than a surrogate payload size.
    size_t updateMutations=0;
    auto updateBase=full;
    for(unsigned version=0;version<2;++version) {
        CHECK(nvs_host::restore(updateBase)==ESP_OK);init();CHECK(yumo_ble_store_status()==ESP_OK);
        std::array<ble_store_value_sec,16> previous{},next{};
        std::memcpy(previous.data(),ble_store_config_our_secs,sizeof previous);next=previous;
        for(unsigned i=0;i<16;++i){next[i].sign_counter+=0x10+version;next[i].ltk[5]^=uint8_t(i+0x31+version);}
        nvs_host::clearIo();std::memcpy(ble_store_config_our_secs,next.data(),sizeof next);
        CHECK(!__wrap_ble_store_config_persist_our_secs());const auto afterUpdate=nvs_host::snapshot();
        const auto updateOps=nvs_host::operations();updateMutations+=updateOps.size();
        for(size_t cut=0;cut<updateOps.size();++cut) {
            for(size_t prefix:{size_t(0),size_t(1),nvs_host::EntireOperation}) {
                ++scenarios;CHECK(nvs_host::restore(updateBase)==ESP_OK);init();
                std::memcpy(ble_store_config_our_secs,next.data(),sizeof next);nvs_host::armPowerCut(cut,prefix);
                try { __wrap_ble_store_config_persist_our_secs(); } catch(const nvs_host::PowerCut &) {}
                CHECK(nvs_host::reboot()==ESP_OK);init();CHECK(yumo_ble_store_status()==ESP_OK);
                CHECK(ble_store_config_num_our_secs==16);
                CHECK(!std::memcmp(ble_store_config_our_secs,previous.data(),sizeof previous)||
                      !std::memcmp(ble_store_config_our_secs,next.data(),sizeof next));
                CHECK(!std::memcmp(ble_store_config_peer_secs,peer.data(),sizeof peer));
                CHECK(read("nimble_bond","local_irk_1")==bytes(irk));
            }
        }
        updateBase=afterUpdate;
    }
    CHECK(nvs_host::restore(full)==ESP_OK);init();CHECK(yumo_ble_store_status()==ESP_OK);

    // A failed persistence call cannot leave a half-added peer in the SDK RAM
    // arrays. Further writes stop until a fresh init reconciles the disk.
    ++scenarios;auto old=ble_store_config_our_secs[0];auto oldCounter=ble_store_config_our_bond_count;
    ble_store_config_our_secs[0].ltk[0]^=0xff;ble_store_config_our_bond_count++;
    nvs_host::armFailure(0);CHECK(__wrap_ble_store_config_persist_our_secs()!=0);
    CHECK(!std::memcmp(&old,&ble_store_config_our_secs[0],sizeof old));
    CHECK(ble_store_config_our_bond_count==oldCounter);CHECK(yumo_ble_store_status()!=ESP_OK);
    ble_store_config_num_our_secs=0;ble_store_config_our_bond_count=65530;
    CHECK(__wrap_ble_store_config_persist_our_secs()!=0);
    CHECK(ble_store_config_num_our_secs==16);CHECK(ble_store_config_our_bond_count==oldCounter);
    ble_store_config_num_peer_secs=0;ble_store_config_peer_bond_count=65530;
    CHECK(__wrap_ble_store_config_persist_peer_secs()!=0);
    CHECK(ble_store_config_num_peer_secs==16);CHECK(ble_store_config_peer_bond_count==peer[15].bond_count);
    ble_store_config_num_cccds=0;CHECK(__wrap_ble_store_config_persist_cccds()!=0);
    CHECK(ble_store_config_num_cccds==128);
    ble_store_config_num_rpa_recs=0;CHECK(__wrap_ble_store_config_persist_rpa_recs()!=0);
    CHECK(ble_store_config_num_rpa_recs==16);
    ble_store_config_num_csfcs=0;CHECK(__wrap_ble_store_config_persist_csfcs()!=0);
    CHECK(ble_store_config_num_csfcs==16);
    CHECK(nvs_host::reboot()==ESP_OK);init();CHECK(yumo_ble_store_status()==ESP_OK);checkObjects(16,8);

    // Old remnants cannot revive a deleted type: zero-record authority remains
    // an actual persisted object, including after a reboot during cleanup.
    ++scenarios;ble_store_config_num_our_secs=0;CHECK(!__wrap_ble_store_config_persist_our_secs());
    blob("nimble_bond","our_sec_1",our[0]);CHECK(nvs_host::reboot()==ESP_OK);init();
    CHECK(yumo_ble_store_status()==ESP_OK);CHECK(ble_store_config_num_our_secs==0);
    CHECK(read("yumo_bonds","our_v1").size()==16);CHECK(read("nimble_bond","our_sec_1").empty());

    // A malformed new authority is a visible restore error, never permission
    // to fallback to stale keys or overwrite the user's persisted pairing.
    ++scenarios;CHECK(nvs_host::restore(migrated)==ESP_OK);init();
    auto corrupt=read("yumo_bonds","our_v1");corrupt.back()^=0x80;
    blob("yumo_bonds","our_v1",corrupt.data(),corrupt.size());blob("nimble_bond","our_sec_1",our[0]);
    const auto corruptBefore=nvs_host::snapshot();init();CHECK(yumo_ble_store_status()!=ESP_OK);
    CHECK(nvs_host::snapshot()==corruptBefore);CHECK(read("nimble_bond","our_sec_1")==bytes(our[0]));

    // Once migration committed its authority marker, a missing blob is damage,
    // not an empty first boot. Refuse fallback even if old keys have reappeared.
    ++scenarios;CHECK(nvs_host::restore(migrated)==ESP_OK);
    nvs_handle_t damaged;CHECK(nvs_open("yumo_bonds",NVS_READWRITE,&damaged)==ESP_OK);
    CHECK(nvs_erase_key(damaged,"our_v1")==ESP_OK);nvs_close(damaged);
    blob("nimble_bond","our_sec_1",our[0]);const auto missingBefore=nvs_host::snapshot();
    init();CHECK(yumo_ble_store_status()!=ESP_OK);CHECK(nvs_host::snapshot()==missingBefore);

    // Exhaustion during initial migration must not erase any old pairing to
    // manufacture capacity. Leave it intact and report the startup error.
    ++scenarios;CHECK(nvs_host::reset()==ESP_OK);legacy(8,3);
    nvs_handle_t filler;CHECK(nvs_open("yumo_bonds",NVS_READWRITE,&filler)==ESP_OK);nvs_close(filler);
    CHECK(nvs_open("filler",NVS_READWRITE,&filler)==ESP_OK);
    for(unsigned i=0;;++i){char key[16];std::snprintf(key,sizeof key,"entry%u",i);
        const auto rc=nvs_set_u32(filler,key,i);if(rc==ESP_ERR_NVS_NOT_ENOUGH_SPACE)break;CHECK(rc==ESP_OK);CHECK(i<400);}
    nvs_close(filler);init();CHECK(yumo_ble_store_status()!=ESP_OK);
    for(unsigned i=0;i<8;++i){char key[16];std::snprintf(key,sizeof key,"our_sec_%u",i+1);CHECK(read("nimble_bond",key)==bytes(our[i]));
        std::snprintf(key,sizeof key,"peer_sec_%u",i+1);CHECK(read("nimble_bond",key)==bytes(peer[i]));}
    CHECK(read("nimble_bond","local_irk_1")==bytes(irk));

    // Every physical boundary of deleting the last authority also restores as
    // a complete old or complete empty object; never a mixed security array.
    CHECK(nvs_host::restore(full)==ESP_OK);init();nvs_host::clearIo();
    ble_store_config_num_our_secs=0;CHECK(!__wrap_ble_store_config_persist_our_secs());
    const auto deleteOps=nvs_host::operations();
    for(size_t cut=0;cut<deleteOps.size();++cut) {
        for(size_t prefix:{size_t(0),size_t(1),nvs_host::EntireOperation}) {
            ++scenarios;CHECK(nvs_host::restore(full)==ESP_OK);init();
            ble_store_config_num_our_secs=0;nvs_host::armPowerCut(cut,prefix);
            try { __wrap_ble_store_config_persist_our_secs(); } catch(const nvs_host::PowerCut &) {}
            CHECK(nvs_host::reboot()==ESP_OK);init();CHECK(yumo_ble_store_status()==ESP_OK);
            CHECK(ble_store_config_num_our_secs==0||ble_store_config_num_our_secs==16);
            if(ble_store_config_num_our_secs==16)
                for(unsigned i=0;i<16;++i)CHECK(!std::memcmp(&ble_store_config_our_secs[i],&our[i],sizeof(our[i])));
            CHECK(ble_store_config_num_peer_secs==16);CHECK(read("nimble_bond","local_irk_1")==bytes(irk));
        }
    }
    std::printf("{\"passed\":true,\"profile\":\"%s\",\"scenarios\":%u,\"checks\":%u,\"migration_mutations\":%zu,\"update_mutations\":%zu,\"delete_mutations\":%zu,\"full_used_entries\":%zu,\"full_available_entries\":%zu,\"flash_bytes\":16384,\"physical_flash\":false,\"history_update_excluded_in_max_profile\":%s}\n",
        maximumProfile?"maximum":"typical",scenarios,checks,migrationOperations.size(),updateMutations,deleteOps.size(),fullUsed,fullAvailable,maximumProfile?"true":"false");
}
