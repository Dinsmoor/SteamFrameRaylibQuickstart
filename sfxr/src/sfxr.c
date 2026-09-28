// sfxr.c - the core: picking a backend, init and shutdown, the frame loop,
// stereo drawing (one pass, both eyes, MSAA), the desktop mirror, screenshots
// and debug drawing. Neighbors: sfxr_pose.c (poses, the rig), sfxr_input.c
// (buttons, pull levels, hand shapes), sfxr_signals.c (headset signals).

#include "sfxr_internal.h"
#include "sfxr_gl.h"
#include "rlgl.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

SfxrState sfxr_state;
#define S sfxr_state

static bool shot_done;   // SFXR_SHOT taken -> exit
static void shot_window(void);
static void shot_target(void);

// Heartbeat: one log line every few seconds so a log read over SSH tells you
// whether frames flow, what state the session is in and if controllers track.
static double   hb_time;
static unsigned hb_loops, hb_rendered;
static double hb_cpu, frame_t0;   // app CPU time per frame: frame_begin returning .. frame_end
static void heartbeat(double now)
{
    hb_loops++;
    if (hb_time == 0) hb_time = now;
    double span = now - hb_time;
    if (span < 5.0) return;
    const char *state = S.backend == SFXR_BACKEND_SIM ? "sim" :
                        S.backend == SFXR_BACKEND_REPLAY ? "replay" : sfxr_xr_session_state();
    SFXR_LOG("heartbeat: loop %.1f fps, rendered %.1f fps, cpu %.2f ms/frame, session=%s, should_render=%d, hands L=%d R=%d, profile=%s",
             hb_loops / span, hb_rendered / span, hb_loops ? 1000.0 * hb_cpu / hb_loops : 0.0, state, S.should_render,
             S.hands[0].active, S.hands[1].active, S.raw[1].profile[0] ? S.raw[1].profile : "-");
    // SFXR_PERF_LOG=1: the runtime's own counters (GPU time...) with every heartbeat
    static int perf_log = -1;
    if (perf_log < 0) perf_log = sfxr_env_flag("SFXR_PERF_LOG", false);
    if (perf_log) {
        if (!sfxr_perf_count()) sfxr_perf_enable(true);
        char line[1024];
        int len = 0;
        for (int i = 0; i < sfxr_perf_count() && len < (int)sizeof line - 96; i++) {
            float v;
            const char *unit;
            if (!sfxr_perf_value(i, &v, &unit)) continue;
            const char *name = strrchr(sfxr_perf_name(i), '/');
            len += snprintf(line + len, sizeof line - (size_t)len, " %s=%.2f%s", name ? name + 1 : sfxr_perf_name(i), v, unit);
        }
        if (len) SFXR_LOG("perf:%s", line);
    }
    hb_time = now;
    hb_loops = hb_rendered = 0;
    hb_cpu = 0;
}

// EXT_texture_sRGB_decode: sample sRGB swapchain images without decoding so
// the mirror shows the same bytes the headset gets.
#define SGL_TEXTURE_SRGB_DECODE_EXT 0x8A48
#define SGL_SKIP_DECODE_EXT         0x8A4A

// ---------------------------------------------------------------------------
// Config / env
// ---------------------------------------------------------------------------

SfxrConfig sfxr_default_config(void)
{
    SfxrConfig c = {0};
    c.app_name = "sfxr app";
    c.backend = SFXR_BACKEND_AUTO;
    c.mirror_window = true;
    c.mirror_width = 1280;
    c.mirror_height = 720;
    c.resolution_scale = 1.0f;
    c.msaa_samples = 4;
    c.near_clip = 0.05f;
    c.far_clip = 500.0f;
    c.preferred_refresh_hz = 0.0f;
    c.allow_sim_fallback = true;
    return c;
}

const char *sfxr_env_str(const char *name)
{
    const char *v = getenv(name);
    return (v && *v) ? v : NULL;
}

bool sfxr_env_flag(const char *name, bool def)
{
    const char *v = sfxr_env_str(name);
    if (!v) return def;
    return !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
}

static const SfxrBackendVtbl *vtbl_for(SfxrBackend b)
{
    switch (b) {
    case SFXR_BACKEND_XR_GL: return &sfxr_backend_xr_gl;
    case SFXR_BACKEND_XR_VK: return &sfxr_backend_xr_vk;
    case SFXR_BACKEND_SIM:   return &sfxr_backend_sim;
    case SFXR_BACKEND_REPLAY: return &sfxr_backend_replay;
    case SFXR_BACKEND_SCRIPT: return &sfxr_backend_script;
    default:                 return NULL;
    }
}

const char *sfxr_backend_name(void)
{
    switch (S.backend) {
    case SFXR_BACKEND_XR_GL: return "OpenXR (OpenGL)";
    case SFXR_BACKEND_XR_VK: return "OpenXR (Vulkan+GL interop)";
    case SFXR_BACKEND_SIM:   return "Desktop simulator";
    case SFXR_BACKEND_REPLAY: return "Replay";
    case SFXR_BACKEND_SCRIPT: return "Test script";
    default:                 return "none";
    }
}

SfxrBackend sfxr_backend(void)        { return S.backend; }
const char *sfxr_runtime_name(void)   { return S.runtime_name; }
const char *sfxr_system_name(void)    { return S.system_name; }
uint64_t    sfxr_frame_index(void)    { return S.frame; }
int         sfxr_eye_width(void)      { return S.eye_w; }
int         sfxr_eye_height(void)     { return S.eye_h; }
float       sfxr_dt(void)             { return S.dt; }
double      sfxr_time(void)           { return S.time; }
bool        sfxr_should_render(void)  { return S.should_render; }

// ---------------------------------------------------------------------------
// MSAA target
// ---------------------------------------------------------------------------

static void destroy_msaa(void)
{
    if (S.msaa_fbo) sgl.DeleteFramebuffers(1, &S.msaa_fbo);
    if (S.msaa_color_rb) sgl.DeleteRenderbuffers(1, &S.msaa_color_rb);
    if (S.msaa_depth_rb) sgl.DeleteRenderbuffers(1, &S.msaa_depth_rb);
    S.msaa_fbo = S.msaa_color_rb = S.msaa_depth_rb = 0;
}

static int msaa_w, msaa_h;

static void create_msaa(int width, int height)
{
    destroy_msaa();
    msaa_w = width;
    msaa_h = height;
    int samples = S.cfg.msaa_samples;
    sgl_int max_samples = 0;
    sgl.GetIntegerv(SGL_MAX_SAMPLES, &max_samples);
    if (samples > max_samples) samples = max_samples;
    S.msaa_samples = samples;
    if (samples <= 1) return;

    sgl.GenRenderbuffers(1, &S.msaa_color_rb);
    sgl.BindRenderbuffer(SGL_RENDERBUFFER, S.msaa_color_rb);
    sgl.RenderbufferStorageMultisample(SGL_RENDERBUFFER, samples, SGL_SRGB8_ALPHA8, width, height);
    sgl.GenRenderbuffers(1, &S.msaa_depth_rb);
    sgl.BindRenderbuffer(SGL_RENDERBUFFER, S.msaa_depth_rb);
    sgl.RenderbufferStorageMultisample(SGL_RENDERBUFFER, samples, SGL_DEPTH_COMPONENT24, width, height);
    sgl.BindRenderbuffer(SGL_RENDERBUFFER, 0);

    sgl.GenFramebuffers(1, &S.msaa_fbo);
    sgl.BindFramebuffer(SGL_FRAMEBUFFER, S.msaa_fbo);
    sgl.FramebufferRenderbuffer(SGL_FRAMEBUFFER, SGL_COLOR_ATTACHMENT0, SGL_RENDERBUFFER, S.msaa_color_rb);
    sgl.FramebufferRenderbuffer(SGL_FRAMEBUFFER, SGL_DEPTH_ATTACHMENT, SGL_RENDERBUFFER, S.msaa_depth_rb);
    sgl_enum st = sgl.CheckFramebufferStatus(SGL_FRAMEBUFFER);
    sgl.BindFramebuffer(SGL_FRAMEBUFFER, 0);
    if (st != SGL_FRAMEBUFFER_COMPLETE) {
        SFXR_WARN("MSAA x%d framebuffer incomplete (0x%x); rendering without MSAA", samples, st);
        destroy_msaa();
        S.msaa_samples = 1;
        return;
    }
    SFXR_LOG("MSAA x%d target %dx%d", samples, width, height);
}

// Stereo (side-by-side eyes) for headset backends and replays of headset recordings.
static bool is_stereo(void)
{
    if (S.backend == SFXR_BACKEND_SIM) return false;
    if (S.backend == SFXR_BACKEND_REPLAY) return S.replay_stereo;
    return true;
}
static int target_width(void)  { return is_stereo() ? S.eye_w * 2 : S.eye_w; }

// ---------------------------------------------------------------------------
// Init / shutdown
// ---------------------------------------------------------------------------

// Log sink: timestamps every line (handy when reading a headset log over SSH)
// and drops raylib's per-frame "mipmaps generated" chatter from vrui panels.
static struct timespec log_t0;
static void trace_sink(int level, const char *fmt, va_list ap)
{
    char msg[1024];
    vsnprintf(msg, sizeof msg, fmt, ap);
    if (strstr(msg, "Mipmaps generated automatically")) return;
    const char *tag = level >= LOG_ERROR ? "ERROR" : level == LOG_WARNING ? "WARN " :
                      level == LOG_DEBUG ? "DEBUG" : "INFO ";
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double t = (double)(now.tv_sec - log_t0.tv_sec) + (double)(now.tv_nsec - log_t0.tv_nsec) * 1e-9;
    fprintf(level >= LOG_WARNING ? stderr : stdout, "[%8.3f] %s %s\n", t, tag, msg);
    fflush(level >= LOG_WARNING ? stderr : stdout);
}

bool sfxr_init(const SfxrConfig *cfg)
{
    clock_gettime(CLOCK_MONOTONIC, &log_t0);
    SetTraceLogCallback(trace_sink);
    memset(&S, 0, sizeof(S));
    S.cfg = cfg ? *cfg : sfxr_default_config();
    S.verbose = sfxr_env_flag("SFXR_LOG", false);
    S.cfg.mirror_window = sfxr_env_flag("SFXR_MIRROR", S.cfg.mirror_window);
    if (S.cfg.resolution_scale <= 0.0f) S.cfg.resolution_scale = 1.0f;
    if (S.cfg.msaa_samples < 1) S.cfg.msaa_samples = 1;

    SfxrBackend want = S.cfg.backend;
    const char *env = sfxr_env_str("SFXR_BACKEND");
    if (env) {
        if (!strcmp(env, "gl")) want = SFXR_BACKEND_XR_GL;
        else if (!strcmp(env, "vk")) want = SFXR_BACKEND_XR_VK;
        else if (!strcmp(env, "sim")) want = SFXR_BACKEND_SIM;
        else if (!strcmp(env, "replay")) want = SFXR_BACKEND_REPLAY;
        else if (!strcmp(env, "auto")) want = SFXR_BACKEND_AUTO;
        else SFXR_WARN("unknown SFXR_BACKEND=%s (use gl|vk|sim|auto)", env);
    }

    SFXR_LOG("env: DISPLAY=%s WAYLAND_DISPLAY=%s XDG_RUNTIME_DIR=%s XR_RUNTIME_JSON=%s",
             getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)",
             getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(unset)",
             getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "(unset)",
             getenv("XR_RUNTIME_JSON") ? getenv("XR_RUNTIME_JSON") : "(default)");

    if (sfxr_env_str("SFXR_REPLAY")) want = SFXR_BACKEND_REPLAY;

    // The GL context lives in raylib's window. In XR modes the window is only a
    // mirror, so it may be hidden; the context is what matters.
    unsigned flags = FLAG_WINDOW_RESIZABLE;
    if (!S.cfg.mirror_window && want != SFXR_BACKEND_SIM) flags |= FLAG_WINDOW_HIDDEN;
    if ((want == SFXR_BACKEND_REPLAY || want == SFXR_BACKEND_SCRIPT) && !sfxr_env_flag("SFXR_MIRROR", false))
        flags |= FLAG_WINDOW_HIDDEN;
    SetConfigFlags(flags);
    InitWindow(S.cfg.mirror_width, S.cfg.mirror_height, S.cfg.app_name);
    if (!IsWindowReady()) { SFXR_ERR("could not create window / GL context"); return false; }
    S.window_open = true;
    SetExitKey(KEY_NULL);  // ESC is not an exit key; close the window or let the runtime quit
    if (!sfxr_gl_load()) { CloseWindow(); return false; }
    SFXR_LOG("GL: %s | %s | %s", (const char *)sgl.GetString(SGL_VENDOR),
             (const char *)sgl.GetString(SGL_RENDERER), (const char *)sgl.GetString(SGL_VERSION));

    S.rig_pos = (Vector3){0};
    S.rig_yaw = 0.0f;
    // SFXR_RIG="x,y,z,yaw_deg": start the player somewhere else (e.g. to frame a
    // remote screenshot while the headset sits on a desk). Apps may override.
    const char *rig = sfxr_env_str("SFXR_RIG");
    if (rig) {
        float x = 0, y = 0, z = 0, yd = 0;
        if (sscanf(rig, "%f,%f,%f,%f", &x, &y, &z, &yd) >= 3) {
            S.rig_pos = (Vector3){ x, y, z };
            S.rig_yaw = yd * DEG2RAD;
            SFXR_LOG("rig from SFXR_RIG: pos %.2f,%.2f,%.2f yaw %.0f", x, y, z, yd);
        }
    }
    S.head_stage = sfxr_pose_identity();
    S.head_stage.position.y = 1.6f;

    SfxrBackend order[3];
    int n = 0;
    if (want == SFXR_BACKEND_AUTO) {
        order[n++] = SFXR_BACKEND_XR_GL;
        order[n++] = SFXR_BACKEND_XR_VK;
        if (S.cfg.allow_sim_fallback) order[n++] = SFXR_BACKEND_SIM;
    } else {
        order[n++] = want;
    }

    for (int i = 0; i < n; i++) {
        const SfxrBackendVtbl *vt = vtbl_for(order[i]);
        S.backend = order[i];
        S.vt = vt;
        SFXR_LOG("trying backend: %s", sfxr_backend_name());
        if (vt->init()) {
            S.initialized = true;
            break;
        }
        SFXR_WARN("backend %s unavailable", sfxr_backend_name());
    }
    if (!S.initialized) {
        SFXR_ERR("no usable backend");
        CloseWindow();
        return false;
    }

    if (S.backend == SFXR_BACKEND_SIM) {
        // In the simulator the window IS the display.
        if (IsWindowState(FLAG_WINDOW_HIDDEN)) ClearWindowState(FLAG_WINDOW_HIDDEN);
        SetTargetFPS(90);
        S.show_help = true;
    } else {
        SetTargetFPS(0);   // the runtime paces us via xrWaitFrame (replay: as fast as possible)
    }
    create_msaa(target_width(), S.eye_h);
    sfxr_record_start();

    char title[256];
    snprintf(title, sizeof(title), "%s  [%s]", S.cfg.app_name, sfxr_backend_name());
    SetWindowTitle(title);
    SFXR_LOG("ready: backend=%s runtime=%s system=%s eye=%dx%d",
             sfxr_backend_name(), S.runtime_name, S.system_name, S.eye_w, S.eye_h);
    S.last_time = GetTime();
    return true;
}

void sfxr_shutdown(void)
{
    if (!S.initialized) return;
    sfxr_record_stop();
    destroy_msaa();
    S.vt->shutdown();
    S.initialized = false;
    if (S.window_open) CloseWindow();
    S.window_open = false;
}

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------

bool sfxr_frame_begin(void)
{
    if (!S.initialized) return false;
    if (S.window_open && WindowShouldClose()) return false;
    if (shot_done) return false;

    double now = GetTime();
    if (S.backend == SFXR_BACKEND_SIM || S.dt <= 0.0f) S.dt = (float)(now - S.last_time);
    S.last_time = now;
    if (S.dt > 0.1f) S.dt = 0.1f;

    heartbeat(now);
    if (!S.vt->frame_begin()) return false;
    S.frame++;
    S.time += S.dt;
    sfxr_record_frame();
    sfxr__derive_input();
    S.rendered_this_frame = false;
    frame_t0 = GetTime();

    if (IsKeyPressed(KEY_F1)) S.show_help = !S.show_help;
    BeginDrawing();
    ClearBackground((Color){ 16, 16, 20, 255 });
    return true;
}

static Matrix fov_projection(const float fov[4], float n, float f)
{
    // fov = { left, right, up, down } angles (left/down negative)
    float l = tanf(fov[0]) * n, r = tanf(fov[1]) * n;
    float t = tanf(fov[2]) * n, b = tanf(fov[3]) * n;
    return MatrixFrustum(l, r, b, t, n, f);
}

bool sfxr_draw_begin(Color clear)
{
    if (!S.should_render || S.in_draw) return false;
    unsigned fbo = 0, tex = 0;
    if (!S.vt->acquire(&fbo, &tex)) return false;
    S.cur_fbo = fbo;
    S.cur_tex = tex;
    S.in_draw = true;
    if (S.cfg.msaa_samples > 1 && (target_width() != msaa_w || S.eye_h != msaa_h))
        create_msaa(target_width(), S.eye_h);   // simulator window was resized

    RenderTexture2D rt = {0};
    rt.id = S.msaa_fbo ? S.msaa_fbo : fbo;
    rt.texture.id = tex;
    rt.texture.width = target_width();
    rt.texture.height = S.eye_h;
    rt.texture.mipmaps = 1;
    rt.texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    BeginTextureMode(rt);
    ClearBackground(clear);

    rlDrawRenderBatchActive();
    SfxrPose rig = sfxr_rig_pose();
    if (!is_stereo()) {
        SfxrPose eye = sfxr_pose_mul(rig, S.eye_stage[0]);
        Matrix view = MatrixInvert(sfxr_pose_to_matrix(eye));
        rlMatrixMode(RL_PROJECTION);
        rlLoadIdentity();
        rlMultMatrixf(MatrixToFloat(fov_projection(S.fov[0], S.cfg.near_clip, S.cfg.far_clip)));
        rlMatrixMode(RL_MODELVIEW);
        rlLoadIdentity();
        rlMultMatrixf(MatrixToFloat(view));
    } else {
        Matrix proj[2], view[2];
        for (int e = 0; e < 2; e++) {
            SfxrPose eye = sfxr_pose_mul(rig, S.eye_stage[e]);
            view[e] = MatrixInvert(sfxr_pose_to_matrix(eye));
            proj[e] = fov_projection(S.fov[e], S.cfg.near_clip, S.cfg.far_clip);
        }
        rlMatrixMode(RL_PROJECTION);
        rlLoadIdentity();
        rlMatrixMode(RL_MODELVIEW);
        rlLoadIdentity();
        // rlgl draws each batch twice: eye 0 into the left half, eye 1 into
        // the right half, modelview = (identity) * viewOffset[eye].
        rlSetMatrixProjectionStereo(proj[0], proj[1]);
        rlSetMatrixViewOffsetStereo(view[0], view[1]);
        rlEnableStereoRender();
    }
    rlEnableDepthTest();
    return true;
}

static void draw_mirror(void)
{
    if (!S.cfg.mirror_window && S.backend != SFXR_BACKEND_SIM) return;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    float ew = (float)S.eye_w, eh = (float)S.eye_h;
    float scale = fminf(sw / ew, sh / eh);
    float dw = ew * scale, dh = eh * scale;
    Texture2D t = { S.cur_tex, target_width(), S.eye_h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    // GL render targets are bottom-up: flip with a negative source height.
    DrawTexturePro(t, (Rectangle){ 0, 0, ew, -eh },
                   (Rectangle){ (sw - dw) * 0.5f, (sh - dh) * 0.5f, dw, dh },
                   (Vector2){0, 0}, 0.0f, WHITE);
    rlDrawRenderBatchActive();
}

void sfxr_draw_end(void)
{
    if (!S.in_draw) return;
    rlDrawRenderBatchActive();
    rlDisableStereoRender();
    rlDisableDepthTest();
    EndTextureMode();

    if (S.msaa_fbo) {
        int w = target_width(), h = S.eye_h;
        sgl.BindFramebuffer(SGL_READ_FRAMEBUFFER, S.msaa_fbo);
        sgl.BindFramebuffer(SGL_DRAW_FRAMEBUFFER, S.cur_fbo);
        sgl.BlitFramebuffer(0, 0, w, h, 0, 0, w, h, SGL_COLOR_BUFFER_BIT, SGL_NEAREST);
        sgl.BindFramebuffer(SGL_FRAMEBUFFER, 0);
    }

    hb_rendered++;
    shot_target();
    draw_mirror();
    S.vt->release();
    S.in_draw = false;
    S.rendered_this_frame = true;
}

// SFXR_SHOT=path.png [SFXR_SHOT_FRAME=N] [SFXR_SHOT_TRIGGER=file]: save a
// screenshot at frame N (or once the trigger file appears), then quit.
//   simulator: the window (incl. HUD).
//   XR:        the submitted side-by-side eye image, read back from the
//              swapchain target -- works with the mirror window hidden.
// Returns the output path if a screenshot is due this frame, else NULL.
static char shot_path[1024];
static const char *shot_due(void)
{
    const char *path = sfxr_env_str("SFXR_SHOT");
    if (!path || shot_done) return NULL;
    const char *list = sfxr_env_str("SFXR_SHOT_FRAMES");
    if (list) {   // "120,240,360": one file per listed frame (path may contain %d)
        uint64_t last = 0;
        bool due = false;
        for (const char *p = list; *p;) {
            char *end;
            uint64_t f = strtoull(p, &end, 10);
            if (end == p) { p++; continue; }
            if (f > last) last = f;
            if (f == S.frame) due = true;
            p = end;
        }
        if (S.frame > last) { shot_done = true; return NULL; }
        if (!due) return NULL;
        snprintf(shot_path, sizeof shot_path, path, (int)S.frame);
        return shot_path;
    }
    const char *fs = sfxr_env_str("SFXR_SHOT_FRAME");
    uint64_t at = fs ? (uint64_t)strtoull(fs, NULL, 10) : 90;
    const char *trig = sfxr_env_str("SFXR_SHOT_TRIGGER");
    if (S.frame >= at || (trig && FileExists(trig))) {
        snprintf(shot_path, sizeof shot_path, "%s", path);
        return shot_path;
    }
    return NULL;
}

static void shot_saved(const char *path)
{
    SFXR_LOG("screenshot -> %s (frame %llu)", path, (unsigned long long)S.frame);
    if (!sfxr_env_str("SFXR_SHOT_FRAMES")) shot_done = true;   // single shot: quit after it
}

static void shot_window(void)
{
    if (S.backend != SFXR_BACKEND_SIM || !S.rendered_this_frame) return;
    const char *path = shot_due();
    if (!path) return;
    rlDrawRenderBatchActive();
    Image img = LoadImageFromScreen();
    ExportImage(img, path);
    UnloadImage(img);
    shot_saved(path);
}

static void shot_target(void)
{
    if (S.backend == SFXR_BACKEND_SIM || !sgl.ReadPixels) return;
    const char *path = shot_due();
    if (!path) return;
    int w = target_width(), h = S.eye_h;
    unsigned char *px = (unsigned char *)RL_MALLOC((size_t)w * h * 4);
    sgl.BindFramebuffer(SGL_READ_FRAMEBUFFER, S.cur_fbo);
    sgl.ReadPixels(0, 0, w, h, SGL_RGBA, SGL_UNSIGNED_BYTE, px);
    sgl.BindFramebuffer(SGL_FRAMEBUFFER, 0);
    for (size_t i = 0; i < (size_t)w * h; i++) px[i * 4 + 3] = 255;
    Image img = { px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    ImageFlipVertical(&img);   // GL rows are bottom-up
    ExportImage(img, path);
    UnloadImage(img);
    shot_saved(path);
}

void sfxr_frame_end(void)
{
    if (S.in_draw) sfxr_draw_end();
    hb_cpu += GetTime() - frame_t0;
    S.vt->frame_end(S.rendered_this_frame);
    if (S.backend == SFXR_BACKEND_SIM && S.show_help) sfxr_sim_draw_help(10, 10);
    shot_window();
    EndDrawing();
}

// Called by backends after creating their GL textures.
void sfxr_texture_skip_srgb_decode(unsigned tex);
void sfxr_texture_skip_srgb_decode(unsigned tex)
{
    sgl.BindTexture(SGL_TEXTURE_2D, tex);
    sgl.TexParameteri(SGL_TEXTURE_2D, SGL_TEXTURE_SRGB_DECODE_EXT, SGL_SKIP_DECODE_EXT);
    sgl.TexParameteri(SGL_TEXTURE_2D, SGL_TEXTURE_MIN_FILTER, SGL_LINEAR);
    sgl.TexParameteri(SGL_TEXTURE_2D, SGL_TEXTURE_MAG_FILTER, SGL_LINEAR);
    sgl.BindTexture(SGL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// Debug drawing
// ---------------------------------------------------------------------------

void sfxr_draw_floor_grid(int half, Color major, Color minor)
{
    float y = S.rig_pos.y + 0.001f;
    rlBegin(RL_LINES);
    for (int i = -half; i <= half; i++) {
        Color c = (i % 5 == 0) ? major : minor;
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlVertex3f((float)i, y, (float)-half); rlVertex3f((float)i, y, (float)half);
        rlVertex3f((float)-half, y, (float)i); rlVertex3f((float)half, y, (float)i);
    }
    rlEnd();
}

void sfxr_draw_controllers(void)
{
    for (int i = 0; i < 2; i++) {
        const SfxrHand *h = &S.hands[i];
        if (!h->active) continue;
        Color body = i ? (Color){ 80, 140, 230, 255 } : (Color){ 230, 120, 80, 255 };
        if (h->squeeze_btn.down) body = ColorBrightness(body, 0.4f);
        sfxr_push_pose(h->grip);
            DrawCube((Vector3){ 0, 0, 0.02f }, 0.035f, 0.035f, 0.11f, body);
            DrawCubeWires((Vector3){ 0, 0, 0.02f }, 0.036f, 0.036f, 0.111f, BLACK);
        sfxr_pop_pose();
        Vector3 a = h->aim.position;
        float len = h->trigger_btn.down ? 3.0f : 0.25f;
        Vector3 b = Vector3Add(a, Vector3Scale(sfxr_pose_forward(h->aim), len));
        DrawLine3D(a, b, h->trigger_btn.down ? YELLOW : (Color){ 255, 255, 255, 160 });
    }
}
