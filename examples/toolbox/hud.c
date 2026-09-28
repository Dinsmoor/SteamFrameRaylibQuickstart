// hud.c - visor HUD templates: four ways to keep a readout with the player
// (docs/ATTACHING.md, "HUDs"). The Menus & HUD station switches between them,
// and the garden uses the same code for its health and score.
//
//   HEAD     locked to the head, low in the view. Always readable, but it
//            swims with every head movement and can't be looked at "properly"
//            (you can't turn to it). Fine for a word or two at the edge;
//            tiring for anything more.
//   FOLLOW   tag-along: stays where it is while you glance around, and glides
//            back in front of you once you've turned away more than 20
//            degrees. The comfortable default for a readout you check often.
//   BODY     on your belt, tilted up: look down to read it, like a watch you
//            wear on your waist. Out of the way until wanted.
//   OFF      nothing; the world has to tell you instead (signs, lamps).
//
// All of them are ordinary passive vrui panels (no laser, no claims), drawn
// on top of the world so a wall can't swallow them. Only the pose differs.

#include "toolbox.h"

const char *const HUD_STYLE_NAMES[HUD_COUNT] = { "Off", "Head", "Follow", "Body" };

#define HUD_W 0.24f
#define HUD_H 0.07f

static SfxrPose head_locked(void)
{
    // 0.8 m ahead and 12 cm below the line of sight, turned to face the eyes.
    // Everything is in head space, so it moves exactly with the head.
    return sfxr_pose_mul(sfxr_head(), (SfxrPose){ { 0, -0.12f, -0.8f }, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, 8.5f * DEG2RAD) });
}

static SfxrPose follow_target(void)
{
    // Where the tag-along wants to be: 1 m ahead in the direction you look
    // (level), a little below eye height, facing you.
    SfxrPose t = vrui_in_front_of_head(1.0f, 0.3f);
    return t;
}

static SfxrPose on_belt(void)
{
    // At the waist, 35 cm in front of you, tilted 55 degrees up toward your face.
    SfxrPose body = vrui_body();
    return sfxr_pose_mul(body, (SfxrPose){ { 0, 0.55f * vrui_eye_height(), -0.35f },
                                           QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -55.0f * DEG2RAD) });
}

void hud_show(HudStyle style, const char *text, Color accent)
{
    if (style == HUD_OFF || !text) return;
    SfxrPose pose;
    switch (style) {
    case HUD_HEAD:   pose = head_locked(); break;
    case HUD_FOLLOW: pose = vrui_follow(VRUI_ID2(G_HUD, 1), follow_target(), 20.0f, 0.45f); break;
    default:         pose = on_belt(); break;
    }
    vrui_on_top_begin();
    vrui_panel_passive();
    if (vrui_panel_begin(VRUI_ID2(G_HUD, 2), &pose, HUD_W, HUD_H, NULL)) {
        Rectangle r = vrui_panel_content();
        DrawRectangle(0, 0, 6, (int)(HUD_H * vrui_style()->px_per_m), accent);   // a colored edge
        vrui_label((Rectangle){ r.x + 4, r.y, r.width - 4, r.height }, text);
        vrui_panel_end();
    }
    vrui_on_top_end();
}
