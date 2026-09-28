// sfxrec_dump - print an sfxr input recording as CSV (one row per frame and hand).
//
//   sfxrec_dump session.sfxrec            > frames.csv
//   sfxrec_dump session.sfxrec --summary        header + per-hand input ranges
//
// Columns: frame, t (s), hand, active, source (ctrl/hand), grip xyz, head
// yaw/pitch (deg), trigger, grip, stick xy, clicked and touched controls
// (e.g. "trig+A"), worn (1/0/?), refresh Hz, battery, hand-joint source,
// profile. Reads current and older (v1, v2) recordings.
// Handy for questions like "how hard did the player actually squeeze?".
#include "sfxr_rec.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void yaw_pitch(Quaternion q, float *yaw, float *pitch)
{
    Vector3 f = Vector3RotateByQuaternion((Vector3){ 0, 0, -1 }, q);
    *yaw = atan2f(-f.x, -f.z) * RAD2DEG;
    *pitch = asinf(fmaxf(-1.0f, fminf(1.0f, f.y))) * RAD2DEG;
}

static const char *battery_pct(float level)   // battery as "85%"
{
    static char buf[16];
    snprintf(buf, sizeof buf, "%.0f%%", level * 100.0f);
    return buf;
}

static const char *CTL_SHORT[SFXR_CTL_COUNT] = {
    "trig", "grip", "stick", "bump", "A", "B", "X", "Y", "menu", "view", "up", "down", "left", "right",
};

static void bits(uint32_t v, char *out, size_t n)
{
    out[0] = 0;
    for (int c = 0; c < SFXR_CTL_COUNT; c++)
        if (v & RAW_BIT(c)) {
            size_t l = strlen(out);
            snprintf(out + l, n - l, "%s%s", l ? "+" : "", CTL_SHORT[c]);
        }
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: sfxrec_dump file.sfxrec [--summary]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    RecHeader h;
    if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, REC_MAGIC, sizeof REC_MAGIC) != 0) {
        fprintf(stderr, "not an sfxr recording\n"); return 1;
    }
    int layout = sfxr_rec_layout(&h);
    if (!layout) {
        fprintf(stderr, "recorded by an incompatible sfxr build (version %u, frame %u bytes; this build %u, %zu)\n",
                h.version, h.frame_size, REC_VERSION, sizeof(RecFrame));
        return 1;
    }
    bool summary = argc > 2 && !strcmp(argv[2], "--summary");
    if (summary) {
        printf("app: %s\nsystem: %s\nruntime: %s\neye: %dx%d %s\nformat: v%u\n", h.app, h.system, h.runtime,
               h.eye_w, h.eye_h, h.stereo ? "stereo" : "mono", h.version);
    } else {
        printf("frame,t,hand,active,source,x,y,z,head_yaw,head_pitch,trigger,squeeze,stick_x,stick_y,"
               "clicks,touches,worn,refresh_hz,battery,joints,profile\n");
    }
    RecFrame fr;
    double t = 0;
    long frames = 0;
    float max_trig[2] = {0}, max_sq[2] = {0};
    long clicks[2][SFXR_CTL_COUNT] = {{0}}, touches[2][SFXR_CTL_COUNT] = {{0}};
    while (sfxr_rec_read(f, layout, &fr)) {
        t += fr.dt;
        frames++;
        float hy, hp;
        yaw_pitch(fr.head.orientation, &hy, &hp);
        for (int i = 0; i < 2; i++) {
            const SfxrRawHand *r = &fr.raw[i];
            if (r->trigger > max_trig[i]) max_trig[i] = r->trigger;
            if (r->squeeze > max_sq[i]) max_sq[i] = r->squeeze;
            for (int c = 0; c < SFXR_CTL_COUNT; c++) {
                clicks[i][c] += (r->click >> c) & 1;
                touches[i][c] += (r->touch >> c) & 1;
            }
            if (summary) continue;
            char cb[128], tb[128];
            bits(r->click, cb, sizeof cb);
            bits(r->touch, tb, sizeof tb);
            const SfxrBattery *bat = &fr.sig.battery[i];
            printf("%llu,%.4f,%c,%d,%s,%.4f,%.4f,%.4f,%.1f,%.1f,%.3f,%.3f,%.3f,%.3f,%s,%s,%s,%.0f,%s,%s,%s\n",
                   (unsigned long long)fr.frame, t, i ? 'R' : 'L', r->active,
                   r->source == SFXR_SOURCE_HAND ? "hand" : r->source == SFXR_SOURCE_CONTROLLER ? "ctrl" : "-",
                   r->grip.position.x, r->grip.position.y, r->grip.position.z, hy, hp,
                   r->trigger, r->squeeze, r->stick.x, r->stick.y, cb, tb,
                   !fr.sig.presence_known ? "?" : fr.sig.present ? "1" : "0", fr.sig.refresh_hz,
                   bat->valid ? battery_pct(bat->level) : "-",
                   !fr.sig.joints[i].valid ? "-" : fr.sig.joints[i].source == SFXR_SOURCE_HAND ? "hand" : "ctrl", r->profile);
        }
    }
    if (summary) {
        printf("frames: %ld (%.1f s)\n", frames, t);
        for (int i = 0; i < 2; i++) {
            printf("%s hand: max trigger %.2f, max grip %.2f\n   frames clicked/touched:",
                   i ? "right" : "left", max_trig[i], max_sq[i]);
            for (int c = 0; c < SFXR_CTL_COUNT; c++)
                if (clicks[i][c] || touches[i][c]) printf(" %s %ld/%ld", CTL_SHORT[c], clicks[i][c], touches[i][c]);
            printf("\n");
        }
    }
    fclose(f);
    return 0;
}
