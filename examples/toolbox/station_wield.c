// station_wield.c - Wielding, past the left end of the row: weapons held the
// Blade & Sorcery way (vrui_wield, docs/WIELDING.md). A rack of four:
//
//   SWORD    a hand-and-a-half grip: one hand, or two on its long handle.
//            However you pick it up, it settles blade-out of your fist,
//            edge toward your knuckles (either edge: it has two).
//   HAMMER   Daddy's, with a long shaft. Hold it at the end and it swings
//            heavy behind your wrist; slide your hand up (loosen the grip,
//            don't let go) to choke up, or put the other hand on it.
//   SPEAR    a round shaft: any way round, slide anywhere along it, two
//            hands to aim it.
//   DAGGER   light, one hand. Pick it up point-down and it's held
//            point-down: an icepick grip.
//
// Hit the sandbag: it swings with the blow, thumps from where you hit it,
// and you feel it. Drop something out of reach? Point at it and squeeze
// the grip: it flies back into your hand. The panel turns each idea off, so
// you can feel what it adds.

#include "toolbox.h"
#include "sfxr_audio.h"

#include <stdio.h>
#include <string.h>

#define X0 -15.4f

enum { W_SWORD, W_HAMMER, W_SPEAR, W_DAGGER, W_COUNT };
static const char *const NAMES[W_COUNT] = { "sword", "hammer", "spear", "dagger" };

static struct {
    bool init;
    VruiWieldSpec spec[W_COUNT];
    SfxrPose pose[W_COUNT], home[W_COUNT];
    VruiWield w[W_COUNT];
    bool sticky, slide, weight, show_handles;
    SfxrPose panel;
    // the sandbag: a pendulum (angles about X and Z, and their speeds)
    Vector2 bag, bag_v;
    float hit_cool[W_COUNT];
    int hits;
} WD;

#define BAG_TOP ((Vector3){ X0 + 1.25f, 2.3f, ROW_Z + 0.55f })
#define BAG_ROPE 1.1f
#define BAG_R 0.17f

static Vector3 bag_center(void)
{
    // hanging straight down, swung by its two angles
    Vector3 d = { sinf(WD.bag.x), -cosf(WD.bag.x) * cosf(WD.bag.y), sinf(WD.bag.y) };
    return Vector3Add(BAG_TOP, Vector3Scale(d, BAG_ROPE));
}

// The four weapons. Each one's own frame: the handle along +Y, its face
// (edge, hammer face) +Z. Origins at the handle's butt end, where it stands on the floor.
static void specs(void)
{
    VruiWieldSpec *s = WD.spec;
    s[W_SWORD] = vrui_wield_spec(VRUI_WEIGHT_MEDIUM);
    s[W_SWORD].grip[0] = (VruiGrip){ { 0, 0.03f, 0 }, { 0, 0.25f, 0 }, { 0, 0, 1 }, 2, false };
    s[W_SWORD].ngrips = 1;
    s[W_SWORD].center = (Vector3){ 0, 0.4f, 0 };
    s[W_SWORD].box_center = (Vector3){ 0, 0.55f, 0 };
    s[W_SWORD].half = (Vector3){ 0.02f, 0.55f, 0.1f };

    s[W_HAMMER] = vrui_wield_spec(VRUI_WEIGHT_HEAVY);
    s[W_HAMMER].mass = 3.0f;
    s[W_HAMMER].grip[0] = (VruiGrip){ { 0, 0.04f, 0 }, { 0, 0.78f, 0 }, { 0, 0, 1 }, 1, false };
    s[W_HAMMER].ngrips = 1;
    s[W_HAMMER].center = (Vector3){ 0, 0.8f, 0 };
    s[W_HAMMER].box_center = (Vector3){ 0, 0.47f, 0 };
    s[W_HAMMER].half = (Vector3){ 0.07f, 0.47f, 0.13f };

    s[W_SPEAR] = vrui_wield_spec(VRUI_WEIGHT_MEDIUM);
    s[W_SPEAR].mass = 2.0f;
    s[W_SPEAR].sticky = s[W_SPEAR].slide = true;
    s[W_SPEAR].grip[0] = (VruiGrip){ { 0, 0.05f, 0 }, { 0, 1.7f, 0 }, { 0, 0, 1 }, 0, false };
    s[W_SPEAR].ngrips = 1;
    s[W_SPEAR].center = (Vector3){ 0, 1.0f, 0 };
    s[W_SPEAR].box_center = (Vector3){ 0, 0.98f, 0 };
    s[W_SPEAR].half = (Vector3){ 0.03f, 0.98f, 0.03f };

    s[W_DAGGER] = vrui_wield_spec(VRUI_WEIGHT_LIGHT);
    s[W_DAGGER].grip[0] = (VruiGrip){ { 0, 0.02f, 0 }, { 0, 0.11f, 0 }, { 0, 0, 1 }, 2, true };
    s[W_DAGGER].ngrips = 1;
    s[W_DAGGER].center = (Vector3){ 0, 0.14f, 0 };
    s[W_DAGGER].box_center = (Vector3){ 0, 0.17f, 0 };
    s[W_DAGGER].half = (Vector3){ 0.015f, 0.17f, 0.04f };
}

static void back_on_the_rack(void)
{
    for (int i = 0; i < W_COUNT; i++) {
        vrui_wield_drop(VRUI_ID2(G_WIELD, 1 + i));
        WD.pose[i] = WD.home[i];
    }
}

static void init(void)
{
    specs();
    // standing in the rack, butt ends on the floor (the dagger on the rack's shelf)
    for (int i = 0; i < 3; i++)
        WD.home[i] = (SfxrPose){ { X0 - 0.45f + 0.35f * (float)i, 0.01f, ROW_Z }, QuaternionIdentity() };
    WD.home[W_DAGGER] = (SfxrPose){ { X0 + 0.55f, TABLE_Y + 0.02f, ROW_Z + 0.1f },
                                    QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, PI / 2) };   // lying flat, point toward you
    WD.panel = (SfxrPose){ { X0 - 1.15f, 1.35f, ROW_Z + 0.1f }, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, 20 * DEG2RAD) };
    WD.sticky = WD.slide = WD.weight = true;
    back_on_the_rack();
    WD.init = true;
}

// What's under a dropped weapon: the dagger's shelf, or the floor.
static float ground(Vector3 at)
{
    bool shelf = fabsf(at.x - (X0 + 0.55f)) < 0.25f && fabsf(at.z - (ROW_Z + 0.05f)) < 0.2f && at.y > TABLE_Y - 0.05f;
    return shelf ? TABLE_Y : 0.0f;
}

// --- drawing each weapon in its own frame ---------------------------------------------

static void part(SfxrPose at, float x, float y, float z, float sx, float sy, float sz, Color c)
{
    vrui_box(sfxr_pose_mul(at, (SfxrPose){ { x, y, z }, QuaternionIdentity() }), (Vector3){ sx, sy, sz }, c);
}

static void draw(int i, SfxrPose p, bool lit)
{
    Color wood = { 120, 84, 52, 255 }, steel = { 200, 205, 215, 255 }, dark = { 70, 70, 80, 255 };
    if (lit) { wood = ColorBrightness(wood, 0.3f); steel = ColorBrightness(steel, 0.15f); }
    switch (i) {
    case W_SWORD:
        part(p, 0, 0.14f, 0, 0.03f, 0.25f, 0.03f, (Color){ 90, 60, 40, 255 });   // grip
        part(p, 0, 0.015f, 0, 0.04f, 0.03f, 0.04f, dark);                       // pommel
        part(p, 0, 0.27f, 0, 0.03f, 0.025f, 0.2f, dark);                        // guard (across the edge line)
        part(p, 0, 0.7f, 0, 0.008f, 0.84f, 0.05f, steel);                        // blade: edges +-Z
        break;
    case W_HAMMER:
        part(p, 0, 0.42f, 0, 0.035f, 0.84f, 0.035f, wood);                      // the long shaft
        part(p, 0, 0.87f, 0, 0.11f, 0.11f, 0.24f, dark);                        // head
        part(p, 0, 0.87f, 0.125f, 0.12f, 0.12f, 0.012f, (Color){ 180, 60, 50, 255 });   // its face (+Z)
        break;
    case W_SPEAR:
        part(p, 0, 0.87f, 0, 0.03f, 1.74f, 0.03f, wood);
        part(p, 0, 1.83f, 0, 0.012f, 0.18f, 0.05f, steel);
        break;
    default:
        part(p, 0, 0.065f, 0, 0.025f, 0.11f, 0.025f, (Color){ 60, 40, 30, 255 });
        part(p, 0, 0.125f, 0, 0.02f, 0.015f, 0.08f, dark);
        part(p, 0, 0.23f, 0, 0.006f, 0.2f, 0.035f, steel);
        break;
    }
    if (WD.show_handles) {   // the handle, and which way its face goes
        const VruiGrip *g = &WD.spec[i].grip[0];
        vrui_line(sfxr_pose_apply(p, g->a), sfxr_pose_apply(p, g->b), (Color){ 90, 230, 120, 255 });
        Vector3 mid = Vector3Lerp(g->a, g->b, 0.5f);
        vrui_line(sfxr_pose_apply(p, mid), sfxr_pose_apply(p, Vector3Add(mid, Vector3Scale(g->face, 0.08f))), (Color){ 250, 200, 80, 255 });
    }
}

// --- the sandbag --------------------------------------------------------------------------

// The part of a weapon that hits: from the top of its handle to its tip.
static void striking_part(int i, Vector3 *a, Vector3 *b)
{
    static const float from[W_COUNT] = { 0.28f, 0.8f, 1.6f, 0.13f }, to[W_COUNT] = { 1.12f, 0.95f, 1.92f, 0.33f };
    *a = sfxr_pose_apply(WD.pose[i], (Vector3){ 0, from[i], 0 });
    *b = sfxr_pose_apply(WD.pose[i], (Vector3){ 0, to[i], 0 });
}

static void sandbag(void)
{
    float dt = sfxr_dt();
    // swing: a pendulum, slowly damped
    for (int k = 0; k < 2; k++) {
        float *a = k ? &WD.bag.y : &WD.bag.x, *v = k ? &WD.bag_v.y : &WD.bag_v.x;
        *v += -9.8f / BAG_ROPE * sinf(*a) * dt;
        *v *= expf(-0.4f * dt);
        *a += *v * dt;
    }
    Vector3 c = bag_center();
    for (int i = 0; i < W_COUNT; i++) {
        WD.hit_cool[i] -= dt;
        if (WD.w[i].hands == 0 && !WD.w[i].loose) continue;
        Vector3 a, b;
        striking_part(i, &a, &b);
        Vector3 ab = Vector3Subtract(b, a);
        float t = Clamp(Vector3DotProduct(Vector3Subtract(c, a), ab) / fmaxf(Vector3DotProduct(ab, ab), 1e-6f), 0, 1);
        Vector3 at = Vector3Add(a, Vector3Scale(ab, t));
        if (Vector3Distance(at, c) > BAG_R || WD.hit_cool[i] > 0) continue;
        // the speed of the part that hit: the weapon's motion plus its spin
        Vector3 v = Vector3Add(WD.w[i].velocity, Vector3CrossProduct(WD.w[i].angular_velocity, Vector3Subtract(at, WD.pose[i].position)));
        float speed = Vector3Length(v);
        if (speed < 1.0f) continue;
        WD.hit_cool[i] = 0.25f;
        WD.hits++;
        // push the bag along the blow (heavier weapons push harder)
        float push = Clamp(speed * sqrtf(WD.spec[i].mass + 0.2f) * 0.25f, 0, 4);
        Vector3 dir = Vector3Normalize(v);
        WD.bag_v.x += dir.x * push / BAG_ROPE;
        WD.bag_v.y += dir.z * push / BAG_ROPE;
        sound_play(SND_THUMP, at, Clamp(speed / 6.0f, 0.2f, 1.0f));
        if (WD.w[i].hands > 0) vrui_haptic_pulse(WD.w[i].hand, Clamp(speed / 5.0f, 0.3f, 1.0f), 0.05f, 0);
        sfxr_event("hit", "sandbag by %s at %.1f m/s", NAMES[i], speed);
    }
    // draw: the rope, the bag, a post and arm to hang it from
    vrui_line(BAG_TOP, c, (Color){ 200, 190, 160, 255 });
    vrui_box((SfxrPose){ c, QuaternionIdentity() }, (Vector3){ 2 * BAG_R, 2.4f * BAG_R, 2 * BAG_R }, (Color){ 150, 120, 80, 255 });
    vrui_box((SfxrPose){ { BAG_TOP.x + 0.6f, 1.15f, BAG_TOP.z }, QuaternionIdentity() }, (Vector3){ 0.08f, 2.3f, 0.08f }, (Color){ 90, 70, 55, 255 });
    vrui_box((SfxrPose){ { BAG_TOP.x + 0.3f, 2.33f, BAG_TOP.z }, QuaternionIdentity() }, (Vector3){ 0.68f, 0.06f, 0.06f }, (Color){ 90, 70, 55, 255 });
    vrui_text3d(Vector3Add(c, (Vector3){ 0, 0.35f, 0 }), TextFormat("sandbag: %d hits", WD.hits), 0.0308f, RAYWHITE);
}

static void panel(void)
{
    if (!vrui_panel_begin(VRUI_ID2(G_WIELD, 0), &WD.panel, 0.546f, 0.47f, "Wielding")) return;
    vrui_layout_begin(vrui_panel_content(), 4);
    vrui_toggle(1, vrui_row(28), "Sticky grip (loosen, don't drop)", &WD.sticky);
    vrui_toggle(2, vrui_row(28), "Slide along the handle", &WD.slide);
    vrui_toggle(3, vrui_row(28), "Weight", &WD.weight);
    vrui_toggle(4, vrui_row(28), "Show handles", &WD.show_handles);
    if (vrui_button(5, vrui_row(32), "Back on the rack")) back_on_the_rack();
    vrui_space(4);
    for (int i = 0; i < W_COUNT; i++) {
        const VruiWield *w = &WD.w[i];
        const char *state = w->pulling ? "flying to you" : w->hands == 2 ? "two hands" : w->hands == 1 ? "in hand"
                          : w->loose ? "falling" : "resting";
        vrui_label(vrui_row(20), TextFormat("%s: %s%s%s", NAMES[i], state, w->sliding[0] || w->sliding[1] ? ", sliding" : "",
                                            w->lag > 0.03f ? TextFormat(", %.0f cm behind", w->lag * 100) : ""));
    }
    vrui_panel_end();
}

void station_wield(void)
{
    if (!WD.init) init();
    station_sign(X0, "Wielding", "take a weapon by its handle: it settles into your hand. Loosen your grip to slide, add a hand to steer");
    // the rack: two posts and two rails behind the weapons, a shelf for the dagger
    Color frame = { 90, 70, 55, 255 };
    for (int s = -1; s <= 1; s += 2)
        vrui_box((SfxrPose){ { X0 + 0.75f * (float)s, 0.6f, ROW_Z - 0.08f }, QuaternionIdentity() }, (Vector3){ 0.06f, 1.2f, 0.06f }, frame);
    vrui_box((SfxrPose){ { X0, 1.15f, ROW_Z - 0.08f }, QuaternionIdentity() }, (Vector3){ 1.56f, 0.05f, 0.05f }, frame);
    vrui_box((SfxrPose){ { X0, 0.3f, ROW_Z - 0.08f }, QuaternionIdentity() }, (Vector3){ 1.56f, 0.05f, 0.05f }, frame);
    vrui_box((SfxrPose){ { X0 + 0.55f, TABLE_Y - 0.01f, ROW_Z + 0.05f }, QuaternionIdentity() }, (Vector3){ 0.45f, 0.02f, 0.35f }, frame);

    for (int i = 0; i < W_COUNT; i++) {
        VruiWieldSpec s = WD.spec[i];
        s.sticky = s.sticky && WD.sticky;
        s.slide = s.slide && WD.slide;
        if (!WD.weight) s.mass = 0;
        s.ground = ground;
        WD.w[i] = vrui_wield(VRUI_ID2(G_WIELD, 1 + i), &WD.pose[i], &s);
        vrui_name_widget(VRUI_ID2(G_WIELD, 1 + i), NAMES[i]);
        draw(i, WD.pose[i], WD.w[i].hovered || WD.w[i].hands > 0);
        sfxr_report(TextFormat("%s_hands", NAMES[i]), (float)WD.w[i].hands);
    }
    sandbag();
    sfxr_report("sandbag_hits", (float)WD.hits);
    panel();
}
