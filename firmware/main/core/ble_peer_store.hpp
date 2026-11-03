#pragma once
#include "ble_hid.hpp"
namespace sys {
class BlePeerStore {
public:
    static constexpr unsigned Capacity=16;
    static void load(); // worker before host starts
    static void flush(); // worker only, callbacks merely mark dirty
    static BleBond enrich(BleBond value);
    static void remember(const BleBond &value);
    static void forget(const BleBond &value);
    static BleBond target();
    static void setTarget(const BleBond &value);
    static void activate(const BleBond &value);
    static BleBond active();
    static void name(const char *value);
    static void clearActive();
    static bool same(const BleBond &a,const BleBond &b);
};
}
