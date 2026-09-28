// sfxr_steam.h - optional Steamworks: achievements, stats, the player's name,
// the Steam overlay. See docs/STEAM.md.
//
// Nothing here needs the Steamworks SDK to build. At run time sfxr looks for
// Valve's libsteam_api.so next to your binary (package it with
// STEAMWORKS_SDK=... make package) and talks to its flat C API. Without the
// library, or outside Steam, every call is a harmless no-op that returns
// false, so the same build runs everywhere: your desk, the simulator, the
// headset.
//
//   if (sfxr_steam_init()) ...                 // once, after sfxr_init
//   sfxr_steam_unlock("FIRST_CLIMB");          // set + store; shows Steam's popup
//   if (sfxr_steam_overlay_active()) pause();
//
// sfxr_frame_begin() pumps Steam's callbacks for you while Steam is up.

#ifndef SFXR_STEAM_H
#define SFXR_STEAM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool        sfxr_steam_init(void);       // true: Steam is running and the library loaded
bool        sfxr_steam_available(void);
const char *sfxr_steam_status(void);     // one line: "Steam: ...", or why it isn't available
void        sfxr_steam_shutdown(void);   // sfxr_shutdown() calls this too

uint32_t    sfxr_steam_app_id(void);     // 0 when unavailable (480 = Spacewar, Valve's test app)
const char *sfxr_steam_player_name(void);   // "" when unavailable

// Achievements: the API names you set up on the Steamworks partner site.
bool sfxr_steam_unlock(const char *achievement);                // set and store (Steam shows its popup)
bool sfxr_steam_achieved(const char *achievement, bool *done);  // false: unknown / unavailable
bool sfxr_steam_clear(const char *achievement);                 // for testing your own unlocks

// Stats (integers), also set up on the partner site. set_int stores immediately.
bool sfxr_steam_stat_set_int(const char *stat, int32_t value);
bool sfxr_steam_stat_get_int(const char *stat, int32_t *value);

// The Steam overlay is open (pause, stop taking input). On the Frame the
// SteamVR dashboard is separate: sfxr's session state covers that.
bool sfxr_steam_overlay_active(void);

void sfxr_steam_update(void);            // called by sfxr_frame_begin; harmless to call yourself

#ifdef __cplusplus
}
#endif

#endif // SFXR_STEAM_H
