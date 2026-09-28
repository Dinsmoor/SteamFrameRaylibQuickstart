// vrui_grab.c - taking hold of things: the grab machinery every held control
// shares (vrui__handle_update), and free grabbable objects.
//
// Everything can be taken hold of two ways:
//   near: reach it with the hand (grip, or the grab style in effect)
//   far:  point the laser at it (trigger or grip)

#include "vrui_internal.h"

// ---------------------------------------------------------------------------
// Shared grab machinery
// ---------------------------------------------------------------------------

VruiHandle vrui__handle_update(VruiId id, VruiItem *it, const float ray_dist[2], const float prox[2])
{
    VruiHandle hd = { -1, 0, false, false, false, false };
    int holding = it->hold_hand;

    // Continue / end an existing hold. Only letting go of the button ends it:
    // how far the hand has drifted from the collider never matters.
    if (holding >= 0) {
        int mode = it->hold_mode;
        bool still = sfxr_hand((SfxrHandId)holding)->active &&
                     (mode == VRUI_MODE_RAY_TRIGGER ? vrui__trigger(holding)->down
                      : mode == VRUI_MODE_HAND ? vrui__grab_down(holding) : vrui__squeeze(holding)->down);
        if (SFXR_BREAK(vrui_hold_breaks_on_drift) && mode == VRUI_MODE_HAND && prox[holding] < 0) still = false;
        if (still) {
            if (mode == VRUI_MODE_HAND) vrui__grab_offer(holding, id, 0);
            else vrui__ray_offer(holding, id, ray_dist[holding] >= 0 ? ray_dist[holding] : 0.5f);
            hd.hand = holding;
            hd.mode = mode;
            hd.held = true;
            return hd;
        }
        if (C.grab_active[holding] == id) C.grab_active[holding] = VRUI_ID_NONE;
        if (C.ray_active[holding] == id) C.ray_active[holding] = VRUI_ID_NONE;
        it->hold_hand = -1;
        sfxr_event("release", "%s %s", vrui__who(id), holding ? "R" : "L");
        hd.hand = holding;
        hd.mode = mode;
        hd.released = true;
        return hd;
    }

    for (int h = 0; h < 2; h++) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        if (!hand->active) continue;
        if (prox[h] >= 0) vrui__grab_offer(h, id, prox[h]);
        if (ray_dist[h] >= 0) vrui__ray_offer(h, id, ray_dist[h]);

        if (vrui__grab_hot(h, id)) {
            hd.hovered = true;
            if (vrui__grab_pressed(h) && vrui__hand_free(h)) {
                it->hold_hand = h; it->hold_mode = VRUI_MODE_HAND;
                C.grab_active[h] = id;
                sfxr_event("grab", "%s %s hand", vrui__who(id), h ? "R" : "L");
                hd.hand = h; hd.mode = VRUI_MODE_HAND; hd.grabbed = hd.held = true;
                vrui__click_pulse(h);
                return hd;
            }
        } else if (vrui__ray_hot(h, id)) {
            hd.hovered = true;
            int mode = vrui__trigger(h)->pressed ? VRUI_MODE_RAY_TRIGGER
                     : vrui__squeeze(h)->pressed ? VRUI_MODE_RAY_GRIP : -1;
            if (mode >= 0 && vrui__hand_free(h)) {
                it->hold_hand = h; it->hold_mode = mode;
                C.ray_active[h] = id;
                sfxr_event("grab", "%s %s laser+%s", vrui__who(id), h ? "R" : "L",
                           mode == VRUI_MODE_RAY_TRIGGER ? "trigger" : "grip");
                hd.hand = h; hd.mode = mode; hd.grabbed = hd.held = true;
                vrui__click_pulse(h);
                return hd;
            }
        }
    }
    return hd;
}

void vrui__ray_box_all(SfxrPose pose, Vector3 half, float out[2])
{
    for (int h = 0; h < 2; h++)
        out[h] = sfxr_hand((SfxrHandId)h)->active ? vrui__ray_box(vrui__hand_ray(h), pose, half) : -1.0f;
}

void vrui__prox_box_all(SfxrPose pose, Vector3 half, float out[2])
{
    for (int h = 0; h < 2; h++)
        out[h] = vrui__point_box_dist(sfxr_hand((SfxrHandId)h)->grip.position, pose, half);
}

Color vrui__hover_tint(Color c, bool hovered, bool held)
{
    if (held) return ColorBrightness(c, 0.35f);
    if (hovered) return ColorTint(ColorBrightness(c, 0.2f), C.style.prop_hover);
    return c;
}

void vrui__label(SfxrPose base, Vector3 local, const char *text)
{
    if (text && *text) vrui_text3d(sfxr_pose_apply(base, local), text, 0.02f, C.style.text);
}

bool vrui__drag_point(int hand, int mode, Vector3 origin, Vector3 normal, Vector3 *out)
{
    const SfxrHand *h = sfxr_hand((SfxrHandId)hand);
    if (mode == VRUI_MODE_HAND) { *out = h->grip.position; return true; }
    Ray r = vrui__hand_ray(hand);
    float denom = Vector3DotProduct(r.direction, normal);
    if (fabsf(denom) < 1e-4f) return false;
    float t = Vector3DotProduct(Vector3Subtract(origin, r.position), normal) / denom;
    if (t < 0) return false;
    *out = Vector3Add(r.position, Vector3Scale(r.direction, t));
    return true;
}

// ---------------------------------------------------------------------------
// Grabbable
// ---------------------------------------------------------------------------

VruiGrab vrui_grab_region(VruiId id, SfxrPose *pose, Vector3 half)
{
    VruiItem *it = vrui__item(id);
    float ray[2], prox[2];
    vrui__ray_box_all(*pose, half, ray);
    vrui__prox_box_all(*pose, half, prox);
    for (int h = 0; h < 2; h++) if (prox[h] > 0.06f) prox[h] = -1;   // near-grab reach

    VruiHandle hd = vrui__handle_update(id, it, ray, prox);
    vrui__hint(id, TextFormat("%s%s | or laser + grip", vrui__grab_words(), vrui__pull_suffix()));
    VruiGrab g = {0};
    g.hovered = hd.hovered;
    g.grabbed = hd.grabbed;
    g.held = hd.held;
    g.released = hd.released;
    g.hand = hd.hand >= 0 ? (SfxrHandId)hd.hand : SFXR_LEFT;

    if (hd.hand >= 0) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)hd.hand);
        // hand mode follows the grip; ray mode keeps the object out at the
        // laser's end (a "force grab") and follows the aim pose.
        SfxrPose anchor = hd.mode == VRUI_MODE_HAND ? hand->grip : hand->aim;
        if (hd.grabbed) it->rel = sfxr_pose_mul(sfxr_pose_inverse(anchor), *pose);
        if (hd.held) *pose = sfxr_pose_mul(anchor, it->rel);
        if (hd.released) {
            g.release_velocity = hand->velocity;
            g.release_angular_velocity = hand->angular_velocity;
            // Add the tangential velocity from wrist rotation for better throws.
            Vector3 lever = Vector3Subtract(pose->position, hand->grip.position);
            g.release_velocity = Vector3Add(g.release_velocity, Vector3CrossProduct(hand->angular_velocity, lever));
        }
    }
    return g;
}

VruiGrab vrui_grabbable(VruiId id, SfxrPose *pose, Vector3 half, Color color)
{
    VruiGrab g = vrui_grab_region(id, pose, half);
    Vector3 size = Vector3Scale(half, 2);
    vrui_box(*pose, size, vrui__hover_tint(color, g.hovered, g.held));
    if (g.hovered || g.held) {
        VruiCmd *c = vrui__cmd(CMD_BOX_WIRES, C.style.prop_hover);
        c->pose = *pose;
        c->size = Vector3Scale(size, 1.02f);
    }
    return g;
}
