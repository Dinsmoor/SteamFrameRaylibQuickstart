// sfq_listen - try the speech recognizer on a sound file, or the
// microphone: what it heard, how long it took, which command it matched.
//
//   build/host-debug/bin/sfq_listen said.wav [attack,return,stop]
//   build/host-debug/bin/sfq_listen --mic 2 [attack,return,stop]    (listen for 2 s)
//
// Needs scripts/get-speech.sh (the recognizer and the model); docs/AUDIO.md.

#include "sfxr.h"
#include "sfxr_audio.h"
#include "sfxr_voice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void sfxr_voice_update(void);   // (sfxr_frame_begin calls it in an app)

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: sfq_listen FILE.wav [cmd,cmd] | --mic SECONDS [cmd,cmd]\n"); return 2; }
    SetTraceLogLevel(LOG_WARNING);
    const char *cmds_arg = argc > (strcmp(argv[1], "--mic") ? 2 : 3) ? argv[strcmp(argv[1], "--mic") ? 2 : 3] : "attack,return,stop";
    char buf[256];
    snprintf(buf, sizeof buf, "%s", cmds_arg);
    const char *cmds[16];
    int n = 0;
    for (char *t = strtok(buf, ","); t && n < 16; t = strtok(NULL, ",")) cmds[n++] = t;
    char prompt[300] = "";
    for (int i = 0; i < n; i++) { strcat(prompt, cmds[i]); if (i + 1 < n) strcat(prompt, ", "); }

    if (!sfxr_voice_init(NULL)) { printf("%s\n", sfxr_voice_status()); return 1; }
    sfxr_voice_set_prompt(prompt);
    sfxr_voice_listen_begin();
    if (!strcmp(argv[1], "--mic")) {
        float secs = argc > 2 ? (float)atof(argv[2]) : 2.0f;
        printf("listening for %.1f s...\n", secs);
        for (int i = 0; i < (int)(secs * 100); i++) { usleep(10000); sfxr_voice_update(); }
    } else {
        Wave w = LoadWave(argv[1]);
        if (!w.frameCount) { printf("can't read %s\n", argv[1]); return 1; }
        WaveFormat(&w, 16000, 32, 1);   // 16 kHz mono float
        sfxr_voice_feed((const float *)w.data, (int)w.frameCount);
        UnloadWave(w);
    }
    sfxr_voice_listen_end();
    char said[256];
    while (!sfxr_voice_result(said, sizeof said)) usleep(5000);
    int c = sfxr_voice_match(said, cmds, n);
    printf("heard: \"%s\" (%.0f ms)  ->  %s\n", said, sfxr_voice_last_ms(), c >= 0 ? cmds[c] : "no command");
    sfxr_voice_shutdown();
    return 0;
}
