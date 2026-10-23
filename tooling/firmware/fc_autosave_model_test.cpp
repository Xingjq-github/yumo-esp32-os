#include "core/fc_autosave.hpp"
#include "core/fc_save_store.h"
using core::fc::AutoSaveClock;
constexpr bool firstMinuteOnlyAfterRunning() {
    AutoSaveClock c;
    c.update(0,false);c.update(3600000000LL,false);
    if(c.due())return false;
    c.update(3600000000LL,true);c.update(3659999999LL,true);
    if(c.due())return false;
    c.update(3660000000LL,true);return c.due();
}
constexpr bool pauseAndMenuDoNotCount() {
    AutoSaveClock c;c.update(0,true);c.update(25000000,false);
    c.update(600000000,false);c.update(600000000,true);c.update(634999999,true);
    if(c.due())return false;
    c.update(635000000,true);return c.due();
}
constexpr bool loadOrAttemptRestartsTheMinute() {
    AutoSaveClock c;c.update(0,true);c.update(59000000,true);
    c.reset(59000000);c.update(900000000,false);c.update(900000000,true);
    c.update(959999999,true);if(c.due())return false;
    c.update(960000000,true);if(!c.due())return false;
    c.reset(965000000);c.update(965000000,true);c.update(1024999999,true);
    return !c.due();
}
constexpr bool longStallProducesOneAttempt() {
    AutoSaveClock c;c.update(0,true);c.update(7200000000LL,true);
    if(!c.due())return false;
    c.reset(7200000000LL);
    c.update(7200000000LL,true);return !c.due();
}
static_assert(FC_SAVE_MANUAL_SLOTS==3 && FC_SAVE_AUTO_SLOT==3 && FC_SAVE_SLOTS==4);
static_assert(firstMinuteOnlyAfterRunning());
static_assert(pauseAndMenuDoNotCount());
static_assert(loadOrAttemptRestartsTheMinute());
static_assert(longStallProducesOneAttempt());
