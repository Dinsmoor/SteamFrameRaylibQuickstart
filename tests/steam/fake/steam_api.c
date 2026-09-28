// A stand-in for Valve's libsteam_api.so: the flat C functions sfxr_steam.c
// uses, with the same names and C types, and a record of what was called.
// Built as build/host-test/tests/libfake_steam_api.so; the test points
// SFQ_STEAM_LIB at it and reads the record through fake_steam().

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct { int32_t user; int callback; uint8_t *param; int param_size; } CallbackMsg;

typedef struct {
    bool running;              // "Steam is up" (init succeeds)
    int  inits, shutdowns;
    char set[8][64];           // achievements set (local copy)
    int  nset;
    char stored[8][64];        // ... and stored (what Steam would know)
    int  nstored;
    int  stores;
    bool overlay_pending;      // queue a GameOverlayActivated on the next RunFrame
    uint8_t overlay_active;
    int  queued, freed;
} FakeSteam;

static FakeSteam F = { .running = true };
static CallbackMsg pending;
static uint8_t overlay_param[8];
static int stats_obj, friends_obj, utils_obj;

FakeSteam *fake_steam(void) { return &F; }

int SteamAPI_InitFlat(char *err) { F.inits++; if (!F.running) { strcpy(err, "fake: Steam is not running"); return 1; } return 0; }
void SteamAPI_Shutdown(void) { F.shutdowns++; }
void SteamAPI_ManualDispatch_Init(void) {}
int32_t SteamAPI_GetHSteamPipe(void) { return 7; }
void SteamAPI_ManualDispatch_RunFrame(int32_t pipe)
{
    (void)pipe;
    if (F.overlay_pending) {
        F.overlay_pending = false;
        overlay_param[0] = F.overlay_active;
        pending = (CallbackMsg){ 1, 331, overlay_param, 16 };
        F.queued++;
    }
}
bool SteamAPI_ManualDispatch_GetNextCallback(int32_t pipe, CallbackMsg *out)
{
    (void)pipe;
    if (F.queued <= F.freed) return false;
    *out = pending;
    return true;
}
void SteamAPI_ManualDispatch_FreeLastCallback(int32_t pipe) { (void)pipe; F.freed++; }

void *SteamAPI_SteamUserStats_v013(void) { return &stats_obj; }
void *SteamAPI_SteamFriends_v018(void) { return &friends_obj; }
void *SteamAPI_SteamUtils_v010(void) { return &utils_obj; }

bool SteamAPI_ISteamUserStats_SetAchievement(void *self, const char *name)
{
    if (self != &stats_obj || F.nset >= 8) return false;
    strncpy(F.set[F.nset++], name, 63);
    return true;
}
bool SteamAPI_ISteamUserStats_GetAchievement(void *self, const char *name, bool *done)
{
    (void)self;
    *done = false;
    for (int i = 0; i < F.nstored; i++) if (!strcmp(F.stored[i], name)) *done = true;
    return true;
}
bool SteamAPI_ISteamUserStats_ClearAchievement(void *self, const char *name) { (void)self; (void)name; return true; }
bool SteamAPI_ISteamUserStats_StoreStats(void *self)
{
    (void)self;
    F.stores++;
    for (int i = 0; i < F.nset && F.nstored < 8; i++) strcpy(F.stored[F.nstored++], F.set[i]);
    F.nset = 0;
    return true;
}
bool SteamAPI_ISteamUserStats_SetStatInt32(void *self, const char *name, int32_t v) { (void)self; (void)name; (void)v; return true; }
bool SteamAPI_ISteamUserStats_GetStatInt32(void *self, const char *name, int32_t *v) { (void)self; (void)name; *v = 3; return true; }
const char *SteamAPI_ISteamFriends_GetPersonaName(void *self) { (void)self; return "Test Player"; }
uint32_t SteamAPI_ISteamUtils_GetAppID(void *self) { (void)self; return 480; }
