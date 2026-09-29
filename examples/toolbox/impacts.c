// impacts.c - one call when something hits something: the right sound for
// the surface, a puff of the right dust (or sparks), and a buzz in the hand
// if a hand is behind it -- all scaled by how hard. Most of what makes
// hitting things feel good ("game feel") is these three together, in the
// same frame, in proportion.
//
//   impact(hit.point, hit.normal, speed, hit.tag, hand);   // hand -1: nobody's
//
// The surface is the collider's tag (vrui_collider_*, graphics.c tags the
// scenery with its material). Below 0.6 m/s nothing happens: resting things
// jostle, and a clatter on every tiny touch is noise.

#include "toolbox.h"

static struct {
    bool ready;
    int dust[MAT_COUNT], sparks;
    int total;
} I;

static const ParticleSpec DUST = {
    .sprite = PSPRITE_PUFF, .blend = PBLEND_ALPHA, .rate = 0, .life_min = 0.4f, .life_max = 0.8f,
    .velocity = { 0, 0.5f, 0 }, .spread_deg = 70, .speed_jitter = 0.5f, .spawn_radius = 0.03f,
    .size_start = 0.04f, .size_end = 0.16f,
    .color_start = { 150, 140, 120, 150 }, .color_mid = { 150, 140, 120, 80 }, .color_end = { 150, 140, 120, 0 },
    .gravity = 0.3f, .drag = 3.0f, .wind = 0.3f, .spin = 1.0f,
};
static const ParticleSpec SPARKS = {
    .sprite = PSPRITE_SPARK, .blend = PBLEND_ADD, .rate = 0, .life_min = 0.2f, .life_max = 0.5f,
    .velocity = { 0, 1.6f, 0 }, .spread_deg = 75, .speed_jitter = 0.5f,
    .size_start = 0.015f, .size_end = 0.008f,
    .color_start = { 255, 250, 220, 255 }, .color_mid = { 255, 180, 70, 255 }, .color_end = { 255, 80, 20, 0 },
    .gravity = 9.8f, .drag = 0.5f, .stretch = true,
};

static void setup(void)
{
    // dust the color of what was hit: dirt from the ground, pale wood, grey stone
    static const Color TINT[MAT_COUNT] = { { 150, 140, 120, 0 }, { 150, 150, 150, 0 }, { 185, 160, 120, 0 },
                                           { 120, 105, 75, 0 },  { 160, 158, 150, 0 }, { 220, 235, 245, 0 } };
    for (int m = 0; m < MAT_COUNT; m++) {
        ParticleSpec s = DUST;
        s.color_start = s.color_mid = s.color_end = TINT[m];
        s.color_start.a = 160; s.color_mid.a = 90; s.color_end.a = 0;
        if (m == MAT_WATER) { s.gravity = 6; s.velocity.y = 1.4f; s.drag = 1; s.size_end = 0.06f; }   // a splash
        I.dust[m] = particles_emitter(&s, (SfxrPose){ { 0 }, QuaternionIdentity() });
        particles_set_on(I.dust[m], false);
    }
    I.sparks = particles_emitter(&SPARKS, (SfxrPose){ { 0 }, QuaternionIdentity() });
    I.ready = true;
}

void impact(Vector3 at, Vector3 normal, float speed, int surface, int hand)
{
    if (speed < 0.6f) return;
    if (!I.ready) setup();
    int m = surface >= 0 && surface < MAT_COUNT ? surface : MAT_GRASS;
    float k = Clamp((speed - 0.6f) / 5.0f, 0, 1);   // 0 a tap .. 1 a hard hit (about 6 m/s)

    // sound: what the surface sounds like, louder and a little lower when harder
    static const SoundId SOUND[MAT_COUNT] = { SND_THUMP, SND_CLANK, SND_KNOCK, SND_THUMP, SND_CLACK, SND_SQUISH };
    SoundId snd = SOUND[m];
    sound_pitch(snd, 1.1f - 0.2f * k);
    sound_play(snd, at, 0.3f + 0.7f * k);

    // dust (or sparks off metal), a few for a tap, more for a hard hit,
    // thrown out along the surface's facing
    Quaternion up_to_normal = QuaternionFromVector3ToVector3((Vector3){ 0, 1, 0 }, normal);
    int e = m == MAT_PAINT && k > 0.3f ? I.sparks : I.dust[m];
    particles_move(e, (SfxrPose){ Vector3Add(at, Vector3Scale(normal, 0.01f)), up_to_normal });
    particles_burst(e, 3 + (int)(k * 14));

    // the hand that did it feels it
    if (hand >= 0) vrui_haptic_pulse((SfxrHandId)hand, 0.25f + 0.75f * k, 0.03f + 0.03f * k, 0);
    I.total++;
    sfxr_event("impact", "%s %.1f m/s", (const char *[]){ "ground", "metal", "wood", "grass", "stone", "water" }[m], speed);
}

int impacts_total(void) { return I.total; }
