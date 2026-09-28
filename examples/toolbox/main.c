// toolbox - a walk-around testbed for everything in this quickstart. Not an
// app with a flow: a set of stations, each in its own file (see toolbox.h).
//
//   Ahead:        the workbench -- SPAWN, GRID, SKY, SIZE, LIFT wired to the
//                 world, and throwable blocks (an open palm shoves them, a
//                 fist punches them)
//   Ahead left:   the Toolbox panel -- world settings, pull level, grab style
//   Left:         the Controllers panel -- every Steam Frame input, live
//   Behind left:  the Headset panel -- worn, refresh rate, passthrough, ...
//   Ahead right:  the Mechanisms bench -- every reference mechanism
//   Behind right: the Linkage bench -- controls wired to mechanical displays
//   Behind:       the Hands-on setup station (docs/ONBOARDING.md)
//   The stations stand on a ring with room to walk around each (toolbox.h).
//   Far right:    Hinges & cords -- a door, a chest lid, a bell cord, a radio dial
//   Far ahead:    the Movement yard -- teleport pads, stairs, climbing wall, monkey bars
//   On your wrist: fps, hand shapes, trigger level
//   Moving:       stick forward = teleport arc, stick sideways = snap turn
//
// Runs in the headset, under Monado, or in the desktop simulator (F1 = keys).

#include "toolbox.h"
#include "onboarding.h"
#include "sfxr_steam.h"

#include <stdlib.h>

int main(void)
{
    SfxrConfig cfg = sfxr_default_config();
    cfg.app_name = "sfxr toolbox";
    if (!sfxr_init(&cfg)) return 1;
    vrui_init();
    sfxr_steam_init();   // optional: no libsteam_api.so next to the app = no Steam, no harm

    // Preferences saved by the hands-on setup station: next to the app (on the
    // headset: its folder). Replays and tests only use them when given
    // explicitly (SFQ_PREFS), so they stay reproducible.
    const char *prefs = getenv("SFQ_PREFS");
    SfxrBackend be = sfxr_backend();
    onboarding_init(prefs ? prefs : (be == SFXR_BACKEND_REPLAY || be == SFXR_BACKEND_SCRIPT) ? NULL : "prefs.cfg");

    world_init();
    VruiLocoConfig loco = vrui_loco_default();
    yard_setup(&loco);   // surfaces, teleport pads, "only pads inside the yard"
    SfxrPose setup_pose = station_pose(165, 2.2f, 1.35f);

    while (sfxr_frame_begin()) {
        vrui_begin();
            panels_toolbox(&loco);
            panels_wrist();
            onboarding_update();
            onboarding_panel(&setup_pose);
            world_workbench();
            bench_mechanisms();
            bench_linkage();
            panel_controllers();
            panel_headset();
            station_hinges();
            yard_update();
            vrui_locomotion(&loco);   // after the handholds (yard_update)
            vrui_text3d((Vector3){ 0, 2.2f, -2.5f }, "sfxr + vrui toolbox", 0.12f, RAYWHITE);
        vrui_end();

        world_step(sfxr_dt());

        if (sfxr_draw_begin(world_sky())) {
            world_draw();
            yard_draw();
            vrui_draw();
            sfxr_draw_end();
        }
        // desktop mirror only (the headset never sees this)
        DrawText(TextFormat("%s | %s | R: %s | %d fps", sfxr_backend_name(), sfxr_system_name(),
                            sfxr_interaction_profile(SFXR_RIGHT), GetFPS()),
                 10, GetScreenHeight() - 20, 10, RAYWHITE);
        sfxr_frame_end();
    }

    vrui_shutdown();
    sfxr_shutdown();
    return 0;
}
