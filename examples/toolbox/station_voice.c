// station_voice.c - Sound & voice, at the left end of the row (docs/AUDIO.md).
//
//   The speaker   flip its switch on and it chimes every second or so (while
//                 you're near it), from where it is: pick it up
//                 and carry it round your head. Left, right, behind, far:
//                 you hear where it is (sfxr_sound_play at a position).
//   The bugs      three little bugs, and a cardboard Daddy. Point at a bug
//                 with the right laser, HOLD the bumper (above the trigger),
//                 say "attack", "return" or "stop", let go: it does it. Point
//                 and speak -- the Bugmaster's controls in Revenge of the
//                 Bugmaster, in miniature. No recognizer installed
//                 (scripts/get-speech.sh)? The panel's buttons do the same.
//   TALK button   a big push-to-talk button on the table: hold it down
//                 (fingertip, or laser + trigger), speak, let go. What was
//                 heard is shown on the board above it, and if it was a
//                 command, the last bug you pointed at does it.
//   The panel     what the recognizer heard, how long it took, the
//                 microphone's level, and every sound the toolbox makes.
//
// Push-to-talk is on purpose: other people in the room, and the game's own
// sounds, can't give commands, and recognition runs once per command on a
// short clip instead of listening all the time.

#include "toolbox.h"
#include "sfxr_audio.h"
#include "sfxr_voice.h"

#include <stdio.h>
#include <string.h>

#define X0 -9.8f

static const char *const CMDS[] = { "attack", "return", "stop" };
enum { CMD_ATTACK, CMD_RETURN, CMD_STOP };

typedef struct { Vector3 home, pos; int order; float hop; } Minion;

static struct {
    bool init;
    SfxrPose speaker, panel;
    bool speaker_held;
    bool chime_on;          // the switch beside it (off to start: a sound that repeats wears thin)
    float chime_t;
    Minion bug[3];
    int selected;           // the bug the laser was on when you started talking (-1 none)
    char heard[128], result[96];
    float heard_ms;
    bool talking;
    bool button_talking;    // the TALK button is held
    char board[160];        // the transcript board's text
} VO;

static Vector3 daddy_at(void) { return (Vector3){ X0 + 0.6f, TABLE_Y, ROW_Z - 0.15f }; }

static void init(void)
{
    VO.speaker = (SfxrPose){ { X0 - 0.45f, TABLE_Y + 0.08f, ROW_Z + 0.1f }, QuaternionIdentity() };
    VO.panel = (SfxrPose){ { X0 - 0.45f, 1.45f, ROW_Z - 0.25f }, QuaternionIdentity() };
    for (int i = 0; i < 3; i++) {
        VO.bug[i].home = (Vector3){ X0 - 0.25f + 0.2f * (float)i, TABLE_Y, ROW_Z + 0.2f };
        VO.bug[i].pos = VO.bug[i].home;
        VO.bug[i].order = CMD_STOP;
    }
    VO.selected = -1;
    snprintf(VO.result, sizeof VO.result, "point at a bug, hold the bumper, speak");
    // the words to listen for: short commands are what Whisper mishears without them
    sfxr_voice_set_prompt("attack, return, stop");
    VO.init = true;
}

static void order(int bug, int cmd, const char *how)
{
    if (bug < 0 || bug > 2) { snprintf(VO.result, sizeof VO.result, "%s: but no bug was pointed at", CMDS[cmd]); return; }
    VO.bug[bug].order = cmd;
    snprintf(VO.result, sizeof VO.result, "bug %d: %s (%s)", bug + 1, CMDS[cmd], how);
    sound_play(SND_BLIP, VO.bug[bug].pos, 0.8f);
    sfxr_event("order", "bug %d %s by %s", bug + 1, CMDS[cmd], how);
}

// Which bug the right laser is on (-1 none).
static int pointed_bug(void)
{
    const SfxrHand *h = sfxr_hand(SFXR_RIGHT);
    Ray ray = { h->aim.position, sfxr_pose_forward(h->aim) };
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < 3; i++) {
        RayCollision rc = GetRayCollisionSphere(ray, Vector3Add(VO.bug[i].pos, (Vector3){ 0, 0.04f, 0 }), 0.07f);
        if (rc.hit && rc.distance < bd) { bd = rc.distance; best = i; }
    }
    return best;
}

static void minions(void)
{
    float dt = sfxr_dt();
    int pointed = pointed_bug();
    for (int i = 0; i < 3; i++) {
        Minion *m = &VO.bug[i];
        Vector3 goal = m->order == CMD_ATTACK ? daddy_at() : m->order == CMD_RETURN ? m->home : m->pos;
        Vector3 to = Vector3Subtract(goal, m->pos);
        to.y = 0;
        float d = Vector3Length(to);
        if (d > 0.09f && m->order != CMD_STOP) {   // hop along, stopping just short of Daddy
            m->pos = Vector3Add(m->pos, Vector3Scale(to, fminf(1, 0.35f * dt / d)));
            m->hop += dt * 12;
        } else if (m->order == CMD_RETURN && d <= 0.09f) {
            m->order = CMD_STOP;
        }
        Vector3 at = Vector3Add(m->pos, (Vector3){ 0, 0.04f + fabsf(sinf(m->hop)) * 0.03f, 0 });
        Color c = i == 0 ? (Color){ 210, 60, 60, 255 } : i == 1 ? (Color){ 60, 110, 230, 255 } : (Color){ 70, 190, 80, 255 };
        bool lit = i == pointed || (VO.talking && i == VO.selected);
        vrui_box((SfxrPose){ at, QuaternionIdentity() }, (Vector3){ 0.08f, 0.06f, 0.1f }, lit ? ColorBrightness(c, 0.4f) : c);
        vrui_text3d(Vector3Add(at, (Vector3){ 0, 0.08f, 0 }), TextFormat("%d: %s", i + 1, CMDS[m->order]), 0.018f,
                    lit ? YELLOW : RAYWHITE);
    }
    // Daddy, in cardboard: the target
    Vector3 dp = daddy_at();
    vrui_box((SfxrPose){ Vector3Add(dp, (Vector3){ 0.1f, 0.2f, 0 }), QuaternionIdentity() }, (Vector3){ 0.14f, 0.4f, 0.01f },
             (Color){ 60, 90, 170, 255 });
    vrui_text3d(Vector3Add(dp, (Vector3){ 0.1f, 0.47f, 0 }), "Daddy (cardboard)", 0.02f, RAYWHITE);
}

static void talk(void)
{
    // hold the right bumper to talk; the bug under the laser when you
    // start is the one you're talking to
    const SfxrHand *r = sfxr_hand(SFXR_RIGHT);
    if (r->bumper.pressed && !vrui_input_claimed(SFXR_RIGHT)) {
        VO.selected = pointed_bug();
        VO.talking = true;
        sfxr_voice_listen_begin();
        sound_play_here(SND_BLIP, 0.3f);
    }
    if (VO.talking && r->bumper.released) {
        VO.talking = false;
        sfxr_voice_listen_end();
    }
    // ...or hold the TALK button on the table (a momentary push button: it's
    // down for as long as it's held)
    VruiPressSpec ps = vrui_press_spec();
    ps.radius = 0.045f;
    ps.color = VO.button_talking ? (Color){ 230, 60, 50, 255 } : (Color){ 170, 40, 40, 255 };
    ps.label = "TALK (hold)";
    SfxrPose at = { { X0 + 0.3f, TABLE_Y, ROW_Z + 0.22f }, QuaternionIdentity() };
    VruiPress p = vrui_press(VRUI_ID2(G_VOICE, 2), at, &ps, NULL);
    if (p.pressed && !VO.talking) { VO.button_talking = true; sfxr_voice_listen_begin(); snprintf(VO.board, sizeof VO.board, "listening..."); }
    if (VO.button_talking && !p.down) { VO.button_talking = false; sfxr_voice_listen_end(); snprintf(VO.board, sizeof VO.board, "thinking..."); }

    char said[128];
    if (sfxr_voice_result(said, sizeof said)) {
        snprintf(VO.board, sizeof VO.board, "you said: \"%.40s\"", said[0] ? said : "(nothing)");
        snprintf(VO.heard, sizeof VO.heard, "%s", said[0] ? said : "(nothing)");
        VO.heard_ms = sfxr_voice_last_ms();
        int c = sfxr_voice_match(said, CMDS, 3);
        if (c >= 0) order(VO.selected, c, "voice");
        else snprintf(VO.result, sizeof VO.result, "not a command: attack, return or stop");
    }
    // the transcript board, standing behind the button
    SfxrPose board = { { X0 + 0.3f, TABLE_Y + 0.35f, ROW_Z - 0.2f }, QuaternionIdentity() };
    vrui_sign(board, 0.6f, "Push to talk", VO.board[0] ? VO.board : (sfxr_voice_available() ? "hold TALK and speak" :
              "no recognizer: run scripts/get-speech.sh"), (Color){ 40, 44, 56, 255 });
    if (VO.talking) {   // a listening light on the controller
        vrui_tag(Vector3Add(r->grip.position, (Vector3){ 0, 0.12f, 0 }), "listening...", 0.015f, RAYWHITE,
                 (Color){ 150, 30, 30, 220 });
    }
}

static void panel(void)
{
    if (!vrui_panel_begin(VRUI_ID2(G_VOICE, 0), &VO.panel, 0.5f, 0.54f, "Sound & voice")) return;
    vrui_layout_begin(vrui_panel_content(), 4);
    vrui_label(vrui_row(20), sfxr_audio_device() ? "sound: on" : "sound: offline (no audio device)");
    vrui_label(vrui_row(20), sfxr_voice_status());
    Rectangle row = vrui_row(14);
    vrui_progress(row, sfxr_voice_level(), sfxr_voice_level() > 0.6f ? RED : vrui_style()->accent);
    vrui_label(vrui_row(20), TextFormat("heard: %s", VO.heard[0] ? VO.heard : "-"));
    if (VO.heard[0]) vrui_label(vrui_row(20), TextFormat("  in %.0f ms", VO.heard_ms));
    vrui_label(vrui_row(20), VO.result);
    vrui_label(vrui_row(20), "no voice? point at a bug, then:");
    Rectangle cols[3];
    vrui_row_cols(32, 3, cols);
    int pointed = VO.selected;
    for (int c = 0; c < 3; c++)
        if (vrui_button(1 + c, cols[c], CMDS[c])) order(pointed, c, "button");
    vrui_label(vrui_row(20), "the toolbox's sounds (made in code):");
    static const char *const NAMES[] = { "click", "tick", "stop", "bell", "chime", "thump", "squish", "chomp", "whoosh", "trill" };
    for (int i = 0; i < 10; i += 5) {
        Rectangle c5[5];
        vrui_row_cols(28, 5, c5);
        for (int k = 0; k < 5; k++)
            if (vrui_button(10 + i + k, c5[k], NAMES[i + k])) sound_play((SoundId)(i + k), VO.panel.position, 1.0f);
    }
    vrui_panel_end();
}

void station_voice(void)
{
    if (!VO.init) init();
    station_sign(X0, "Sound & voice", "carry the speaker round your head; point at a\nbug, hold the bumper, say attack / return / stop");
    vrui_box((SfxrPose){ { X0, TABLE_Y - 0.025f, ROW_Z }, QuaternionIdentity() }, (Vector3){ 1.4f, 0.05f, 0.7f }, (Color){ 120, 92, 66, 255 });

    // the speaker: a box that chimes from wherever it is
    VruiGrab g = vrui_grabbable(VRUI_ID2(G_VOICE, 1), &VO.speaker, (Vector3){ 0.05f, 0.07f, 0.05f }, (Color){ 50, 50, 56, 255 });
    vrui_name_widget(VRUI_ID2(G_VOICE, 1), "speaker");
    if (g.released && VO.speaker.position.y < 0.2f) VO.speaker.position.y = TABLE_Y + 0.08f;   // dropped: back on the table
    // it chimes only while its switch is on, and only while you're near it
    // (or carrying it): a sound that repeats forever becomes noise
    // everywhere else in the toolbox
    if (vrui_switch(VRUI_ID2(G_VOICE, 3), (SfxrPose){ { X0 - 0.62f, TABLE_Y, ROW_Z + 0.1f }, QuaternionIdentity() },
                    &VO.chime_on, "chime"))
        VO.chime_t = 0;   // switched on: chime right away
    vrui_name_widget(VRUI_ID2(G_VOICE, 3), "chime switch");
    bool near = g.held || Vector3Distance(sfxr_head().position, VO.speaker.position) < 3.0f;
    if (VO.chime_on && near && (VO.chime_t -= sfxr_dt()) <= 0) {
        VO.chime_t = 1.2f;
        sound_play(SND_CHIME, VO.speaker.position, 0.9f);
    }
    vrui_text3d(Vector3Add(VO.speaker.position, (Vector3){ 0, 0.11f, 0 }), VO.chime_on ? "speaker: carry me around" : "speaker: switch me on", 0.018f, RAYWHITE);

    minions();
    talk();
    panel();
    // selecting by laser for the buttons: the last bug pointed at
    int p = pointed_bug();
    if (p >= 0 && !VO.talking) VO.selected = p;
}
