// vrui_menu.c - the radial (pie) menu: one hand, no aiming.
//
// Hold a button and the choices open in a ring above that controller; tilt
// the stick toward one (it lights up and ticks); let go of the button to pick
// it. Letting go with the stick centered picks nothing. It's quick once the
// positions are learned (the direction is remembered, not read), and it needs
// no second hand and no laser. The stick is claimed while it is open, so
// locomotion doesn't teleport or turn you.

#include "vrui_internal.h"

typedef struct {
    bool open;
    bool settle;        // closed, but the stick hasn't come back to center yet
    int  hot;           // highlighted choice, -1 none
} RadialState;
VRUI_STATE_FITS(RadialState);

int vrui_radial_menu(VruiId id, SfxrHandId hand, const SfxrButton *hold, const char *const *items, int count)
{
    RadialState *rs = VRUI_STATE(vrui__item(id), RadialState);
    const SfxrHand *h = sfxr_hand(hand);
    if (!h->active || count <= 0) { rs->open = rs->settle = false; return -1; }
    // After choosing, the thumb is still on the tilted stick. Keep the stick
    // claimed until it's back at center, or letting go of it would teleport.
    if (rs->settle) {
        if (Vector2Length(h->stick) < 0.3f) rs->settle = false;
        else if (!SFXR_BREAK(vrui_radial_no_claim)) vrui_claim_input(hand);
    }
    if (!rs->open) {
        // Open on a fresh press, and not while this hand is using something.
        if (!hold->pressed || vrui_hand_busy(hand)) return -1;
        rs->open = true;
        rs->hot = -1;
        vrui_haptic_pulse(hand, 0.2f, 0.015f, 0);
    }
    if (!SFXR_BREAK(vrui_radial_no_claim)) vrui_claim_input(hand);

    // The stick picks a slice: slice 0 is straight up (stick forward), then
    // clockwise.
    float slice = 2.0f * PI / (float)count;
    int hot = -1;
    if (Vector2Length(h->stick) > 0.5f) {
        float a = SFXR_BREAK(vrui_radial_angle_from_x) ? atan2f(h->stick.y, h->stick.x) : atan2f(h->stick.x, h->stick.y);
        if (a < 0) a += 2.0f * PI;
        hot = (int)floorf(a / slice + 0.5f) % count;
    }
    if (hot != rs->hot && hot >= 0) vrui_haptic_pulse(hand, 0.25f, 0.012f, 0);
    if (hot >= 0 || !SFXR_BREAK(vrui_radial_keeps_last)) rs->hot = hot;
    else hot = rs->hot;

    if (!hold->down) {
        rs->open = false;
        rs->settle = true;
        if (hot >= 0) {
            vrui__click_pulse(hand);
            sfxr_event("menu", "%s picked \"%s\" (%s hand)", vrui__who(id), items[hot], hand == SFXR_LEFT ? "left" : "right");
        }
        return hot;
    }

    // The ring floats 11 cm above the controller in a plane facing you, on
    // top of everything (the controller model would otherwise hide it).
    SfxrPose head = sfxr_head();
    Vector3 center = Vector3Add(h->grip.position, (Vector3){ 0, 0.11f, 0 });
    SfxrPose plane = vrui_facing(center, head.position);
    Vector3 right = sfxr_pose_right(plane), up = sfxr_pose_up(plane);
    float dist = Vector3Distance(head.position, center);
    float th = Clamp(vrui_text_height(dist, 1.3f), 0.009f, 0.03f);
    float radius = th * 5.0f;
    Color plate = { 20, 22, 28, 220 }, lit = C.style.accent;

    vrui_on_top_begin();
    vrui__sphere(center, th * 0.3f, C.style.text_dim);
    if (hot >= 0) {
        Vector2 s = Vector2Normalize(h->stick);
        vrui_line(center, Vector3Add(center, Vector3Add(Vector3Scale(right, s.x * radius * 0.6f), Vector3Scale(up, s.y * radius * 0.6f))), lit);
    }
    for (int i = 0; i < count; i++) {
        float a = (float)i * slice;
        Vector3 at = Vector3Add(center, Vector3Add(Vector3Scale(right, sinf(a) * radius), Vector3Scale(up, cosf(a) * radius)));
        vrui_tag(at, items[i], th, i == hot ? (Color){ 20, 22, 28, 255 } : C.style.text, i == hot ? lit : plate);
    }
    vrui_on_top_end();
    return -1;
}

bool vrui_radial_open(VruiId id) { return VRUI_STATE(vrui__item(id), RadialState)->open; }
