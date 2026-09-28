// toolbox - a walk-around testbed for everything in this quickstart. Not an
// app with a flow: a set of stations, each in its own file (see toolbox.h).
//
// They stand in one row in front of you; walk along it (stick forward to
// teleport, sideways to turn). From left to right:
//   Menus & HUD        hand menus (watch, palm, tablet, radial) and visor HUDs
//   Hands-on setup     learns how you like to grab, point and press
//   Headset            worn, refresh rate, passthrough, batteries, joints
//   Controllers        every Steam Frame input, live
//   Toolbox            world settings, pull level, grab style
//   Workbench          (straight ahead) SPAWN, GRID, SKY, SIZE, LIFT wired to
//                      the world, and throwable blocks
//   Mechanisms         every reference mechanism
//   Linkage bench      controls wired to mechanical displays
//   Attach & label     things riding on things; every kind of world label
//   Hinges & cords     a door, a chest lid, a bell cord, a radio dial,
//                      a two-handed valve, a key switch
// Behind you: the LIFT platform and the Movement yard (teleport pads,
// stairs, climbing wall, monkey bars).
// With you everywhere: the hand menus and the HUD (station_menus.c, hud.c).
//
// Runs in the headset, under Monado, or in the desktop simulator (F1 = keys).

#include "toolbox.h"
#include "onboarding.h"
#include "sfxr_steam.h"

#include <stdlib.h>

// The HUD's text here: where you are along the row. The edge arrows point
// home (the workbench) and to the yard when they're out of view.
static void toolbox_hud(void)
{
    static const struct { float x; const char *name; } ROW[] = {
        { -8.0f, "Menus & HUD" }, { -6.4f, "Hands-on setup" }, { -4.8f, "Headset" }, { -3.2f, "Controllers" },
        { -1.6f, "Toolbox" }, { 0, "Workbench" }, { 2.5f, "Mechanisms" }, { 5.2f, "Linkage bench" },
        { 7.9f, "Attach & label" }, { 12.8f, "Hinges & cords" },
    };
    Vector3 me = sfxr_head().position;
    const char *near = "the yard";
    if (me.z < 3.0f) {
        float best = 1e9f;
        for (size_t i = 0; i < sizeof ROW / sizeof ROW[0]; i++)
            if (fabsf(ROW[i].x - me.x) < best) { best = fabsf(ROW[i].x - me.x); near = ROW[i].name; }
    }
    hud_show(menus_hud_style(), TextFormat("%s\n%.0f fps", near, sfxr_dt() > 0 ? 1.0f / sfxr_dt() : 0.0f), vrui_style()->accent);
    if (menus_edge_arrows()) {
        vrui_offscreen_arrow((Vector3){ 0, 1.2f, TABLE_Z }, "home", (Color){ 255, 255, 255, 230 });
        vrui_offscreen_arrow((Vector3){ 0, 1.2f, 5.0f }, "yard", (Color){ 120, 200, 255, 230 });
    }
}

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
    SfxrPose setup_pose = row_pose(-6.4f, 1.35f);

    // What the hand menus offer here (every menu shows the same list; the
    // palm shows the first three).
    static const char *const MENU[] = { "Grid", "Day / dusk", "Go home", "Reset blocks", "HUD style", "Hints" };
    const int NMENU = (int)(sizeof MENU / sizeof MENU[0]);

    while (sfxr_frame_begin()) {
        vrui_begin();
            panels_toolbox(&loco);
            station_menus();
            switch (menus_update(MENU, NMENU, TextFormat("blocks %d", world.spawned))) {
            case 0: world.show_grid = !world.show_grid; break;
            case 1: world.sky = world.sky > 0.5f ? 0.0f : 1.0f; break;
            case 2: sfxr_rig_teleport((Vector3){ 0, 0, 0 }); vrui_fade(1.0f); break;
            case 3: world_reset_blocks(); break;
            case 4: menus_set_hud_style((HudStyle)((menus_hud_style() + 1) % HUD_COUNT)); break;
            case 5: vrui_style()->show_hints = !vrui_style()->show_hints; break;
            default: break;
            }
            onboarding_update();
            onboarding_panel(&setup_pose);
            world_workbench();
            bench_mechanisms();
            bench_linkage();
            panel_controllers();
            panel_headset();
            station_hinges();
            station_attach();
            yard_update();
            vrui_locomotion(&loco);   // after the handholds (yard_update)
            toolbox_hud();
            station_sign(-6.4f, "Hands-on setup", "learns how you like to\ngrab, point and press");
            vrui_text3d((Vector3){ 0, 3.0f, -1.8f }, "sfxr + vrui toolbox", 0.14f, RAYWHITE);
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
