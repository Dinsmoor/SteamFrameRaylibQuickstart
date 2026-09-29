// toolbox - a walk-around testbed for everything in this quickstart. Not an
// app with a flow: a set of stations, each in its own file (see toolbox.h).
//
// They stand in one row in front of you; walk along it (stick forward to
// teleport, sideways to turn). From left to right:
//   Weights            feather to anvil: how heavy things should feel, and a lane to throw them
//   Wielding           weapons held by their handles, Blade & Sorcery style, and a sandbag
//   Voice commands     one set of orders bound four ways: context button, ring menu, world button, hands-free
//   Sound              sounds from where things are: point, cone, line, box and ambient emitters
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
//   Smoothing          how held things follow the hand: snap, lag, spring,
//                      heavy, steady; easing curves
//   Hinges & cords     a door, a chest lid, a bell cord, a radio dial,
//                      a two-handed valve, a key switch
// Past the right end: the gate to Daddy Bug Smasher, part three: a small game
// made from these pieces (garden.c).
// Behind you: the LIFT platform and the Movement yard (teleport pads,
// stairs, climbing wall, monkey bars).
// With you everywhere: the hand menus and the HUD (station_menus.c, hud.c).
//
// Runs in the headset, under Monado, or in the desktop simulator (F1 = keys).

#include "toolbox.h"
#include "garden.h"
#include "onboarding.h"
#include "sfxr_steam.h"
#include "sfxr_voice.h"

#include <stdlib.h>

// The HUD's text here: where you are along the row. The edge arrows point
// home (the workbench) and to the yard when they're out of view.
static void toolbox_hud(void)
{
    static const struct { float x; const char *name; } ROW[] = {
        { -23.0f, "Particles" }, { -20.1f, "Physics guns" }, { -18.8f, "Weights" }, { -15.4f, "Wielding" }, { -12.2f, "Voice commands" }, { -9.8f, "Sound" }, { -8.0f, "Menus & HUD" }, { -6.4f, "Hands-on setup" }, { -4.8f, "Headset" }, { -3.2f, "Controllers" },
        { -1.6f, "Toolbox" }, { 0, "Workbench" }, { 2.5f, "Mechanisms" }, { 5.2f, "Linkage bench" },
        { 7.9f, "Attach & label" }, { 10.9f, "Smoothing" }, { 15.3f, "Hinges & cords" }, { 19.5f, "Daddy Bug Smasher" },
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

// The toolbox in three parts, so tests can run the real thing: setup once
// (after sfxr and vrui start), logic every frame (between vrui_begin and
// vrui_end), and drawing (inside sfxr_draw_begin/end). tests/toolbox runs
// scenario files against exactly these (docs/TESTING.md).
static VruiLocoConfig loco;
static SfxrPose setup_pose;

void toolbox_setup(void)
{
    sfxr_steam_init();   // optional: no libsteam_api.so next to the app = no Steam, no harm
    sounds_init();       // sounds made in code; no audio device = silence, no harm
    sfxr_voice_init(NULL);   // optional: voice commands, when scripts/get-speech.sh has run

    // Preferences saved by the hands-on setup station: next to the app (on the
    // headset: its folder). Replays and tests only use them when given
    // explicitly (SFQ_PREFS), so they stay reproducible.
    const char *prefs = getenv("SFQ_PREFS");
    SfxrBackend be = sfxr_backend();
    onboarding_init(prefs ? prefs : (be == SFXR_BACKEND_REPLAY || be == SFXR_BACKEND_SCRIPT) ? NULL : "prefs.cfg");

    // names for the widget registry: "table.sky", "mech.knob"... (tests find widgets by these)
    static const struct { unsigned g; const char *name; } GROUPS[] = {
        { G_TABLE, "table" }, { G_BLOCKS, "blocks" }, { G_BENCH, "mech" }, { G_LINK, "link" }, { G_YARD, "yard" },
        { G_HINGE, "hinges" }, { G_VOICE, "voice" }, { G_SOUND, "sound" }, { G_WIELD, "rack" }, { G_WEIGHTS, "weights" }, { G_GUNS, "guns" }, { G_PARTICLES, "particles" }, { G_ATTACH, "attach" }, { G_MENUS, "menus" }, { G_SMOOTH, "smooth" }, { G_GARDEN, "garden" },
    };
    for (size_t i = 0; i < sizeof GROUPS / sizeof GROUPS[0]; i++) vrui_group_name(GROUPS[i].g, GROUPS[i].name);

    world_init();
    gfx_init();          // the lighting shader and textures (graphics.c)
    particles_init();    // fire, smoke, steam, sparks (particles.c)
    loco = vrui_loco_default();
    yard_setup(&loco);   // surfaces, walls, teleport pads, "only pads inside the yard"
    setup_pose = row_pose(-6.4f, 1.35f);
}

void toolbox_logic(void)
{
    // What the hand menus offer here (every menu shows the same list; the
    // palm shows the first three).
    static const char *const MENU[] = { "Grid", "Day / dusk", "Go home", "Reset blocks", "HUD style", "Hints", "Bug Smasher", "Screenshot" };
    const int NMENU = (int)(sizeof MENU / sizeof MENU[0]);

    gfx_frame(garden_active() ? 0.0f : world.sky, toolbox_sky());
    screenshot_update();
    if (garden_active()) {
        garden_update(&loco);   // part three: the garden replaces the stations while you're in it
        return;
    }
    panels_toolbox(&loco);
    station_particles();
    station_weights();
    station_wield();
    station_sound();
    station_voice();
    station_menus();
    switch (menus_update(MENU, NMENU, TextFormat("blocks %d", world.spawned))) {
    case 0: world.show_grid = !world.show_grid; break;
    case 1: world.sky = world.sky > 0.5f ? 0.0f : 1.0f; break;
    case 2: sfxr_rig_teleport((Vector3){ 0, 0, 0 }); vrui_fade(1.0f); break;
    case 3: world_reset_blocks(); break;
    case 4: menus_set_hud_style((HudStyle)((menus_hud_style() + 1) % HUD_COUNT)); break;
    case 5: vrui_style()->show_hints = !vrui_style()->show_hints; break;
    case 6: garden_enter(); break;
    case 7: screenshot_start("toolbox"); break;
    default: break;
    }
    onboarding_update();
    onboarding_panel(&setup_pose);
    station_sign(-6.4f, "Hands-on setup", "learns how you like to grab, point and press");
    world_workbench();
    controls_diagram();
    bench_mechanisms();
    bench_linkage();
    panel_controllers();
    panel_headset();
    station_hinges();
    station_attach();
    station_smoothing();
    garden_gate();
    yard_update();
    vrui_locomotion(&loco);   // after the handholds (yard_update)
    toolbox_hud();
    vrui_text3d((Vector3){ 0, 3.0f, -1.8f }, "sfxr + vrui toolbox", 0.196f, RAYWHITE);
    world_step(sfxr_dt());
    particles_update(sfxr_dt());

    // what tests check ("expect app sky >= 0.9"): the app's own state
    sfxr_report("sky", world.sky);
    sfxr_report("grid", world.show_grid);
    sfxr_report("blocks", (float)world_block_count());
    sfxr_report("lift", world.lift);
}

void toolbox_draw(void)
{
    gfx_draw_begin();   // everything solid in the world goes through the lighting shader
    if (garden_active()) {
        garden_draw();
    } else {
        world_draw();
        scenery_draw();
        controls_diagram_draw();
        yard_draw();
    }
    gfx_draw_end();
    // see-through things last, over the finished world: particles (their own
    // blending; smoke dims at dusk, fire doesn't)
    if (!garden_active()) particles_draw(1.0f - 0.6f * world.sky);
}

Color toolbox_sky(void) { return garden_active() ? garden_sky() : world_sky(); }

#ifndef TOOLBOX_NO_MAIN   // (tests/toolbox brings its own main)
int main(void)
{
    SfxrConfig cfg = sfxr_default_config();
    cfg.app_name = "sfxr toolbox";
    if (!sfxr_init(&cfg)) return 1;
    vrui_init();
    toolbox_setup();

    while (sfxr_frame_begin()) {
        vrui_begin();
        toolbox_logic();
        vrui_end();

        if (sfxr_draw_begin(toolbox_sky())) {
            toolbox_draw();
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
#endif
