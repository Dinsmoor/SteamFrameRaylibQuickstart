// garden_world.c - the garden level: terrain you walk on, props made in
// Blender, a sun, colliders, and a chair you can knock over (garden.h).
//
// The level is a table below (it was a JSON level file in the game this
// came from; a table is easier to read in one place). Props are models from
// resources/garden/, each standing at x, z with its base on the terrain.
//
// Walking on the terrain: at load, the terrain mesh is raycast straight down
// on a 96 x 96 grid, once. After that "how high is the ground here?" is a
// bilinear lookup in that grid, cheap enough to ask for every bug every
// frame and for the teleport arc at every step. One height per spot: no
// caves or overhangs, which a garden doesn't need.
//
// Lighting: one small shader (below) with a sun and a sky fill. Surfaces
// light by how they face the sun, which is what makes a hill read as a hill.
// It uses raylib's standard attribute and uniform names, including `mvp`,
// so sfxr's stereo rendering sets it per eye like any raylib shader.

#include "garden.h"
#include "rlgl.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define S GARDEN_SCALE

// --- the level (in the level's own units; scaled by S at load) ----------------

typedef struct {
    const char *model;
    float x, y, z;          // y: the terrain height under it
    float sx, sy, sz;       // size: the collider (and the chair's box)
    float yaw, scale;
    bool collide;
    float trunk;            // collider radius as a fraction of the footprint (a tree: its trunk)
    bool loose;             // a rigid body: knocked over when hit or walked into
    float mass;
} PropDef;

static const PropDef PROPS[] = {
    { "tower.glb", 16,   0.663f, -14, 3.6f, 13.8f, 3.6f, 180, 1,    true, 0.9f },
    { "tree.glb",  -12,  3.66f,  -8,  2.2f, 7.3f,  2.2f, 0,   1,    true, 0.18f },
    { "tree.glb",  -18,  1.487f,  6,  1.87f, 6.21f, 1.87f, 40, 0.85f, true, 0.18f },
    { "tree.glb",  20,   1.556f,  8,  2.42f, 8.03f, 2.42f, 200, 1.1f, true, 0.18f },
    { "bush.glb",  -4,   0.737f, -2,  1.5f, 1.54f, 1.5f, 0,   1,    true, 0.7f },
    { "bush.glb",  9,    1.745f, -6,  1.35f, 1.39f, 1.35f, 90, 0.9f, true, 0.7f },
    { "rock.glb",  6,    0.344f,  5,  0.94f, 1.2f, 0.94f, 25, 1,    true, 0.9f },
    { "rock.glb",  -8,   0.171f, 12,  1.22f, 1.56f, 1.22f, 110, 1.3f, true, 0.9f },
    { "table.glb", 6,    0.682f,  0,  1,    1.09f, 1,    0,   1,    true, 0.9f },
    { "chair.glb", 6,    0.977f, -1.4f, 0.45f, 1.5f, 0.45f, 180, 1,  false, 0, true, 1 },
};
#define NPROPS ((int)(sizeof PROPS / sizeof PROPS[0]))
#define GROUND_SIZE 60.0f   // level units, square, centered

// --- state --------------------------------------------------------------------

typedef struct { float x, z, r, y0, y1; } Collider;   // a vertical cylinder (m)

static struct {
    bool loaded, failed;
    Model terrain;
    Model models[NPROPS];
    Collider col[NPROPS];
    int ncol;
    RigidBody body[NPROPS];     // the loose props
    float floor_y[NPROPS];
    Shader lit;
    bool lit_ok;
} G;

#define GRID 96
static float height[GRID][GRID];
static float gx0, gz0, gw, gd;   // the grid's extent (m)

// --- where the files are --------------------------------------------------------

const char *gw_path(const char *file)
{
    // Next to the app (a package: resources/garden/ beside the binary), then
    // the source tree from the repo root (make run), then from build/<target>/bin.
    static char buf[PATH_MAX + 64];
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = 0;
    char *slash = strrchr(exe, '/');
    if (slash) *slash = 0;
    for (int i = 0; i < 3; i++) {
        if (i == 0) snprintf(buf, sizeof buf, "%s/resources/garden/%s", exe, file);
        if (i == 1) snprintf(buf, sizeof buf, "examples/toolbox/resources/garden/%s", file);
        if (i == 2) snprintf(buf, sizeof buf, "%s/../../../examples/toolbox/resources/garden/%s", exe, file);
        if (FileExists(buf)) return buf;
    }
    snprintf(buf, sizeof buf, "%s/resources/garden/%s", exe, file);
    return buf;
}

// --- lighting -------------------------------------------------------------------

static const char *LIT_VS =
    "#version 330\n"
    "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor;\n"
    "uniform mat4 mvp; uniform mat4 matNormal;\n"
    "out vec2 fragTexCoord; out vec4 fragColor; out vec3 fragNormal;\n"
    "void main() {\n"
    "    fragTexCoord = vertexTexCoord; fragColor = vertexColor;\n"
    "    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";
static const char *LIT_FS =
    "#version 330\n"
    "in vec2 fragTexCoord; in vec4 fragColor; in vec3 fragNormal;\n"
    "uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
    "uniform vec3 sunDir; uniform vec3 sunColor; uniform vec3 ambient;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 base = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    float d = max(dot(normalize(fragNormal), -sunDir), 0.0);\n"
    "    finalColor = vec4(base.rgb * (ambient + sunColor * d), base.a);\n"
    "}\n";

void gw_light(Model *m)
{
    if (!G.lit_ok) return;
    for (int i = 0; i < m->materialCount; i++) m->materials[i].shader = G.lit;
}

static void lighting_init(void)
{
    G.lit = LoadShaderFromMemory(LIT_VS, LIT_FS);
    G.lit_ok = G.lit.id != 0 && G.lit.id != rlGetShaderIdDefault();
    if (!G.lit_ok) { TraceLog(LOG_WARNING, "GARDEN: lighting shader failed: drawing unlit"); return; }
    Vector3 sun = Vector3Normalize((Vector3){ -0.4f, -0.85f, -0.35f });   // down and to one side
    Vector3 sun_col = { 0.75f, 0.72f, 0.65f }, sky = { 0.42f, 0.44f, 0.5f };
    SetShaderValue(G.lit, GetShaderLocation(G.lit, "sunDir"), &sun, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, GetShaderLocation(G.lit, "sunColor"), &sun_col, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, GetShaderLocation(G.lit, "ambient"), &sky, SHADER_UNIFORM_VEC3);
}

// --- terrain heights ------------------------------------------------------------

static void bake_heights(void)
{
    BoundingBox bb = GetMeshBoundingBox(G.terrain.meshes[0]);
    for (int m = 1; m < G.terrain.meshCount; m++) {
        BoundingBox b = GetMeshBoundingBox(G.terrain.meshes[m]);
        bb.min = Vector3Min(bb.min, b.min);
        bb.max = Vector3Max(bb.max, b.max);
    }
    bb.min = Vector3Scale(bb.min, S);   // the mesh is in level units; the model's transform scales it
    bb.max = Vector3Scale(bb.max, S);
    gx0 = bb.min.x; gz0 = bb.min.z;
    gw = fmaxf(bb.max.x - bb.min.x, 0.01f);
    gd = fmaxf(bb.max.z - bb.min.z, 0.01f);
    for (int i = 0; i < GRID; i++)
        for (int j = 0; j < GRID; j++) {
            Ray down = { { gx0 + gw * (float)i / (GRID - 1), 100.0f, gz0 + gd * (float)j / (GRID - 1) }, { 0, -1, 0 } };
            float best = 0;
            for (int m = 0; m < G.terrain.meshCount; m++) {
                RayCollision rc = GetRayCollisionMesh(down, G.terrain.meshes[m], G.terrain.transform);
                if (rc.hit && rc.point.y > best) best = rc.point.y;
            }
            height[i][j] = best;
        }
}

float gw_ground(float x, float z)
{
    if (!G.loaded) return 0;
    float fi = Clamp((x - gx0) / gw * (GRID - 1), 0, GRID - 1), fj = Clamp((z - gz0) / gd * (GRID - 1), 0, GRID - 1);
    int i0 = (int)fi, j0 = (int)fj, i1 = i0 < GRID - 1 ? i0 + 1 : i0, j1 = j0 < GRID - 1 ? j0 + 1 : j0;
    float tx = fi - (float)i0, tz = fj - (float)j0;
    float a = Lerp(height[i0][j0], height[i1][j0], tx), b = Lerp(height[i0][j1], height[i1][j1], tx);
    return Lerp(a, b, tz);
}

float gw_half_size(void) { return GROUND_SIZE * 0.5f * S; }

// --- loading --------------------------------------------------------------------

static Vector3 half_of(const PropDef *p)
{
    float k = 0.5f * S * p->scale;
    return (Vector3){ p->sx * k, p->sy * k, p->sz * k };
}

void gw_reset(void)
{
    for (int i = 0; i < NPROPS; i++) {
        const PropDef *p = &PROPS[i];
        if (!p->loose) continue;
        Vector3 h = half_of(p);
        G.floor_y[i] = p->y * S;
        rb_init(&G.body[i], (Vector3){ p->x * S, p->y * S + h.y, p->z * S }, h,
                QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, p->yaw * DEG2RAD), p->mass > 0 ? p->mass * 4 : 4);
        G.body[i].asleep = true;   // it stands still until something bumps it
    }
}

bool gw_load(void)
{
    if (G.loaded || G.failed) return G.loaded;
    lighting_init();
    G.terrain = LoadModel(gw_path("terrain.glb"));
    if (G.terrain.meshCount == 0) {
        TraceLog(LOG_WARNING, "GARDEN: %s not found: the garden needs its models (examples/toolbox/resources/garden)", gw_path("terrain.glb"));
        G.failed = true;
        return false;
    }
    G.terrain.transform = MatrixScale(S, S, S);
    gw_light(&G.terrain);
    bake_heights();

    G.ncol = 0;
    for (int i = 0; i < NPROPS; i++) {
        const PropDef *p = &PROPS[i];
        G.models[i] = LoadModel(gw_path(p->model));
        gw_light(&G.models[i]);
        if (p->collide) {
            float r = fmaxf(p->sx, p->sz) * 0.5f * p->scale * S * p->trunk;
            G.col[G.ncol++] = (Collider){ p->x * S, p->z * S, r, p->y * S - 0.5f, (p->y + p->sy * p->scale) * S };
        }
    }
    gw_reset();
    G.loaded = true;
    TraceLog(LOG_INFO, "GARDEN: loaded (%d props, %d colliders, terrain %.0f x %.0f m)", NPROPS, G.ncol, gw, gd);
    return true;
}

void gw_unload(void)
{
    if (!G.loaded) return;
    UnloadModel(G.terrain);
    for (int i = 0; i < NPROPS; i++) UnloadModel(G.models[i]);
    if (G.lit_ok) UnloadShader(G.lit);
    memset(&G, 0, sizeof G);
}

// --- collision ------------------------------------------------------------------

float gw_solid_depth(Vector3 p)
{
    float deepest = 0;
    for (int i = 0; i < G.ncol; i++) {
        const Collider *c = &G.col[i];
        if (p.y < c->y0 || p.y > c->y1) continue;
        float d = c->r - sqrtf((p.x - c->x) * (p.x - c->x) + (p.z - c->z) * (p.z - c->z));
        if (d > deepest) deepest = d;
    }
    return deepest;
}

bool gw_resolve_circle(float *x, float *z, float r)
{
    bool moved = false;
    for (int i = 0; i < G.ncol; i++) {
        const Collider *c = &G.col[i];
        float dx = *x - c->x, dz = *z - c->z, d = sqrtf(dx * dx + dz * dz), min = c->r + r;
        if (d >= min) continue;
        if (d < 1e-4f) { dx = 1; dz = 0; d = 1; }
        *x = c->x + dx / d * min;
        *z = c->z + dz / d * min;
        moved = true;
    }
    return moved;
}

// A walker at (x, z) moving at vel shoves any loose prop it overlaps, low
// down on the near side (so it tips over rather than slides), and is pushed
// back out of it.
void gw_push_loose(float *x, float *z, float r, Vector3 vel, float mass)
{
    for (int i = 0; i < NPROPS; i++) {
        if (!PROPS[i].loose) continue;
        RigidBody *b = &G.body[i];
        float pr = fmaxf(b->half.x, b->half.z);
        float dx = b->pos.x - *x, dz = b->pos.z - *z, d = sqrtf(dx * dx + dz * dz), min = pr + r;
        if (d >= min) continue;
        float nx = d > 1e-4f ? dx / d : 1, nz = d > 1e-4f ? dz / d : 0;
        float speed = fmaxf(sqrtf(vel.x * vel.x + vel.z * vel.z), 0.7f);
        Vector3 at = { *x + nx * r, b->pos.y - b->half.y * 0.33f, *z + nz * r };
        rb_apply_impulse(b, (Vector3){ nx * mass * speed * 0.6f, 0, nz * mass * speed * 0.6f }, at);
        *x -= nx * (min - d);
        *z -= nz * (min - d);
    }
}

bool gw_hit_loose(Vector3 at, Vector3 vel, float mass, float reach)
{
    bool hit = false;
    for (int i = 0; i < NPROPS; i++) {
        if (!PROPS[i].loose) continue;
        RigidBody *b = &G.body[i];
        Vector3 local = Vector3RotateByQuaternion(Vector3Subtract(at, b->pos), QuaternionInvert(b->orient));
        if (fabsf(local.x) > b->half.x + reach || fabsf(local.y) > b->half.y + reach || fabsf(local.z) > b->half.z + reach) continue;
        rb_apply_impulse(b, Vector3Scale(vel, mass), at);
        hit = true;
    }
    return hit;
}

void gw_step(float dt)
{
    for (int i = 0; i < NPROPS; i++)
        if (PROPS[i].loose) rb_step(&G.body[i], dt, 9.8f, gw_ground(G.body[i].pos.x, G.body[i].pos.z));
}

// --- drawing --------------------------------------------------------------------

Color gw_sky(void) { return (Color){ 173, 216, 230, 255 }; }

void gw_draw(void)
{
    if (!G.loaded) return;
    DrawModel(G.terrain, (Vector3){ 0 }, 1.0f, WHITE);   // its transform carries the scale
    for (int i = 0; i < NPROPS; i++) {
        const PropDef *p = &PROPS[i];
        float s = S * p->scale;
        if (p->loose) {
            // the model's origin is at its base; the body spins about its middle
            const RigidBody *b = &G.body[i];
            sfxr_push_pose(rb_pose(b));
            DrawModelEx(G.models[i], (Vector3){ 0, -b->half.y, 0 }, (Vector3){ 0, 1, 0 }, 0, (Vector3){ s, s, s }, WHITE);
            sfxr_pop_pose();
        } else {
            DrawModelEx(G.models[i], (Vector3){ p->x * S, p->y * S, p->z * S }, (Vector3){ 0, 1, 0 }, p->yaw, (Vector3){ s, s, s }, WHITE);
        }
    }
}
