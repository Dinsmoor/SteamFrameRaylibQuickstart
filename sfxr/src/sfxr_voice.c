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
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
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

    // hands-free: a speech detector on 20 ms blocks
    bool free_on, free_clip;    // on; the current clip was started by it
    float free_threshold;
    int free_pause;             // samples of quiet that end a clip
    float block_sum;
    int block_n, loud_run, quiet_run;
} V;

// Design notes (the end of this file): a longer clip, saved rather than recognized.
#define NOTE_MAX (RATE * 90)
static struct {
    float *pcm;
    int n;
    bool on;
    int count;
    char last[48];
    char dir[512];
} N;

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

void sfxr_voice_hands_free(bool on, float threshold, float pause_s)
{
    if (!on && V.free_clip && V.listening) sfxr_voice_listen_end();
    V.free_on = on;
    V.free_threshold = threshold > 0 ? threshold : 0.15f;
    V.free_pause = (int)((pause_s > 0 ? pause_s : 0.6f) * RATE);
    V.loud_run = V.quiet_run = 0;
    sfxr_event("voice", "hands-free %s", on ? "on" : "off");
}
bool sfxr_voice_hands_free_on(void) { return V.free_on; }
bool sfxr_voice_hands_free_heard(void) { return V.free_clip; }

// The speech detector, on each 20 ms block: loud (at the level meter's
// scale) for 60 ms starts a clip, quiet for the pause ends it; a clip also
// ends at 6 s, whatever is going on.
static void detect(float block_level)
{
    const int block = RATE / 50;
    bool loud = block_level > (SFXR_BREAK(sfxr_voice_any_sound_starts) ? 0.0f : V.free_threshold);
    if (!V.listening) {
        V.loud_run = loud ? V.loud_run + block : 0;
        if (V.loud_run >= RATE * 6 / 100 && !atomic_load(&V.busy)) {
            sfxr_voice_listen_begin();
            V.free_clip = true;
            V.quiet_run = 0;
        }
        return;
    }
    if (!V.free_clip) return;   // a button's clip: the button ends it
    V.quiet_run = loud ? 0 : V.quiet_run + block;
    if (V.quiet_run >= V.free_pause || V.nclip >= RATE * 6) {
        V.loud_run = 0;
        sfxr_voice_listen_end();
    }
}

// Samples as the microphone hears them: into the clip, or the moment-before.
static void take(const float *s, int n)
{
    const int block = RATE / 50;
    for (int i = 0; i < n; i++) {
        if (N.on && N.n < NOTE_MAX) N.pcm[N.n++] = s[i];
        if (V.listening) { if (V.nclip < MAX_CLIP) V.clip[V.nclip++] = s[i]; }
        else { V.pre[V.pre_w] = s[i]; V.pre_w = (V.pre_w + 1) % PREROLL; }
        if (!V.free_on) continue;
        V.block_sum += s[i] * s[i];
        if (++V.block_n == block) {
            float level = sqrtf(V.block_sum / (float)block) * 4.0f;   // the same scale as sfxr_mic_level
            V.block_sum = 0;
            V.block_n = 0;
            detect(level > 1 ? 1 : level);
        }
    }
}

// Every frame (sfxr_frame_begin): keep the pre-roll fresh, or grow the clip.
void sfxr_voice_update(void)
{
    if (!sfxr_mic_on()) return;
    float buf[4096];
    int n;
    while ((n = sfxr_mic_read(buf, 4096)) > 0) take(buf, n);
}

void sfxr_voice_listen_begin(void)
{
    if (V.listening) return;
    V.listening = true;
    V.free_clip = false;
    V.nclip = 0;
    for (int i = 0; i < PREROLL; i++) V.clip[V.nclip++] = V.pre[(V.pre_w + i) % PREROLL];   // oldest first
    sfxr_event("voice", "listening");
}

void sfxr_voice_feed(const float *pcm, int n) { take(pcm, n); }

// Whisper sometimes gets stuck in a loop on a short clip and says a word (or
// a few) over and over: "return return return return". Keep the first of
// each run of repeats -- a word or a phrase of up to four words, ignoring
// case and punctuation -- so a looped "return" is still just "return".
static void collapse_repeats(char *text)
{
    enum { MAXW = 48 };
    char *word[MAXW], key[MAXW][32];
    int n = 0;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", text);
    for (char *t = strtok(buf, " "); t && n < MAXW; t = strtok(NULL, " ")) {
        word[n] = t;
        int k = 0;
        for (const char *c = t; *c && k < 31; c++)
            if (isalnum((unsigned char)*c)) key[n][k++] = (char)tolower((unsigned char)*c);
        key[n][k] = 0;
        n++;
    }
    for (int len = 1; len <= 4; len++) {
        for (int i = 0; i + 2 * len <= n;) {
            bool same = true;
            for (int j = 0; j < len && same; j++) same = key[i + j][0] && strcmp(key[i + j], key[i + len + j]) == 0;
            if (!same) { i++; continue; }
            // drop the second copy, and look again from the same place
            memmove(&word[i + len], &word[i + 2 * len], (size_t)(n - i - 2 * len) * sizeof word[0]);
            memmove(&key[i + len], &key[i + 2 * len], (size_t)(n - i - 2 * len) * sizeof key[0]);
            n -= len;
        }
    }
    int at = 0;
    text[0] = 0;
    for (int i = 0; i < n; i++) at += snprintf(text + at, (size_t)(256 - at > 0 ? 256 - at : 0), "%s%s", i ? " " : "", word[i]);
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
    if (!SFXR_BREAK(sfxr_voice_keeps_repeats)) collapse_repeats(V.text);
    atomic_store(&V.ready, true);
    atomic_store(&V.busy, false);
    return NULL;
}

void sfxr_voice_listen_end(void)
{
    if (!V.listening && V.nclip == 0) return;
    V.listening = false;
    if (!V.free_clip) sfxr_voice_update();   // (hands-free ends inside the update: don't re-enter it)
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

// --- design notes (sfxr_voice.h, docs/NOTES.md) --------------------------------------
// A clip of you describing what you want, saved with the moment it was said
// in, for the tools on the build machine to transcribe and act on. The same
// microphone ring as the commands; the note buffer is filled alongside the
// clip, so a command and a note can overlap without stealing samples.

static const char *note_dir(void)
{
    if (!N.dir[0]) {
        const char *d = sfxr_env_str("SFXR_NOTES_DIR");
        snprintf(N.dir, sizeof N.dir, "%s", d && *d ? d : "notes");
    }
    return N.dir;
}

bool sfxr_note_recording(void) { return N.on; }
float sfxr_note_seconds(void) { return (float)N.n / RATE; }
const char *sfxr_note_last(void) { return N.last; }
int sfxr_note_count(void) { return N.count; }

bool sfxr_note_begin(void)
{
    if (N.on) return true;
    bool mic = sfxr_mic_on() || (sfxr_backend() == SFXR_BACKEND_SCRIPT && !sfxr_env_flag("SFXR_AUDIO", false)) || sfxr_mic_start();
    if (!mic) { SFXR_WARN("note: no microphone"); return false; }
    if (!N.pcm) N.pcm = malloc(sizeof(float) * NOTE_MAX);
    if (!N.pcm) return false;
    N.n = 0;
    // people start talking as they press: the 0.3 s before the button
    if (!SFXR_BREAK(sfxr_note_no_preroll))
        for (int i = 0; i < PREROLL; i++) N.pcm[N.n++] = V.pre[(V.pre_w + i) % PREROLL];
    N.on = true;
    sfxr_event("note", "recording");
    return true;
}

static bool write_wav(const char *path, const float *pcm, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint32_t data = (uint32_t)n * 2, rate = RATE, byte_rate = RATE * 2, fmt_len = 16;
    uint16_t pcm_fmt = 1, channels = 1, block = 2, bits = 16;
    uint32_t riff = 36 + data;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmt_len, 4, 1, f); fwrite(&pcm_fmt, 2, 1, f); fwrite(&channels, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f); fwrite(&block, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (int i = 0; i < n; i++) {
        float v = pcm[i] > 1 ? 1 : pcm[i] < -1 ? -1 : pcm[i];
        int16_t s = (int16_t)(v * 32767.0f);
        fwrite(&s, 2, 1, f);
    }
    return fclose(f) == 0;
}

static void jvec(FILE *f, Vector3 v) { fprintf(f, "[%.3f,%.3f,%.3f]", v.x, v.y, v.z); }
static void jpose(FILE *f, const char *key, SfxrPose p)
{
    fprintf(f, "\"%s\":{\"pos\":", key); jvec(f, p.position);
    fprintf(f, ",\"forward\":"); jvec(f, sfxr_pose_forward(p));
    fprintf(f, "}");
}

bool sfxr_note_end(const char *context_json)
{
    if (!N.on) return false;
    sfxr_voice_update();   // the last samples
    N.on = false;
    if (N.n < RATE / 4) { sfxr_event("note", "dropped: %.2f s", (float)N.n / RATE); return false; }

    const char *dir = note_dir();
    if (!DirectoryExists(dir)) MakeDirectory(dir);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    static int last_sec = -1, same = 0;
    same = (int)now == last_sec ? same + 1 : 0;
    last_sec = (int)now;
    char id[48];
    snprintf(id, sizeof id, "note-%04d%02d%02d-%02d%02d%02d%s", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, same ? TextFormat("-%d", same) : "");
    char path[600];
    snprintf(path, sizeof path, "%s/%s.wav", dir, id);
    if (!write_wav(path, N.pcm, N.n)) { SFXR_WARN("note: can't write %s", path); return false; }

    snprintf(path, sizeof path, "%s/%s.json", dir, id);
    FILE *f = fopen(path, "w");
    if (!f) { SFXR_WARN("note: can't write %s", path); return false; }
    fprintf(f, "{\"id\":\"%s\",\"seconds\":%.2f,\"time\":%.3f,\"frame\":%llu,\"wav\":\"%s.wav\",\"screenshot\":\"%s.png\",",
            id, (float)N.n / RATE, sfxr_time(), (unsigned long long)sfxr_frame_index(), id, id);
    fprintf(f, "\"clock\":\"%04d-%02d-%02dT%02d:%02d:%02d\",", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    jpose(f, "head", sfxr_head());
    fprintf(f, ",\"hands\":{");
    for (int h = 0; h < SFXR_HAND_COUNT; h++) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        fprintf(f, "%s\"%s\":", h ? "," : "", h == SFXR_LEFT ? "left" : "right");
        if (!hand->active) { fprintf(f, "null"); continue; }
        fprintf(f, "{"); jpose(f, "aim", hand->aim); fprintf(f, ","); jpose(f, "grip", hand->grip); fprintf(f, "}");
    }
    fprintf(f, "}");
    SfxrPose gaze;
    if (sfxr_gaze(&gaze)) { fprintf(f, ","); jpose(f, "gaze", gaze); }
    fprintf(f, ",\"context\":%s}\n", context_json && *context_json ? context_json : "null");
    fclose(f);

    snprintf(path, sizeof path, "%s/%s.png", dir, id);
    sfxr_screenshot(path);
    snprintf(N.last, sizeof N.last, "%s", id);
    N.count++;
    sfxr_event("note", "%s saved: %.1f s", id, (float)N.n / RATE);
    SFXR_LOG("note %s: %.1f s -> %s/", id, (float)N.n / RATE, dir);
    return true;
}
