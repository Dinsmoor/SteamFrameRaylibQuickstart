// sfxr_steam.c - Steamworks through its flat C API, loaded at run time.
//
// Why dlopen instead of linking: the SDK can't live in this repo (Valve's
// license lets you ship libsteam_api.so with a built game, not publish the
// SDK), and a quickstart has to build without an account. So we declare the
// few flat-API functions we use ourselves (plain C types: the interfaces are
// opaque pointers) and look them up by name.
//
// Names that changed across SDK versions are tried newest first:
//   init:      SteamAPI_InitFlat (1.59+), SteamInternal_SteamAPI_Init, SteamAPI_Init
//   accessors: SteamAPI_SteamUserStats_v013/v012, SteamFriends_v018/v017, SteamUtils_v010
// Callbacks use manual dispatch (the C way: pull them in a loop, free each).
//
// Where the library comes from: SFQ_STEAM_LIB if set, else libsteam_api.so
// next to the executable (/proc/self/exe), else the loader's search path.

#include "sfxr_internal.h"
#include "sfxr_steam.h"

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef int32_t HSteamPipe;
typedef struct { int32_t user; int callback; uint8_t *param; int param_size; } SteamCallbackMsg;   // CallbackMsg_t

#define STEAM_CB_GAME_OVERLAY_ACTIVATED 331    // GameOverlayActivated_t: first byte = active

static struct {
    void *lib;
    bool up;
    char status[256];
    char name[128];
    uint32_t app_id;
    bool overlay;
    HSteamPipe pipe;
    void *stats, *friends, *utils;

    int  (*InitFlat)(char *err_1024);                                  // ESteamAPIInitResult, 0 = OK
    int  (*InternalInit)(const char *versions, char *err_1024);
    bool (*Init)(void);
    void (*Shutdown)(void);
    void (*DispatchInit)(void);
    HSteamPipe (*GetPipe)(void);
    void (*DispatchRunFrame)(HSteamPipe);
    bool (*DispatchNext)(HSteamPipe, SteamCallbackMsg *);
    void (*DispatchFree)(HSteamPipe);

    bool (*SetAchievement)(void *, const char *);
    bool (*GetAchievement)(void *, const char *, bool *);
    bool (*ClearAchievement)(void *, const char *);
    bool (*StoreStats)(void *);
    bool (*SetStatInt)(void *, const char *, int32_t);
    bool (*GetStatInt)(void *, const char *, int32_t *);
    const char *(*PersonaName)(void *);
    uint32_t (*AppId)(void *);
} St;

static void *sym(const char *name) { return St.lib ? dlsym(St.lib, name) : NULL; }

// the first of several names that exists
static void *sym_any(const char *const *names)
{
    for (; *names; names++) { void *p = sym(*names); if (p) return p; }
    return NULL;
}

static void *open_library(void)
{
    const char *env = sfxr_env_str("SFQ_STEAM_LIB");
    if (env && *env) return dlopen(env, RTLD_NOW | RTLD_LOCAL);
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        char *slash = strrchr(exe, '/');
        if (slash && (size_t)(slash - exe) + sizeof "/libsteam_api.so" < sizeof exe) {
            strcpy(slash, "/libsteam_api.so");
            void *h = dlopen(exe, RTLD_NOW | RTLD_LOCAL);
            if (h) return h;
        }
    }
    return dlopen("libsteam_api.so", RTLD_NOW | RTLD_LOCAL);
}

bool sfxr_steam_init(void)
{
    if (St.up) return true;
    St.lib = open_library();
    if (!St.lib) {
        snprintf(St.status, sizeof St.status, "Steam: off (no libsteam_api.so by the app: docs/STEAM.md)");
        SFXR_LOG("%s", St.status);
        return false;
    }
    St.InitFlat = (int (*)(char *))sym("SteamAPI_InitFlat");
    St.InternalInit = (int (*)(const char *, char *))sym("SteamInternal_SteamAPI_Init");
    St.Init = (bool (*)(void))sym("SteamAPI_Init");
    St.Shutdown = (void (*)(void))sym("SteamAPI_Shutdown");

    char err[1024] = "";
    bool ok = St.InitFlat ? St.InitFlat(err) == 0
            : St.InternalInit ? St.InternalInit(NULL, err) == 0
            : St.Init ? St.Init() : false;
    if (!ok) {
        snprintf(St.status, sizeof St.status, "Steam: off (%.200s)",
                 err[0] ? err : "init failed: is Steam running, and is there a steam_appid.txt when launched outside Steam?");
        SFXR_LOG("%s", St.status);
        dlclose(St.lib);
        char keep[sizeof St.status];
        memcpy(keep, St.status, sizeof keep);
        memset(&St, 0, sizeof St);
        memcpy(St.status, keep, sizeof keep);
        return false;
    }

    St.DispatchInit = (void (*)(void))sym("SteamAPI_ManualDispatch_Init");
    St.GetPipe = (HSteamPipe (*)(void))sym("SteamAPI_GetHSteamPipe");
    St.DispatchRunFrame = (void (*)(HSteamPipe))sym("SteamAPI_ManualDispatch_RunFrame");
    St.DispatchNext = (bool (*)(HSteamPipe, SteamCallbackMsg *))sym("SteamAPI_ManualDispatch_GetNextCallback");
    St.DispatchFree = (void (*)(HSteamPipe))sym("SteamAPI_ManualDispatch_FreeLastCallback");
    if (St.DispatchInit && St.GetPipe && !SFXR_BREAK(sfxr_steam_no_dispatch)) {
        St.DispatchInit();
        St.pipe = St.GetPipe();
    }

    static const char *const stats_v[] = { "SteamAPI_SteamUserStats_v013", "SteamAPI_SteamUserStats_v012", NULL };
    static const char *const friends_v[] = { "SteamAPI_SteamFriends_v018", "SteamAPI_SteamFriends_v017", NULL };
    static const char *const utils_v[] = { "SteamAPI_SteamUtils_v010", NULL };
    void *(*get)(void);
    if ((get = (void *(*)(void))sym_any(stats_v))) St.stats = get();
    if ((get = (void *(*)(void))sym_any(friends_v))) St.friends = get();
    if ((get = (void *(*)(void))sym_any(utils_v))) St.utils = get();

    St.SetAchievement = (bool (*)(void *, const char *))sym("SteamAPI_ISteamUserStats_SetAchievement");
    St.GetAchievement = (bool (*)(void *, const char *, bool *))sym("SteamAPI_ISteamUserStats_GetAchievement");
    St.ClearAchievement = (bool (*)(void *, const char *))sym("SteamAPI_ISteamUserStats_ClearAchievement");
    St.StoreStats = (bool (*)(void *))sym("SteamAPI_ISteamUserStats_StoreStats");
    St.SetStatInt = (bool (*)(void *, const char *, int32_t))sym("SteamAPI_ISteamUserStats_SetStatInt32");
    St.GetStatInt = (bool (*)(void *, const char *, int32_t *))sym("SteamAPI_ISteamUserStats_GetStatInt32");
    St.PersonaName = (const char *(*)(void *))sym("SteamAPI_ISteamFriends_GetPersonaName");
    St.AppId = (uint32_t (*)(void *))sym("SteamAPI_ISteamUtils_GetAppID");

    St.up = true;
    const char *who = St.friends && St.PersonaName ? St.PersonaName(St.friends) : NULL;
    snprintf(St.name, sizeof St.name, "%s", who ? who : "");
    St.app_id = St.utils && St.AppId ? St.AppId(St.utils) : 0;
    snprintf(St.status, sizeof St.status, "Steam: on, app %u, player %.60s%s", St.app_id, St.name[0] ? St.name : "?",
             St.stats ? "" : " (no user-stats interface: achievements off)");
    SFXR_LOG("%s", St.status);
    return true;
}

bool sfxr_steam_available(void) { return St.up; }
const char *sfxr_steam_status(void) { return St.status[0] ? St.status : "Steam: not started (sfxr_steam_init)"; }
uint32_t sfxr_steam_app_id(void) { return St.up ? St.app_id : 0; }
const char *sfxr_steam_player_name(void) { return St.up ? St.name : ""; }
bool sfxr_steam_overlay_active(void) { return St.up && St.overlay; }

void sfxr_steam_update(void)
{
    if (!St.up || !St.pipe || !St.DispatchRunFrame || !St.DispatchNext || !St.DispatchFree) return;
    St.DispatchRunFrame(St.pipe);
    SteamCallbackMsg msg;
    while (St.DispatchNext(St.pipe, &msg)) {
        if (msg.callback == STEAM_CB_GAME_OVERLAY_ACTIVATED && msg.param && msg.param_size >= 1)
            St.overlay = msg.param[0] != 0;
        // Every callback must be freed, handled or not, or the queue stalls.
        St.DispatchFree(St.pipe);
    }
}

bool sfxr_steam_unlock(const char *achievement)
{
    if (SFXR_BREAK(sfxr_steam_claims_success) && !St.up) return true;
    if (!St.up || !St.stats || !St.SetAchievement || !St.SetAchievement(St.stats, achievement)) return false;
    // Setting only changes the local copy; storing sends it (and shows the popup).
    if (SFXR_BREAK(sfxr_steam_no_store)) return true;
    return St.StoreStats && St.StoreStats(St.stats);
}

bool sfxr_steam_achieved(const char *achievement, bool *done)
{
    return St.up && St.stats && St.GetAchievement && St.GetAchievement(St.stats, achievement, done);
}

bool sfxr_steam_clear(const char *achievement)
{
    if (!St.up || !St.stats || !St.ClearAchievement || !St.ClearAchievement(St.stats, achievement)) return false;
    return St.StoreStats && St.StoreStats(St.stats);
}

bool sfxr_steam_stat_set_int(const char *stat, int32_t value)
{
    if (!St.up || !St.stats || !St.SetStatInt || !St.SetStatInt(St.stats, stat, value)) return false;
    return St.StoreStats && St.StoreStats(St.stats);
}

bool sfxr_steam_stat_get_int(const char *stat, int32_t *value)
{
    return St.up && St.stats && St.GetStatInt && St.GetStatInt(St.stats, stat, value);
}

void sfxr_steam_shutdown(void)
{
    if (St.up && St.Shutdown) St.Shutdown();
    if (St.lib) dlclose(St.lib);
    memset(&St, 0, sizeof St);
}
