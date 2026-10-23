#include "settings_store.hpp"
#include "lan_hotspot_model.hpp"
#include "mp3_model.hpp"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <mutex>
#include <new>
#include <utility>
#include <sys/time.h>

namespace sys {
namespace {
constexpr const char *kNamespace = "settings";
constexpr size_t kMaxHistory = 16;
enum Dirty : uint32_t { Brightness = 1, Volume = 2,
    SysInfo = 16, Credentials = 64, History = 128, Clock = 256, LanCredentials = 512, DinoScore = 1024, PowerConfig = 2048, AppOrder = 4096, CastScreenOff = 8192, ClockDrift = 16384, Mp3Track = 32768 };
std::mutex mutex;
uint32_t dirty = 0;
int64_t dirtySince = 0, lastChange = 0, retryAt = 0;
void mark(uint32_t bits) {
    const auto now = esp_timer_get_time();
    if (!dirty) dirtySince = now;
    lastChange = now; dirty |= bits;
}
std::string readString(nvs_handle_t h, const char *key, size_t limit) {
    size_t n = 0;
    if (nvs_get_str(h, key, nullptr, &n) != ESP_OK || n == 0 || n > limit) return {};
    std::string text(n, '\0');
    if (nvs_get_str(h, key, &text[0], &n) != ESP_OK) return {};
    text.resize(n - 1);
    return text;
}
std::string serialize(const std::vector<Settings::WifiEntry> &history) {
    std::string text;
    for (const auto &entry : history) text += entry.ssid + '\t' + entry.password + '\n';
    return text;
}
bool safeField(const std::string &s) { return s.find_first_of("\t\r\n") == std::string::npos; }
std::vector<Settings::WifiEntry> deserialize(const std::string &text) {
    std::vector<Settings::WifiEntry> entries;
    for (size_t pos = 0; pos < text.size() && entries.size() < kMaxHistory;) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        auto row = text.substr(pos, end-pos); pos = end + 1;
        auto split = row.find('\t');
        auto ssid = row.substr(0, split);
        auto password = split == std::string::npos ? std::string() : row.substr(split+1);
        if (!ssid.empty() && ssid.size() <= 32 && password.size() <= 64)
            entries.push_back({std::move(ssid), std::move(password)});
    }
    return entries;
}
}
Settings::Cache &Settings::cache() { static Cache c; return c; }
void Settings::load()
{
    const std::lock_guard<std::mutex> lock(mutex);
    auto &c = cache();
    if (c.loaded) return;
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) == ESP_OK) {
        int32_t value;
        if (nvs_get_i32(h,"brightness",&value)==ESP_OK) c.brightness=std::max(5,std::min(100,(int)value));
        if (nvs_get_i32(h,"volume",&value)==ESP_OK) c.volume=std::max(0,std::min(100,(int)value));
        if (nvs_get_i32(h,"sysinfo",&value)==ESP_OK) c.sysinfo=value!=0;
        if (nvs_get_i32(h,"cast_dark",&value)==ESP_OK) c.castScreenOff=value==1;
        if (nvs_get_i32(h,"theme",&value)==ESP_OK) c.theme=themes::normalize(value);
        c.ssid=readString(h,"ssid",33); c.password=readString(h,"pass",65);
        c.mp3Track=readString(h,"mp3_track",1024);
        if(!core::mp3::trackPath(c.mp3Track))c.mp3Track.clear();
        c.wifiHist=deserialize(readString(h,"wifihist",4096));
        const auto lanName=readString(h,"lan_ssid",33),lanPass=readString(h,"lan_pass",64);
        if(lan::validate(lanName,lanPass)==lan::ConfigError::None) { c.lanSsid=lanName; c.lanPassword=lanPass; }
        (void)nvs_get_i64(h,"clock",&c.clockEpoch);
        clock_drift::Encoded drift{};size_t driftSize=sizeof(drift);
        if(nvs_get_blob(h,"rtc_drift_v1",drift.data(),&driftSize)==ESP_OK && driftSize==sizeof(drift))
            c.clockDrift=clock_drift::decode(drift);
        (void)nvs_get_u32(h,"dino_best",&c.dinoBest);
        app_order::Encoded order{};size_t orderSize=sizeof(order);
        if(nvs_get_blob(h,"app_order_v1",order.data(),&orderSize)==ESP_OK&&orderSize==sizeof(order))
            c.appOrder=app_order::decode(order);
        power::Encoded energy{};size_t energySize=sizeof(energy);
        if(nvs_get_blob(h,"power_v2",energy.data(),&energySize)==ESP_OK && energySize==sizeof(energy))
            c.power=power::decode(energy);
        else {
            power::LegacyEncoded legacy{};energySize=sizeof(legacy);
            if(nvs_get_blob(h,"power_v1",legacy.data(),&energySize)==ESP_OK && energySize==sizeof(legacy))
                c.power=power::decodeLegacy(legacy);
        }
        nvs_close(h);
    }
    // Both radios start enabled on every boot, including RTC-preserving deep wake.
    // SystemService restores the last successful targets once; motion stays off.
    // Keep network credentials, bond keys and old NVS preference keys intact.
    c.bluetooth=true;c.wifi=true;c.motion=false;
    c.loaded = true;
}
bool Settings::flush(bool force)
{
    // Do not construct/copy the full cache for a clock or slider-only update.
    struct Snapshot {
        int brightness=0,volume=0;bool sysinfo=false,castScreenOff=false;
        int64_t clockEpoch=0;uint32_t dinoBest=0;power::Config power;
        clock_drift::Model clockDrift;
        std::string ssid,password,lanSsid,lanPassword,history,mp3Track;
        app_order::Order order;uint32_t orderRevision=0;
    } snapshot;
    uint32_t bits;
    const auto now = esp_timer_get_time();
    try {
        const std::lock_guard<std::mutex> lock(mutex);
        if (!dirty) return true;
        if (!force && (now < retryAt || (now-lastChange < 500000 && now-dirtySince < 5000000))) return true;
        const auto &c=cache(); bits=dirty;
        snapshot.brightness=c.brightness;snapshot.volume=c.volume;snapshot.sysinfo=c.sysinfo;
        snapshot.castScreenOff=c.castScreenOff;
        snapshot.clockEpoch=c.clockEpoch;snapshot.dinoBest=c.dinoBest;snapshot.power=c.power;
        snapshot.clockDrift=c.clockDrift;
        if(bits&Credentials) { snapshot.ssid=c.ssid;snapshot.password=c.password; }
        if(bits&LanCredentials) { snapshot.lanSsid=c.lanSsid;snapshot.lanPassword=c.lanPassword; }
        if(bits&History) snapshot.history=serialize(c.wifiHist);
        if(bits&Mp3Track) snapshot.mp3Track=c.mp3Track;
        if(bits&AppOrder){snapshot.order=c.appOrder;snapshot.orderRevision=c.orderRevision;}
        dirty=0; // Only consume flags after every allocating operation succeeded.
    } catch(const std::bad_alloc &) {
        const std::lock_guard<std::mutex> lock(mutex);
        retryAt=now+2000000;
        return false;
    }
    // NVS and flash operations never run under the cache mutex or GUI lock.
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(kNamespace,NVS_READWRITE,&h);
    if (err == ESP_OK) {
        auto integer = [&](uint32_t bit,const char *key,int value) {
            if (err==ESP_OK && (bits&bit)) err=nvs_set_i32(h,key,value);
        };
        integer(Brightness,"brightness",snapshot.brightness);
        integer(Volume,"volume",snapshot.volume);
        integer(SysInfo,"sysinfo",snapshot.sysinfo);
        integer(CastScreenOff,"cast_dark",snapshot.castScreenOff);
        if (err==ESP_OK && (bits&Credentials)) err=nvs_set_str(h,"ssid",snapshot.ssid.c_str());
        if (err==ESP_OK && (bits&Credentials)) err=nvs_set_str(h,"pass",snapshot.password.c_str());
        if (err==ESP_OK && (bits&LanCredentials)) err=nvs_set_str(h,"lan_ssid",snapshot.lanSsid.c_str());
        if (err==ESP_OK && (bits&LanCredentials)) err=nvs_set_str(h,"lan_pass",snapshot.lanPassword.c_str());
        if (err==ESP_OK && (bits&History)) err=nvs_set_str(h,"wifihist",snapshot.history.c_str());
        if (err==ESP_OK && (bits&Mp3Track)) err=nvs_set_str(h,"mp3_track",snapshot.mp3Track.c_str());
        if (err==ESP_OK && (bits&Clock)) err=nvs_set_i64(h,"clock",snapshot.clockEpoch);
        if (err==ESP_OK && (bits&ClockDrift)) {
            const auto drift=clock_drift::encode(snapshot.clockDrift);
            err=nvs_set_blob(h,"rtc_drift_v1",drift.data(),sizeof(drift));
        }
        if (err==ESP_OK && (bits&DinoScore)) err=nvs_set_u32(h,"dino_best",snapshot.dinoBest);
        if (err==ESP_OK && (bits&PowerConfig)) {
            const auto energy=power::encode(snapshot.power);
            err=nvs_set_blob(h,"power_v2",energy.data(),sizeof(energy));
        }
        if(err==ESP_OK && (bits&AppOrder)){
            const auto order=app_order::encode(snapshot.order);
            err=nvs_set_blob(h,"app_order_v1",order.data(),sizeof(order));
        }
        if (err==ESP_OK) err=nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (!dirty) dirtySince=now;
        dirty |= bits; retryAt = now + 2000000;
        ESP_LOGW("settings","NVS commit deferred: %s",esp_err_to_name(err));
    }
    if(bits&AppOrder){
        const std::lock_guard<std::mutex> lock(mutex);
        if(cache().orderRevision==snapshot.orderRevision)
            cache().orderState=err==ESP_OK?app_order::SaveState::Saved:app_order::SaveState::Failed;
    }
    return err == ESP_OK;
}

app_order::Order Settings::appOrder(){const std::lock_guard<std::mutex> lock(mutex);return cache().appOrder;}
app_order::SaveState Settings::appOrderSaveState(){const std::lock_guard<std::mutex> lock(mutex);return cache().orderState;}
bool Settings::setAppOrder(const app_order::Order &order){
    if(!app_order::valid(order)||!order.count)return false;
    const std::lock_guard<std::mutex> lock(mutex);auto &c=cache();
    if(c.appOrder==order)return true;
    c.appOrder=order;++c.orderRevision;c.orderState=app_order::SaveState::Pending;mark(AppOrder);return true;
}
themes::Id Settings::theme() {
    const std::lock_guard<std::mutex> lock(mutex); return cache().theme;
}
uint32_t Settings::dinoHighScore() {
    const std::lock_guard<std::mutex> lock(mutex);return cache().dinoBest;
}
void Settings::recordDinoScore(uint32_t score) {
    const std::lock_guard<std::mutex> lock(mutex);
    if(score<=cache().dinoBest)return;
    cache().dinoBest=score;mark(DinoScore);
}
bool Settings::saveTheme(themes::Id id) {
    if(!themes::valid(static_cast<int>(id))) return false;
    nvs_handle_t h=0;
    auto err=nvs_open(kNamespace,NVS_READWRITE,&h);
    if(err==ESP_OK) {
        err=nvs_set_i32(h,"theme",static_cast<int>(id));
        if(err==ESP_OK) err=nvs_commit(h);
        nvs_close(h);
    }
    if(err!=ESP_OK) return false;
    const std::lock_guard<std::mutex> lock(mutex); cache().theme=id; return true;
}
clock_drift::Model Settings::clockDrift() {
    const std::lock_guard<std::mutex> lock(mutex);return cache().clockDrift;
}
void Settings::saveClockDrift(clock_drift::Model model) {
    if(!clock_drift::valid(model))return;
    const std::lock_guard<std::mutex> lock(mutex);
    if(clock_drift::encode(cache().clockDrift)==clock_drift::encode(model))return;
    cache().clockDrift=model;mark(ClockDrift);
}
void Settings::saveClock(int64_t epoch)
{
    if (epoch < 1609459200LL) return;
    const std::lock_guard<std::mutex> lock(mutex);
    if (cache().clockEpoch==epoch) return;
    cache().clockEpoch=epoch; mark(Clock);
}
bool Settings::restoreClock()
{
    int64_t epoch;
    { const std::lock_guard<std::mutex> lock(mutex); epoch=cache().clockEpoch; }
    if (epoch < 1609459200LL) return false;
    timeval tv{static_cast<time_t>(epoch),0};
    return settimeofday(&tv,nullptr)==0;
}
std::vector<Settings::WifiEntry> Settings::wifiHistory() {
    const std::lock_guard<std::mutex> lock(mutex); return cache().wifiHist;
}
std::string Settings::wifiPasswordFor(const std::string &ssid) {
    const std::lock_guard<std::mutex> lock(mutex);
    for (const auto &e:cache().wifiHist) if(e.ssid==ssid) return e.password;
    return {};
}
bool Settings::wifiKnown(const std::string &ssid) {
    const std::lock_guard<std::mutex> lock(mutex);
    for (const auto &e:cache().wifiHist) if(e.ssid==ssid) return true;
    return false;
}
void Settings::rememberWifi(const std::string &ssid,const std::string &password) {
    if (ssid.empty() || ssid.size()>32 || password.size()>64 || !safeField(ssid) || !safeField(password)) return;
    const std::lock_guard<std::mutex> lock(mutex);
    auto &hist=cache().wifiHist;
    if (!hist.empty() && hist.front().ssid==ssid && hist.front().password==password) return;
    // Allocate before erasing/reordering: OOM must preserve the old entry.
    WifiEntry replacement{ssid,password};
    hist.reserve(hist.size()+1);
    hist.erase(std::remove_if(hist.begin(),hist.end(),[&](const WifiEntry &e){return e.ssid==ssid;}),hist.end());
    hist.insert(hist.begin(),std::move(replacement));
    if(hist.size()>kMaxHistory) hist.resize(kMaxHistory);
    mark(History);
}
void Settings::forgetWifi(const std::string &ssid) {
    const std::lock_guard<std::mutex> lock(mutex);
    auto &c=cache(); auto size=c.wifiHist.size();
    c.wifiHist.erase(std::remove_if(c.wifiHist.begin(),c.wifiHist.end(),[&](const WifiEntry &e){return e.ssid==ssid;}),c.wifiHist.end());
    if(size!=c.wifiHist.size()) mark(History);
    if(c.ssid==ssid) { c.ssid.clear(); c.password.clear(); mark(Credentials); }
}
std::string Settings::lanSsid() { const std::lock_guard<std::mutex> lock(mutex); return cache().lanSsid; }
std::string Settings::lanPassword() { const std::lock_guard<std::mutex> lock(mutex); return cache().lanPassword; }
bool Settings::setLanCredentials(const std::string &ssid,const std::string &password) {
    if(lan::validate(ssid,password)!=lan::ConfigError::None) return false;
    const std::lock_guard<std::mutex> lock(mutex);
    auto &c=cache();
    if(c.lanSsid!=ssid || c.lanPassword!=password) {
        std::string nextSsid(ssid),nextPassword(password);
        c.lanSsid.swap(nextSsid);c.lanPassword.swap(nextPassword);mark(LanCredentials);
    }
    return true;
}
std::string Settings::wifiSsid() { const std::lock_guard<std::mutex> lock(mutex); return cache().ssid; }
std::string Settings::wifiPassword() { const std::lock_guard<std::mutex> lock(mutex); return cache().password; }
void Settings::setWifiCredentials(const std::string &ssid,const std::string &password) {
    if(ssid.size()>32 || password.size()>64) return;
    const std::lock_guard<std::mutex> lock(mutex);
    auto &c=cache(); if(c.ssid==ssid && c.password==password) return;
    std::string nextSsid(ssid),nextPassword(password);
    c.ssid.swap(nextSsid);c.password.swap(nextPassword);mark(Credentials);
}
int Settings::brightness() { const std::lock_guard<std::mutex> lock(mutex); return cache().brightness; }
bool Settings::castScreenOff() { const std::lock_guard<std::mutex> lock(mutex); return cache().castScreenOff; }
void Settings::setCastScreenOff(bool value) {
    const std::lock_guard<std::mutex> lock(mutex);
    if(cache().castScreenOff==value) return;
    cache().castScreenOff=value; mark(CastScreenOff);
}
power::Config Settings::powerConfig() { const std::lock_guard<std::mutex> lock(mutex);return cache().power; }
void Settings::setPowerConfig(power::Config value) {
    value=power::normalize(value);
    const std::lock_guard<std::mutex> lock(mutex);
    if(power::encode(cache().power)==power::encode(value))return;
    cache().power=value;mark(PowerConfig);
}
void Settings::setBrightness(int value) {
    value=std::max(5,std::min(100,value));
    const std::lock_guard<std::mutex> lock(mutex);
    if(cache().brightness==value) return;
    cache().brightness=value; mark(Brightness);
}
int Settings::volume() { const std::lock_guard<std::mutex> lock(mutex); return cache().volume; }
std::string Settings::mp3Track() {const std::lock_guard<std::mutex> lock(mutex);return cache().mp3Track;}
void Settings::rememberMp3Track(const std::string &path) {
    if(path.size()>1023||path.find('\0')!=std::string::npos||!core::mp3::trackPath(path))return;
    const std::lock_guard<std::mutex> lock(mutex);
    if(cache().mp3Track==path)return;
    cache().mp3Track=path;mark(Mp3Track);
}
void Settings::setVolume(int value) {
    value=std::max(0,std::min(100,value));
    const std::lock_guard<std::mutex> lock(mutex);
    if(cache().volume==value) return;
    cache().volume=value; mark(Volume);
}
bool Settings::bluetoothOn() { const std::lock_guard<std::mutex> lock(mutex); return cache().bluetooth; }
void Settings::setBluetoothOn(bool value) {
    const std::lock_guard<std::mutex> lock(mutex);
    if(cache().bluetooth==value) return;
    cache().bluetooth=value;
}
bool Settings::wifiOn() { const std::lock_guard<std::mutex> lock(mutex); return cache().wifi; }
void Settings::setWifiOn(bool value) {
    const std::lock_guard<std::mutex> lock(mutex);
    if(cache().wifi==value) return;
    cache().wifi=value;
}
bool Settings::sysInfoOn() { const std::lock_guard<std::mutex> lock(mutex); return cache().sysinfo; }
void Settings::setSysInfoOn(bool value) {
    const std::lock_guard<std::mutex> lock(mutex);
    if(cache().sysinfo==value) return;
    cache().sysinfo=value; mark(SysInfo);
}
bool Settings::motionSensorOn() { const std::lock_guard<std::mutex> lock(mutex); return cache().motion; }
void Settings::setMotionSensorOn(bool value) {
    const std::lock_guard<std::mutex> lock(mutex);
    if(cache().motion==value) return;
    cache().motion=value;
}
} // namespace sys
