// notes.c - design notes from inside the headset (docs/NOTES.md).
//
// Hold VIEW (the small button on the left controller; key 3 with the left
// hand in the simulator) and say what you want changed: "make a variant of
// this bug that flies and spits slop while it hovers". Let go: sfxr saves the
// clip, a screenshot, and this file's idea of what "this" was:
//
//   pointing   what each hand's laser is on: the nearest named widget or mark
//              within a few degrees of the aim ray (the registry: "garden.bug 3",
//              "table.sky", "voice.bug 2"...)
//   looking    the same for your gaze (eye tracking) or, without it, your head
//   place      the toolbox area you're in: the garden, or the station nearest you
//
// The build machine turns the clip into text and hands both to the coding
// agent, which knows the registry names (scripts/notes/, .claude/skills/).

#include "toolbox.h"
#include "garden.h"
#include "sfxr_voice.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static struct {
    bool holding;
    float shown;        // "saved" tag, seconds left
    char saved[48];
} NT;

// The registry entry closest to a ray (widget or mark), within `max_deg`
// of it and `max_m` away; "" if none. Names are the registry's ("garden.bug 3").
static const char *aimed(Vector3 from, Vector3 dir, float max_deg, float max_m)
{
    static char best[48];
    best[0] = 0;
    float best_deg = max_deg;
    for (int i = 0; i < vrui_widget_count(); i++) {
        const VruiWidgetInfo *w = vrui_widget_at(i);
        Vector3 to = Vector3Subtract(w->pose.position, from);
        float d = Vector3Length(to);
        if (d < 0.05f || d > max_m) continue;
        float c = Vector3DotProduct(dir, Vector3Scale(to, 1 / d));
        float deg = acosf(c > 1 ? 1 : c < -1 ? -1 : c) * RAD2DEG;
        // a big thing near you subtends more: allow a little for size... the
        // registry has no size, so close things get a wider cone
        float allow = best_deg + (d < 1.5f ? 6.0f : 0.0f);
        if (deg < allow) { best_deg = deg; snprintf(best, sizeof best, "%s", w->name); }
    }
    return best;
}

// JSON-safe copy of a registry name (they're plain ASCII; quotes just in case)
static const char *jstr(const char *s)
{
    static char out[96];
    int at = 0;
    for (; *s && at < 90; s++) {
        if (*s == '"' || *s == '\\') out[at++] = '\\';
        out[at++] = *s;
    }
    out[at] = 0;
    return out;
}

// Where you are, in the toolbox's own words.
static const char *place(void)
{
    if (garden_active()) return "garden (Daddy Bug Smasher)";
    static const struct { float x; const char *name; } STATIONS[] = {
        { -23, "particles" }, { -19, "weights" }, { -16, "wielding" }, { -14.5f, "sound" }, { -12.2f, "voice commands" },
        { -9.5f, "menus & hud" }, { -6.4f, "hands-on setup" }, { -3.5f, "controls diagram" }, { 0, "workbench" },
        { 3.2f, "mechanisms" }, { 6.2f, "linkage" }, { 9.5f, "controllers & headset panels" }, { 12.5f, "hinges & cords" },
        { 15.5f, "attach & label" }, { 17.5f, "smoothing" }, { 19.5f, "the garden gate" },
    };
    Vector3 me = sfxr_head_floor_point();
    if (me.z > 3.5f) return "movement yard";
    int best = 0;
    for (int i = 1; i < (int)(sizeof STATIONS / sizeof STATIONS[0]); i++)
        if (fabsf(STATIONS[i].x - me.x) < fabsf(STATIONS[best].x - me.x)) best = i;
    return STATIONS[best].name;
}

static const char *context(void)
{
    static char json[512];
    const SfxrHand *l = sfxr_hand(SFXR_LEFT), *r = sfxr_hand(SFXR_RIGHT);
    char pl[48] = "", pr[48] = "", look[48] = "";
    if (l->active) snprintf(pl, sizeof pl, "%s", aimed(l->aim.position, sfxr_pose_forward(l->aim), 6, 8));
    if (r->active) snprintf(pr, sizeof pr, "%s", aimed(r->aim.position, sfxr_pose_forward(r->aim), 6, 8));
    SfxrPose eye;
    bool gaze = sfxr_gaze(&eye);
    if (!gaze) eye = sfxr_head();
    snprintf(look, sizeof look, "%s", aimed(eye.position, sfxr_pose_forward(eye), gaze ? 5 : 10, 12));
    int at = snprintf(json, sizeof json, "{\"app\":\"toolbox\",\"place\":\"%s\"", place());
    at += snprintf(json + at, sizeof json - (size_t)at, ",\"pointing\":{\"left\":%s%s%s", pl[0] ? "\"" : "", pl[0] ? jstr(pl) : "null", pl[0] ? "\"" : "");
    at += snprintf(json + at, sizeof json - (size_t)at, ",\"right\":%s%s%s}", pr[0] ? "\"" : "", pr[0] ? jstr(pr) : "null", pr[0] ? "\"" : "");
    at += snprintf(json + at, sizeof json - (size_t)at, ",\"looking\":%s%s%s,\"looking_by\":\"%s\"}", look[0] ? "\"" : "", look[0] ? jstr(look) : "null", look[0] ? "\"" : "", gaze ? "gaze" : "head");
    (void)at;
    return json;
}

void notes_update(void)
{
    const SfxrHand *lh = sfxr_hand(SFXR_LEFT);
    if (lh->active && lh->view.pressed && !NT.holding) NT.holding = sfxr_note_begin();
    if (NT.holding && (lh->view.released || !lh->active)) {
        NT.holding = false;
        if (sfxr_note_end(context())) { snprintf(NT.saved, sizeof NT.saved, "%s", sfxr_note_last()); NT.shown = 2.5f; }
    }
    if (NT.holding) {
        vrui_tag(Vector3Add(lh->grip.position, (Vector3){ 0, 0.12f, 0 }),
                 TextFormat("note %.0fs: say what you want changed", sfxr_note_seconds()), 0.028f, RAYWHITE, (Color){ 30, 90, 160, 220 });
    } else if (NT.shown > 0) {
        NT.shown -= sfxr_dt();
        vrui_tag(Vector3Add(lh->grip.position, (Vector3){ 0, 0.12f, 0 }), TextFormat("note saved: %s", NT.saved), 0.028f, RAYWHITE,
                 (Color){ 30, 120, 60, 220 });
    }
    sfxr_report("notes", (float)sfxr_note_count());
}
