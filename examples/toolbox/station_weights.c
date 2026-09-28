// station_weights.c - Weights, at the far left end of the row: how things
// of different weights should feel in VR, where nothing pushes back
// (vrui_wield_spec, docs/WIELDING.md "Weights"). One thing per preset:
//
//   FEATHER  on your hand exactly; let go and it drifts down; can't be thrown
//   BALL     light: throws far, bounces
//   BRICK    medium: a moment behind a quick move, a thud when it lands
//   KETTLEBELL heavy: swings behind your wrist, can't be thrown far
//   ANVIL    huge: one hand only drags it along the floor; two hands lift it,
//            slowly; you can't throw it at all
//
// Throw them down the lane behind the table (it's marked every meter): the
// panel shows how far each went. Each thing's label says the numbers behind
// its feel, so you can take a preset as it is or start from one.

#include "toolbox.h"
#include "sfxr_audio.h"

#include <stdio.h>
#include <string.h>

#define X0 -18.8f
#define TABLE_H 0.8f

enum { K_FEATHER, K_BALL, K_BRICK, K_BELL, K_ANVIL, K_COUNT };
static const char *const NAMES[K_COUNT] = { "feather", "ball", "brick", "kettlebell", "anvil" };
static const VruiWeight WEIGHT[K_COUNT] = { VRUI_WEIGHT_FEATHER, VRUI_WEIGHT_LIGHT, VRUI_WEIGHT_MEDIUM, VRUI_WEIGHT_HEAVY, VRUI_WEIGHT_HUGE };
static const Vector3 HALF[K_COUNT] = { { 0.06f, 0.004f, 0.02f }, { 0.04f, 0.04f, 0.04f }, { 0.1f, 0.033f, 0.05f }, { 0.07f, 0.09f, 0.07f },
                                       { 0.2f, 0.14f, 0.1f } };
static const Color COLOR[K_COUNT] = { { 235, 225, 200, 255 }, { 220, 80, 60, 255 }, { 170, 70, 50, 255 }, { 50, 50, 55, 255 },
                                      { 80, 84, 92, 255 } };

static struct {
    bool init;
    VruiWieldSpec spec[K_COUNT];
    SfxrPose pose[K_COUNT], home[K_COUNT];
    VruiWield w[K_COUNT];
    Vector3 thrown_from[K_COUNT];
    float distance[K_COUNT];       // the last throw, meters along the floor (-1: none yet)
    bool flying[K_COUNT];
    SfxrPose panel;
} WT;

static float ground(Vector3 at)
{
    bool table = fabsf(at.x - X0) < 0.7f && fabsf(at.z - ROW_Z) < 0.3f && at.y > TABLE_H - 0.1f;
    return table ? TABLE_H : 0.0f;
}

static void put_back(void)
{
    for (int k = 0; k < K_COUNT; k++) {
        vrui_wield_drop(VRUI_ID2(G_WEIGHTS, 1 + k));
        WT.pose[k] = WT.home[k];
    }
}

static void init(void)
{
    for (int k = 0; k < K_COUNT; k++) {
        WT.spec[k] = vrui_wield_spec(WEIGHT[k]);
        WT.spec[k].half = HALF[k];
        WT.spec[k].ground = ground;
        WT.distance[k] = -1;
        float x = X0 - 0.5f + 0.25f * (float)k;
        WT.home[k] = (SfxrPose){ { x, TABLE_H + HALF[k].y + 0.002f, ROW_Z }, QuaternionIdentity() };
    }
    WT.spec[K_BELL].mass = 8.0f;   // a kettlebell: heavy, but one hand lifts it
    WT.spec[K_BELL].lift_hands = 1;
    WT.home[K_ANVIL] = (SfxrPose){ { X0 + 0.25f, HALF[K_ANVIL].y + 0.002f, ROW_Z + 0.55f }, QuaternionIdentity() };   // on the floor
    WT.panel = (SfxrPose){ { X0 + 1.05f, 1.35f, ROW_Z + 0.1f }, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, -20 * DEG2RAD) };
    put_back();
    WT.init = true;
}

// The numbers behind a thing's feel, for its label.
static const char *numbers(int k)
{
    const VruiWieldSpec *s = &WT.spec[k];
    return TextFormat("%s: %s, %.2g kg\nthrows up to %.0f m/s%s%s", NAMES[k], vrui_weight_name(WEIGHT[k]), s->mass, s->max_throw,
                      s->drag > 1 ? ", drifts down" : s->bounce > 0.3f ? ", bouncy" : "", s->lift_hands > 1 ? "\nlift with two hands" : "");
}

static void lane(void)
{
    // the throwing lane behind the table, a line every meter
    for (int m = 1; m <= 10; m++) {
        float z = ROW_Z - 0.3f - (float)m;
        vrui_line((Vector3){ X0 - 0.8f, 0.005f, z }, (Vector3){ X0 + 0.8f, 0.005f, z }, (Color){ 255, 255, 255, 160 });
        vrui_text_at((SfxrPose){ { X0 - 0.95f, 0.01f, z }, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -PI / 2) },
                     TextFormat("%d m", m), 0.08f, RAYWHITE);
    }
}

static void panel(void)
{
    if (!vrui_panel_begin(VRUI_ID2(G_WEIGHTS, 0), &WT.panel, 0.4f, 0.4f, "Weights")) return;
    vrui_layout_begin(vrui_panel_content(), 4);
    vrui_label(vrui_row(20), "the last throw of each:");
    for (int k = 0; k < K_COUNT; k++) {
        const VruiWield *w = &WT.w[k];
        vrui_label(vrui_row(20), TextFormat("  %s: %s%s", NAMES[k], WT.distance[k] < 0 ? "-" : TextFormat("%.1f m", WT.distance[k]),
                                            w->straining ? "  (too heavy for one hand)" : ""));
    }
    if (vrui_button(1, vrui_row(32), "Put them back")) put_back();
    vrui_panel_end();
}

void station_weights(void)
{
    if (!WT.init) init();
    station_sign(X0, "Weights", "feather to anvil: pick each up, swing it, throw\nit down the lane. Heavy should feel heavy");
    vrui_box((SfxrPose){ { X0, TABLE_H - 0.025f, ROW_Z }, QuaternionIdentity() }, (Vector3){ 1.4f, 0.05f, 0.6f }, (Color){ 120, 92, 66, 255 });
    for (int i = 0; i < 4; i++)
        vrui_box((SfxrPose){ { X0 + ((i & 1) ? 0.62f : -0.62f), TABLE_H * 0.5f - 0.025f, ROW_Z + ((i & 2) ? 0.24f : -0.24f) }, QuaternionIdentity() },
                 (Vector3){ 0.05f, TABLE_H - 0.05f, 0.05f }, (Color){ 90, 68, 52, 255 });
    lane();
    for (int k = 0; k < K_COUNT; k++) {
        VruiWield *w = &WT.w[k];
        *w = vrui_wield(VRUI_ID2(G_WEIGHTS, 1 + k), &WT.pose[k], &WT.spec[k]);
        vrui_name_widget(VRUI_ID2(G_WEIGHTS, 1 + k), NAMES[k]);
        // a throw: from where it left the hand to where it came to rest
        if (w->released) { WT.thrown_from[k] = WT.pose[k].position; WT.flying[k] = true; }
        if (WT.flying[k] && !w->loose) {
            WT.flying[k] = false;
            Vector3 d = Vector3Subtract(WT.pose[k].position, WT.thrown_from[k]);
            WT.distance[k] = sqrtf(d.x * d.x + d.z * d.z);
            if (k >= K_BRICK) sound_play(SND_THUMP, WT.pose[k].position, Clamp(WT.spec[k].mass / 8.0f, 0.3f, 1.0f));
            sfxr_event("landed", "%s %.1f m", NAMES[k], WT.distance[k]);
        }
        vrui_box(WT.pose[k], Vector3Scale(HALF[k], 2), (w->hovered || w->hands > 0) ? ColorBrightness(COLOR[k], 0.3f) : COLOR[k]);
        if (w->hands == 0 && !w->loose && !w->pulling)
            vrui_text3d(Vector3Add(WT.pose[k].position, (Vector3){ 0, HALF[k].y + 0.1f, 0 }), numbers(k), 0.013f, RAYWHITE);
        sfxr_report(TextFormat("%s_y", NAMES[k]), WT.pose[k].position.y);
    }
    panel();
}
