// toolbox.h - what the toolbox's stations share. Each station is its own
// file and can be read (and copied) on its own:
//
//   main.c               the frame loop: calls every station
//   world.c              ground, trees, sky; the workbench (app-wired controls, throwable blocks)
//   controls_diagram.c   the controls, where you start: both controllers with a callout on every control
//   graphics.c           lighting shader (resources/shaders/), textures, culling, static batching
//   panels.c             the Toolbox panel (settings) and the wrist panel
//   bench_mechanisms.c   one of every reference mechanism, default feel
//   bench_linkage.c      controls wired to gauges, rolling counters and lamps
//   panel_controllers.c  every controller input live, finger curl, a haptics tester
//   panel_headset.c      worn state, refresh rate, passthrough, batteries, joints, counters
//   onboarding.c         the hands-on setup station (learns the player's habits)
//   station_weights.c    Weights: feather, ball, brick, kettlebell, anvil, and a lane to throw them down
//   station_guns.c       a physgun (Garry's Mod) and a gravity gun (Half-Life 2), beside the Weights table
//   particles.c, station_particles.c  a particle system; the Particles bench: fire, smoke, steam, sparks
//   station_wield.c      Wielding: sword, hammer, spear, dagger held by their handles; a sandbag to hit
//   station_sound.c      Sound: point, cone, line, box and ambient emitters, and what your ears get
//   station_voice.c      Voice commands: one command, bound every way (context button, radial, world button, hands-free)
//   station_hinges.c     Hinges & cords: a door, a chest lid, a bell cord, a radio dial, a valve, a key switch
//   yard.c               the Movement yard: teleport pads, stairs, climbing wall, monkey bars
//   station_attach.c     Attach & label: things riding on things, every kind of world label
//   station_smoothing.c  Smoothing: how held things follow the hand, easing curves
//   station_menus.c, hud.c  hand menus and visor HUDs (with you everywhere)
//   garden*.c            Daddy Bug Smasher: part three, a small real game (garden.h)

#ifndef TOOLBOX_H
#define TOOLBOX_H

#include "sfxr.h"
#include "vrui.h"

#include <math.h>
#include <stddef.h>

// Layout (meters; the player starts at the origin facing -Z).
//
// The stations stand in ONE ROW along the X axis, just in front of you, all
// facing the aisle you stand in (+Z), with a gap of a meter or more between
// neighbours so you can walk between them and round the back. Walk (or
// teleport) along the aisle to go from one to the next. Each station has a
// sign above it (vrui_sign) saying what it is.
//
//      x   station                       file
//  -23.0   Particles                     station_particles.c (fire, smoke, steam, sparks)
//  -20.1   (a stand of physics guns)     station_guns.c (physgun and gravity gun, on the Weights things)
//  -18.8   Weights                       station_weights.c (feather to anvil: how heavy should feel)
//  -15.4   Wielding                      station_wield.c (weapons held by their handles, Blade & Sorcery style)
//  -12.2   Voice commands                station_voice.c (ways to bind a command: button, menu, hands-free)
//   -9.8   Sound                         station_sound.c (emitters: point, cone, line, box, ambient)
//   -8.0   Menus & HUD                   station_menus.c (hand menus, visor HUDs)
//   -6.4   Hands-on setup                onboarding.c
//   -4.8   Headset panel                 panel_headset.c
//   -3.2   Controllers panel             panel_controllers.c
//   -1.6   Toolbox panel                 panels.c
//    0     the workbench                 world.c
//    2.5   Mechanisms bench              bench_mechanisms.c
//    5.2   Linkage bench                 bench_linkage.c
//    7.9   Attach & label bench          station_attach.c
//   10.9   Smoothing                     station_smoothing.c
//   13.9   Hinges & cords (4 m wide)     station_hinges.c
//   19.5   Daddy Bug Smasher's gate      garden.c: part three, a small game
//
// Behind you (+Z): the Movement yard (yard.c), and the LIFT platform on the
// way to it.
#define ROW_Z   -0.8f           // the row's line (bench centers, panel faces)
#define TABLE_Y  0.9f           // table top height
#define TABLE_Z  ROW_Z          // the workbench, straight ahead
#define TABLE_W  1.4f
#define TABLE_D  0.7f

#define LIFT_X  -3.0f           // the lift platform (world.c draws it, yard.c walks on it)
#define LIFT_Z   2.2f

// A station's frame: on the row at x, height y, facing the aisle (+Z), so
// "+X to your right, +Z toward you" as you stand in front of it.
static inline SfxrPose row_pose(float x, float y)
{
    return (SfxrPose){ { x, y, ROW_Z }, QuaternionIdentity() };
}

// The sign above a station: a board 2.1 m up, set back behind the station
// so it never blocks your reach. vrui_sign (vrui.h section 9) sizes the
// letters from the board's width.
static inline void station_sign(float x, const char *title, const char *body)
{
    vrui_sign((SfxrPose){ { x, 2.3f, ROW_Z - 0.45f }, QuaternionIdentity() }, 1.2f, title, body,
              (Color){ 44, 50, 64, 255 });
}

// Widget id groups: VRUI_ID2(group, index).
enum { G_TABLE = 1, G_PANEL, G_WRIST, G_BLOCKS, G_BENCH, G_CTRL, G_HEADSET, G_LINK, G_YARD, G_HINGE,
       G_ATTACH, G_MENUS, G_HUD, G_GARDEN, G_SMOOTH, G_VOICE, G_SOUND, G_WIELD, G_WEIGHTS, G_GUNS, G_PARTICLES };

// graphics.c: the world's look (the switches on the workbench) ------------------
typedef enum { MAT_NONE, MAT_PAINT, MAT_WOOD, MAT_GRASS, MAT_STONE, MAT_WATER, MAT_COUNT } GfxMaterial;
typedef struct { bool lighting, shine, fog, textures, culling, batching; } GfxSettings;
typedef struct {
    int scenery_prims;                  // scenery calls this frame
    int scenery_drawn, scenery_culled;  // chunks (batched) or primitives (not)
    int chunks, builds;                 // batched meshes, and how many times they were built
} GfxStats;
extern GfxSettings gfx;
Color toolbox_sky(void);                           // main.c: the sky color now (the fog fades into it)
void gfx_init(void);                               // after vrui_init
void gfx_frame(float dusk, Color sky);             // every frame, before the stations
void gfx_draw_begin(void);                         // around the world's drawing
void gfx_draw_end(void);
void gfx_material(GfxMaterial m);                  // for DrawCube etc. between begin and end
bool gfx_visible(Vector3 center, float radius);    // culling (when the CULL switch is on)
Shader gfx_model_shader(void);                     // for a model's materials (id 0 if the shader failed)
// Scenery: things that never move, declared every frame (batched once).
void scenery_box(SfxrPose pose, Vector3 size, Color color, GfxMaterial mat);
void scenery_cylinder(Vector3 a, Vector3 b, float radius, Color color, GfxMaterial mat);
void scenery_sphere(Vector3 center, float radius, Color color, GfxMaterial mat);
void scenery_draw(void);                           // between gfx_draw_begin and _end
const GfxStats *gfx_stats(void);

// particles.c: fire, smoke, steam, sparks (the pattern is explained there) ------
typedef enum { PSPRITE_PUFF, PSPRITE_FLAME, PSPRITE_SPARK, PSPRITE_COUNT } ParticleSprite;
typedef enum { PBLEND_ALPHA, PBLEND_ADD } ParticleBlend;   // smoke and steam / fire and sparks
typedef struct {
    ParticleSprite sprite;
    ParticleBlend blend;
    float rate;                      // spawned per second while on (0: bursts only)
    float life_min, life_max;        // seconds
    Vector3 velocity;                // at birth, in the emitter's frame (m/s)
    float spread_deg, speed_jitter;  // cone around it; +- fraction of its speed
    float spawn_radius;              // born within this of the emitter (m, flat disc)
    Vector3 spawn_line;              // ...and anywhere along this (emitter frame)
    float size_start, size_end;      // meters across, over its life
    Color color_start, color_mid, color_end;   // over its life (alpha too)
    float gravity;                   // m/s^2 down; negative rises (hot smoke, steam)
    float drag;                      // 1/s: how fast it loses speed
    float wind;                      // 1/s: how fast the wind takes it over
    float spin;                      // up to this many rad/s either way
    bool  stretch;                   // drawn as a streak along its motion (sparks)
    bool  bounce;                    // bounces: on floor_y over the floor rectangle, else on the ground
    float floor_y;
    Rectangle floor;                 // x, z (world), width along x, height along z
} ParticleSpec;
typedef struct { int alive, drawn, culled; } ParticleStats;
void particles_init(void);
int  particles_emitter(const ParticleSpec *spec, SfxrPose pose);   // a handle, or -1
void particles_set_on(int e, bool on);
void particles_move(int e, SfxrPose pose);
void particles_burst(int e, int count);
void particles_wind(Vector3 wind);   // m/s, for everything
void particles_update(float dt);     // every frame, logic
void particles_draw(float light);    // after the world (light 0..1: how lit smoke looks)
int  particles_alive(int e);
int  particles_bounces(int e);        // bounces so far
Vector3 particles_bounds(int e, Vector3 *hi);   // a box round its live particles: the low corner (and the high)
const ParticleStats *particles_stats(void);
void station_particles(void);        // station_particles.c: the Particles bench

// World settings, changed by the workbench controls and the Toolbox panel.
typedef struct {
    bool  show_grid;
    float sky;            // 0 day .. 1 dusk (the SKY lever)
    float block_size;     // the SIZE knob
    float lift;           // 0..1, the LIFT slider
    bool  gravity;
    float throw_power;
    int   color_index;    // spawn color
    int   spawned;
    bool  passthrough;    // Headset panel: alpha blend, scenery hidden
} ToolboxWorld;
extern ToolboxWorld world;

// world.c
void  world_init(void);
void  world_workbench(void);          // interaction (between vrui_begin/end)
void  world_step(float dt);           // physics
Color world_sky(void);
void  world_draw(void);               // inside sfxr_draw_begin/end
void  controls_diagram(void);         // controls_diagram.c: labels (between vrui_begin/end)
void  controls_diagram_draw(void);    // ... and the controllers (inside sfxr_draw_begin/end)
void  world_spawn_block(Vector3 at);
void  world_reset_blocks(void);
int   world_block_count(void);
float world_lift_height(void);        // top of the lift platform (the LIFT slider)
extern const char *const SPAWN_COLOR_NAMES[6];

// the stations (each between vrui_begin/end)
void panels_toolbox(VruiLocoConfig *loco);
void bench_mechanisms(void);
void bench_linkage(void);
void panel_controllers(void);
void panel_headset(void);
void station_hinges(void);
void station_attach(void);
void station_smoothing(void);
// the smoothing settings tuned at the Smoothing station (Daddy Bug Smasher's hammer uses them)
const VruiSmoothSpec *smoothing_spec(VruiSmoothMode mode);

// sounds.c: the toolbox's sounds, made in code (docs/AUDIO.md)
typedef enum { SND_CLICK, SND_TICK, SND_STOP, SND_BELL, SND_CHIME, SND_THUMP, SND_SQUISH, SND_CHOMP, SND_WHOOSH,
               SND_TRILL, SND_BLIP, SND_COUNT } SoundId;
void sounds_init(void);
void sound_play(SoundId id, Vector3 at, float volume);   // from a place in the world
void sound_play_here(SoundId id, float volume);          // not positional
void sound_pitch(SoundId id, float pitch);
int  sound_handle(SoundId id);                           // its SfxrSound
void sounds_vrui(VruiSound kind, Vector3 at, float strength);
void station_wield(void);                                // station_wield.c: Wielding (a weapon rack, a sandbag)
void station_weights(void);                              // station_weights.c: Weights (feather to anvil, a throwing lane)
// The Weights station's things, for the physics guns beside it (station_guns.c)
#define WEIGHT_THINGS 5
typedef struct {
    VruiId id;
    const char *name;
    SfxrPose *pose;                  // the guns move it (with vrui_wield_set_motion)
    const VruiWieldSpec *spec;
    const VruiWield *w;              // last frame's
} WeightThing;
const WeightThing *weights_thing(int k);
void station_guns(void);                                 // station_guns.c: a physgun and a gravity gun (run by station_weights)
void guns_reset(void);                                   // drop whatever the guns hold; unfreeze everything
void station_sound(void);                                // station_sound.c: Sound (emitters)
void station_voice(void);                                // station_voice.c: Voice commands

// hud.c: visor HUD templates (Daddy Bug Smasher uses them too)
typedef enum { HUD_OFF, HUD_HEAD, HUD_FOLLOW, HUD_BODY, HUD_COUNT } HudStyle;
extern const char *const HUD_STYLE_NAMES[HUD_COUNT];
void hud_show(HudStyle style, const char *text, Color accent);
void screenshot_start(const char *app);   // a 2 s countdown, then sfxr_screenshot into shots/
void screenshot_update(void);             // every frame

// station_menus.c: hand menus (watch, palm, tablet, radial) and the Menus &
// HUD station that switches them on and off. Every menu offers the same list
// of choices (the caller's: the toolbox and the garden pass their own);
// menus_update returns the index picked this frame from any of them, or -1.
int      menus_update(const char *const *items, int count, const char *watch_text);
void     station_menus(void);
HudStyle menus_hud_style(void);
void     menus_set_hud_style(HudStyle style);
bool     menus_edge_arrows(void);
SfxrHandId menus_main_hand(void);     // the player's main hand (Hands-on setup; right until then)

// yard.c: surfaces, pads and handholds; yard_setup fills in the loco config
void yard_setup(VruiLocoConfig *loco);
void yard_update(void);               // handholds and signs (before vrui_locomotion)
void yard_draw(void);                 // blocks, pads (inside sfxr_draw_begin/end)

#endif
