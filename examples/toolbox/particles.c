// particles.c - a small particle system: fire, smoke, steam, sparks. The
// pattern nearly every game engine uses (Half-Life 2's env_fire,
// env_smokestack, env_steam and env_spark are the same idea):
//
//   An EMITTER sits somewhere and spawns PARTICLES at a steady rate (or in a
//   burst). Each particle is a point with a velocity and an age. Every frame
//   it moves, feels gravity (or buoyancy: smoke falls UP), drag and wind, and
//   gets older; over its life its size and color follow a ramp (a flame
//   starts small, yellow and bright and ends red and gone). Past its life it
//   dies and its slot is reused.
//
//   Each particle is drawn as a SPRITE: a small square picture (a soft puff,
//   a flame tongue, a spark) that always turns to face you (a "billboard"),
//   so a flat picture reads as a round puff from every side. Hundreds of them
//   overlapping make the volume.
//
// Two ways of laying them over the world:
//   ALPHA     smoke and steam: each puff partly hides what's behind. The
//             order matters (far puffs must go down first), so they're sorted.
//   ADDITIVE  fire and sparks: each adds its light to what's behind, so the
//             order doesn't matter and overlaps glow brighter, like flame.
// Neither writes depth (a see-through puff mustn't hide the puff behind it),
// but both test it (a puff behind a table stays hidden).
//
// What costs: each particle is 4 corners the CPU works out every frame
// (cheap: a few hundred is nothing), but every pixel of every puff is drawn
// once per puff covering it ("overdraw"). Big smoke puffs stacked ten deep
// over half the view are the real cost on a mobile GPU, so smoke stays sparse
// and its puffs grow instead of multiplying.

#include "toolbox.h"
#include "rlgl.h"
#include "sfxr_break.h"

#define BREAK SFXR_BREAK_DECLARE
#include "toolbox_breaks.def"
#undef BREAK

#include <stdlib.h>
#include <string.h>

#define MAX_EMITTERS  16
#define MAX_PARTICLES 2048

typedef struct {
    Vector3 pos, vel;
    float age, life, size0, angle, spin;
    int emitter;
    bool alive;
} Particle;

typedef struct {
    ParticleSpec spec;
    SfxrPose pose;
    bool on, used;
    float owed;               // spawns owed from the rate (fractions carry over)
    int alive, bounces;
    Vector3 lo, hi;           // bounds of its live particles (culling)
    bool seen;
} Emitter;

static struct {
    Emitter em[MAX_EMITTERS];
    Particle p[MAX_PARTICLES];
    int count;
    Texture2D sprite[PSPRITE_COUNT];
    unsigned rng;
    Vector3 wind;
    ParticleStats stats;
} P;

// Our own random numbers, the same sequence every run: replays and tests must
// see the same smoke (raylib's are seeded from the clock).
static float rnd(void)   // 0..1
{
    P.rng = P.rng * 1664525u + 1013904223u;
    return (float)(P.rng >> 8) / 16777216.0f;
}
static float rnd_range(float a, float b) { return a + (b - a) * rnd(); }

// ---------------------------------------------------------------------------
// Sprites: made at startup, soft round pictures with see-through edges
// ---------------------------------------------------------------------------

static Texture2D make_sprite(ParticleSprite kind)
{
    const int S = 64;
    Image img = GenImageColor(S, S, BLANK);
    Image noise = GenImagePerlinNoise(S, S, kind * 50, 0, 3.0f);   // cloudy lumps for the puff
    Color *n = LoadImageColors(noise);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float u = ((float)x + 0.5f) / S * 2 - 1, v = ((float)y + 0.5f) / S * 2 - 1;   // -1..1
            float r = sqrtf(u * u + v * v), a = 0;
            switch (kind) {
            case PSPRITE_PUFF:    // a soft ball, lumpy: denser where the noise is
                a = Clamp(1 - r, 0, 1);
                a = a * a * (0.55f + 0.9f * (float)n[y * S + x].r / 255.0f);
                break;
            case PSPRITE_FLAME: { // brighter toward the bottom, a tongue narrowing upward
                float w = 1 - Clamp((1 - v) * 0.35f, 0, 0.6f);   // (v = 1 is the bottom row)
                float rr = sqrtf((u / w) * (u / w) + v * v);
                a = Clamp(1 - rr, 0, 1);
                a = a * a * (0.7f + 0.6f * (float)n[y * S + x].r / 255.0f);
                break;
            }
            default:              // a spark: a hot pinpoint with a small glow
                a = Clamp(1 - r, 0, 1);
                a = a * a * a;
                break;
            }
            ImageDrawPixel(&img, x, y, (Color){ 255, 255, 255, (unsigned char)(Clamp(a, 0, 1) * 255) });
        }
    UnloadImageColors(n);
    UnloadImage(noise);
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    return t;
}

void particles_init(void)
{
    memset(&P, 0, sizeof P);
    P.rng = 12345;
    for (int k = 0; k < PSPRITE_COUNT; k++) P.sprite[k] = make_sprite((ParticleSprite)k);
}

// ---------------------------------------------------------------------------
// Emitters
// ---------------------------------------------------------------------------

int particles_emitter(const ParticleSpec *spec, SfxrPose pose)
{
    for (int e = 0; e < MAX_EMITTERS; e++)
        if (!P.em[e].used) {
            P.em[e] = (Emitter){ .spec = *spec, .pose = pose, .on = true, .used = true };
            return e;
        }
    return -1;
}

void particles_set_on(int e, bool on)          { if (e >= 0) P.em[e].on = on; }
void particles_move(int e, SfxrPose pose)      { if (e >= 0) P.em[e].pose = pose; }
void particles_wind(Vector3 wind)              { P.wind = wind; }
int  particles_alive(int e)                    { return e >= 0 ? P.em[e].alive : 0; }
int  particles_bounces(int e)                  { return e >= 0 ? P.em[e].bounces : 0; }
Vector3 particles_bounds(int e, Vector3 *hi)   { if (hi) *hi = P.em[e].hi; return P.em[e].lo; }
const ParticleStats *particles_stats(void)     { return &P.stats; }

static void spawn(int e)
{
    const Emitter *em = &P.em[e];
    const ParticleSpec *s = &em->spec;
    int slot = -1;
    for (int i = 0; i < P.count; i++) if (!P.p[i].alive) { slot = i; break; }
    if (slot < 0) {
        if (P.count >= MAX_PARTICLES) return;   // full: skip (better than stealing a live one)
        slot = P.count++;
    }
    // a direction within `spread` degrees of the emitter's velocity
    Vector3 v = s->velocity;
    float speed = Vector3Length(v) * (1 + rnd_range(-s->speed_jitter, s->speed_jitter));
    Vector3 dir = speed > 0 ? Vector3Normalize(v) : (Vector3){ 0, 1, 0 };
    Vector3 side = Vector3Normalize(Vector3CrossProduct(dir, fabsf(dir.y) < 0.9f ? (Vector3){ 0, 1, 0 } : (Vector3){ 1, 0, 0 }));
    Quaternion tilt = QuaternionMultiply(QuaternionFromAxisAngle(dir, rnd_range(0, 2 * PI)),
                                         QuaternionFromAxisAngle(side, rnd_range(0, s->spread_deg) * DEG2RAD));
    dir = Vector3RotateByQuaternion(dir, tilt);
    // where: the emitter, plus a random offset in a disc (or along a line)
    Vector3 off = { rnd_range(-1, 1) * s->spawn_radius, 0, rnd_range(-1, 1) * s->spawn_radius };
    off = Vector3Add(off, Vector3Scale(s->spawn_line, rnd()));
    P.p[slot] = (Particle){
        .pos = sfxr_pose_apply(em->pose, off),
        .vel = Vector3RotateByQuaternion(Vector3Scale(dir, speed), em->pose.orientation),
        .life = rnd_range(s->life_min, s->life_max),
        .size0 = 1 + rnd_range(-0.25f, 0.25f),
        .angle = rnd_range(0, 2 * PI),
        .spin = rnd_range(-s->spin, s->spin),
        .emitter = e,
        .alive = true,
    };
}

void particles_burst(int e, int n)
{
    if (e < 0) return;
    for (int i = 0; i < n; i++) spawn(e);
}

// ---------------------------------------------------------------------------
// Every frame: spawn, move, age
// ---------------------------------------------------------------------------

void particles_update(float dt)
{
    for (int e = 0; e < MAX_EMITTERS; e++) {
        Emitter *em = &P.em[e];
        if (!em->used) continue;
        if (em->on) {
            em->owed += em->spec.rate * dt;
            while (em->owed >= 1) { spawn(e); em->owed -= 1; }
        }
        em->alive = 0;
        em->lo = (Vector3){ 1e9f, 1e9f, 1e9f };
        em->hi = (Vector3){ -1e9f, -1e9f, -1e9f };
    }
    int alive = 0;
    for (int i = 0; i < P.count; i++) {
        Particle *p = &P.p[i];
        if (!p->alive) continue;
        p->age += dt;
        if (p->age >= p->life) { p->alive = false; continue; }
        Emitter *em = &P.em[p->emitter];
        const ParticleSpec *s = &em->spec;
        // forces: gravity (negative: it rises, like hot smoke), drag slowing
        // it, and the wind pulling it along
        p->vel.y -= s->gravity * dt;
        p->vel = Vector3Scale(p->vel, 1.0f / (1.0f + s->drag * dt));
        if (!SFXR_BREAK(toolbox_particles_ignore_wind)) {
            float k = Clamp(s->wind * dt, 0, 1);
            p->vel.x += (P.wind.x - p->vel.x) * k;
            p->vel.z += (P.wind.z - p->vel.z) * k;
        }
        p->pos = Vector3Add(p->pos, Vector3Scale(p->vel, dt));
        p->angle += p->spin * dt;
        // the floor it bounces on: the table top over the table, else the ground
        bool over = p->pos.x > s->floor.x && p->pos.x < s->floor.x + s->floor.width &&
                    p->pos.z > s->floor.y && p->pos.z < s->floor.y + s->floor.height;
        float floor_y = over && !SFXR_BREAK(toolbox_sparks_fall_through) ? s->floor_y : 0;
        if (s->bounce && p->pos.y < floor_y && p->pos.y > floor_y - 0.1f) {
            if (floor_y > 0) em->bounces++;
            p->pos.y = floor_y;
            p->vel.y = -p->vel.y * 0.35f;   // a bounce loses most of its speed
            p->vel.x *= 0.6f;
            p->vel.z *= 0.6f;
        }
        em->alive++;
        em->lo = Vector3Min(em->lo, p->pos);
        em->hi = Vector3Max(em->hi, p->pos);
        alive++;
    }
    P.stats.alive = alive;
}

// Over a particle's life, t = 0..1: the color runs start -> mid -> end.
static Color ramp(const ParticleSpec *s, float t)
{
    return t < 0.5f ? ColorLerp(s->color_start, s->color_mid, t * 2) : ColorLerp(s->color_mid, s->color_end, t * 2 - 1);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static Vector3 g_right, g_up, g_eye;
static float g_light;

static void quad(const Particle *p)
{
    const ParticleSpec *s = &P.em[p->emitter].spec;
    float t = p->age / p->life;
    float size = Lerp(s->size_start, s->size_end, t) * p->size0;
    Color c = ramp(s, t);
    if (s->blend == PBLEND_ALPHA) {   // smoke and steam take the scene's light; fire makes its own
        c.r = (unsigned char)(c.r * g_light);
        c.g = (unsigned char)(c.g * g_light);
        c.b = (unsigned char)(c.b * g_light);
    }
    Vector3 a, b;   // the sprite's half-width and half-height, across the view
    if (s->stretch) {
        // a spark: a streak along its motion (as a camera shutter would
        // smear a fast glowing thing), across the view
        Vector3 axis = Vector3Normalize(p->vel);
        Vector3 to_eye = Vector3Normalize(Vector3Subtract(g_eye, p->pos));
        Vector3 across = Vector3Normalize(Vector3CrossProduct(axis, to_eye));
        float len = size + Vector3Length(p->vel) * 0.025f;
        a = Vector3Scale(across, size * 0.5f);
        b = Vector3Scale(axis, len * 0.5f);
    } else {
        // a billboard: the view's right and up, turned by the particle's spin
        float cs = cosf(p->angle), sn = sinf(p->angle);
        a = Vector3Scale(Vector3Add(Vector3Scale(g_right, cs), Vector3Scale(g_up, sn)), size * 0.5f);
        b = Vector3Scale(Vector3Add(Vector3Scale(g_up, cs), Vector3Scale(g_right, -sn)), size * 0.5f);
    }
    Vector3 q[4] = { Vector3Subtract(Vector3Subtract(p->pos, a), b), Vector3Subtract(Vector3Add(p->pos, a), b),
                     Vector3Add(Vector3Add(p->pos, a), b), Vector3Add(Vector3Subtract(p->pos, a), b) };
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlTexCoord2f(0, 1); rlVertex3f(q[0].x, q[0].y, q[0].z);
    rlTexCoord2f(1, 1); rlVertex3f(q[1].x, q[1].y, q[1].z);
    rlTexCoord2f(1, 0); rlVertex3f(q[2].x, q[2].y, q[2].z);
    rlTexCoord2f(0, 0); rlVertex3f(q[3].x, q[3].y, q[3].z);
}

static int by_distance(const void *a, const void *b)   // far first
{
    float da = Vector3DistanceSqr(P.p[*(const int *)a].pos, g_eye), db = Vector3DistanceSqr(P.p[*(const int *)b].pos, g_eye);
    return da < db ? 1 : da > db ? -1 : 0;
}

void particles_draw(float light)
{
    SfxrPose head = sfxr_head();
    g_right = sfxr_pose_right(head);
    g_up = sfxr_pose_up(head);
    g_eye = head.position;
    g_light = light;
    P.stats.drawn = P.stats.culled = 0;

    // culling: a whole emitter's cloud at a time
    for (int e = 0; e < MAX_EMITTERS; e++) {
        Emitter *em = &P.em[e];
        if (!em->used || !em->alive) continue;
        Vector3 c = Vector3Scale(Vector3Add(em->lo, em->hi), 0.5f);
        float r = Vector3Distance(c, em->hi) + em->spec.size_end;
        em->seen = gfx_visible(c, r);
        if (em->seen) P.stats.drawn += em->alive; else P.stats.culled += em->alive;
    }

    static int order[MAX_PARTICLES];
    rlDrawRenderBatchActive();
    rlDisableDepthMask();          // see-through: don't hide the puffs behind
    rlDisableBackfaceCulling();    // (a spinning quad may face either way)

    // 1. alpha (smoke, steam), sorted far to near, one sprite
    int n = 0;
    for (int i = 0; i < P.count; i++)
        if (P.p[i].alive && P.em[P.p[i].emitter].seen && P.em[P.p[i].emitter].spec.blend == PBLEND_ALPHA) order[n++] = i;
    qsort(order, (size_t)n, sizeof order[0], by_distance);
    BeginBlendMode(BLEND_ALPHA);
    for (int k = 0; k < PSPRITE_COUNT; k++) {
        // (sorted order matters more than grouping by sprite: smoke uses one)
        rlSetTexture(P.sprite[k].id);
        rlBegin(RL_QUADS);
        for (int i = 0; i < n; i++)
            if (P.em[P.p[order[i]].emitter].spec.sprite == (ParticleSprite)k) {
                rlCheckRenderBatchLimit(4);
                quad(&P.p[order[i]]);
            }
        rlEnd();
    }
    EndBlendMode();

    // 2. additive (fire, sparks): any order, grouped by sprite
    BeginBlendMode(BLEND_ADDITIVE);
    for (int k = 0; k < PSPRITE_COUNT; k++) {
        rlSetTexture(P.sprite[k].id);
        rlBegin(RL_QUADS);
        for (int i = 0; i < P.count; i++) {
            const Particle *p = &P.p[i];
            if (!p->alive || !P.em[p->emitter].seen) continue;
            const ParticleSpec *s = &P.em[p->emitter].spec;
            if (s->blend != PBLEND_ADD || s->sprite != (ParticleSprite)k) continue;
            rlCheckRenderBatchLimit(4);
            quad(p);
        }
        rlEnd();
    }
    EndBlendMode();
    rlSetTexture(0);

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}
