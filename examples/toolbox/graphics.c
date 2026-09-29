// graphics.c - making the world nicer to be in without costing frame rate:
// one lighting shader, pixel-art textures, culling, and static batching.
// The switches for each sit on the workbench, beside the SKY lever.
//
//   the shader     resources/shaders/world.vs + world.fs (read those: they
//                  explain themselves). Sun, sky and ground light, a shine,
//                  fog, and a texture laid on by world position.
//   textures       made here at startup with raylib's image generators
//                  (GenImagePerlinNoise, GenImageCellular...), 32 x 32 pixels,
//                  with mipmaps (see make_texture).
//   culling        don't draw what neither eye can see (sfxr_in_view): vrui's
//                  shapes one by one, the scenery chunk by chunk.
//   static batching  the scenery that never moves (tables, posts, trees) is
//                  merged into a few big meshes ONCE and kept on the GPU,
//                  instead of being rebuilt on the CPU every frame.
//
// A station declares its scenery every frame, like any immediate-mode call:
//
//   scenery_box(pose, size, color, MAT_WOOD);
//
// With batching off, that draws the box this frame (the CPU works out its 36
// corners and sends them, every frame). With batching on, the first frame's
// calls are merged into meshes and later calls only count themselves: if the
// count changes, the scenery is rebuilt. So it must really be static: things
// that move stay vrui_box / DrawCube.

#include "toolbox.h"
#include "rlgl.h"
#include "sfxr_break.h"

#define BREAK SFXR_BREAK_DECLARE
#include "toolbox_breaks.def"
#undef BREAK

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DETAIL_UNIT 14        // the texture unit the pattern is bound to (raylib uses the low ones)
#define CHUNK_W     6.0f      // scenery chunks: 6 m slices along the row
#define MAX_PRIMS   1024
#define MAX_CHUNKS  128

GfxSettings gfx = { .lighting = true, .shine = true, .fog = true, .textures = true, .culling = true, .batching = true };

typedef enum { PRIM_BOX, PRIM_CYLINDER, PRIM_SPHERE } PrimKind;
typedef struct {
    PrimKind kind;
    Vector3 pos, scale;       // unit shape -> world: scale, then rotate, then move
    Quaternion rot;
    Color color;
    GfxMaterial mat;
    Vector3 center;           // bounding sphere (culling)
    float radius;
} Prim;

typedef struct {
    Mesh mesh;
    GfxMaterial mat;
    Vector3 center;
    float radius;
} Chunk;

static struct {
    bool ok;                           // the shader loaded
    Shader shader;
    Material material;                 // for the batched meshes: our shader, raylib's defaults otherwise
    Texture2D detail[MAT_COUNT];
    int loc_light, loc_shine, loc_fog, loc_sun_dir, loc_sun_color, loc_sky, loc_ground, loc_eye,
        loc_fog_color, loc_fog_density, loc_detail, loc_detail_scale, loc_detail_strength;
    Mesh unit[3];                      // a unit box, cylinder, sphere (raylib's GenMesh*)

    Prim prims[MAX_PRIMS];             // this frame's scenery calls (while recording)
    int  nprims, calls;                // recorded, and calls made this frame
    bool recording;
    Chunk chunks[MAX_CHUNKS];
    int  nchunks, built_calls, builds;
    bool built;

    GfxStats stats;
} G;

// ---------------------------------------------------------------------------
// Files: next to the app when packaged, else the source tree
// ---------------------------------------------------------------------------

static const char *resource(const char *file)
{
    static char buf[PATH_MAX + 64];
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = 0;
    char *slash = strrchr(exe, '/');
    if (slash) *slash = 0;
    const char *tries[3] = { "%s/resources/%s", "examples/toolbox/resources/%s", "%s/../../../examples/toolbox/resources/%s" };
    for (int i = 0; i < 3; i++) {
        if (i == 1) snprintf(buf, sizeof buf, tries[i], file);
        else snprintf(buf, sizeof buf, tries[i], exe, file);
        if (FileExists(buf)) return buf;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

// Blend `top` over `img` at `amount` (0..1), both grayscale.
static void blend(Image *img, Image top, float amount)
{
    ImageDraw(img, top, (Rectangle){ 0, 0, (float)top.width, (float)top.height },
              (Rectangle){ 0, 0, (float)img->width, (float)img->height }, ColorAlpha(WHITE, amount));
}

// Squeeze an image's grays toward the middle (0.5 = "leave the color alone"
// in the shader), so a pattern varies the color instead of replacing it.
static void around_middle(Image *img, float contrast)
{
    Color *px = LoadImageColors(*img);
    for (int i = 0; i < img->width * img->height; i++) {
        float v = 0.5f + ((float)px[i].r / 255.0f - 0.5f) * contrast;
        unsigned char c = (unsigned char)Clamp(v * 255.0f, 0, 255);
        px[i] = (Color){ c, c, c, 255 };
    }
    Image out = { px, img->width, img->height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    UnloadImage(*img);
    *img = out;
}

// Noise that TILES: a pattern repeated across a floor must match itself at
// its edges, or every repeat shows a seam. (raylib's GenImagePerlinNoise and
// GenImageCellular don't wrap; its white noise does, having no neighbors to
// match.) Value noise: random heights on a grid of cx x cy cells that wraps
// around, smoothly blended in between.
static float lattice(int x, int y, int cx, int cy, unsigned seed)
{
    x = ((x % cx) + cx) % cx;
    y = ((y % cy) + cy) % cy;
    unsigned h = seed ^ (unsigned)x * 374761393u ^ (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}

static float tile_noise(float u, float v, int cx, int cy, unsigned seed)   // u, v in 0..1
{
    float x = u * (float)cx, y = v * (float)cy;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - (float)x0, fy = y - (float)y0;
    fx = fx * fx * (3 - 2 * fx);   // smoothstep: no creases at the grid lines
    fy = fy * fy * (3 - 2 * fy);
    float a = Lerp(lattice(x0, y0, cx, cy, seed), lattice(x0 + 1, y0, cx, cy, seed), fx);
    float b = Lerp(lattice(x0, y0 + 1, cx, cy, seed), lattice(x0 + 1, y0 + 1, cx, cy, seed), fx);
    return Lerp(a, b, fy);
}

// A gray image of tiling noise: a few layers ("octaves"), each twice as fine
// and half as strong, so there are big patches with detail inside them.
static Image noise_image(int size, int cx, int cy, int octaves, unsigned seed)
{
    Image img = GenImageColor(size, size, GRAY);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float v = 0, amp = 1, total = 0;
            for (int o = 0; o < octaves; o++) {
                v += amp * tile_noise((float)x / size, (float)y / size, cx << o, cy << o, seed + (unsigned)o);
                total += amp;
                amp *= 0.5f;
            }
            unsigned char c = (unsigned char)(v / total * 255.0f);
            ImageDrawPixel(&img, x, y, (Color){ c, c, c, 255 });
        }
    return img;
}

// Paving stones: random points, one per cell of a wrapping grid; each pixel
// is darker the nearer it is to the edge between its two nearest points.
static Image stones_image(int size, int cells, unsigned seed)
{
    Image img = GenImageColor(size, size, GRAY);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float u = (float)x / size * cells, v = (float)y / size * cells, d1 = 9, d2 = 9;
            for (int j = -1; j <= 1; j++)
                for (int i = -1; i <= 1; i++) {
                    int cx = (int)floorf(u) + i, cy = (int)floorf(v) + j;
                    float px = (float)cx + lattice(cx, cy, cells, cells, seed), py = (float)cy + lattice(cx, cy, cells, cells, seed + 7);
                    float d = sqrtf((u - px) * (u - px) + (v - py) * (v - py));
                    if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
                }
            float edge = Clamp((d2 - d1) * 4.0f, 0, 1);   // 0 on the edge between two stones
            unsigned char c = (unsigned char)(90 + 120 * edge + 30 * lattice((int)floorf(u), (int)floorf(v), cells, cells, seed + 3));
            ImageDrawPixel(&img, x, y, (Color){ c, c, c, 255 });
        }
    return img;
}

// Each material's pattern: 64 x 64 pixels, gray. Kept gentle: a little
// variation reads as a real surface; a lot reads as noise.
static Image pattern(GfxMaterial m)
{
    const int S = 64;
    Image img;
    switch (m) {
    case MAT_WOOD: {
        // grain: noise stretched along the boards (2 cells across, 24 down),
        // then four boards each its own shade, with a soft seam between them
        img = noise_image(S, 2, 24, 2, 11);
        for (int b = 0; b < 4; b++) {
            unsigned char v = (unsigned char)(118 + 12 * ((b * 5) % 3));
            ImageDrawRectangle(&img, 0, b * 16 + 15, S, 1, (Color){ 60, 60, 60, 255 });
            Image board = GenImageColor(S, 15, (Color){ v, v, v, 255 });
            ImageDraw(&img, board, (Rectangle){ 0, 0, (float)S, 15 }, (Rectangle){ 0, (float)(b * 16), (float)S, 15 }, ColorAlpha(WHITE, 0.6f));
            UnloadImage(board);
        }
        around_middle(&img, 0.6f);
        break;
    }
    case MAT_GRASS: {   // soft patches of lighter and darker grass, and a fine speckle
        img = noise_image(S, 4, 4, 3, 21);
        Image speck = GenImageWhiteNoise(S, S, 0.5f);
        blend(&img, speck, 0.25f);
        UnloadImage(speck);
        around_middle(&img, 0.4f);
        break;
    }
    case MAT_STONE: {
        img = stones_image(S, 4, 31);
        Image speck = GenImageWhiteNoise(S, S, 0.5f);
        blend(&img, speck, 0.15f);
        UnloadImage(speck);
        around_middle(&img, 0.5f);
        break;
    }
    default:            // MAT_PAINT: painted wood, faintly uneven
        img = noise_image(S, 8, 8, 2, 41);
        around_middle(&img, 0.15f);
        break;
    }
    return img;
}

// A texture ready to be seen from any distance in VR:
//   MIPMAPS    smaller copies (16x16, 8x8... 1x1) made once. Far away, many
//              texels land in one screen pixel; without mipmaps the GPU picks
//              one at random-ish, and the ground SHIMMERS as your head moves.
//              With them it reads the copy that fits. (Also faster.)
//   TRILINEAR  blends between neighboring texels and between mip levels:
//              smooth up close, steady far away.
//   ANISOTROPIC  for surfaces seen at a slant (the floor ahead): keeps them
//              sharp instead of blurring to a smear.
//   REPEAT     the pattern tiles forever.
static Texture2D make_texture(GfxMaterial m)
{
    Image img = pattern(m);
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    rlTextureParameters(t.id, RL_TEXTURE_FILTER_ANISOTROPIC, 8);
    return t;
}

// ---------------------------------------------------------------------------
// Setup and the per-frame light
// ---------------------------------------------------------------------------

void gfx_init(void)
{
    G.unit[PRIM_BOX] = GenMeshCube(1, 1, 1);
    G.unit[PRIM_CYLINDER] = GenMeshCylinder(1, 1, 12);   // radius 1, from y 0 to y 1
    G.unit[PRIM_SPHERE] = GenMeshSphere(1, 8, 12);

    const char *vs = resource("shaders/world.vs");
    char vs_path[PATH_MAX + 64] = "";
    if (vs) snprintf(vs_path, sizeof vs_path, "%s", vs);
    const char *fs = resource("shaders/world.fs");
    if (vs && fs) G.shader = LoadShader(vs_path, fs);
    G.ok = vs && fs && G.shader.id != 0 && G.shader.id != rlGetShaderIdDefault();
    if (!G.ok) { TraceLog(LOG_WARNING, "TOOLBOX: world shader not loaded: drawing flat"); return; }

    Shader s = G.shader;
    G.loc_light = GetShaderLocation(s, "useLight");
    G.loc_shine = GetShaderLocation(s, "useShine");
    G.loc_fog = GetShaderLocation(s, "useFog");
    G.loc_sun_dir = GetShaderLocation(s, "sunDir");
    G.loc_sun_color = GetShaderLocation(s, "sunColor");
    G.loc_sky = GetShaderLocation(s, "skyLight");
    G.loc_ground = GetShaderLocation(s, "groundLight");
    G.loc_eye = GetShaderLocation(s, "eyePos");
    G.loc_fog_color = GetShaderLocation(s, "fogColor");
    G.loc_fog_density = GetShaderLocation(s, "fogDensity");
    G.loc_detail = GetShaderLocation(s, "detailTex");
    G.loc_detail_scale = GetShaderLocation(s, "detailScale");
    G.loc_detail_strength = GetShaderLocation(s, "detailStrength");
    int unit = DETAIL_UNIT;
    SetShaderValue(s, G.loc_detail, &unit, SHADER_UNIFORM_INT);

    // The same textures every run (raylib seeds its random numbers from the
    // clock; replays and the golden-image tests need them identical).
    SetRandomSeed(20260928);
    for (int m = 1; m < MAT_COUNT; m++) G.detail[m] = make_texture((GfxMaterial)m);
    SetRandomSeed((unsigned)time(NULL));
    G.material = LoadMaterialDefault();
    G.material.shader = s;
    vrui_solid_shader(s);   // vrui's boxes and spheres are lit the same way
}

static void set_int(int loc, bool v) { int i = v; SetShaderValue(G.shader, loc, &i, SHADER_UNIFORM_INT); }
static void set_vec3(int loc, Vector3 v) { SetShaderValue(G.shader, loc, &v, SHADER_UNIFORM_VEC3); }

// The light for this frame. `dusk` is the SKY lever (0 day .. 1 dusk): the
// sun sinks, reddens and dims, and the sky light turns blue and dark. The
// numbers are "linear" light amounts (world.fs explains).
void gfx_frame(float dusk, Color sky)
{
    vrui_culling(gfx.culling && !SFXR_BREAK(toolbox_culling_ignored));
    if (!G.ok) return;
    Vector3 sun_day = Vector3Normalize((Vector3){ -0.4f, -0.85f, -0.35f });
    Vector3 sun_dusk = Vector3Normalize((Vector3){ -0.8f, -0.25f, -0.55f });
    set_int(G.loc_light, gfx.lighting);
    set_int(G.loc_shine, gfx.shine);
    set_int(G.loc_fog, gfx.fog);
    set_vec3(G.loc_sun_dir, Vector3Normalize(Vector3Lerp(sun_day, sun_dusk, dusk)));
    set_vec3(G.loc_sun_color, Vector3Lerp((Vector3){ 0.85f, 0.8f, 0.72f }, (Vector3){ 0.9f, 0.45f, 0.25f }, dusk));
    set_vec3(G.loc_sky, Vector3Lerp((Vector3){ 0.32f, 0.38f, 0.48f }, (Vector3){ 0.10f, 0.11f, 0.20f }, dusk));
    set_vec3(G.loc_ground, Vector3Lerp((Vector3){ 0.20f, 0.19f, 0.14f }, (Vector3){ 0.05f, 0.05f, 0.06f }, dusk));
    set_vec3(G.loc_eye, sfxr_head().position);
    set_vec3(G.loc_fog_color, (Vector3){ sky.r / 255.0f, sky.g / 255.0f, sky.b / 255.0f });
    float density = 0.018f;
    SetShaderValue(G.shader, G.loc_fog_density, &density, SHADER_UNIFORM_FLOAT);
    gfx_material(MAT_NONE);

    // a new frame of scenery calls
    G.calls = 0;
    G.recording = !gfx.batching || !G.built;
    if (G.recording) G.nprims = 0;
}

// Switching material: draw what's queued with the old one first (raylib
// collects shapes into a batch and draws them later; changing the shader's
// settings under a half-full batch would repaint it all), then point the
// shader at this material's texture. Every switch is a new draw call, so draw
// things of one material together.
void gfx_material(GfxMaterial m)
{
    if (!G.ok) return;
    rlDrawRenderBatchActive();
    static const float SCALE[MAT_COUNT] = { 1, 1.0f, 0.8f, 0.33f, 1.0f };   // pattern repeats per meter (grass: every 3 m)
    float strength = gfx.textures && m != MAT_NONE ? 1.0f : 0.0f;
    float scale = SCALE[m];
    SetShaderValue(G.shader, G.loc_detail_strength, &strength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(G.shader, G.loc_detail_scale, &scale, SHADER_UNIFORM_FLOAT);
    rlActiveTextureSlot(DETAIL_UNIT);
    rlEnableTexture(G.detail[m == MAT_NONE ? MAT_PAINT : m].id);
    rlActiveTextureSlot(0);
}

void gfx_draw_begin(void) { if (G.ok) { BeginShaderMode(G.shader); gfx_material(MAT_NONE); } }
void gfx_draw_end(void)   { if (G.ok) { gfx_material(MAT_NONE); EndShaderMode(); } }

bool gfx_visible(Vector3 center, float radius)
{
    return !gfx.culling || SFXR_BREAK(toolbox_culling_ignored) || sfxr_in_view(center, radius);
}

// ---------------------------------------------------------------------------
// Scenery
// ---------------------------------------------------------------------------

static void add(Prim p)
{
    G.calls++;
    if (!G.recording || G.nprims >= MAX_PRIMS) return;
    // bounding sphere: the unit shapes fit in a sphere of radius ~0.87 (the
    // box's corner), scaled by the biggest scale
    float s = fmaxf(p.scale.x, fmaxf(p.scale.y, p.scale.z));
    p.center = p.kind == PRIM_CYLINDER ? Vector3Add(p.pos, Vector3RotateByQuaternion((Vector3){ 0, p.scale.y * 0.5f, 0 }, p.rot))
                                       : p.pos;
    p.radius = p.kind == PRIM_CYLINDER ? sqrtf(p.scale.y * p.scale.y * 0.25f + p.scale.x * p.scale.x) : s * 0.87f;
    G.prims[G.nprims++] = p;
}

void scenery_box(SfxrPose pose, Vector3 size, Color color, GfxMaterial mat)
{
    add((Prim){ PRIM_BOX, pose.position, size, pose.orientation, color, mat });
}

void scenery_cylinder(Vector3 a, Vector3 b, float radius, Color color, GfxMaterial mat)
{
    Vector3 d = Vector3Subtract(b, a);
    float len = Vector3Length(d);
    if (len < 1e-5f) return;
    Quaternion q = QuaternionFromVector3ToVector3((Vector3){ 0, 1, 0 }, Vector3Scale(d, 1.0f / len));
    add((Prim){ PRIM_CYLINDER, a, { radius, len, radius }, q, color, mat });
}

void scenery_sphere(Vector3 center, float radius, Color color, GfxMaterial mat)
{
    add((Prim){ PRIM_SPHERE, center, { radius, radius, radius }, QuaternionIdentity(), color, mat });
}

// One corner of a unit shape, moved to where the primitive is. A normal is
// scaled by 1/scale (so a squashed box's sides still face the right way) and
// turned, but not moved.
static void corner(const Prim *p, const Mesh *m, int v, Vector3 *pos, Vector3 *nrm)
{
    const float *vp = m->vertices + 3 * v, *vn = m->normals + 3 * v;
    Vector3 lp = { vp[0] * p->scale.x, vp[1] * p->scale.y, vp[2] * p->scale.z };
    Vector3 ln = Vector3Normalize((Vector3){ vn[0] / p->scale.x, vn[1] / p->scale.y, vn[2] / p->scale.z });
    *pos = Vector3Add(p->pos, Vector3RotateByQuaternion(lp, p->rot));
    *nrm = Vector3RotateByQuaternion(ln, p->rot);
}

static int corner_count(const Mesh *m) { return m->indices ? m->triangleCount * 3 : m->vertexCount; }
static int corner_index(const Mesh *m, int i) { return m->indices ? m->indices[i] : i; }

// Batching off: send one primitive's triangles through raylib's batch, as
// DrawCube does -- the CPU redoes this for every primitive, every frame.
static void draw_now(const Prim *p)
{
    const Mesh *m = &G.unit[p->kind];
    int n = corner_count(m);
    rlCheckRenderBatchLimit(n);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(p->color.r, p->color.g, p->color.b, p->color.a);
    for (int i = 0; i < n; i++) {
        Vector3 pos, nrm;
        corner(p, m, corner_index(m, i), &pos, &nrm);
        rlNormal3f(nrm.x, nrm.y, nrm.z);
        rlVertex3f(pos.x, pos.y, pos.z);
    }
    rlEnd();
}

// Batching on: merge every primitive of one material in one chunk into one
// mesh, and hand it to the GPU (UploadMesh). From then on, drawing it is one
// call and no CPU work, however many boxes are in it.
static void build(void)
{
    for (int i = 0; i < G.nchunks; i++) UnloadMesh(G.chunks[i].mesh);
    G.nchunks = 0;
    int cell_of[MAX_PRIMS];
    for (int i = 0; i < G.nprims; i++) cell_of[i] = (int)floorf(G.prims[i].center.x / CHUNK_W);
    bool done[MAX_PRIMS] = { 0 };
    for (int i = 0; i < G.nprims && G.nchunks < MAX_CHUNKS; i++) {
        if (done[i]) continue;
        // everything sharing this one's cell and material
        int corners = 0;
        for (int k = i; k < G.nprims; k++)
            if (!done[k] && cell_of[k] == cell_of[i] && G.prims[k].mat == G.prims[i].mat)
                corners += corner_count(&G.unit[G.prims[k].kind]);
        Mesh mesh = { 0 };
        mesh.vertexCount = corners;
        mesh.triangleCount = corners / 3;
        mesh.vertices = RL_MALLOC(sizeof(float) * 3 * corners);
        mesh.normals = RL_MALLOC(sizeof(float) * 3 * corners);
        mesh.colors = RL_MALLOC(4 * corners);
        int at = 0;
        Vector3 lo = { 1e9f, 1e9f, 1e9f }, hi = { -1e9f, -1e9f, -1e9f };
        for (int k = i; k < G.nprims; k++) {
            const Prim *p = &G.prims[k];
            if (done[k] || cell_of[k] != cell_of[i] || p->mat != G.prims[i].mat) continue;
            done[k] = true;
            const Mesh *u = &G.unit[p->kind];
            for (int c = 0; c < corner_count(u); c++, at++) {
                Vector3 pos, nrm;
                corner(p, u, corner_index(u, c), &pos, &nrm);
                memcpy(mesh.vertices + 3 * at, &pos, sizeof pos);
                memcpy(mesh.normals + 3 * at, &nrm, sizeof nrm);
                memcpy(mesh.colors + 4 * at, &p->color, 4);
                lo = Vector3Min(lo, pos);
                hi = Vector3Max(hi, pos);
            }
        }
        UploadMesh(&mesh, false);
        Vector3 center = Vector3Scale(Vector3Add(lo, hi), 0.5f);
        G.chunks[G.nchunks++] = (Chunk){ mesh, G.prims[i].mat, center, Vector3Distance(center, hi) };
    }
    G.built = true;
    G.built_calls = G.calls;
    G.builds++;
}

void scenery_draw(void)
{
    GfxStats *st = &G.stats;
    st->scenery_drawn = st->scenery_culled = 0;
    st->chunks = 0;
    if (gfx.batching) {
        // Build from a recorded frame; if the number of calls changes later
        // (the scenery changed), record the next frame and build again.
        if (!G.built && G.recording && G.calls > 0) build();
        else if (G.built && G.calls != G.built_calls) G.built = false;
        st->chunks = G.nchunks;
        for (int i = 0; i < G.nchunks; i++) {
            const Chunk *c = &G.chunks[i];
            if (!gfx_visible(c->center, c->radius)) { st->scenery_culled++; continue; }
            gfx_material(c->mat);
            DrawMesh(c->mesh, G.material, MatrixIdentity());
            st->scenery_drawn++;
        }
        if (SFXR_BREAK(toolbox_scenery_rebuilds_every_frame)) G.built = false;
    } else {
        if (G.built) {   // batching switched off: free the meshes
            for (int i = 0; i < G.nchunks; i++) UnloadMesh(G.chunks[i].mesh);
            G.nchunks = 0;
            G.built = false;
        }
        for (int m = 0; m < MAT_COUNT; m++) {   // one material at a time
            gfx_material((GfxMaterial)m);
            for (int i = 0; i < G.nprims; i++) {
                const Prim *p = &G.prims[i];
                if (p->mat != (GfxMaterial)m) continue;
                if (!gfx_visible(p->center, p->radius)) { st->scenery_culled++; continue; }
                draw_now(p);
                st->scenery_drawn++;
            }
        }
    }
    gfx_material(MAT_NONE);
    st->scenery_prims = G.calls;
    st->builds = G.builds;
}

const GfxStats *gfx_stats(void) { return &G.stats; }
