// station_menus.c - hand menus, and the Menus & HUD station that switches
// them (docs/ATTACHING.md, "Hand menus"). Four ways to carry a menu, each a
// template to copy; they can all be on at once because each has its own cue:
//
//   Watch    on the back of your less-used wrist: turn it toward your face
//            to read it, like checking a watch. Read-only: status, not choices.
//   Palm     turn that hand's palm toward your face: three big buttons float
//            above it; poke one with your other hand's finger. Fast for the
//            two or three things you do most.
//   Tablet   press that hand's menu button (left: View, right: Menu): a panel
//            appears held in the hand like a clipboard; use it with the other
//            hand's laser. Room for everything; put it away with the button.
//   Radial   hold the primary button on your main hand (right: A, left:
//            D-pad down), tilt the stick toward a choice, let go. One hand, no
//            aiming; the stick is yours again when you let go. (It was B:
//            but B's click never reached the app on the Frame, only its
//            touch -- docs/INPUT.md, "Buttons that don't arrive".)
//
// "Less-used" and "main" hand come from the Hands-on setup (left-handed
// players get everything mirrored); right-handed until then.

#include "toolbox.h"
#include "onboarding.h"

static struct {
    bool watch, watch_always, palm, tablet, radial, arrows;
    int  hud;                 // HudStyle
    bool watch_shown, palm_shown, tablet_shown;
    float palm_timer;         // palm cue held this long (debounce)
    float flash;              // demo damage flash, seconds left
    SfxrPose station;
    bool placed;
} M = { .watch = true, .palm = true, .tablet = true, .radial = true, .arrows = true, .hud = HUD_FOLLOW };

HudStyle menus_hud_style(void) { return (HudStyle)M.hud; }
void     menus_set_hud_style(HudStyle s) { M.hud = (int)s % HUD_COUNT; }
bool     menus_edge_arrows(void) { return M.arrows; }

static SfxrHandId main_hand(void)
{
    return onboarding_prefs()->valid && onboarding_prefs()->dominant == SFXR_LEFT ? SFXR_LEFT : SFXR_RIGHT;
}
static SfxrHandId other(SfxrHandId h) { return h == SFXR_LEFT ? SFXR_RIGHT : SFXR_LEFT; }

// Is a surface at `p`, facing `normal`, turned toward the eyes? Enters at
// `enter_deg`, leaves past `exit_deg`, so it doesn't flicker at the edge.
static bool facing_eyes(Vector3 p, Vector3 normal, bool was, float enter_deg, float exit_deg)
{
    Vector3 to = Vector3Normalize(Vector3Subtract(sfxr_head().position, p));
    float c = Vector3DotProduct(normal, to);
    return c > cosf((was ? exit_deg : enter_deg) * DEG2RAD);
}

// --- watch: a read-only panel on the back of the wrist
static void watch(SfxrHandId h, const char *text)
{
    const SfxrHand *hand = sfxr_hand(h);
    if (!hand->active) { M.watch_shown = false; return; }
    // 7 cm above the back of the hand, toward the wrist, face tilted toward you
    SfxrPose p = sfxr_pose_mul(hand->grip, (SfxrPose){ { 0, 0.07f, 0.06f }, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -60.0f * DEG2RAD) });
    Vector3 face = Vector3RotateByQuaternion((Vector3){ 0, 0, 1 }, p.orientation);
    M.watch_shown = M.watch_always || facing_eyes(p.position, face, M.watch_shown, 35, 55);
    if (!M.watch_shown) return;
    vrui_panel_passive();
    if (!vrui_panel_begin(VRUI_ID2(G_MENUS, 1), &p, 0.18f, 0.12f, NULL)) return;
    vrui_layout_begin(vrui_panel_content(), 2);
    vrui_label(vrui_row(22), TextFormat("%.0f fps", sfxr_dt() > 0 ? 1.0f / sfxr_dt() : 0.0f));   // sfxr_dt: replays stay deterministic
    vrui_label(vrui_row(22), TextFormat("L %s  R %s", sfxr_hand_shape_name(sfxr_hand(SFXR_LEFT)->shape),
                                        sfxr_hand_shape_name(sfxr_hand(SFXR_RIGHT)->shape)));
    const SfxrHand *r = sfxr_hand(main_hand());
    const char *lvl = r->trigger_at[SFXR_PULL_FULL].down ? "full" : r->trigger_at[SFXR_PULL_FIRM].down ? "firm"
                    : r->trigger_at[SFXR_PULL_SOFT].down ? "soft" : "-";
    vrui_label(vrui_row(22), TextFormat("trigger %.2f %s", r->trigger, lvl));
    if (text) vrui_label(vrui_row(22), text);
    vrui_panel_end();
}

// --- palm: three buttons above the open palm, poked by the other hand
static int palm(SfxrHandId h, const char *const *items, int count)
{
    const SfxrHand *hand = sfxr_hand(h);
    Vector3 normal = Vector3Negate(sfxr_pose_up(hand->palm));   // -Y comes out of the palm
    bool cue = hand->active && !vrui_hand_busy(h) && !hand->squeeze_btn.down &&
               facing_eyes(hand->palm.position, normal, M.palm_shown, 40, 60);
    // the cue must hold a moment, so a palm flashing past your face doesn't open it
    M.palm_timer = cue ? M.palm_timer + sfxr_dt() : 0;
    M.palm_shown = cue && (M.palm_shown || M.palm_timer > 0.25f);
    if (!M.palm_shown) return -1;

    int picked = -1, n = count < 3 ? count : 3;
    Vector3 head = sfxr_head().position;
    Vector3 center = Vector3Add(hand->palm.position, Vector3Scale(normal, 0.1f));
    SfxrPose face = vrui_facing(center, head);
    Vector3 right = sfxr_pose_right(face);
    // each button's +Y (the way it's pressed from) points at your eyes
    Quaternion toward = QuaternionFromVector3ToVector3((Vector3){ 0, 1, 0 }, Vector3Normalize(Vector3Subtract(head, center)));
    for (int i = 0; i < n; i++) {
        SfxrPose b = { Vector3Add(center, Vector3Scale(right, 0.065f * (float)(i - (n - 1) * 0.5f))), toward };
        if (vrui_push_button(VRUI_ID2(G_MENUS, 20 + i), b, 0.022f, (Color){ 80, 140, 220, 255 }, items[i])) picked = i;
    }
    return picked;
}

// --- tablet: a panel held in the hand like a clipboard
static int tablet(SfxrHandId h, const char *const *items, int count)
{
    const SfxrHand *hand = sfxr_hand(h);
    const SfxrButton *btn = h == SFXR_LEFT ? &hand->view : &hand->menu;
    if (hand->active && btn->pressed && !vrui_input_claimed(h)) M.tablet_shown = !M.tablet_shown;
    if (!hand->active || !M.tablet_shown) return -1;
    // 12 cm above the grip and a little forward, turned to face you and tipped
    // back 20 degrees, the way you'd hold a clipboard to read it
    Vector3 at = Vector3Add(sfxr_pose_apply(hand->grip, (Vector3){ 0, 0.05f, -0.06f }), (Vector3){ 0, 0.12f, 0 });
    SfxrPose p = vrui_facing(at, sfxr_head().position);
    p.orientation = QuaternionMultiply(p.orientation, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -20.0f * DEG2RAD));
    int picked = -1;
    if (!vrui_panel_begin(VRUI_ID2(G_MENUS, 3), &p, 0.3f, 0.26f, NULL)) return -1;
    vrui_layout_begin(vrui_panel_content(), 6);
    Rectangle cols[2];
    for (int i = 0; i < count; i += 2) {
        vrui_row_cols(40, 2, cols);
        for (int k = 0; k < 2 && i + k < count; k++)
            if (vrui_button(10 + i + k, cols[k], items[i + k])) picked = i + k;
    }
    if (vrui_button(99, vrui_row(34), "Put away")) M.tablet_shown = false;
    vrui_panel_end();
    return picked;
}

int menus_update(const char *const *items, int count, const char *watch_text)
{
    SfxrHandId mh = main_hand(), oh = other(mh);
    int picked = -1, p;
    if (M.watch) watch(oh, watch_text);
    if (M.palm && (p = palm(oh, items, count)) >= 0) picked = p;
    if (M.tablet && (p = tablet(oh, items, count)) >= 0) picked = p;
    // (a station's own ring menu on the same button -- the Voice commands
    // bugs' orders -- opens first and claims the hand: then this one waits)
    if (M.radial && (!vrui_input_claimed(mh) || vrui_radial_open(VRUI_ID2(G_MENUS, 4)))) {
        const SfxrHand *hand = sfxr_hand(mh);
        if ((p = vrui_radial_menu(VRUI_ID2(G_MENUS, 4), mh, &hand->primary, items, count)) >= 0) picked = p;
    }

    if (M.flash > 0) {   // the station's demo flash: strong at first, fading out
        vrui_tint((Color){ 200, 20, 20, 255 }, 0.35f * M.flash / 0.4f);
        M.flash -= sfxr_dt();
    }
    return picked;
}

void station_menus(void)
{
    station_sign(-8.0f, "Menus & HUD", "menus you carry on your hands,\nreadouts that go with you");
    if (!M.placed) { M.station = row_pose(-8.0f, 1.35f); M.placed = true; }
    if (!vrui_panel_begin(VRUI_ID2(G_MENUS, 0), &M.station, 0.5f, 0.56f, "Menus & HUD")) return;
    vrui_layout_begin(vrui_panel_content(), 6);
    Rectangle cols[2];
    const char *main_name = main_hand() == SFXR_RIGHT ? "right" : "left";
    const char *other_name = main_hand() == SFXR_RIGHT ? "left" : "right";

    vrui_label(vrui_row(22), "Hand menus (all can be on at once)");
    vrui_row_cols(34, 2, cols);
    vrui_toggle(1, cols[0], "Watch", &M.watch);
    vrui_toggle(2, cols[1], "Always show it", &M.watch_always);
    vrui_label(vrui_row(20), TextFormat("  turn your %s wrist toward your face", other_name));
    vrui_toggle(3, vrui_row(34), "Palm buttons", &M.palm);
    vrui_label(vrui_row(20), TextFormat("  %s palm to your face, poke with the other", other_name));
    vrui_toggle(4, vrui_row(34), "Tablet", &M.tablet);
    vrui_label(vrui_row(20), TextFormat("  %s hand's %s button", other_name, main_hand() == SFXR_RIGHT ? "View" : "Menu"));
    vrui_toggle(5, vrui_row(34), "Radial", &M.radial);
    vrui_label(vrui_row(20), TextFormat("  hold %s, tilt the %s stick, let go", main_hand() == SFXR_RIGHT ? "A" : "D-pad down", main_name));

    vrui_space(6);
    vrui_label(vrui_row(22), "Visor HUD");
    vrui_segmented(6, vrui_row(34), HUD_STYLE_NAMES, HUD_COUNT, &M.hud);
    vrui_row_cols(34, 2, cols);
    vrui_toggle(7, cols[0], "Edge arrows", &M.arrows);
    if (vrui_button(8, cols[1], "Flash (tint)")) M.flash = 0.4f;
    vrui_panel_end();
}
