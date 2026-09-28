// sfq_speech.c - the speech shim: a tiny, stable C interface over whisper.cpp,
// built as libsfq_speech.so by scripts/get-speech.sh (docs/AUDIO.md).
//
// Why a shim: whisper.cpp's own parameter struct changes between releases,
// so sfxr can't safely declare it itself the way it does Steam's flat API.
// This file is compiled against the whisper.h it ships with, and sfxr only
// ever sees these three functions (loaded with dlopen: without the library
// there's simply no voice, and the quickstart still builds and runs).

#include "whisper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// whisper.cpp and ggml log every detail of loading to stderr; the app's log
// only needs sfxr's one line ("voice: on ...").
static void quiet(enum ggml_log_level level, const char *text, void *user) { (void)level; (void)text; (void)user; }

void *sfq_speech_open(const char *model_path, int threads)
{
    (void)threads;
    whisper_log_set(quiet, NULL);
    struct whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = false;   // the Frame's GPU is busy drawing the frames; the CPU has cores to spare
    return whisper_init_from_file_with_params(model_path, cp);
}

// Transcribe 16 kHz mono float audio. `prompt` biases it toward the words
// you expect ("attack, return, stop"): short commands are exactly what
// Whisper mishears without context. 0 on success.
int sfq_speech_transcribe(void *ctx, const float *pcm, int n, const char *prompt, int threads, char *out, int out_size)
{
    if (!ctx || !out || out_size < 1) return -1;
    out[0] = 0;
    struct whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.n_threads = threads > 0 ? threads : 4;
    p.language = "en";
    p.translate = false;
    p.no_context = true;
    p.no_timestamps = true;
    p.single_segment = true;
    p.print_progress = false;
    p.print_realtime = false;
    p.print_special = false;
    p.print_timestamps = false;
    p.suppress_blank = true;
    p.max_tokens = 16;               // commands are a few words
    p.initial_prompt = prompt;
    // Whisper works on 30 s windows (1500 encoder positions). For a command of
    // a few seconds, a smaller window is several times faster -- but below
    // about 512 the tiny model starts repeating itself ("attack attack
    // attack..."), so 512 is the floor. Measured on an ARM64 build machine:
    // full window 350 ms, 512 about 120 ms, same words.
    // SFQ_SPEECH_AUDIO_CTX overrides it (0 = the full window).
    int ctx_frames = n / 160 / 2 + 64;   // 10 ms per mel frame, 2 per encoder position, some margin
    if (ctx_frames < 512) ctx_frames = 512;
    const char *env = getenv("SFQ_SPEECH_AUDIO_CTX");
    p.audio_ctx = env ? atoi(env) : ctx_frames < 1500 ? ctx_frames : 0;
    if (whisper_full((struct whisper_context *)ctx, p, pcm, n) != 0) return -2;
    int segs = whisper_full_n_segments((struct whisper_context *)ctx);
    for (int i = 0; i < segs; i++) {
        const char *t = whisper_full_get_segment_text((struct whisper_context *)ctx, i);
        strncat(out, t, (size_t)(out_size - 1) - strlen(out));
    }
    return 0;
}

void sfq_speech_close(void *ctx)
{
    if (ctx) whisper_free((struct whisper_context *)ctx);
}
