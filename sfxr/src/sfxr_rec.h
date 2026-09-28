// sfxr_rec.h - on-disk format of input recordings (.sfxrec).
// Shared by sfxr_replay.c (record + replay) and tools/sfxrec_dump.c.
// Layout: one RecHeader, then one RecFrame per app frame. Native struct layout
// (little-endian, same on aarch64 and x86_64); the header records the struct
// sizes so incompatible builds refuse the file instead of misreading it.
#ifndef SFXR_REC_H
#define SFXR_REC_H

#include "sfxr_internal.h"
#include <string.h>

#define REC_MAGIC   "SFXREC1"
#define REC_VERSION 3u   // 2: every Frame control's click+touch, poke/pinch/palm poses
                         // 3: headset signals (presence, refresh, batteries, hand joints)

typedef struct {
    char     magic[8];
    uint32_t version;
    uint32_t header_size, frame_size, raw_hand_size;
    int32_t  eye_w, eye_h;
    uint32_t stereo;              // 1 = headset (two eyes), 0 = simulator (mono)
    uint32_t source_backend;      // SfxrBackend of the recording
    char     runtime[128];
    char     system[128];
    char     app[64];
} RecHeader;

typedef struct {
    uint64_t    frame;
    float       dt;
    uint8_t     should_render, views_valid, gaze_valid, pad;
    int32_t     eye_w, eye_h;     // can change (simulator window resize)
    SfxrPose    head;
    SfxrPose    eye[2];
    float       fov[2][4];
    SfxrPose    gaze;
    SfxrRawHand raw[2];
    SfxrSignals sig;
} RecFrame;

// --- version 2, still readable: sfxr_rec_upgrade_v2() ---
typedef struct {
    uint64_t    frame;
    float       dt;
    uint8_t     should_render, views_valid, gaze_valid, pad;
    int32_t     eye_w, eye_h;
    SfxrPose    head;
    SfxrPose    eye[2];
    float       fov[2][4];
    SfxrPose    gaze;
    SfxrRawHand raw[2];
} RecFrameV2;

// Signals a recording didn't have: unknown presence (counts as worn), no
// batteries, no joints.
static inline void sfxr_rec_upgrade_v2(const RecFrameV2 *in, RecFrame *out)
{
    memset(out, 0, sizeof *out);
    memcpy(out, in, sizeof *in);     // v3 starts with the v2 layout
    out->sig.present = 1;
}

// --- version 1 (before 2026-09-28), still readable: sfxr_rec_upgrade_v1() ---
typedef struct {
    bool     active;
    SfxrPose grip, aim;
    Vector3  velocity, angular_velocity;
    bool     has_velocity;
    float    trigger, squeeze;
    bool     trigger_click, has_trigger_click;
    bool     squeeze_click, has_squeeze_click;
    bool     trigger_touch;
    Vector2  stick;
    bool     stick_click, stick_touch;
    bool     primary, secondary, menu;
    bool     primary_touch, secondary_touch;
    bool     face_x, face_y, bumper;
    bool     dpad_up, dpad_down, dpad_left, dpad_right;
    char     profile[128];
} SfxrRawHandV1;

typedef struct {
    uint64_t      frame;
    float         dt;
    uint8_t       should_render, views_valid, gaze_valid, pad;
    int32_t       eye_w, eye_h;
    SfxrPose      head;
    SfxrPose      eye[2];
    float         fov[2][4];
    SfxrPose      gaze;
    SfxrRawHandV1 raw[2];
} RecFrameV1;

// Convert one v1 frame. `hand` is 0 = left, 1 = right (v1 stored the Frame's
// left D-pad down/up as primary/secondary and view as menu).
static inline void sfxr_rec_upgrade_v1(const RecFrameV1 *in, RecFrame *out)
{
    memset(out, 0, sizeof *out);
    out->sig.present = 1;
    out->frame = in->frame; out->dt = in->dt;
    out->should_render = in->should_render; out->views_valid = in->views_valid; out->gaze_valid = in->gaze_valid;
    out->eye_w = in->eye_w; out->eye_h = in->eye_h;
    out->head = in->head; out->eye[0] = in->eye[0]; out->eye[1] = in->eye[1];
    memcpy(out->fov, in->fov, sizeof out->fov);
    out->gaze = in->gaze;
    for (int h = 0; h < 2; h++) {
        const SfxrRawHandV1 *a = &in->raw[h];
        SfxrRawHand *b = &out->raw[h];
        b->active = a->active;
        b->source = a->active ? SFXR_SOURCE_CONTROLLER : SFXR_SOURCE_NONE;
        b->pose_valid = RAW_POSE_GRIP | RAW_POSE_AIM;
        b->grip = a->grip; b->aim = a->aim;
        b->velocity = a->velocity; b->angular_velocity = a->angular_velocity; b->has_velocity = a->has_velocity;
        b->trigger = a->trigger; b->squeeze = a->squeeze; b->stick = a->stick;
        #define V1BIT(cond, c, field) do { if (cond) b->field |= RAW_BIT(c); } while (0)
        V1BIT(a->trigger_click, SFXR_CTL_TRIGGER, click); V1BIT(a->trigger_touch, SFXR_CTL_TRIGGER, touch);
        V1BIT(a->squeeze_click, SFXR_CTL_SQUEEZE, click);
        V1BIT(a->stick_click, SFXR_CTL_STICK, click); V1BIT(a->stick_touch, SFXR_CTL_STICK, touch);
        V1BIT(a->bumper, SFXR_CTL_BUMPER, click);
        if (h == 1) {
            V1BIT(a->primary, SFXR_CTL_A, click); V1BIT(a->primary_touch, SFXR_CTL_A, touch);
            V1BIT(a->secondary, SFXR_CTL_B, click); V1BIT(a->secondary_touch, SFXR_CTL_B, touch);
            V1BIT(a->face_x, SFXR_CTL_X, click); V1BIT(a->face_y, SFXR_CTL_Y, click);
            V1BIT(a->menu, SFXR_CTL_MENU, click);
        } else {
            V1BIT(a->dpad_up || a->secondary, SFXR_CTL_DPAD_UP, click);
            V1BIT(a->dpad_down || a->primary, SFXR_CTL_DPAD_DOWN, click);
            V1BIT(a->dpad_left, SFXR_CTL_DPAD_LEFT, click); V1BIT(a->dpad_right, SFXR_CTL_DPAD_RIGHT, click);
            V1BIT(a->menu, SFXR_CTL_VIEW, click);
        }
        #undef V1BIT
        memcpy(b->profile, a->profile, sizeof b->profile);
    }
}

// --- reading any version -----------------------------------------------------

// The frame layout a header describes: 1, 2, REC_VERSION, or 0 (unreadable,
// e.g. recorded by a build with different struct sizes).
static inline int sfxr_rec_layout(const RecHeader *h)
{
    if (h->version == REC_VERSION && h->frame_size == sizeof(RecFrame) && h->raw_hand_size == sizeof(SfxrRawHand)) return REC_VERSION;
    if (h->version == 2 && h->frame_size == sizeof(RecFrameV2) && h->raw_hand_size == sizeof(SfxrRawHand)) return 2;
    if (h->version == 1 && h->frame_size == sizeof(RecFrameV1) && h->raw_hand_size == sizeof(SfxrRawHandV1)) return 1;
    return 0;
}

// Read the next frame in the current layout.
static inline bool sfxr_rec_read(FILE *f, int layout, RecFrame *out)
{
    if (layout == 1) {
        RecFrameV1 old;
        if (fread(&old, sizeof old, 1, f) != 1) return false;
        sfxr_rec_upgrade_v1(&old, out);
        return true;
    }
    if (layout == 2) {
        RecFrameV2 old;
        if (fread(&old, sizeof old, 1, f) != 1) return false;
        sfxr_rec_upgrade_v2(&old, out);
        return true;
    }
    return fread(out, sizeof *out, 1, f) == 1;
}

#endif // SFXR_REC_H
