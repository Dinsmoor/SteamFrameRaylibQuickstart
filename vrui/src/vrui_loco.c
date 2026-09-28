// vrui_loco.c - comfortable locomotion defaults.
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

#include "vrui_internal.h"

static struct {
    bool aiming[2];
    bool turn_armed[2];
    float fade_left;
} L = { { false, false }, { true, true }, 0 };

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
    return c;
}

// Ballistic arc from the aim pose. Returns true if it lands on the floor
// within range; fills the polyline.
static bool teleport_arc(int h, const VruiLocoConfig *cfg, Vector3 *pts, int *npts, Vector3 *target)
{
    const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
    Vector3 p = hand->aim.position;
    Vector3 v = Vector3Scale(sfxr_pose_forward(hand->aim), 7.0f);
    const float g = 9.8f, dt = 0.025f;
    int n = 0;
    pts[n++] = p;
    for (int i = 0; i < 120 && n < 128; i++) {
        Vector3 np = Vector3Add(p, Vector3Scale(v, dt));
        v.y -= g * dt;
        if (np.y <= cfg->floor_y) {
            float t = (p.y - cfg->floor_y) / fmaxf(p.y - np.y, 1e-5f);
            Vector3 hit = Vector3Lerp(p, np, t);
            hit.y = cfg->floor_y;
            pts[n++] = hit;
            *npts = n;
            *target = hit;
            Vector3 from = sfxr_head_floor_point();
            float dx = hit.x - from.x, dz = hit.z - from.z;
            if (sqrtf(dx * dx + dz * dz) > cfg->teleport_max_dist) return false;
            if (cfg->valid_target && !cfg->valid_target(hit, cfg->user)) return false;
            return true;
        }
        pts[n++] = np;
        p = np;
    }
    *npts = n;
    return false;
}

void vrui_locomotion(const VruiLocoConfig *cfg)
{
    float dt = sfxr_dt();
    for (int h = 0; h < 2; h++) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        // Only a hand nobody else owns moves you (vrui_input_claimed).
        if (!hand->active || vrui_hand_busy((SfxrHandId)h) || C.ray_hot[h] || vrui_input_claimed((SfxrHandId)h)) {
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
                sfxr_rig_move(Vector3Scale(d, cfg->move_speed * dt));
            }
            continue;
        }

        // --- teleport
        if (cfg->teleport) {
            if (!L.aiming[h] && s.y > 0.6f && fabsf(s.x) < 0.6f) L.aiming[h] = true;
            if (L.aiming[h]) {
                Vector3 pts[128], target = {0};
                int n = 0;
                bool ok = teleport_arc(h, cfg, pts, &n, &target);
                Color col = ok ? C.style.accent : (Color){ 230, 70, 60, 255 };
                for (int i = 0; i + 1 < n; i++) vrui_line(pts[i], pts[i + 1], col);
                if (ok) {
                    SfxrPose ring = { Vector3Add(target, (Vector3){ 0, 0.01f, 0 }), { 0, 0, 0, 1 } };
                    vrui__ring(ring, 0.25f, col);
                    vrui__ring(ring, 0.18f, col);
                }
                if (Vector2Length(s) < 0.3f) {   // stick released: go
                    L.aiming[h] = false;
                    if (ok) {
                        sfxr_rig_teleport(target);
                        L.fade_left = cfg->fade_seconds;
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
                L.turn_armed[h] = false;
                L.fade_left = cfg->fade_seconds;
            } else if (fabsf(s.x) < 0.35f) {
                L.turn_armed[h] = true;
            }
        }
    }

    if (L.fade_left > 0 && cfg->fade_seconds > 0) {
        vrui_fade(L.fade_left / cfg->fade_seconds);
        L.fade_left -= dt;
    }
}
