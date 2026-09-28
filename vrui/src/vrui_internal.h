// vrui_internal.h - shared context for the vrui modules.
//
// Targeting model (per hand, one frame of latency like any immediate-mode UI):
//   * Every widget that can be reached by the LASER calls ray_offer(h, id, dist)
//     with its hit distance. At vrui_end() the closest offer becomes that
//     hand's ray_hot for the next frame.
//   * Every widget that can be reached by the HAND calls grab_offer(h, id, d)
//     with a proximity score (smaller = better). Closest becomes grab_hot.
//   * Pressing trigger (ray) or grip (hand) on the hot widget captures it:
//     ray_active / grab_active stay set until release, and a captured hand
//     doesn't hover anything else.

#ifndef VRUI_INTERNAL_H
#define VRUI_INTERNAL_H

#include "vrui.h"
#include "rlgl.h"
#include "sfxr_break.h"

#define BREAK SFXR_BREAK_DECLARE
#include "vrui_breaks.def"
#undef BREAK

#include <math.h>
#include <string.h>

#define VRUI_MAX_CMDS     2048
#define VRUI_TEXT_ARENA   (32 * 1024)
#define VRUI_MAX_ITEMS    512
#define VRUI_MAX_PANELS   32
#define VRUI_RAY_LENGTH   8.0f
#define VRUI_PULL_STACK   8

typedef enum {
    CMD_BOX, CMD_BOX_WIRES, CMD_SPHERE, CMD_CYLINDER, CMD_LINE, CMD_PANEL, CMD_TEXT, CMD_RING, CMD_TRI,
} VruiCmdKind;

typedef struct {
    VruiCmdKind kind;
    Color color;
    SfxrPose pose;        // BOX / PANEL / RING
    Vector3 a, b;         // LINE, CYLINDER endpoints; SPHERE center = a; TEXT position = a; TRI a, b, size
    Vector3 size;         // BOX size; PANEL (w,h,_); CYLINDER (r0,r1,_); SPHERE (r,_,_); TEXT (height); RING (r)
    unsigned tex;         // PANEL
    int text_off;         // TEXT
    bool billboard;       // TEXT: faces the head (else lies on `pose`, facing its +Z)
    Color bg;             // TEXT: backing plate (alpha 0 = none)
    bool on_top;          // drawn after everything, without depth test (vrui_on_top_begin)
} VruiCmd;

// Persistent per-widget state, found by id with vrui__item(). The grab
// machinery's fields are named here; every module keeps its own typed struct
// in `state` (zeroed on first use):
//
//   typedef struct { float shown, velocity; } NeedleState;
//   VRUI_STATE_FITS(NeedleState);
//   NeedleState *ns = VRUI_STATE(vrui__item(id), NeedleState);
#define VRUI_STATE_BYTES 128
typedef struct {
    VruiId   id;
    bool     used;
    uint64_t last_frame;
    int      hold_hand;   // vrui__handle_update: the holding hand, -1 none
    int      hold_mode;   // VRUI_MODE_* of that hold
    SfxrPose rel;         // grab offset (object in hand space), or a grab-time reference pose
    char     name[24];    // the widget's label, for the event log (vrui__name)
    union { unsigned char bytes[VRUI_STATE_BYTES]; double align; } state;
} VruiItem;
#define VRUI_STATE(it, T) ((T *)(void *)(it)->state.bytes)
#define VRUI_STATE_FITS(T) _Static_assert(sizeof(T) <= VRUI_STATE_BYTES, #T " must fit in VruiItem.state")

typedef struct {
    VruiId id;
    RenderTexture2D rt;
    int w, h;
} VruiPanelTex;

typedef struct {
    bool   pulse_pending;
    float  pulse_amp, pulse_secs, pulse_hz;
    double pulse_until;       // a pulse plays out before a hum resumes
    float  hum_amp, hum_hz;   // strongest hum requested this frame
    bool   humming;
} VruiHapticMix;

typedef struct {
    VruiStyle style;
    Font font;
    bool custom_font;
    bool show_controllers;
    bool controller_models;   // draw the runtime's controller models when available
    bool joints_always;       // hand joints also while holding controllers

    // arbitration
    VruiId ray_hot[2], ray_active[2];
    float  ray_hot_dist[2];
    VruiId ray_offer_id[2];
    float  ray_offer_dist[2];
    VruiId grab_hot[2], grab_active[2];
    VruiId grab_offer_id[2];
    float  grab_offer_score[2];
    VruiId last_hover_tick[2];

    // input claims (vrui_claim_input): this frame and last frame
    bool claim_cur[2], claim_prev[2];
    VruiCapture next_capture;     // for the next vrui_panel_begin
    bool next_passive;            // next panel is a display: no laser, no claims (vrui_panel_passive)
    int  on_top;                  // vrui_on_top_begin nesting: commands queued now draw over the world

    VruiHapticMix hap[2];

    // handholds (vrui_handhold -> vrui_locomotion): which hands are on one
    // this frame, since when, and the world point each hand is pinned to
    struct { uint64_t held_frame[2], since[2]; Vector3 anchor[2]; } climb;

    // "how do I use this?" hints for whatever each hand is on (vrui__hint)
    struct { VruiId id; char text[80]; } hints[64];
    int nhints;

    // VRUI_GRAB_CLOSE state per hand (updated at vrui_begin)
    bool close_armed[2], close_down[2], close_pressed[2];

    // pull level stack (vrui_push_pull)
    SfxrPull pull_stack[VRUI_PULL_STACK];
    int npull;

    // laser visuals computed at vrui_end
    bool   laser_on[2];
    Vector3 laser_from[2], laser_to[2];
    bool   laser_hit[2];
    bool   laser_on_panel[2];

    // draw queue
    VruiCmd cmds[VRUI_MAX_CMDS];
    int ncmds;
    char text[VRUI_TEXT_ARENA];
    int ntext;
    float fade;
    Color tint;                   // vrui_tint: colored flash over the view (alpha = strength)

    // vrui_body(): the player's estimated torso, yaw kept relative to the rig
    // so snap turns carry it along
    struct { bool init; float yaw_rel; SfxrPose pose; } body;

    VruiItem items[VRUI_MAX_ITEMS];
    VruiPanelTex panels[VRUI_MAX_PANELS];

    // current panel
    struct {
        bool open;
        VruiId id;
        SfxrPose pose;
        float w_m, h_m;
        int w_px, h_px;
        bool has_title;
        int hand;              // hand pointing at it, -1 none
        Vector2 ptr;           // pointer in panel px
        bool down, pressed, released;
        Vector2 stick;
        VruiId active_widget;  // per-panel capture (widget id), stored in item
        VruiItem *item;
        bool capturing;        // holding both hands (VRUI_CAPTURE_LOOK / MODAL)
        // layout
        Rectangle lay_area;
        float lay_y, lay_spacing;
    } p;

    uint64_t frame;
} VruiCtx;

extern VruiCtx vrui_ctx;
#define C vrui_ctx

// arbitration helpers (vrui.c)
void  vrui__ray_offer(int hand, VruiId id, float dist);
void  vrui__grab_offer(int hand, VruiId id, float score);
bool  vrui__ray_hot(int hand, VruiId id);
bool  vrui__grab_hot(int hand, VruiId id);
bool  vrui__hand_free(int hand);          // not capturing anything
void  vrui__hover_tick(int hand, VruiId id);
void  vrui__click_pulse(int hand);
Ray   vrui__hand_ray(int hand);
Vector3 vrui__tip(int hand);              // poke point (controller tip)
VruiItem *vrui__item(VruiId id);
// State for a widget whose id is only unique inside its owner (a panel's widgets).
VruiItem *vrui__widget_item(VruiId owner, VruiId local);
void vrui__haptics_flush(void);           // vrui_haptics.c, at vrui_end
// Say how a widget is used ("poke it", "grab it"...). Shown next to the hand
// or laser spot while that hand is on it (vrui_style()->show_hints).
void vrui__hint(VruiId id, const char *how);
// Event log (sfxr_event): name a widget after its label, and get a printable name.
void vrui__name(VruiId id, const char *label);
const char *vrui__who(VruiId id);         // the label, or the id in hex
// The words for the current grab style and pull level ("grab it", "close your hand on it"...).
const char *vrui__grab_words(void);
const char *vrui__pull_suffix(void);   // "" at FIRM, else " (light pull)" / " (full pull)"
// Near grab by the style in effect (vrui_style()->grab).
bool vrui__grab_pressed(int hand);
bool vrui__grab_down(int hand);
// A hand's trigger / grip at the current pull level (vrui_push_pull).
const SfxrButton *vrui__trigger(int hand);
const SfxrButton *vrui__squeeze(int hand);

// grab machinery shared by grabbables and mechanisms (vrui_grab.c)
enum { VRUI_MODE_HAND = 0, VRUI_MODE_RAY_TRIGGER = 1, VRUI_MODE_RAY_GRIP = 2 };
typedef struct {
    int  hand;               // holding hand, -1 if none
    int  mode;               // VRUI_MODE_*
    bool hovered, grabbed, held, released;
} VruiHandle;
// Uses it->hold_hand / hold_mode. ray_dist[h] < 0: the laser misses;
// prox[h] < 0: out of reach, else the distance from the grip to the collider.
VruiHandle vrui__handle_update(VruiId id, VruiItem *it, const float ray_dist[2], const float prox[2]);
void  vrui__ray_box_all(SfxrPose pose, Vector3 half, float out[2]);
void  vrui__prox_box_all(SfxrPose pose, Vector3 half, float out[2]);
Color vrui__hover_tint(Color c, bool hovered, bool held);
void  vrui__label(SfxrPose base, Vector3 local, const char *text);
// Where the hand "is" for dragging: the grip (hand mode) or the laser's hit
// on the plane through `origin` with `normal` (ray mode).
bool  vrui__drag_point(int hand, int mode, Vector3 origin, Vector3 normal, Vector3 *out);

// draw queue (vrui.c)
VruiCmd *vrui__cmd(VruiCmdKind kind, Color color);
void vrui__sphere(Vector3 c, float r, Color color);
void vrui__cylinder(Vector3 a, Vector3 b, float r, Color color);
void vrui__ring(SfxrPose pose, float r, Color color);
void vrui__triangle(Vector3 a, Vector3 b, Vector3 c, Color color);   // two-sided

// vrui_attach.c: the body estimate, updated once per frame at vrui_begin
void vrui__body_update(void);

// math
float vrui__ray_box(Ray ray, SfxrPose pose, Vector3 half);   // distance or -1
float vrui__point_box_dist(Vector3 p, SfxrPose pose, Vector3 half);

#endif // VRUI_INTERNAL_H
