/*
 * Physical button manager.
 *
 *   BOOT (GPIO0): single -> volume down / reader previous / photo zoom out / random start; double -> Home
 *   KEY  (GPIO4): single -> volume up / reader next / photo zoom in / random start
 *                double -> screenshot (disabled in FC gameplay and photo viewing)
 *   PWR  (GPIO5): single -> return from photo/text viewing / screen on/off, long -> power off
 */
#pragma once
#include <cstdint>
#include "timer_key_state.hpp"

namespace sys {

class Buttons {
public:
    // Create all candidate GPIO buttons and register their callbacks.
    static void init(void);
    // GUI-only ownership. Button task publishes events, never touches an App.
    // A generation token rejects queued clicks from a previously closed reader/photo/random App.
    static uint32_t claimPageKeys();
    static void releasePageKeys(uint32_t session);
    // Photo/text-view-only PWR exit. GUI polls a token mailbox, so a full event queue
    // cannot accidentally turn off the screen or deliver a stale App callback.
    static uint32_t claimViewExit();
    static void releaseViewExit(uint32_t session);
    static bool takeViewExit(uint32_t session);
    // Garden claims only BOOT single Back. KEY/PWR and BOOT double keep their routes.
    static uint32_t claimBootBack();
    static void releaseBootBack(uint32_t session);
    static bool takeBootBack(uint32_t session);
    static void setSystemPanel(bool visible); // temporary routing; preserves App ownership tokens
    // FC owns volume keys throughout the App. Only the game page suppresses
    // BOOT Home; worker reads debounced held A/B without GUI event-queue delay.
    static uint32_t claimGameKeys();
    static void releaseGameKeys(uint32_t session);
    static void gameMode(uint32_t session,bool gameplay);
    static void gameRunning(uint32_t session,bool running);
    static void clearGameKeys(uint32_t session);
    static uint8_t takeGameKeys(uint32_t session);
    static bool blockHomeShortcut();
    // Foreground stopwatch: timestamped KEY/PWR releases, BOOT double exits.
    static uint32_t claimTimerKeys();
    static void releaseTimerKeys(uint32_t session);
    static bool takeTimerKey(uint32_t session,TimerKey &key);
};

}// namespace sys
