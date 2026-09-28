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

#ifndef TOOLBOX_H
#define TOOLBOX_H

#include "sfxr.h"
#include "vrui.h"

#include <stddef.h>

// Layout (meters; the player starts at the origin facing -Z).
#define TABLE_Y  0.9f           // table top height
#define TABLE_Z -0.75f          // the workbench, straight ahead
#define TABLE_W  1.4f
#define TABLE_D  0.7f

// Widget id groups: VRUI_ID2(group, index).
enum { G_TABLE = 1, G_PANEL, G_WRIST, G_BLOCKS, G_BENCH, G_CTRL, G_HEADSET, G_LINK };

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
extern const char *const SPAWN_COLOR_NAMES[6];

// the stations (each between vrui_begin/end)
void panels_toolbox(VruiLocoConfig *loco);
void panels_wrist(void);
void bench_mechanisms(void);
void bench_linkage(void);
void panel_controllers(void);
void panel_headset(void);

#endif
