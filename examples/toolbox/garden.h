// garden.h - Daddy Bug Smasher: the toolbox's third and last part, a small real
// game built from the same pieces as the stations. Walk through the gate at
// the right end of the row to play it (docs/DADDY_BUG_SMASHER.md).
//
//   garden.c            the game: take the hammer from Daddy, smash the
//                       bugs before they get you; the HUD, menus, Steam
//   garden_world.c      the level: sculpted terrain you walk on, Blender-made
//                       props, a sun, colliders, a chair you can knock over
//   garden_rigidbody.c  a small box rigid body (the chair, a thrown hammer)
//   garden_anim.c       named animation clips with cross-fades, and where a
//                       bone is (Daddy holds the hammer in his hand)
//
// Daddy Bug Smasher is a flat-screen raylib game the author made with their
// kids, from the kids' drawings: Daddy smashes the bugs the Bugmaster sends
// from his tower. The engine parts here (world, rigid body, animation) are
// ported from its 3D mode, and its level and models came with them. Its units were about 1.4 per meter, so everything is scaled by
// GARDEN_SCALE on the way in, and all the numbers here are meters.

#ifndef GARDEN_H
#define GARDEN_H

#include "toolbox.h"

#define GARDEN_SCALE 0.7f     // level units -> meters

// --- garden_world.c ---------------------------------------------------------

bool  gw_load(void);                    // models, lighting, terrain heights (once; false if the models are missing)
void  gw_unload(void);
void  gw_reset(void);                   // loose props back where they started
float gw_ground(float x, float z);      // terrain height (m)
float gw_half_size(void);               // the playable square: -h..h in x and z
float gw_solid_depth(Vector3 p);        // how deep p is inside a trunk, rock, the tower... (vrui_locomotion)
bool  gw_resolve_circle(float *x, float *z, float r);   // push a walker out of solid props
void  gw_push_loose(float *x, float *z, float r, Vector3 vel, float mass);   // walk into the chair: it tips
bool  gw_hit_loose(Vector3 at, Vector3 vel, float mass, float reach);       // hit the chair with something
void  gw_step(float dt);                // loose-prop physics
void  gw_draw(void);                    // inside sfxr_draw_begin/end
Color gw_sky(void);
void  gw_light(Model *m);               // light a model the game loads (Daddy, the hammer)
const char *gw_path(const char *file);  // where resources/garden/<file> is (next to the app, or in the source tree)

// --- garden_rigidbody.c -----------------------------------------------------
// One free box: falls, tips, bounces, settles on a flat floor, sleeps.

typedef struct {
    Vector3 half;           // half-extents (m)
    Vector3 pos, vel;       // center of mass, m and m/s
    Quaternion orient;
    Vector3 ang_vel;        // rad/s, world axes
    float mass, inv_mass;
    Vector3 inv_inertia;    // diagonal, body space
    bool asleep;
    float rest_timer;
} RigidBody;

void   rb_init(RigidBody *rb, Vector3 center, Vector3 half, Quaternion orient, float mass);
void   rb_step(RigidBody *rb, float dt, float gravity, float floor_y);
void   rb_apply_impulse(RigidBody *rb, Vector3 impulse, Vector3 at);
SfxrPose rb_pose(const RigidBody *rb);
Vector3  rb_point_velocity(const RigidBody *rb, Vector3 at);

// --- garden_anim.c ----------------------------------------------------------
// A model with named clips ("idle", "walk"...), cross-fading between them.

typedef struct {
    Model model;
    ModelAnimation *clips;
    int  clip_count;
    int  cur, prev;
    float cur_frame, prev_frame, blend;
} AnimModel;

bool   anim_load(AnimModel *a, const char *path);
void   anim_unload(AnimModel *a);
void   anim_play(AnimModel *a, const char *name);   // loops; cross-fades from the current clip
void   anim_update(AnimModel *a, float dt);
void   anim_draw(const AnimModel *a, SfxrPose pose, float scale);
// World pose of a bone (e.g. "hand.R") for a model drawn at `pose`, `scale`:
// the parent to attach a held thing to (docs/ATTACHING.md).
SfxrPose anim_bone_pose(const AnimModel *a, const char *bone, SfxrPose pose, float scale);

// --- garden.c ---------------------------------------------------------------

bool  garden_active(void);
void  garden_gate(void);                // in the toolbox: the gate at the right end of the row
void  garden_update(const VruiLocoConfig *toolbox_loco);   // instead of the stations, between vrui_begin/end
void  garden_draw(void);
Color garden_sky(void);
void  garden_enter(void);
void  garden_leave(void);

// For tests (tests/garden): the round as numbers, a bug placed by hand (from
// then on: no timed waves, and bugs stay where they're put, though they still
// bite), and starting the round without the hammer.
typedef struct {
    int round;              // 0 waiting for the hammer, 1 playing, 2 won, 3 lost
    int score, hp;
    int hammer_at;          // 0 with Daddy, 1 in your hand, 2 on your belt, 3 loose
    SfxrPose hammer;        // the handle's middle
    Vector3 head;           // the hammer's head
    SfxrPose belt;          // the belt slot on your right hip
    int bugs;               // bugs alive (not being squashed)
    Vector3 first_bug;      // the first one's body center
} GardenState;
GardenState garden_state(void);
void garden_test_bug(int type, float x, float z);   // type: 0 red, 1 blue, 2 green (3 hits)
void garden_test_start(void);
void garden_test_feel(int mode);                  // VruiSmoothMode for the hammer

#endif
