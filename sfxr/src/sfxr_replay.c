// sfxr_replay.c - input recording and deterministic replay.
//
// RECORD (any backend):  SFXR_RECORD=path.sfxrec
//   Every frame, right after the backend sampled the head/eyes/controllers,
//   the raw inputs are appended to the file: head + eye poses, FOVs, both
//   controllers (poses, velocities, every button/axis), eye gaze, headset
//   signals, the frame's dt and whether the runtime wanted it rendered.
//   Flushed each frame, so a killed app still leaves a usable file.
//
//   Only frames with someone wearing the headset are recorded: the file is
//   created the first time the headset is worn (never, if nobody puts it on),
//   and frames while it's taken off are skipped. A replay therefore starts
//   from a fresh app at the moment the headset went on. Apps that change a
//   lot while nobody watches can differ slightly; most sit still.
//
// REPLAY (its own backend): SFXR_REPLAY=path.sfxrec  [SFXR_REPLAY_SCALE=0.5]
//   Feeds those inputs back frame-for-frame and renders offscreen exactly like
//   the source (stereo side-by-side for headset recordings, mono for simulator
//   ones). The app sees the same sequence of sfxr_hand()/sfxr_head()/sfxr_dt()
//   values, so if it only uses sfxr time (sfxr_dt/sfxr_time/frame index) the
//   run is deterministic: same frames, same pixels (on the same GPU/driver).
//   Combine with SFXR_SHOT(_FRAMES) for golden-image regression tests
//   (scripts/regress.sh). The app exits when the recording ends.

#include "sfxr_internal.h"
#include "sfxr_gl.h"

#include <stdlib.h>
#include <string.h>

#define S sfxr_state

void sfxr_texture_skip_srgb_decode(unsigned tex);

#include "sfxr_rec.h"

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

static FILE *rec_file;
static const char *rec_path;      // SFXR_RECORD, opened once the headset is worn
static uint64_t rec_frames, rec_skipped;

static void fill_frame(RecFrame *f)
{
    memset(f, 0, sizeof *f);
    f->frame = S.frame;
    f->dt = S.dt;
    f->should_render = S.should_render;
    f->views_valid = S.views_valid;
    f->gaze_valid = S.gaze_valid;
    f->eye_w = S.eye_w;
    f->eye_h = S.eye_h;
    f->head = S.head_stage;
    f->eye[0] = S.eye_stage[0];
    f->eye[1] = S.eye_stage[1];
    memcpy(f->fov, S.fov, sizeof f->fov);
    f->gaze = S.gaze_stage;
    f->raw[0] = S.raw[0];
    f->raw[1] = S.raw[1];
    f->sig = S.sig;
    if (S.backend == SFXR_BACKEND_SCRIPT) {   // a test run renders nothing itself; its replay should
        f->should_render = true;
        f->eye_w = f->eye_h = 960;
    }
}

void sfxr_record_start(void)
{
    rec_path = sfxr_env_str("SFXR_RECORD");
    if (rec_path && S.backend == SFXR_BACKEND_REPLAY) rec_path = NULL;
    if (rec_path) SFXR_LOG("will record inputs to %s once the headset is worn", rec_path);
}

static void record_open(void)
{
    const char *path = rec_path;
    rec_path = NULL;
    rec_file = fopen(path, "wb");
    if (!rec_file) { SFXR_WARN("cannot open SFXR_RECORD file %s", path); return; }
    RecHeader h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, REC_MAGIC, sizeof REC_MAGIC);
    h.version = REC_VERSION;
    h.header_size = sizeof(RecHeader);
    h.frame_size = sizeof(RecFrame);
    h.raw_hand_size = sizeof(SfxrRawHand);
    h.eye_w = S.eye_w;
    h.eye_h = S.eye_h;
    h.stereo = S.backend != SFXR_BACKEND_SIM;
    if (S.backend == SFXR_BACKEND_SCRIPT) h.eye_w = h.eye_h = 960;
    h.source_backend = (uint32_t)S.backend;
    snprintf(h.runtime, sizeof h.runtime, "%s", S.runtime_name);
    snprintf(h.system, sizeof h.system, "%s", S.system_name);
    snprintf(h.app, sizeof h.app, "%s", S.cfg.app_name);
    fwrite(&h, sizeof h, 1, rec_file);
    fflush(rec_file);
    SFXR_LOG("recording inputs -> %s", path);
    // the event log's frame numbers match the recording's frame field; this
    // line says where the recording starts (scripts/clips.sh uses it)
    sfxr_event("record", "started (frame %llu) %s", (unsigned long long)S.frame, path);
}

void sfxr_record_frame(void)
{
    // someone wearing it (or a runtime that can't tell) and frames wanted;
    // a scripted test run is always "worn" (its run.sfxrec replays the test)
    bool worn = ((!S.sig.presence_known || S.sig.present) && S.should_render) || S.backend == SFXR_BACKEND_SCRIPT;
    if (!rec_file && rec_path && worn) record_open();
    if (!rec_file) return;
    if (!worn) { rec_skipped++; return; }
    rec_frames++;
    RecFrame f;
    fill_frame(&f);
    fwrite(&f, sizeof f, 1, rec_file);
    fflush(rec_file);
}

void sfxr_record_stop(void)
{
    if (!rec_file) return;
    SFXR_LOG("recorded %llu frames (%llu skipped while the headset was off)",
             (unsigned long long)rec_frames, (unsigned long long)rec_skipped);
    fclose(rec_file);
    rec_file = NULL;
}

// ---------------------------------------------------------------------------
// Replay backend
// ---------------------------------------------------------------------------

static struct {
    FILE *f;
    RecHeader h;
    float scale;
    uint64_t frames_total;
    int layout;               // recording version; older ones are upgraded frame by frame
    unsigned tex, depth_rb, fbo;
    int tw, th;
} R;

static bool ensure_target(int w, int h)
{
    if (R.fbo && R.tw == w && R.th == h) return true;
    if (R.fbo) sgl.DeleteFramebuffers(1, &R.fbo);
    if (R.tex) sgl.DeleteTextures(1, &R.tex);
    if (R.depth_rb) sgl.DeleteRenderbuffers(1, &R.depth_rb);
    sgl.GenTextures(1, &R.tex);
    sgl.BindTexture(SGL_TEXTURE_2D, R.tex);
    sgl.TexImage2D(SGL_TEXTURE_2D, 0, SGL_SRGB8_ALPHA8, w, h, 0, SGL_RGBA, SGL_UNSIGNED_BYTE, NULL);
    sgl.BindTexture(SGL_TEXTURE_2D, 0);
    sfxr_texture_skip_srgb_decode(R.tex);
    sgl.GenRenderbuffers(1, &R.depth_rb);
    sgl.BindRenderbuffer(SGL_RENDERBUFFER, R.depth_rb);
    sgl.RenderbufferStorage(SGL_RENDERBUFFER, SGL_DEPTH_COMPONENT24, w, h);
    sgl.BindRenderbuffer(SGL_RENDERBUFFER, 0);
    R.fbo = sfxr_make_fbo(R.tex, R.depth_rb);
    R.tw = w;
    R.th = h;
    return R.fbo != 0;
}

static void apply_size(int eye_w, int eye_h)
{
    S.eye_w = (int)(eye_w * R.scale);
    S.eye_h = (int)(eye_h * R.scale);
    if (S.eye_w < 16) S.eye_w = 16;
    if (S.eye_h < 16) S.eye_h = 16;
    ensure_target(S.replay_stereo ? S.eye_w * 2 : S.eye_w, S.eye_h);
}

static bool replay_init(void)
{
    memset(&R, 0, sizeof R);
    const char *path = sfxr_env_str("SFXR_REPLAY");
    if (!path) { SFXR_WARN("replay backend needs SFXR_REPLAY=<file.sfxrec>"); return false; }
    R.f = fopen(path, "rb");
    if (!R.f) { SFXR_ERR("cannot open %s", path); return false; }
    if (fread(&R.h, sizeof R.h, 1, R.f) != 1 || memcmp(R.h.magic, REC_MAGIC, sizeof REC_MAGIC) != 0) {
        SFXR_ERR("%s is not an sfxr recording", path);
        return false;
    }
    R.layout = sfxr_rec_layout(&R.h);
    if (!R.layout) {
        SFXR_ERR("%s was recorded by an incompatible sfxr (version %u, frame %u bytes; this build: %u, %zu)",
                 path, R.h.version, R.h.frame_size, REC_VERSION, sizeof(RecFrame));
        return false;
    }
    long here = ftell(R.f);
    fseek(R.f, 0, SEEK_END);
    R.frames_total = (uint64_t)((ftell(R.f) - here) / (long)R.h.frame_size);
    fseek(R.f, here, SEEK_SET);

    const char *sc = sfxr_env_str("SFXR_REPLAY_SCALE");
    R.scale = sc ? (float)atof(sc) : 1.0f;
    if (R.scale <= 0.05f || R.scale > 4.0f) R.scale = 1.0f;
    S.replay_stereo = R.h.stereo != 0;
    snprintf(S.runtime_name, sizeof S.runtime_name, "replay of %.100s", R.h.runtime);
    snprintf(S.system_name, sizeof S.system_name, "%.120s", R.h.system);
    apply_size(R.h.eye_w, R.h.eye_h);
    SFXR_LOG("replaying %s: %llu frames from '%s' on '%s' (%s, scale %.2f)", path,
             (unsigned long long)R.frames_total, R.h.app, R.h.system,
             S.replay_stereo ? "stereo" : "mono", R.scale);
    return R.fbo != 0;
}

static void replay_shutdown(void)
{
    if (R.f) fclose(R.f);
    if (R.fbo) sgl.DeleteFramebuffers(1, &R.fbo);
    if (R.tex) sgl.DeleteTextures(1, &R.tex);
    if (R.depth_rb) sgl.DeleteRenderbuffers(1, &R.depth_rb);
    memset(&R, 0, sizeof R);
}

static bool replay_frame_begin(void)
{
    RecFrame f;
    if (!sfxr_rec_read(R.f, R.layout, &f)) {
        SFXR_LOG("replay finished (%llu frames)", (unsigned long long)R.frames_total);
        return false;
    }
    S.dt = f.dt;
    S.should_render = f.should_render;
    S.views_valid = f.views_valid;
    S.gaze_valid = f.gaze_valid;
    if ((int)(f.eye_w * R.scale) != S.eye_w || (int)(f.eye_h * R.scale) != S.eye_h) apply_size(f.eye_w, f.eye_h);
    S.head_stage = f.head;
    S.eye_stage[0] = f.eye[0];
    S.eye_stage[1] = f.eye[1];
    memcpy(S.fov, f.fov, sizeof S.fov);
    S.gaze_stage = f.gaze;
    S.raw[0] = f.raw[0];
    S.raw[1] = f.raw[1];
    S.sig = f.sig;
    return true;
}

static bool replay_acquire(unsigned *fbo, unsigned *tex)
{
    if (!R.fbo) return false;
    *fbo = R.fbo;
    *tex = R.tex;
    return true;
}

static void replay_release(void) {}
static void replay_frame_end(bool rendered) { (void)rendered; }
static void replay_haptic(SfxrHandId hand, float amplitude, float seconds, float freq)
{
    (void)hand; (void)amplitude; (void)seconds; (void)freq;
}

const SfxrBackendVtbl sfxr_backend_replay = {
    replay_init, replay_shutdown, replay_frame_begin, replay_acquire,
    replay_release, replay_frame_end, replay_haptic,
};
