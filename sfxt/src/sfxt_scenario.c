// sfxt_scenario.c - .sfxt scenario files: test choreography in plain lines,
// run by the same harness as the C tests (docs/TESTING.md, "The test
// language"). Every command is one sfxt_* call, so nothing here is
// scenario-only.
//
//   # tests/toolbox/workbench.sfxt
//   case push-sky-lever
//     fails-if toolbox_sky_unwired
//     hand    R to table.sky/handle over 0.4s
//     grip    R 1
//     expect  grab table.sky by R within 5f
//     hand    R move 0,0,-20cm in table.sky over 0.6s
//     grip    R 0
//     expect  value table.sky >= 0.8
//     expect  app sky >= 0.8
//
// Declarative on purpose: no variables, loops or conditions (write those in
// C). Time moves only on `over T`, `wait T`, `expect ... within T` and
// `expect no ... for T`; everything else happens in the current frame.

#include "sfxt.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 4096
#define MAX_TOK   16

typedef struct {
    char file[64];          // "workbench" (the file's name without .sfxt)
    int  line;
    char text[256];
    bool is_case;
    char case_name[112];
    char fails_if[48];      // on a case line: its break switch (from its fails-if line)
} Line;

static Line L[MAX_LINES];
static int nlines;

// --- reading the files ------------------------------------------------------------

static void trim(char *s)
{
    char *h = strchr(s, '#');
    if (h) *h = 0;
    int n = (int)strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static void load_file(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char base[64];
    snprintf(base, sizeof base, "%s", name);
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;
    char buf[256];
    int ln = 0, last_case = -1;
    while (fgets(buf, sizeof buf, f) && nlines < MAX_LINES) {
        ln++;
        trim(buf);
        char *s = buf;
        while (isspace((unsigned char)*s)) s++;
        if (!*s) continue;
        Line *l = &L[nlines];
        memset(l, 0, sizeof *l);
        snprintf(l->file, sizeof l->file, "%s", base);
        l->line = ln;
        snprintf(l->text, sizeof l->text, "%s", s);
        if (!strncmp(s, "case ", 5)) {
            l->is_case = true;
            snprintf(l->case_name, sizeof l->case_name, "%s/%s", base, s + 5);
            last_case = nlines;
        } else if (!strncmp(s, "fails-if ", 9) && last_case >= 0) {
            snprintf(L[last_case].fails_if, sizeof L[0].fails_if, "%s", s + 9);
        }
        nlines++;
    }
    fclose(f);
}

static int by_name(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static void load_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;
    char *names[256];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < 256) {
        size_t len = strlen(e->d_name);
        if (len > 5 && !strcmp(e->d_name + len - 5, ".sfxt")) names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], by_name);
    for (int i = 0; i < n; i++) { load_file(dir, names[i]); free(names[i]); }
}

// --- words and units ------------------------------------------------------------------

static const Line *cur;   // the line being run (for failure messages)
// Event expectations look at everything since the previous one (or the case
// start): "poke it over 0.4s, then expect the flip" sees a flip that happened
// during the poke.
static uint64_t mark;

static void fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void fail(const char *fmt, ...)
{
    char msg[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    char file[80];
    snprintf(file, sizeof file, "%s.sfxt", cur->file);
    sfxt_check(false, cur->text, file, cur->line, "%s", msg);
}

// "0.4s", "250ms", "3f" -> seconds (a frame is 1/72 s)
static bool parse_time(const char *s, float *out)
{
    char *end;
    float v = strtof(s, &end);
    if (end == s) return false;
    if (!strcmp(end, "s")) *out = v;
    else if (!strcmp(end, "ms")) *out = v / 1000.0f;
    else if (!strcmp(end, "f")) *out = v / 72.0f;
    else return false;
    return true;
}

// "20cm", "5mm", "0.2" (meters)
static bool parse_len(const char *s, float *out)
{
    char *end;
    float v = strtof(s, &end);
    if (end == s) return false;
    if (!*end || !strcmp(end, "m")) *out = v;
    else if (!strcmp(end, "cm")) *out = v / 100.0f;
    else if (!strcmp(end, "mm")) *out = v / 1000.0f;
    else return false;
    return true;
}

static bool parse_vec(const char *s, Vector3 *out)
{
    char buf[64];
    snprintf(buf, sizeof buf, "%s", s);
    char *parts[3];
    int n = 0;
    for (char *t = strtok(buf, ","); t && n < 3; t = strtok(NULL, ",")) parts[n++] = t;
    if (n != 3) return false;
    return parse_len(parts[0], &out->x) && parse_len(parts[1], &out->y) && parse_len(parts[2], &out->z);
}

static int parse_hand(const char *s)
{
    if (!strcmp(s, "R") || !strcmp(s, "right")) return SFXR_RIGHT;
    if (!strcmp(s, "L") || !strcmp(s, "left")) return SFXR_LEFT;
    return -1;
}

static bool compare(float a, const char *op, float b)
{
    if (!strcmp(op, ">=")) return a >= b;
    if (!strcmp(op, "<=")) return a <= b;
    if (!strcmp(op, ">")) return a > b;
    if (!strcmp(op, "<")) return a < b;
    if (!strcmp(op, "==")) return a == b;
    if (!strcmp(op, "~=")) return fabsf(a - b) <= 0.02f * fmaxf(1.0f, fabsf(b));
    return false;
}

// "over 0.4s" at the end of a line (default: this frame)
static float over(char **tok, int n)
{
    float t = 0;
    for (int i = 0; i + 1 < n; i++)
        if (!strcmp(tok[i], "over") && !parse_time(tok[i + 1], &t)) fail("bad time '%s'", tok[i + 1]);
    return t;
}

static int find_word(char **tok, int n, const char *w)
{
    for (int i = 0; i < n; i++) if (!strcmp(tok[i], w)) return i;
    return -1;
}

static SfxrControl control_named(const char *s)
{
    static const struct { const char *n; SfxrControl c; } C[] = {
        { "a", SFXR_CTL_A }, { "b", SFXR_CTL_B }, { "x", SFXR_CTL_X }, { "y", SFXR_CTL_Y }, { "menu", SFXR_CTL_MENU },
        { "view", SFXR_CTL_VIEW }, { "stick", SFXR_CTL_STICK }, { "bumper", SFXR_CTL_BUMPER },
        { "dpad_up", SFXR_CTL_DPAD_UP }, { "dpad_down", SFXR_CTL_DPAD_DOWN }, { "dpad_left", SFXR_CTL_DPAD_LEFT },
        { "dpad_right", SFXR_CTL_DPAD_RIGHT },
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) if (!strcmp(s, C[i].n)) return C[i].c;
    return SFXR_CTL_COUNT;
}

// --- expect ---------------------------------------------------------------------------

// The label a widget's events carry ("SKY"), from the registry.
static const char *label_of(const char *target)
{
    char name[64];
    snprintf(name, sizeof name, "%s", target);
    char *slash = strchr(name, '/');
    if (slash) *slash = 0;
    const VruiWidgetInfo *w = vrui_find_widget(name, NULL);
    return w ? w->label : NULL;
}

static void expect(char **tok, int n)
{
    if (n < 2) { fail("expect what?"); return; }
    // expect value TARGET OP V / expect app KEY OP V
    if (!strcmp(tok[1], "value") || !strcmp(tok[1], "app")) {
        if (n < 5) { fail("expect %s NAME OP VALUE", tok[1]); return; }
        float v, want = strtof(tok[4], NULL);
        bool have = !strcmp(tok[1], "value") ? !isnan(v = sfxt_value(tok[2])) : sfxr_reported(tok[2], &v);
        if (!have) { fail("no %s '%s'", tok[1], tok[2]); return; }
        if (!compare(v, tok[3], want)) fail("%s %s is %.3f, want %s %s", tok[1], tok[2], v, tok[3], tok[4]);
        return;
    }
    // expect haptic H count|max OP V
    if (!strcmp(tok[1], "haptic")) {
        if (n < 6) { fail("expect haptic H count|max OP VALUE"); return; }
        int h = parse_hand(tok[2]);
        float v = !strcmp(tok[3], "max") ? sfxt_haptic_max((SfxrHandId)h) : (float)sfxt_haptic_count((SfxrHandId)h);
        if (!compare(v, tok[4], strtof(tok[5], NULL))) fail("haptic %s %s is %.2f, want %s %s", tok[2], tok[3], v, tok[4], tok[5]);
        return;
    }
    // expect [no] EVENT TARGET [by H] within|for T
    bool none = !strcmp(tok[1], "no");
    int k = none ? 2 : 1;
    if (n < k + 2) { fail("expect [no] EVENT TARGET within|for TIME"); return; }
    const char *event = tok[k], *target = tok[k + 1];
    int by = find_word(tok, n, "by"), hand = by > 0 && by + 1 < n ? parse_hand(tok[by + 1]) : -1;
    int w = find_word(tok, n, none ? "for" : "within");
    float window = 0;
    if (w > 0 && w + 1 < n && !parse_time(tok[w + 1], &window)) { fail("bad time '%s'", tok[w + 1]); return; }
    const char *label = label_of(target);
    if (!label) { fail("no widget '%s'", target); return; }
    uint64_t since = mark;
    int frames = (int)lroundf(window * 72.0f);
    for (int f = 0;; f++) {
        int seen = sfxt_events(event, label, hand, since);
        if (!none && seen > 0) { mark = sfxt_frame() + 1; return; }
        if (none && seen > 0) { mark = sfxt_frame() + 1; fail("%s %s happened", event, target); return; }
        if (f >= frames) break;
        sfxt_frames(1);
    }
    mark = sfxt_frame() + 1;
    if (!none) fail("no %s of %s%s%s within %d frames", event, target, hand >= 0 ? " by " : "", hand >= 0 ? tok[by + 1] : "", frames);
}

// --- one line ---------------------------------------------------------------------------

static void run_line(const Line *l)
{
    cur = l;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", l->text);
    char *tok[MAX_TOK];
    int n = 0;
    for (char *t = strtok(buf, " \t"); t && n < MAX_TOK; t = strtok(NULL, " \t")) tok[n++] = t;
    if (!n) return;
    const char *c = tok[0];

    if (!strcmp(c, "fails-if") || !strcmp(c, "app")) return;   // for the runner / documentation
    if (!strcmp(c, "rate")) { if (n < 2 || atoi(tok[1]) != 72) fail("only rate 72 is supported"); return; }
    if (!strcmp(c, "noise")) {
        if (n >= 2 && !strcmp(tok[1], "off")) { sfxt_noise(0, 0, 0); return; }
        float pos = 0.001f, rot = 0.2f, analog = 0.01f;
        for (int i = 1; i < n; i++) {
            if (!strncmp(tok[i], "hand=", 5)) parse_len(tok[i] + 5, &pos);
            else if (!strncmp(tok[i], "rot=", 4)) rot = strtof(tok[i] + 4, NULL);
            else if (!strncmp(tok[i], "trigger=", 8)) analog = strtof(tok[i] + 8, NULL);
        }
        sfxt_noise(pos, rot, analog);
        return;
    }
    if (!strcmp(c, "wait")) {
        float t;
        if (n < 2 || !parse_time(tok[1], &t)) { fail("wait TIME"); return; }
        sfxt_wait(t);
        return;
    }
    if (!strcmp(c, "look")) {
        if (n >= 3 && !strcmp(tok[1], "at")) { sfxt_look_at(tok[2], over(tok, n)); return; }
        if (n >= 3 && !strcmp(tok[1], "dir")) {
            float yaw = 0, pitch = 0;
            sscanf(tok[2], "%f,%f", &yaw, &pitch);
            SfxrPose h = sfxt_head();
            h.orientation = QuaternionMultiply(QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, -yaw * DEG2RAD),
                                               QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, pitch * DEG2RAD));
            sfxt_head_to(h, over(tok, n));
            return;
        }
        fail("look at TARGET | look dir YAW,PITCH");
        return;
    }
    if (!strcmp(c, "expect")) { expect(tok, n); return; }
    // stand at TARGET [back D]: in front of it (on its +Z side, where the
    // row's aisle is), D away (default 0.6 m), facing -Z; teleport X,Y,Z
    if (!strcmp(c, "stand") && n >= 3 && !strcmp(tok[1], "at")) {
        SfxrPose w;
        if (!sfxt_find(tok[2], &w)) { fail("no widget '%s'", tok[2]); return; }
        float back = 0.6f;
        int b = find_word(tok, n, "back");
        if (b > 0 && b + 1 < n) parse_len(tok[b + 1], &back);
        Vector3 f = sfxr_pose_forward(sfxr_head());
        sfxr_rig_turn(atan2f(f.x, -f.z));   // face -Z
        sfxr_rig_teleport((Vector3){ w.position.x, 0, w.position.z + back });
        sfxt_frames(2);
        return;
    }
    if (!strcmp(c, "teleport") && n >= 2) {
        Vector3 p;
        if (!parse_vec(tok[1], &p)) { fail("teleport X,Y,Z"); return; }
        sfxr_rig_teleport(p);
        sfxt_frames(2);
        return;
    }

    // the rest act on a hand: VERB H ...
    int h = n >= 2 ? parse_hand(tok[1]) : -1;
    if (h < 0) { fail("unknown command, or a hand missing (L or R)"); return; }
    SfxrHandId H = (SfxrHandId)h;
    float t = over(tok, n);
    if (!strcmp(c, "trigger") || !strcmp(c, "grip")) {
        if (n < 3) { fail("%s H VALUE", c); return; }
        float v = strtof(tok[2], NULL);
        if (!strcmp(c, "trigger") && t > 0) sfxt_trigger_ramp(H, v, t);
        else if (!strcmp(c, "trigger")) sfxt_trigger(H, v);
        else sfxt_grip(H, v);
        return;
    }
    if (!strcmp(c, "stick")) {
        Vector2 s = { 0 };
        if (n < 3 || sscanf(tok[2], "%f,%f", &s.x, &s.y) != 2) { fail("stick H X,Y"); return; }
        sfxt_stick(H, s);
        return;
    }
    if (!strcmp(c, "button")) {
        if (n < 4) { fail("button H NAME down|up|press"); return; }
        SfxrControl ctl = control_named(tok[2]);
        if (ctl == SFXR_CTL_COUNT) { fail("no button '%s'", tok[2]); return; }
        if (!strcmp(tok[3], "press")) { sfxt_button(H, ctl, true); sfxt_frames(3); sfxt_button(H, ctl, false); }
        else sfxt_button(H, ctl, !strcmp(tok[3], "down"));
        return;
    }
    if (!strcmp(c, "hand")) {
        if (n < 3) { fail("hand H to|move|point|poke ..."); return; }
        const char *verb = tok[2];
        if (!strcmp(verb, "to") && n >= 4) {
            Vector3 off = { 0 };
            int o = find_word(tok, n, "offset");
            if (o > 0 && (o + 1 >= n || !parse_vec(tok[o + 1], &off))) { fail("offset X,Y,Z"); return; }
            sfxt_hand_to_target(H, tok[3], off, t > 0 ? t : 1.0f / 72.0f);
            return;
        }
        if (!strcmp(verb, "move") && n >= 4) {
            Vector3 d;
            if (!parse_vec(tok[3], &d)) { fail("move X,Y,Z"); return; }
            int in = find_word(tok, n, "in");
            SfxrPose frame;
            if (in > 0 && in + 1 < n) {   // along that widget's own axes
                if (!sfxt_find(tok[in + 1], &frame)) { fail("no widget '%s'", tok[in + 1]); return; }
                d = Vector3RotateByQuaternion(d, frame.orientation);
            }
            d = Vector3RotateByQuaternion(d, sfxt_to_tracking((SfxrPose){ { 0 }, QuaternionIdentity() }).orientation);   // world -> tracking
            SfxrPose g = sfxt_hand(H);
            g.position = Vector3Add(g.position, d);
            sfxt_hand_to(H, g, t);
            return;
        }
        if (!strcmp(verb, "point") && n >= 5 && !strcmp(tok[3], "at")) { sfxt_point_at(H, tok[4], t > 0 ? t : 0.2f); return; }
        if (!strcmp(verb, "poke") && n >= 4) {
            // the tip comes down onto the target from 4 cm above, 1.5 cm in, and back up
            SfxrPose w;
            if (!sfxt_find(tok[3], &w)) { fail("no widget '%s'", tok[3]); return; }
            Vector3 top = sfxt_to_tracking(w).position, down = { 0, -1, 0 };
            sfxt_hand_to(H, sfxt_tip_pose(Vector3Add(top, (Vector3){ 0, 0.04f, 0 }), down), 0.3f);
            sfxt_hand_to(H, sfxt_tip_pose(Vector3Add(top, (Vector3){ 0, -0.015f, 0 }), down), 0.2f);
            sfxt_hand_to(H, sfxt_tip_pose(Vector3Add(top, (Vector3){ 0, 0.05f, 0 }), down), 0.2f);
            return;
        }
        fail("hand H to TARGET | move X,Y,Z [in TARGET] | point at TARGET | poke TARGET");
        return;
    }
    fail("unknown command '%s'", c);
}

// --- the runner --------------------------------------------------------------------------

int sfxt_scenario_main(int argc, char **argv, const char *dir, void (*scene)(void), void (*setup)(void), void (*draw)(void))
{
    load_dir(dir);
    if (argc < 2 || !strcmp(argv[1], "--list")) {
        for (int i = 0; i < nlines; i++)
            if (L[i].is_case) printf("%s %s\n", L[i].case_name, L[i].fails_if[0] ? L[i].fails_if : "-");
        return argc < 2 ? 2 : 0;
    }
    int start = -1;
    for (int i = 0; i < nlines; i++) if (L[i].is_case && !strcmp(L[i].case_name, argv[1])) start = i;
    if (start < 0) { fprintf(stderr, "no case '%s' (--list shows them)\n", argv[1]); return 2; }

    sfxt__begin(L[start].case_name, scene);
    mark = 0;
    sfxt_set_draw(draw);
    if (setup) setup();
    sfxt_frames(3);
    // the file's header (lines before its first case), then the case
    for (int i = 0; i < start; i++) {
        if (strcmp(L[i].file, L[start].file)) continue;
        if (L[i].is_case) break;
        run_line(&L[i]);
    }
    for (int i = start + 1; i < nlines && !L[i].is_case && !strcmp(L[i].file, L[start].file); i++) run_line(&L[i]);
    return sfxt__finish();
}
