// hud.c - visor HUD templates: four ways to keep a readout with the player
// (docs/ATTACHING.md, "HUDs"). The Menus & HUD station switches between them,
// and Daddy Bug Smasher uses the same code for its health and score.
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

#include <time.h>

const char *const HUD_STYLE_NAMES[HUD_COUNT] = { "Off", "Head", "Follow", "Body" };

#define HUD_W 0.312f
#define HUD_H 0.091f

static SfxrPose head_locked(void)
{
    // 0.8 m ahead and 12 cm below the line of sight, turned to face the eyes.
    // Everything is in head space, so it moves exactly with the head.
    return sfxr_pose_mul(sfxr_head(), (SfxrPose){ { 0, -0.12f, -0.8f }, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, 8.5f * DEG2RAD) });
}

static SfxrPose follow_target(void)
{
    // Where the tag-along wants to be: 1 m out, 28 degrees below wherever
    // you look (up, level or down at a table), facing you. Below the gaze,
    // not at a fixed height: look down at a bench and a HUD at a fixed
    // height lands right on the bench.
    SfxrPose head = sfxr_head();
    Vector3 fwd = sfxr_pose_forward(head);
    float yaw = atan2f(fwd.x, -fwd.z), pitch = asinf(Clamp(fwd.y, -1, 1));
    pitch = Clamp(pitch - 28.0f * DEG2RAD, -80.0f * DEG2RAD, 60.0f * DEG2RAD);   // never past straight down
    Vector3 dir = { sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch) };
    Vector3 at = Vector3Add(head.position, dir);
    SfxrPose t = vrui_facing(at, head.position);
    Vector3 to_eye = Vector3Normalize(Vector3Subtract(head.position, at));
    float tilt = asinf(Clamp(to_eye.y, -1, 1));   // tip it back to face the eyes
    t.orientation = QuaternionMultiply(t.orientation, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -tilt));
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

// --- screenshots from inside the headset: a menu item starts a short
// countdown (so the menu is gone and you can aim), then sfxr_screenshot
// saves what both eyes see into shots/ next to the app. A flash and a click
// say it's taken (after it's taken: they're not in the picture).
static float shot_in, shot_flash;
static const char *shot_app;

void screenshot_start(const char *app)
{
    shot_in = 2.0f;
    shot_app = app;
}

void screenshot_update(void)
{
    if (shot_flash > 0) {
        vrui_tint(RAYWHITE, 0.5f * shot_flash / 0.15f);
        shot_flash -= sfxr_dt();
    }
    if (shot_in <= 0) return;
    float was = shot_in;
    shot_in -= sfxr_dt();
    if (shot_in > 0.4f) {   // the countdown, until just before (it would be in the picture)
        SfxrPose t = vrui_in_front_of_head(0.9f, 0.25f);
        vrui_tag(t.position, TextFormat("screenshot in %.0f", ceilf(shot_in - 0.4f)), 0.03f, RAYWHITE, (Color){ 20, 22, 28, 200 });
    }
    if (was > 0 && shot_in <= 0) {
        char stamp[32];
        time_t now = time(NULL);
        strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
        sfxr_screenshot(TextFormat("shots/%s-%s.png", shot_app ? shot_app : "app", stamp));
        shot_flash = 0.15f;
        sound_play_here(SND_CLICK, 0.6f);
    }
}
