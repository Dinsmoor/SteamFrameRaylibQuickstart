// tests/steam - the optional Steamworks module (docs/STEAM.md) against a fake
// libsteam_api.so (tests/steam/fake/): it loads by path, reports the player
// and app, stores achievements (not just sets them), pumps and frees
// callbacks so the overlay state arrives, and without the library every call
// is a harmless "no".
//
//   make test T=steam

#include "sfxt.h"
#include "sfxr_steam.h"

#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    bool running;
    int  inits, shutdowns;
    char set[8][64];
    int  nset;
    char stored[8][64];
    int  nstored;
    int  stores;
    bool overlay_pending;
    uint8_t overlay_active;
    int  queued, freed;
} FakeSteam;

static void scene(void) {}

// The fake library sits next to the test binary.
static const char *fake_path(void)
{
    static char p[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", p, sizeof p - 32);
    p[n > 0 ? n : 0] = 0;
    char *slash = strrchr(p, '/');
    strcpy(slash ? slash + 1 : p, "libfake_steam_api.so");
    return p;
}

// The same instance sfxr loaded (dlopen of the same path returns it).
static FakeSteam *fake(void)
{
    void *h = dlopen(fake_path(), RTLD_NOW | RTLD_NOLOAD);
    FakeSteam *(*get)(void) = h ? (FakeSteam * (*)(void)) dlsym(h, "fake_steam") : NULL;
    return get ? get() : NULL;
}

static bool start(void)
{
    setenv("SFQ_STEAM_LIB", fake_path(), 1);
    bool ok = sfxr_steam_init();
    CHECK(ok, "init with the fake library (%s)", sfxr_steam_status());
    return ok && fake();
}

static void loads_and_reports(void)
{
    if (!start()) return;
    CHECK(sfxr_steam_app_id() == 480, "app id %u", sfxr_steam_app_id());
    CHECK(!strcmp(sfxr_steam_player_name(), "Test Player"), "player name '%s'", sfxr_steam_player_name());
}

static void unlock_is_stored(void)
{
    if (!start()) return;
    CHECK(sfxr_steam_unlock("FIRST_CLIMB"), "unlock returns true");
    FakeSteam *f = fake();
    CHECK(f->stores >= 1, "StoreStats called (%d)", f->stores);
    bool done = false;
    CHECK(sfxr_steam_achieved("FIRST_CLIMB", &done) && done, "Steam knows it's achieved");
}

static void overlay_callback_arrives(void)
{
    if (!start()) return;
    FakeSteam *f = fake();
    f->overlay_active = 1;
    f->overlay_pending = true;
    sfxt_frames(2);   // sfxr_frame_begin pumps the callbacks
    CHECK(sfxr_steam_overlay_active(), "overlay reported open");
    CHECK(f->freed == f->queued, "every callback freed (%d of %d)", f->freed, f->queued);
    f->overlay_active = 0;
    f->overlay_pending = true;
    sfxt_frames(2);
    CHECK(!sfxr_steam_overlay_active(), "overlay reported closed");
}

static void absent_is_harmless(void)
{
    setenv("SFQ_STEAM_LIB", "/nonexistent/libsteam_api.so", 1);
    CHECK(!sfxr_steam_init(), "no library: init says no");
    CHECK(!sfxr_steam_available(), "not available");
    CHECK(!sfxr_steam_unlock("FIRST_CLIMB"), "unlock says no");
    bool done = true;
    CHECK(!sfxr_steam_achieved("FIRST_CLIMB", &done), "achieved says unknown");
    CHECK(sfxr_steam_app_id() == 0 && !strcmp(sfxr_steam_player_name(), ""), "no app, no name");
    sfxt_frames(3);   // the per-frame pump is safe too
    CHECK(strstr(sfxr_steam_status(), "libsteam_api.so") != NULL, "status says why: %s", sfxr_steam_status());
}

static const SfxtCase CASES[] = {
    { "steam/loads-and-reports",       loads_and_reports,       NULL },
    { "steam/unlock-is-stored",        unlock_is_stored,        "sfxr_steam_no_store" },
    { "steam/overlay-callback-arrives", overlay_callback_arrives, "sfxr_steam_no_dispatch" },
    { "steam/absent-is-harmless",      absent_is_harmless,      "sfxr_steam_claims_success" },
};

int main(int argc, char **argv) { return sfxt_main(argc, argv, CASES, SFXT_COUNT(CASES), scene); }
