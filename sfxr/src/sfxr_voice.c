// sfxr_voice.c - push-to-talk voice commands (sfxr_voice.h, docs/AUDIO.md).
//
// The pipeline:
//   the microphone (sfxr_audio.c) fills a ring buffer all the time;
//   listen_begin starts a clip, with the last 0.3 s before the press in it
//     (people start talking as they press);
//   every frame, sfxr_voice_update moves the new samples into the clip;
//   listen_end hands the clip to a background thread, which runs Whisper
//     and leaves the text for sfxr_voice_result;
//   sfxr_voice_match turns the text into one of your commands.
// Recognition never runs on the frame loop's thread: even the tiny model
// takes a few hundred milliseconds, and a frame is 14.
//
// The recognizer is whisper.cpp behind a three-function shim
// (tools/speech/sfq_speech.c), loaded with dlopen, so nothing here needs
// whisper.cpp to build.

#include "sfxr_internal.h"
#include "sfxr_audio.h"
#include "sfxr_voice.h"

#include <ctype.h>
#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RATE    16000
#define MAX_CLIP (RATE * 10)    // ten seconds is plenty for a command
#define PREROLL (RATE * 3 / 10) // 0.3 s from before the button

static struct {
    void *lib, *ctx;
    void *(*open)(const char *, int);
    int (*transcribe)(void *, const float *, int, const char *, int, char *, int);
    void (*close)(void *);
    char status[256];
    char prompt[256];
    int threads;

    float pre[PREROLL];         // the last 0.3 s, always
    int pre_w;
    float clip[MAX_CLIP];
    int nclip;
    bool listening;

    pthread_t thread;
    atomic_bool busy, ready;
    char text[256];
    float ms;
} V;

bool sfxr_voice_available(void) { return V.ctx != NULL; }
const char *sfxr_voice_status(void) { return V.status[0] ? V.status : "voice: not started (sfxr_voice_init)"; }
bool sfxr_voice_listening(void) { return V.listening; }
bool sfxr_voice_busy(void) { return atomic_load(&V.busy); }
float sfxr_voice_level(void) { return sfxr_mic_level(); }
float sfxr_voice_last_ms(void) { return V.ms; }

void sfxr_voice_set_prompt(const char *words)
{
    snprintf(V.prompt, sizeof V.prompt, "%s", SFXR_BREAK(sfxr_voice_no_prompt) || !words ? "" : words);
}

// The first of these that exists: `env`, then next to the app, then the source tree.
static bool find_file(char *out, size_t size, const char *env, const char *beside_app, const char *in_tree)
{
    const char *e = sfxr_env_str(env);
    if (e) { snprintf(out, size, "%s", e); return access(out, R_OK) == 0; }
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = 0;
    char *slash = strrchr(exe, '/');
    if (slash) *slash = 0;
    snprintf(out, size, "%s/%s", exe, beside_app);
    if (access(out, R_OK) == 0) return true;
    snprintf(out, size, "%s/../../../%s", exe, in_tree);   // build/<target>/bin -> the repo
    if (access(out, R_OK) == 0) return true;
    snprintf(out, size, "%s", in_tree);                     // run from the repo root
    return access(out, R_OK) == 0;
}

bool sfxr_voice_init(const char *model_path)
{
    if (V.ctx) return true;
    char lib[PATH_MAX + 64], model[PATH_MAX + 64];
    if (!find_file(lib, sizeof lib, "SFQ_SPEECH_LIB", "libsfq_speech.so", "build/host-speech/libsfq_speech.so")) {
        snprintf(V.status, sizeof V.status, "voice: off (no libsfq_speech.so: scripts/get-speech.sh builds it)");
        SFXR_LOG("%s", V.status);
        return false;
    }
    if (model_path) snprintf(model, sizeof model, "%s", model_path);
    else if (!find_file(model, sizeof model, "SFQ_SPEECH_MODEL", "speech/ggml-tiny.en-q5_1.bin", "external/speech/ggml-tiny.en-q5_1.bin")) {
        snprintf(V.status, sizeof V.status, "voice: off (no model: scripts/get-speech.sh downloads it)");
        SFXR_LOG("%s", V.status);
        return false;
    }
    V.lib = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (V.lib) {
        V.open = (void *(*)(const char *, int))dlsym(V.lib, "sfq_speech_open");
        V.transcribe = (int (*)(void *, const float *, int, const char *, int, char *, int))dlsym(V.lib, "sfq_speech_transcribe");
        V.close = (void (*)(void *))dlsym(V.lib, "sfq_speech_close");
    }
    if (!V.lib || !V.open || !V.transcribe || !V.close) {
        const char *why = dlerror();
        snprintf(V.status, sizeof V.status, "voice: off (libsfq_speech.so won't load: %.200s)", why ? why : "missing functions");
        SFXR_LOG("%s", V.status);
        if (V.lib) dlclose(V.lib);
        V.lib = NULL;
        return false;
    }
    V.threads = sfxr_env_str("SFQ_SPEECH_THREADS") ? atoi(sfxr_env_str("SFQ_SPEECH_THREADS")) : 4;
    V.ctx = V.open(model, V.threads);
    if (!V.ctx) {
        snprintf(V.status, sizeof V.status, "voice: off (the model %.180s won't load)", model);
        SFXR_LOG("%s", V.status);
        dlclose(V.lib);
        V.lib = NULL;
        return false;
    }
    // (tests feed their own samples: never the real microphone under the script backend)
    bool mic = sfxr_backend() == SFXR_BACKEND_SCRIPT && !sfxr_env_flag("SFXR_AUDIO", false) ? false : sfxr_mic_start();
    snprintf(V.status, sizeof V.status, "voice: on (%.120s)%s", strrchr(model, '/') ? strrchr(model, '/') + 1 : model,
             mic ? "" : ", but no microphone");
    SFXR_LOG("%s", V.status);
    return true;
}

void sfxr_voice_shutdown(void)
{
    if (atomic_load(&V.busy)) pthread_join(V.thread, NULL);
    if (V.ctx) V.close(V.ctx);
    if (V.lib) dlclose(V.lib);
    memset(&V, 0, sizeof V);
}

// Every frame (sfxr_frame_begin): keep the pre-roll fresh, or grow the clip.
void sfxr_voice_update(void)
{
    if (!sfxr_mic_on()) return;
    float buf[4096];
    int n;
    while ((n = sfxr_mic_read(buf, 4096)) > 0) {
        for (int i = 0; i < n; i++) {
            if (V.listening) { if (V.nclip < MAX_CLIP) V.clip[V.nclip++] = buf[i]; }
            else { V.pre[V.pre_w] = buf[i]; V.pre_w = (V.pre_w + 1) % PREROLL; }
        }
    }
}

void sfxr_voice_listen_begin(void)
{
    if (V.listening) return;
    V.listening = true;
    V.nclip = 0;
    for (int i = 0; i < PREROLL; i++) V.clip[V.nclip++] = V.pre[(V.pre_w + i) % PREROLL];   // oldest first
    sfxr_event("voice", "listening");
}

void sfxr_voice_feed(const float *pcm, int n)
{
    for (int i = 0; i < n && V.nclip < MAX_CLIP; i++) V.clip[V.nclip++] = pcm[i];
}

static void *recognize(void *arg)
{
    (void)arg;
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    char text[256];
    if (V.transcribe(V.ctx, V.clip, V.nclip, V.prompt[0] ? V.prompt : NULL, V.threads, text, sizeof text) != 0) text[0] = 0;
    clock_gettime(CLOCK_MONOTONIC, &b);
    V.ms = (float)(b.tv_sec - a.tv_sec) * 1000.0f + (float)(b.tv_nsec - a.tv_nsec) / 1e6f;
    // Whisper's text starts with a space and may be "[BLANK_AUDIO]" or "(wind)" for no speech
    const char *t = text;
    while (*t == ' ') t++;
    snprintf(V.text, sizeof V.text, "%s", (*t == '[' || *t == '(') ? "" : t);
    atomic_store(&V.ready, true);
    atomic_store(&V.busy, false);
    return NULL;
}

void sfxr_voice_listen_end(void)
{
    if (!V.listening && V.nclip == 0) return;
    V.listening = false;
    sfxr_voice_update();
    if (!V.ctx || atomic_load(&V.busy)) { V.nclip = 0; return; }
    // Whisper wants at least a second: pad short clips with silence
    while (V.nclip < RATE + RATE / 10 && V.nclip < MAX_CLIP) V.clip[V.nclip++] = 0;
    atomic_store(&V.busy, true);
    sfxr_event("voice", "recognizing %.1f s", (float)V.nclip / RATE);
    if (pthread_create(&V.thread, NULL, recognize, NULL) != 0) atomic_store(&V.busy, false);
    else pthread_detach(V.thread);
}

bool sfxr_voice_result(char *text, int size)
{
    if (!atomic_load(&V.ready)) return false;
    atomic_store(&V.ready, false);
    snprintf(text, (size_t)size, "%s", V.text);
    sfxr_event("voice", "heard \"%s\" in %.0f ms", V.text, V.ms);
    return true;
}

// --- matching words to commands -------------------------------------------------------

static int edit_distance(const char *a, const char *b)
{
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la > 31 || lb > 31) return 99;
    int d[32][32];
    for (int i = 0; i <= la; i++) d[i][0] = i;
    for (int j = 0; j <= lb; j++) d[0][j] = j;
    for (int i = 1; i <= la; i++)
        for (int j = 1; j <= lb; j++) {
            int c = d[i - 1][j - 1] + (a[i - 1] != b[j - 1]);
            if (d[i - 1][j] + 1 < c) c = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < c) c = d[i][j - 1] + 1;
            d[i][j] = c;
        }
    return d[la][lb];
}

int sfxr_voice_match(const char *text, const char *const *commands, int count)
{
    if (!text) return -1;
    // the words, lower case, letters only
    char words[16][32];
    int nw = 0, len = 0;
    for (const char *s = text;; s++) {
        if (isalpha((unsigned char)*s) && len < 31 && nw < 16) words[nw][len++] = (char)tolower((unsigned char)*s);
        else if (len > 0) { words[nw][len] = 0; nw++; len = 0; }
        if (!*s || nw >= 16) break;
    }
    // the closest match; on a tie, the one said first ("return, then attack" is return)
    int best = -1, best_d = 99, best_w = 99;
    for (int c = 0; c < count; c++) {
        char cmd[32];
        snprintf(cmd, sizeof cmd, "%s", commands[c]);
        for (char *p = cmd; *p; p++) *p = (char)tolower((unsigned char)*p);
        // a letter off for short words, two for long ones; none for tiny ones ("go" isn't "no")
        size_t cl = strlen(cmd);
        int allow = SFXR_BREAK(sfxr_voice_match_exact) || cl <= 3 ? 0 : cl <= 5 ? 1 : 2;
        for (int w = 0; w < nw; w++) {
            int d = edit_distance(words[w], cmd);
            if (d <= allow && (d < best_d || (d == best_d && w < best_w))) { best = c; best_d = d; best_w = w; }
        }
    }
    return best;
}
