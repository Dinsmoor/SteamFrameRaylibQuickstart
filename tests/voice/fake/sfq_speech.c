// A fake speech recognizer with the real shim's three functions, so
// tests/voice can check the pipeline (push to talk, the background thread,
// the prompt, matching) without whisper.cpp or a model. It "hears" whatever
// FAKE_SPEECH says, and adds "[prompt]" when it was given the command words.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *sfq_speech_open(const char *model_path, int threads) { (void)model_path; (void)threads; static int ctx; return &ctx; }

int sfq_speech_transcribe(void *ctx, const float *pcm, int n, const char *prompt, int threads, char *out, int out_size)
{
    (void)ctx; (void)pcm; (void)threads;
    const char *said = getenv("FAKE_SPEECH");
    snprintf(out, (size_t)out_size, " %s%s (%d samples)", said ? said : "", prompt && *prompt ? " [prompt]" : "", n);
    return 0;
}

void sfq_speech_close(void *ctx) { (void)ctx; }
