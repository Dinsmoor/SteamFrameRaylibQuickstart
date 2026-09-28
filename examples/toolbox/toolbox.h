// toolbox.h - what the toolbox's stations share. Each station is its own
// file and can be read (and copied) on its own:
//
//   main.c               the frame loop: calls every station
//   world.c              ground, trees, sky; the workbench (app-wired controls, throwable blocks)
//   panels.c             the Toolbox panel (settings) and the wrist panel
//   bench_mechanisms.c   one of every reference mechanism, default feel
//   bench_linkage.c      controls wired to gauges, rolling counters and lamps
//   panel_controllers.c  every controller input live, finger curl, a haptics tester
//   panel_headset.c      worn state, refresh rate, passthrough, batteries, joints, counters
//   onboarding.c         the hands-on setup station (learns the player's habits)
//   station_weights.c    Weights: feather, ball, brick, kettlebell, anvil, and a lane to throw them down
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
    vrui_sign((SfxrPose){ { x, 2.15f, ROW_Z - 0.45f }, QuaternionIdentity() }, 1.2f, title, body,
              (Color){ 44, 50, 64, 255 });
}

// Widget id groups: VRUI_ID2(group, index).
enum { G_TABLE = 1, G_PANEL, G_WRIST, G_BLOCKS, G_BENCH, G_CTRL, G_HEADSET, G_LINK, G_YARD, G_HINGE,
       G_ATTACH, G_MENUS, G_HUD, G_GARDEN, G_SMOOTH, G_VOICE, G_SOUND, G_WIELD, G_WEIGHTS };

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
void station_sound(void);                                // station_sound.c: Sound (emitters)
void station_voice(void);                                // station_voice.c: Voice commands

// hud.c: visor HUD templates (Daddy Bug Smasher uses them too)
typedef enum { HUD_OFF, HUD_HEAD, HUD_FOLLOW, HUD_BODY, HUD_COUNT } HudStyle;
extern const char *const HUD_STYLE_NAMES[HUD_COUNT];
void hud_show(HudStyle style, const char *text, Color accent);

// station_menus.c: hand menus (watch, palm, tablet, radial) and the Menus &
// HUD station that switches them on and off. Every menu offers the same list
// of choices (the caller's: the toolbox and the garden pass their own);
// menus_update returns the index picked this frame from any of them, or -1.
int      menus_update(const char *const *items, int count, const char *watch_text);
void     station_menus(void);
HudStyle menus_hud_style(void);
void     menus_set_hud_style(HudStyle style);
bool     menus_edge_arrows(void);

// yard.c: surfaces, pads and handholds; yard_setup fills in the loco config
void yard_setup(VruiLocoConfig *loco);
void yard_update(void);               // handholds and signs (before vrui_locomotion)
void yard_draw(void);                 // blocks, pads (inside sfxr_draw_begin/end)

#endif
