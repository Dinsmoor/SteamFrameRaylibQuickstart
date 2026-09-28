// sfxr_script.c - the "script" backend used by the test harness (sfxt/):
// every frame the harness fills in the head and raw hands; time advances by
// exactly 1/72 s; nothing is rendered. Also home of the break-switch lookup
// in test builds (sfxr_break.h).

#include "sfxr_internal.h"

#include <stdlib.h>
#include <string.h>

#define S sfxr_state

void (*sfxr_script_fill)(void);
void (*sfxr_script_haptic)(int hand, float amplitude, float seconds);

static bool script_init(void)
{
    S.eye_w = S.eye_h = 64;
    S.sig.presence_known = 1;
    S.sig.present = 1;
    S.sig.refresh_hz = 72.0f;
    snprintf(S.runtime_name, sizeof S.runtime_name, "sfxt script");
    snprintf(S.system_name, sizeof S.system_name, "scripted hands");
    S.should_render = false;
    return true;
}

static void script_shutdown(void) {}

static bool script_frame_begin(void)
{
    S.dt = 1.0f / 72.0f;
    S.should_render = false;
    S.views_valid = true;
    if (sfxr_script_fill) sfxr_script_fill();
    return true;
}

static bool script_acquire(unsigned *fbo, unsigned *tex) { (void)fbo; (void)tex; return false; }
static void script_release(void) {}
static void script_frame_end(bool rendered) { (void)rendered; }
static void script_haptic(SfxrHandId hand, float amplitude, float seconds, float freq)
{
    (void)freq;
    if (sfxr_script_haptic) sfxr_script_haptic(hand == SFXR_RIGHT ? 1 : 0, amplitude, seconds);
}

const SfxrBackendVtbl sfxr_backend_script = {
    script_init, script_shutdown, script_frame_begin, script_acquire,
    script_release, script_frame_end, script_haptic,
};

#ifdef SFXR_TESTING
bool sfxr_break_on(const char *name)
{
    const char *v = getenv("SFXR_BREAK");
    if (!v || !*v) return false;
    size_t n = strlen(name);
    for (const char *p = v; *p; ) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == n && !strncmp(p, name, n)) return true;
        if (!e) break;
        p = e + 1;
    }
    return false;
}
#endif
