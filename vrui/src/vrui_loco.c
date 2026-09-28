// vrui_loco.c - moving the player: the stick, surfaces, pads and climbing.
//
//   Either stick forward  -> aim a teleport arc; let go of the stick to jump.
//   Either stick sideways -> snap turn (or smooth turn if configured).
//   smooth_move           -> left stick walks (head-relative), right keeps turning.
// A hand's stick is left alone while its laser points at UI, or while
// anything else has claimed that hand (vrui_input_claimed): a panel you're
// looking at, a tool that uses the stick, a menu.
//
// Comfort: teleport + snap turn with a short blink is the least sickening
// combination; smooth movement/turning should stay opt-in for players.
//
// Climbing moves the world, not the hand: a held handhold pins that hand's
// grab point in the world, and every frame the rig moves by however far the
// tracked hand has drifted from it. Your own arm motion is the only thing
// that moves you, which is why climbing is comfortable when artificial
// movement is not. With two hands on, the most recent grab leads; when it
// lets go the other hand takes over from wherever it is now.
//
// Surfaces: ground_height() is asked in three places, always "what is the
// highest ground at or below this point":
//   * the teleport arc, at each step, so it lands on top of platforms;
//   * standing, at step_height above your feet, so you step up stairs but a
//     table you walk into (taller than a step) isn't climbed;
//   * after letting go of a handhold, at your HEAD, so pulling yourself up
//     until your head is over a ledge and letting go puts you on the ledge.
//
// Walls: solid_depth() says how deep a point is inside something solid. A
// head inside a wall fades the view (the player's body can't be stopped, so
// the eyes are covered instead); stick walking slides along walls; teleport
// targets inside walls are refused.

#include "vrui_internal.h"

static struct {
    bool aiming[2];
    bool turn_armed[2];
    float fade_left;
    int  lead;          // the hand the rig follows while climbing, -1 none
    bool airborne;      // let go of a handhold, or walked off an edge: coming down
    float vy;           // VRUI_FALL_DROP vertical speed
} L = { { false, false }, { true, true }, 0, -1, false, 0 };

VruiLocoConfig vrui_loco_default(void)
{
    VruiLocoConfig c = {0};
    c.teleport = true;
    c.teleport_max_dist = 8.0f;
    c.snap_turn = true;
    c.snap_angle_deg = 30.0f;
    c.smooth_turn = false;
    c.smooth_turn_deg_s = 90.0f;
    c.smooth_move = false;
    c.move_speed = 2.0f;
    c.fade_seconds = 0.08f;
    c.floor_y = 0.0f;
    c.step_height = 0.35f;
    c.fall = VRUI_FALL_BLINK;
    c.gravity = 9.8f;
    return c;
}

bool vrui_climbing(void) { return L.lead >= 0; }
bool vrui_airborne(void) { return L.airborne; }

static float ground_at(const VruiLocoConfig *cfg, Vector3 p)
{
    return cfg->ground_height ? cfg->ground_height(p, cfg->user) : cfg->floor_y;
}

static void blink(const VruiLocoConfig *cfg) { L.fade_left = cfg->fade_seconds; }

static float solid_at(const VruiLocoConfig *cfg, Vector3 p)
{
    return cfg->solid_depth ? cfg->solid_depth(p, cfg->user) : 0.0f;
}

// Stick walking: move by d unless it takes the head deeper into a wall; then
// try each horizontal part alone, so you slide along the wall.
static void walk(const VruiLocoConfig *cfg, Vector3 d)
{
    if (!cfg->solid_depth || SFXR_BREAK(vrui_loco_walk_through_walls)) { sfxr_rig_move(d); return; }
    Vector3 head = sfxr_head().position;
    float now = solid_at(cfg, head);
    const Vector3 tries[3] = { d, { d.x, 0, 0 }, { 0, 0, d.z } };
    for (int i = 0; i < 3; i++) {
        if (solid_at(cfg, Vector3Add(head, tries[i])) <= now + 1e-4f) { sfxr_rig_move(tries[i]); return; }
    }
}

// ---------------------------------------------------------------------------
// Handholds
// ---------------------------------------------------------------------------

static Vector3 closest_on_segment(Vector3 a, Vector3 b, Vector3 p)
{
    Vector3 ab = Vector3Subtract(b, a);
    float len2 = Vector3DotProduct(ab, ab);
    if (len2 < 1e-8f) return a;
    float t = Clamp(Vector3DotProduct(Vector3Subtract(p, a), ab) / len2, 0.0f, 1.0f);
    return Vector3Add(a, Vector3Scale(ab, t));
}

VruiHold vrui_handhold(VruiId id, Vector3 a, Vector3 b, float radius, Color color)
{
    if (SFXR_BREAK(vrui_handhold_point_only)) a = b = Vector3Lerp(a, b, 0.5f);
    VruiItem *it = vrui__item(id);
    float ray[2] = { -1, -1 }, prox[2];
    for (int h = 0; h < 2; h++) {
        Vector3 g = sfxr_hand((SfxrHandId)h)->grip.position;
        float d = Vector3Distance(g, closest_on_segment(a, b, g)) - radius;
        prox[h] = d < 0.06f ? fmaxf(d, 0.0f) : -1.0f;   // same reach as grabbables
    }
    VruiHandle hd = vrui__handle_update(id, it, ray, prox);
    vrui__hint(id, TextFormat("%s to climb%s", vrui__grab_words(), vrui__pull_suffix()));

    VruiHold r = {0};
    r.hovered = hd.hovered;
    r.grabbed = hd.grabbed;
    r.held = hd.held;
    r.released = hd.released;
    r.hand = hd.hand >= 0 ? (SfxrHandId)hd.hand : SFXR_LEFT;
    if (hd.held) {
        int h = hd.hand;
        if (hd.grabbed) {
            C.climb.since[h] = C.frame;
            C.climb.anchor[h] = sfxr_hand((SfxrHandId)h)->grip.position;
        }
        C.climb.held_frame[h] = C.frame;
    }

    Color c = vrui__hover_tint(color, hd.hovered, hd.held);
    if (Vector3Distance(a, b) < 1e-4f) vrui__sphere(a, radius, c);
    else vrui__cylinder(a, b, radius, c);
    return r;
}

// The rig follows the leading hand. Returns true while climbing.
static bool climb_step(const VruiLocoConfig *cfg)
{
    int lead = -1;
    for (int h = 0; h < 2; h++)
        if (C.climb.held_frame[h] == C.frame && (lead < 0 || C.climb.since[h] > C.climb.since[lead])) lead = h;

    if (lead < 0) {
        if (L.lead >= 0) {   // let go: come down (or up onto a ledge)
            L.airborne = true;
            L.vy = 0;
            sfxr_event("climb", "let go at height %.2f", sfxr_rig_position().y);
        }
        L.lead = -1;
        return false;
    }
    // The other hand takes over from where it is NOW, not where it first
    // grabbed (the rig has moved since), so switching hands never yanks you.
    if (lead != L.lead && L.lead >= 0 && C.climb.since[lead] != C.frame && !SFXR_BREAK(vrui_climb_no_reanchor))
        C.climb.anchor[lead] = sfxr_hand((SfxrHandId)lead)->grip.position;
    if (lead != L.lead) sfxr_event("climb", "%s leads", lead ? "R" : "L");
    L.lead = lead;
    L.airborne = false;
    L.vy = 0;

    if (SFXR_BREAK(vrui_climb_fixed_world)) return true;
    Vector3 before = sfxr_rig_position();
    Vector3 pull = Vector3Subtract(C.climb.anchor[lead], sfxr_hand((SfxrHandId)lead)->grip.position);
    sfxr_rig_move(pull);
    // Pushing yourself down onto a surface stops there (no sinking through
    // the floor by pushing on a low hold).
    Vector3 rig = sfxr_rig_position();
    float g = ground_at(cfg, sfxr_head().position);
    if (before.y >= g - 0.001f && rig.y < g) sfxr_rig_set((Vector3){ rig.x, g, rig.z }, sfxr_rig_yaw());
    return true;
}

// ---------------------------------------------------------------------------
// Standing on surfaces, falling
// ---------------------------------------------------------------------------

static void ground_step(const VruiLocoConfig *cfg, float dt)
{
    // With a flat floor and nothing to climb, leave the rig alone: apps that
    // move it themselves keep working.
    if (!cfg->ground_height && !L.airborne) return;

    Vector3 rig = sfxr_rig_position();
    Vector3 head = sfxr_head().position;
    bool from_head = (L.airborne && !SFXR_BREAK(vrui_loco_no_mantle)) || SFXR_BREAK(vrui_loco_step_any_height);
    float probe = from_head ? head.y : rig.y + cfg->step_height;
    float g = ground_at(cfg, (Vector3){ head.x, probe, head.z });

    if (g > rig.y + 0.001f) {
        // A step, or pulling yourself over a ledge: stand on it. A big rise
        // (mantling) blinks, a stair doesn't.
        if (g - rig.y > cfg->step_height) { blink(cfg); sfxr_event("mantle", "onto %.2f", g); }
        sfxr_rig_set((Vector3){ rig.x, g, rig.z }, sfxr_rig_yaw());
        L.airborne = false;
        L.vy = 0;
    } else if (g < rig.y - 0.001f) {
        if (!L.airborne && rig.y - g <= cfg->step_height) {   // a stair down, or a lift lowering: follow it
            sfxr_rig_set((Vector3){ rig.x, g, rig.z }, sfxr_rig_yaw());
            return;
        }
        L.airborne = true;
        if (SFXR_BREAK(vrui_loco_no_fall)) return;
        float y;
        if (cfg->fall == VRUI_FALL_DROP) {
            L.vy -= cfg->gravity * dt;
            y = rig.y + L.vy * dt;
        } else {
            y = g;
            blink(cfg);
        }
        if (y <= g) {
            y = g;
            L.airborne = false;
            // a landing thump, bigger for a harder landing
            float amp = cfg->fall == VRUI_FALL_DROP ? Clamp(-L.vy * 0.08f, 0.1f, 0.6f) : 0.2f;
            sfxr_event("land", "on %.2f after falling %.2f m", g, rig.y - g);
            for (int h = 0; h < 2; h++) vrui_haptic_pulse((SfxrHandId)h, amp, 0.04f, 0);
            L.vy = 0;
        }
        sfxr_rig_set((Vector3){ rig.x, y, rig.z }, sfxr_rig_yaw());
    } else {
        L.airborne = false;
        L.vy = 0;
    }
}

// ---------------------------------------------------------------------------
// Teleport
// ---------------------------------------------------------------------------

typedef struct {
    bool ok;
    Vector3 target;
    int pad;            // snapped to this pad, -1 none
} ArcResult;

// Ballistic arc from the aim pose, landing on the ground (surfaces
// included), then snapped to a pad if it's close to one.
static ArcResult teleport_arc(int h, const VruiLocoConfig *cfg, Vector3 *pts, int *npts)
{
    ArcResult r = { false, { 0 }, -1 };
    const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
    Vector3 p = hand->aim.position;
    Vector3 v = Vector3Scale(sfxr_pose_forward(hand->aim), 7.0f);
    const float g = 9.8f, dt = 0.025f;
    int n = 0;
    pts[n++] = p;
    for (int i = 0; i < 120 && n < 128; i++) {
        Vector3 np = Vector3Add(p, Vector3Scale(v, dt));
        v.y -= g * dt;
        // The ground under the previous point: the arc comes down on top of
        // a platform rather than passing through it.
        float gy = SFXR_BREAK(vrui_loco_arc_floor_only) ? cfg->floor_y : ground_at(cfg, p);
        if (np.y <= gy) {
            float t = (p.y - gy) / fmaxf(p.y - np.y, 1e-5f);
            Vector3 hit = Vector3Lerp(p, np, t);
            hit.y = gy;
            pts[n++] = hit;
            *npts = n;
            r.target = hit;

            float best = 1e9f;
            for (int k = 0; k < cfg->npads; k++) {
                const VruiTeleportPad *pad = &cfg->pads[k];
                float dx = hit.x - pad->center.x, dz = hit.z - pad->center.z, d = sqrtf(dx * dx + dz * dz);
                if (d <= pad->radius && fabsf(hit.y - pad->center.y) < 0.5f && d < best) { best = d; r.pad = k; }
            }
            if (r.pad >= 0 && !SFXR_BREAK(vrui_loco_no_pad_snap)) r.target = cfg->pads[r.pad].center;

            Vector3 from = sfxr_head_floor_point();
            float dx = r.target.x - from.x, dz = r.target.z - from.z;
            if (sqrtf(dx * dx + dz * dz) > cfg->teleport_max_dist) return r;
            if (r.pad < 0) {
                if (cfg->pads_only && !SFXR_BREAK(vrui_loco_pads_only_ignored)) return r;
                if (cfg->valid_target && !cfg->valid_target(hit, cfg->user)) return r;
                // your head would be inside a wall there
                if (solid_at(cfg, Vector3Add(hit, (Vector3){ 0, vrui_eye_height(), 0 })) > 0) return r;
            }
            r.ok = true;
            return r;
        }
        pts[n++] = np;
        p = np;
    }
    *npts = n;
    return r;
}

static void draw_pad(const VruiTeleportPad *pad, Color col)
{
    SfxrPose ring = { Vector3Add(pad->center, (Vector3){ 0, 0.015f, 0 }), { 0, 0, 0, 1 } };
    vrui__ring(ring, pad->radius, col);
    if (pad->face) {
        float a = pad->yaw_deg * DEG2RAD;
        Vector3 f = { sinf(a), 0, -cosf(a) };
        vrui_line(ring.position, Vector3Add(ring.position, Vector3Scale(f, pad->radius)), col);
    }
}

// Turn the player to look along yaw_deg (0 = -Z, positive = right).
static void face_yaw(float yaw_deg)
{
    Vector3 f = sfxr_pose_forward(sfxr_head());
    float now = atan2f(f.x, -f.z);
    float want = yaw_deg * DEG2RAD;
    // sfxr_rig_turn: positive turns the view to the LEFT
    sfxr_rig_turn(-(want - now));
}

// ---------------------------------------------------------------------------

void vrui_locomotion(const VruiLocoConfig *cfg)
{
    float dt = sfxr_dt();
    bool climbing = climb_step(cfg);
    if (!climbing) ground_step(cfg, dt);

    for (int h = 0; h < 2; h++) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        // Only a hand nobody else owns moves you (vrui_input_claimed); no
        // stick movement while climbing (turning would swing you off the wall).
        if (climbing || !hand->active || vrui_hand_busy((SfxrHandId)h) || C.ray_hot[h] ||
            vrui_input_claimed((SfxrHandId)h)) {
            L.aiming[h] = false;
            continue;
        }
        Vector2 s = hand->stick;

        // --- smooth move (left hand)
        if (cfg->smooth_move && h == SFXR_LEFT) {
            if (Vector2Length(s) > 0.15f) {
                SfxrPose head = sfxr_head();
                Vector3 f = sfxr_pose_forward(head); f.y = 0;
                Vector3 r = sfxr_pose_right(head);   r.y = 0;
                f = Vector3Normalize(f);
                r = Vector3Normalize(r);
                Vector3 d = Vector3Add(Vector3Scale(f, s.y), Vector3Scale(r, s.x));
                walk(cfg, Vector3Scale(d, cfg->move_speed * dt));
            }
            continue;
        }

        // --- teleport
        if (cfg->teleport) {
            if (!L.aiming[h] && s.y > 0.6f && fabsf(s.x) < 0.6f) L.aiming[h] = true;
            if (L.aiming[h]) {
                Vector3 pts[128];
                int n = 0;
                ArcResult a = teleport_arc(h, cfg, pts, &n);
                Color col = a.ok ? C.style.accent : (Color){ 230, 70, 60, 255 };
                for (int i = 0; i + 1 < n; i++) vrui_line(pts[i], pts[i + 1], col);
                // While aiming, show where the pads are; the one you'd land on lights up.
                for (int k = 0; k < cfg->npads; k++)
                    if (k != a.pad) draw_pad(&cfg->pads[k], C.style.text_dim);
                if (a.pad >= 0) draw_pad(&cfg->pads[a.pad], col);
                else if (a.ok) {
                    SfxrPose ring = { Vector3Add(a.target, (Vector3){ 0, 0.01f, 0 }), { 0, 0, 0, 1 } };
                    vrui__ring(ring, 0.25f, col);
                    vrui__ring(ring, 0.18f, col);
                }
                if (Vector2Length(s) < 0.3f) {   // stick released: go
                    L.aiming[h] = false;
                    if (a.ok) {
                        if (a.pad >= 0 && cfg->pads[a.pad].face && !SFXR_BREAK(vrui_loco_no_pad_snap))
                            face_yaw(cfg->pads[a.pad].yaw_deg);
                        sfxr_rig_teleport(a.target);
                        if (a.pad >= 0) sfxr_event("teleport", "pad %d (%.2f %.2f %.2f)", a.pad, a.target.x, a.target.y, a.target.z);
                        else sfxr_event("teleport", "to %.2f %.2f %.2f", a.target.x, a.target.y, a.target.z);
                        L.airborne = false;
                        L.vy = 0;
                        blink(cfg);
                        sfxr_haptic((SfxrHandId)h, 0.3f, 0.02f, 0);
                    }
                }
                continue;   // no turning while aiming
            }
        }

        // --- turning
        if (cfg->smooth_turn) {
            if (fabsf(s.x) > 0.2f) sfxr_rig_turn(-s.x * cfg->smooth_turn_deg_s * DEG2RAD * dt);
        } else if (cfg->snap_turn) {
            if (L.turn_armed[h] && fabsf(s.x) > 0.7f) {
                sfxr_rig_turn((s.x > 0 ? -1.0f : 1.0f) * cfg->snap_angle_deg * DEG2RAD);
                sfxr_event("turn", "%s %.0f", s.x > 0 ? "right" : "left", cfg->snap_angle_deg);
                L.turn_armed[h] = false;
                blink(cfg);
            } else if (fabsf(s.x) < 0.35f) {
                L.turn_armed[h] = true;
            }
        }
    }

    if (L.fade_left > 0 && cfg->fade_seconds > 0) {
        vrui_fade(L.fade_left / cfg->fade_seconds);
        L.fade_left -= dt;
    }

    // Head in a wall: fully dark by 10 cm in, so you back out rather than
    // look around inside it.
    float depth = solid_at(cfg, sfxr_head().position);
    if (depth > 0 && !SFXR_BREAK(vrui_loco_no_wall_fade)) vrui_fade(Clamp(depth / 0.1f, 0.3f, 1.0f));
}
