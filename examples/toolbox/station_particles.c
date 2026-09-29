// station_particles.c - Particles: a campfire (flames, embers and its smoke),
// a chimney's smoke, a steam pipe and a spark grinder, the defaults a game
// engine ships with (Half-Life 2's env_fire, env_smokestack, env_steam,
// env_spark). Each is a ParticleSpec: the numbers ARE the effect, so read
// them side by side. Switches and a wind slider on the table; the plaque
// counts particles and what culling skipped. (particles.c is the system.)
//
//   FIRE     additive flame tongues rising and shrinking, yellow to red;
//            embers: sparks that wander up on the heat; grey smoke above
//   SMOKE    a chimney: dark puffs that rise slowly, grow big and thin out,
//            and lean with the wind
//   STEAM    a jet out of a pipe: fast, white, spreading and slowing hard,
//            gone in under a second
//   SPARKS   a burst from the grinder: hot streaks thrown up that fall under
//            gravity and bounce on the table

#include "toolbox.h"

#define X0 -23.0f

static const ParticleSpec FLAME = {
    .sprite = PSPRITE_FLAME, .blend = PBLEND_ADD, .rate = 45, .life_min = 0.45f, .life_max = 0.8f,
    .velocity = { 0, 0.7f, 0 }, .spread_deg = 12, .speed_jitter = 0.25f, .spawn_radius = 0.09f,
    .size_start = 0.2f, .size_end = 0.05f,
    .color_start = { 255, 235, 170, 230 }, .color_mid = { 255, 130, 40, 170 }, .color_end = { 120, 25, 5, 0 },
    .gravity = -0.8f, .drag = 0.6f, .wind = 0.4f, .spin = 1.5f,
};
static const ParticleSpec EMBERS = {
    .sprite = PSPRITE_SPARK, .blend = PBLEND_ADD, .rate = 6, .life_min = 1.2f, .life_max = 2.4f,
    .velocity = { 0, 1.0f, 0 }, .spread_deg = 25, .speed_jitter = 0.4f, .spawn_radius = 0.1f,
    .size_start = 0.025f, .size_end = 0.012f,
    .color_start = { 255, 210, 130, 255 }, .color_mid = { 255, 120, 30, 220 }, .color_end = { 200, 40, 0, 0 },
    .gravity = -0.3f, .drag = 0.4f, .wind = 0.8f,
};
static const ParticleSpec FIRE_SMOKE = {
    .sprite = PSPRITE_PUFF, .blend = PBLEND_ALPHA, .rate = 7, .life_min = 2.5f, .life_max = 3.5f,
    .velocity = { 0, 0.5f, 0 }, .spread_deg = 15, .speed_jitter = 0.2f, .spawn_radius = 0.08f,
    .size_start = 0.2f, .size_end = 0.8f,
    .color_start = { 45, 45, 45, 120 }, .color_mid = { 75, 75, 75, 70 }, .color_end = { 110, 110, 110, 0 },
    .gravity = -0.15f, .drag = 0.4f, .wind = 1.0f, .spin = 0.4f,
};
static const ParticleSpec CHIMNEY = {
    .sprite = PSPRITE_PUFF, .blend = PBLEND_ALPHA, .rate = 12, .life_min = 5, .life_max = 7,
    .velocity = { 0, 0.9f, 0 }, .spread_deg = 10, .speed_jitter = 0.2f, .spawn_radius = 0.12f,
    .size_start = 0.3f, .size_end = 1.5f,
    .color_start = { 38, 38, 42, 200 }, .color_mid = { 62, 62, 66, 120 }, .color_end = { 95, 95, 100, 0 },
    .gravity = -0.1f, .drag = 0.3f, .wind = 1.0f, .spin = 0.3f,
};
static const ParticleSpec STEAM = {
    .sprite = PSPRITE_PUFF, .blend = PBLEND_ALPHA, .rate = 60, .life_min = 0.5f, .life_max = 0.9f,
    .velocity = { 0, 0, -2.4f }, .spread_deg = 8, .speed_jitter = 0.25f, .spawn_radius = 0.008f,   // out of the nozzle (-Z)
    .size_start = 0.03f, .size_end = 0.4f,
    .color_start = { 255, 255, 255, 170 }, .color_mid = { 232, 236, 242, 90 }, .color_end = { 220, 225, 232, 0 },
    .gravity = -0.6f, .drag = 2.2f, .wind = 0.6f, .spin = 0.8f,
};
static const ParticleSpec SPARKS = {
    .sprite = PSPRITE_SPARK, .blend = PBLEND_ADD, .rate = 0, .life_min = 0.5f, .life_max = 1.1f,
    .velocity = { 0, 1.8f, 0 }, .spread_deg = 70, .speed_jitter = 0.6f, .spawn_radius = 0.005f,
    .size_start = 0.02f, .size_end = 0.01f,
    .color_start = { 255, 255, 225, 255 }, .color_mid = { 255, 190, 80, 255 }, .color_end = { 255, 90, 20, 0 },
    .gravity = 9.8f, .drag = 0.4f, .stretch = true, .bounce = true, .floor_y = TABLE_Y,
    .floor = { X0 - 0.7f, ROW_Z - 0.3f, 1.4f, 0.6f },   // the table top
};

static struct {
    bool ready, fire, smoke, steam;
    float wind;                    // 0..1 on the slider: up to 1.5 m/s toward +X
    int flame, embers, fire_smoke, chimney, steam_e, sparks;
} PS = { .fire = true, .smoke = true, .steam = true, .wind = 0.3f };

static SfxrPose at(float x, float y, float z) { return (SfxrPose){ { x, y, z }, QuaternionIdentity() }; }
static SfxrPose on_table(float x, float z_off) { return at(X0 + x, TABLE_Y, ROW_Z + z_off); }

#define FIRE_AT      (Vector3){ X0 - 1.3f, 0.12f, ROW_Z + 0.1f }
#define CHIMNEY_TOP  (Vector3){ X0 - 1.6f, 2.4f, ROW_Z - 1.3f }
#define NOZZLE       (Vector3){ X0 + 0.62f, TABLE_Y + 0.25f, ROW_Z - 0.15f }
#define GRINDER      (Vector3){ X0 + 0.1f, TABLE_Y + 0.06f, ROW_Z - 0.1f }

static void setup(void)
{
    PS.flame = particles_emitter(&FLAME, at(FIRE_AT.x, FIRE_AT.y, FIRE_AT.z));
    PS.embers = particles_emitter(&EMBERS, at(FIRE_AT.x, FIRE_AT.y, FIRE_AT.z));
    PS.fire_smoke = particles_emitter(&FIRE_SMOKE, at(FIRE_AT.x, FIRE_AT.y + 0.45f, FIRE_AT.z));
    PS.chimney = particles_emitter(&CHIMNEY, at(CHIMNEY_TOP.x, CHIMNEY_TOP.y, CHIMNEY_TOP.z));
    // the nozzle points along +X: turn the jet's -Z that way
    PS.steam_e = particles_emitter(&STEAM, (SfxrPose){ NOZZLE, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, -PI / 2) });
    PS.sparks = particles_emitter(&SPARKS, at(GRINDER.x, GRINDER.y, GRINDER.z));
    PS.ready = true;
}

// The props, which never move: scenery (graphics.c batches it).
static void scenery(void)
{
    Color wood = { 120, 92, 66, 255 }, legs = { 90, 68, 52, 255 }, iron = { 70, 72, 78, 255 }, brick = { 150, 80, 60, 255 };
    scenery_box(at(X0, TABLE_Y - 0.025f, ROW_Z), (Vector3){ 1.4f, 0.05f, 0.6f }, wood, MAT_WOOD);
    for (int i = 0; i < 4; i++)
        scenery_box(at(X0 + ((i & 1) ? 0.62f : -0.62f), (TABLE_Y - 0.05f) * 0.5f, ROW_Z + ((i & 2) ? 0.24f : -0.24f)),
                    (Vector3){ 0.05f, TABLE_Y - 0.05f, 0.05f }, legs, MAT_WOOD);
    // the fire pit: a ring of stones round two crossed logs
    for (int i = 0; i < 9; i++) {
        float a = (float)i / 9 * 2 * PI;
        scenery_box((SfxrPose){ { FIRE_AT.x + cosf(a) * 0.32f, 0.06f, FIRE_AT.z + sinf(a) * 0.32f },
                                QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, -a) },
                    (Vector3){ 0.14f, 0.12f, 0.2f }, (Color){ 120, 118, 112, 255 }, MAT_STONE);
    }
    Vector3 f = FIRE_AT;
    scenery_cylinder((Vector3){ f.x - 0.25f, 0.05f, f.z - 0.08f }, (Vector3){ f.x + 0.25f, 0.08f, f.z + 0.08f }, 0.05f, (Color){ 90, 62, 40, 255 }, MAT_WOOD);
    scenery_cylinder((Vector3){ f.x - 0.08f, 0.08f, f.z + 0.25f }, (Vector3){ f.x + 0.08f, 0.05f, f.z - 0.25f }, 0.05f, (Color){ 80, 55, 36, 255 }, MAT_WOOD);
    // the chimney
    Vector3 c = CHIMNEY_TOP;
    scenery_box(at(c.x, c.y * 0.5f - 0.05f, c.z), (Vector3){ 0.45f, c.y - 0.1f, 0.45f }, brick, MAT_STONE);
    scenery_box(at(c.x, c.y - 0.05f, c.z), (Vector3){ 0.52f, 0.1f, 0.52f }, (Color){ 110, 60, 45, 255 }, MAT_STONE);
    // the steam pipe: up out of the table, then along to the nozzle
    Vector3 n = NOZZLE;
    scenery_cylinder((Vector3){ n.x - 0.3f, TABLE_Y + 0.05f, n.z }, (Vector3){ n.x - 0.3f, n.y, n.z }, 0.035f, iron, MAT_PAINT);
    scenery_cylinder((Vector3){ n.x - 0.3f, n.y, n.z }, (Vector3){ n.x, n.y, n.z }, 0.035f, iron, MAT_PAINT);
    scenery_cylinder((Vector3){ n.x - 0.02f, n.y, n.z }, (Vector3){ n.x + 0.01f, n.y, n.z }, 0.045f, (Color){ 150, 120, 60, 255 }, MAT_PAINT);   // brass
    // the grinder: a block and its stone wheel
    Vector3 g = GRINDER;
    scenery_box(at(g.x - 0.12f, TABLE_Y + 0.06f, g.z), (Vector3){ 0.14f, 0.12f, 0.12f }, iron, MAT_PAINT);
    scenery_cylinder((Vector3){ g.x - 0.03f, g.y + 0.03f, g.z }, (Vector3){ g.x + 0.0f, g.y + 0.03f, g.z }, 0.07f, (Color){ 150, 150, 155, 255 }, MAT_STONE);
}

void station_particles(void)
{
    if (!PS.ready) setup();
    station_sign(X0, "Particles", "fire, smoke, steam and sparks: little pictures that face you, drift, grow and fade");
    scenery();

    vrui_switch(VRUI_ID2(G_PARTICLES, 1), on_table(-0.5f, 0.2f), &PS.fire, "FIRE");
    vrui_switch(VRUI_ID2(G_PARTICLES, 2), on_table(-0.3f, 0.2f), &PS.smoke, "SMOKE");
    vrui_switch(VRUI_ID2(G_PARTICLES, 3), on_table(-0.1f, 0.2f), &PS.steam, "STEAM");
    if (vrui_push_button(VRUI_ID2(G_PARTICLES, 4), on_table(0.12f, 0.2f), 0.03f, (Color){ 240, 160, 40, 255 }, "SPARKS"))
        particles_burst(PS.sparks, 60);
    vrui_slider3d(VRUI_ID2(G_PARTICLES, 5), on_table(0.42f, 0.2f), 0.25f, &PS.wind, "WIND");

    particles_set_on(PS.flame, PS.fire);
    particles_set_on(PS.embers, PS.fire);
    particles_set_on(PS.fire_smoke, PS.fire);
    particles_set_on(PS.chimney, PS.smoke);
    particles_set_on(PS.steam_e, PS.steam);
    particles_wind((Vector3){ PS.wind * 1.5f, 0, 0 });

    const ParticleStats *st = particles_stats();
    vrui_text_at((SfxrPose){ { X0 - 0.35f, TABLE_Y + 0.14f, ROW_Z - 0.28f }, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -15.0f * DEG2RAD) },
                 TextFormat("%d particles\n%d drawn, %d culled", st->alive, st->drawn, st->culled), 0.035f, RAYWHITE);

    // what tests check
    sfxr_report("fire_particles", (float)particles_alive(PS.flame));
    sfxr_report("sparks_alive", (float)particles_alive(PS.sparks));
    sfxr_report("particles", (float)st->alive);
    sfxr_report("spark_bounces", (float)particles_bounces(PS.sparks));
    Vector3 hi;
    Vector3 lo = particles_bounds(PS.chimney, &hi);
    sfxr_report("smoke_drift", particles_alive(PS.chimney) ? (lo.x + hi.x) * 0.5f - CHIMNEY_TOP.x : 0);
}
