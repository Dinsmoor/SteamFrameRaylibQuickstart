// station_voice.c - Voice commands, at the left end of the row (docs/AUDIO.md,
// "Binding a command"). Three little bugs and a cardboard Daddy, and one
// set of orders -- attack, return, stop -- given four different ways, so
// you can feel which suits what:
//
//   1 POINT + BUMPER   point at a bug with either laser: it lights up and
//                      says "hold the bumper". Hold it, say the order, let
//                      go. A binding IN CONTEXT: the bumper only talks while
//                      you're pointing at a bug, so it's free for other
//                      things everywhere else. (Daddy Bug Smasher binds the
//                      bumper to voice everywhere instead: a global binding.)
//   2 POINT + A        point at a bug and hold A (left: D-pad down): the
//                      orders open in a ring; tilt the stick to one, let go.
//                      No voice: a context menu. Elsewhere, A opens the
//                      toolbox's own ring menu -- the same button, a
//                      different menu, because of what you point at.
//   3 TALK button      a big push-to-talk button on the table, an intercom
//                      to every bug: hold it (fingertip or laser), speak.
//                      Nothing to point at, so both hands are free for it.
//   4 HANDS-FREE       flip the switch and just talk: "bugs, attack". No
//                      button at all; a pause ends what you said. Anything
//                      without the wake word "bugs" is ignored, since the
//                      microphone hears everything (you, the room, the
//                      game's sounds).
//
// The panel's buttons give the same orders by laser, with no recognizer
// installed (scripts/get-speech.sh builds one).

#include "toolbox.h"
#include "sfxr_audio.h"
#include "sfxr_voice.h"
#include "sfxr_break.h"

#define BREAK SFXR_BREAK_DECLARE
#include "toolbox_breaks.def"
#undef BREAK

#include <stdio.h>
#include <string.h>

#define X0 -12.2f
#define ALL 3               // an order for every bug

static const char *const CMDS[] = { "attack", "return", "stop" };
enum { CMD_ATTACK, CMD_RETURN, CMD_STOP };

typedef struct { Vector3 home, pos; int order; float hop; } Minion;

static struct {
    bool init;
    SfxrPose panel;
    Minion bug[3];
    int pointed[2];         // the bug each laser is on (-1 none)
    int last_pointed;       // the last bug either laser was on (the panel's buttons order it)
    int talk_to;            // who the clip being recorded is for (a bug, or ALL)
    int talk_hand;          // the hand holding the bumper (-1: not by bumper)
    bool button_talking;    // the TALK button is held
    int radial_bug[2];      // the bug a hand's ring menu is giving orders to
    bool hands_free;
    char board[160];        // the transcript board's text
    char result[96];
    char heard[128];
    float heard_ms;
} VO;

static Vector3 daddy_at(void) { return (Vector3){ X0 + 0.6f, TABLE_Y, ROW_Z - 0.15f }; }
static SfxrPose on_table(float x, float y, float z) { return (SfxrPose){ { X0 + x, TABLE_Y + y, ROW_Z + z }, QuaternionIdentity() }; }

static void init(void)
{
    VO.panel = (SfxrPose){ { X0 - 0.45f, 1.5f, ROW_Z - 0.25f }, QuaternionIdentity() };
    for (int i = 0; i < 3; i++) {
        VO.bug[i].home = (Vector3){ X0 - 0.25f + 0.2f * (float)i, TABLE_Y, ROW_Z + 0.05f };
        VO.bug[i].pos = VO.bug[i].home;
        VO.bug[i].order = CMD_STOP;
    }
    VO.pointed[0] = VO.pointed[1] = VO.last_pointed = -1;
    VO.talk_hand = -1;
    snprintf(VO.result, sizeof VO.result, "point at a bug: it says what to do");
    // the words to listen for: short commands are what Whisper mishears without them
    sfxr_voice_set_prompt("bugs, attack, return, stop");
    VO.init = true;
}

static void order(int who, int cmd, const char *how)
{
    if (who < 0) { snprintf(VO.result, sizeof VO.result, "%s: but to whom? (point at a bug first)", CMDS[cmd]); return; }
    for (int i = 0; i < 3; i++) {
        if (who != ALL && who != i) continue;
        VO.bug[i].order = cmd;
        sound_play(SND_BLIP, VO.bug[i].pos, 0.8f);
    }
    snprintf(VO.result, sizeof VO.result, "%s: %s (%s)", who == ALL ? "all bugs" : TextFormat("bug %d", who + 1), CMDS[cmd], how);
    sfxr_event("order", "%s %s by %s", who == ALL ? "all" : TextFormat("bug %d", who + 1), CMDS[cmd], how);
}

// Which bug a hand's laser is on (-1 none).
static int pointed_bug(int h)
{
    const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
    if (!hand->active) return -1;
    Ray ray = { hand->aim.position, sfxr_pose_forward(hand->aim) };
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
        bool lit = i == VO.pointed[0] || i == VO.pointed[1] || (VO.talk_hand >= 0 && VO.talk_to == i);
        vrui_box((SfxrPose){ at, QuaternionIdentity() }, (Vector3){ 0.08f, 0.06f, 0.1f }, lit ? ColorBrightness(c, 0.4f) : c);
        vrui_mark(VRUI_ID2(G_VOICE, 20 + i), TextFormat("bug %d", i + 1), (SfxrPose){ at, QuaternionIdentity() });   // tests point at "voice.bug_2"
        sfxr_report(TextFormat("bug%d", i + 1), (float)m->order);   // ...and check "app bug2 == 0" (attack)
        vrui_text3d(Vector3Add(at, (Vector3){ 0, 0.08f, 0 }), TextFormat("%d: %s", i + 1, CMDS[m->order]), 0.018f,
                    lit ? YELLOW : RAYWHITE);
    }
    // Daddy, in cardboard: the target
    Vector3 dp = daddy_at();
    vrui_box((SfxrPose){ Vector3Add(dp, (Vector3){ 0.1f, 0.2f, 0 }), QuaternionIdentity() }, (Vector3){ 0.14f, 0.4f, 0.01f },
             (Color){ 60, 90, 170, 255 });
    vrui_text3d(Vector3Add(dp, (Vector3){ 0.1f, 0.47f, 0 }), "Daddy (cardboard)", 0.02f, RAYWHITE);
}

static void listen_for(int who, int hand, const char *board)
{
    VO.talk_to = who;
    VO.talk_hand = hand;
    sfxr_voice_listen_begin();
    snprintf(VO.board, sizeof VO.board, "%s", board);
    sound_play_here(SND_BLIP, 0.3f);
}

// 1: point at a bug, hold the bumper, speak. The bumper means "talk" only
// while pointing at a bug: that's the context.
static void bind_point_and_bumper(void)
{
    for (int h = 0; h < 2; h++) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        int bug = VO.pointed[h];
        if (bug >= 0 && VO.talk_hand < 0 && !vrui_radial_open(VRUI_ID2(G_VOICE, 10 + h))) {
            // the binding says itself, where you're looking
            vrui_tag(Vector3Add(VO.bug[bug].pos, (Vector3){ 0, 0.2f, 0 }),
                     TextFormat("hold the bumper and speak\nor hold %s for the orders", h ? "A" : "D-pad down"), 0.014f,
                     RAYWHITE, (Color){ 20, 22, 28, 220 });
            if (hand->bumper.pressed && !vrui_input_claimed((SfxrHandId)h) && !sfxr_voice_listening())
                listen_for(bug, h, "listening...");
        }
        if (VO.talk_hand == h && hand->bumper.released) {
            VO.talk_hand = -1;
            sfxr_voice_listen_end();
            snprintf(VO.board, sizeof VO.board, "thinking...");
        }
    }
    if (VO.talk_hand >= 0) {   // a listening light on that controller
        const SfxrHand *hand = sfxr_hand((SfxrHandId)VO.talk_hand);
        vrui_tag(Vector3Add(hand->grip.position, (Vector3){ 0, 0.12f, 0 }), "listening...", 0.015f, RAYWHITE, (Color){ 150, 30, 30, 220 });
    }
}

// 2: point at a bug, hold A: a ring of orders. The ring is offered only
// while pointing at a bug (or while it's open), so everywhere else A opens
// the toolbox's own ring menu (station_menus.c): this one runs first, and
// an open ring claims the hand, which the toolbox's ring checks.
static void bind_point_and_radial(void)
{
    for (int h = 0; h < 2; h++) {
        VruiId id = VRUI_ID2(G_VOICE, 10 + h);
        bool open = vrui_radial_open(id);
        if (!open && VO.pointed[h] < 0 && !SFXR_BREAK(toolbox_voice_ring_everywhere)) continue;
        if (!open) VO.radial_bug[h] = VO.pointed[h];
        int pick = vrui_radial_menu(id, (SfxrHandId)h, &sfxr_hand((SfxrHandId)h)->primary, CMDS, 3);
        if (pick >= 0) order(VO.radial_bug[h], pick, "ring menu");
    }
}

// 3: the TALK button, an intercom: every bug hears it. A momentary push
// button: it's down for as long as it's held.
static void bind_world_button(void)
{
    VruiPressSpec ps = vrui_press_spec();
    ps.radius = 0.045f;
    ps.color = VO.button_talking ? (Color){ 230, 60, 50, 255 } : (Color){ 170, 40, 40, 255 };
    ps.label = "TALK (hold)";
    VruiPress p = vrui_press(VRUI_ID2(G_VOICE, 2), on_table(0.3f, 0, 0.2f), &ps, NULL);
    if (p.pressed && VO.talk_hand < 0 && !sfxr_voice_listening()) {
        VO.button_talking = true;
        listen_for(ALL, -1, "listening (to all bugs)...");
    }
    if (VO.button_talking && !p.down) {
        VO.button_talking = false;
        sfxr_voice_listen_end();
        snprintf(VO.board, sizeof VO.board, "thinking...");
    }
}

// 4: hands-free: a switch turns it on; "bugs, <order>" orders them all.
static void bind_hands_free(void)
{
    if (vrui_switch(VRUI_ID2(G_VOICE, 3), on_table(-0.55f, 0, 0.22f), &VO.hands_free, "HANDS-FREE")) {
        sfxr_voice_hands_free(VO.hands_free, 0.15f, 0.6f);
        snprintf(VO.board, sizeof VO.board, "%s", VO.hands_free ? "say \"bugs, attack\" (or return, stop)" : "");
    }
    if (VO.hands_free && sfxr_voice_listening() && sfxr_voice_hands_free_heard()) snprintf(VO.board, sizeof VO.board, "hearing you...");
}

static void results(void)
{
    char said[128];
    if (!sfxr_voice_result(said, sizeof said)) return;
    snprintf(VO.heard, sizeof VO.heard, "%s", said[0] ? said : "(nothing)");
    VO.heard_ms = sfxr_voice_last_ms();
    snprintf(VO.board, sizeof VO.board, "you said: \"%.40s\"", VO.heard);
    int c = sfxr_voice_match(said, CMDS, 3);
    if (sfxr_voice_hands_free_heard()) {
        // hands-free: only with the wake word, and then it's for every bug
        static const char *const WAKE[] = { "bugs", "bug" };
        if (sfxr_voice_match(said, WAKE, 2) < 0) { snprintf(VO.result, sizeof VO.result, "(no \"bugs\" in it: ignored)"); return; }
        if (c >= 0) order(ALL, c, "hands-free");
        else snprintf(VO.result, sizeof VO.result, "bugs... but what? attack, return or stop");
        return;
    }
    if (c >= 0) order(VO.talk_to, c, "voice");
    else snprintf(VO.result, sizeof VO.result, "not an order: attack, return or stop");
}

static void panel(void)
{
    if (!vrui_panel_begin(VRUI_ID2(G_VOICE, 0), &VO.panel, 0.5f, 0.5f, "Voice commands")) return;
    vrui_layout_begin(vrui_panel_content(), 4);
    vrui_label(vrui_row(20), sfxr_voice_status());
    Rectangle row = vrui_row(14);
    vrui_progress(row, sfxr_voice_level(), sfxr_voice_level() > 0.6f ? RED : vrui_style()->accent);
    vrui_label(vrui_row(20), TextFormat("heard: %s", VO.heard[0] ? VO.heard : "-"));
    if (VO.heard[0]) vrui_label(vrui_row(20), TextFormat("  in %.0f ms", VO.heard_ms));
    vrui_label(vrui_row(20), VO.result);
    vrui_space(6);
    vrui_label(vrui_row(20), "by laser (no voice): the bug you last pointed at");
    Rectangle cols[3];
    vrui_row_cols(32, 3, cols);
    for (int c = 0; c < 3; c++)
        if (vrui_button(1 + c, cols[c], CMDS[c])) order(VO.last_pointed, c, "panel");
    vrui_panel_end();
}

// A placard on the table's front edge for each binding.
static void placards(void)
{
    static const char *const P[4] = { "1  point + BUMPER: speak", "2  point + A: ring menu", "3  TALK: intercom", "4  HANDS-FREE: \"bugs, ...\"" };
    for (int i = 0; i < 4; i++)
        vrui_text_at(on_table(-0.5f + 0.34f * (float)i, -0.03f, 0.352f), P[i], 0.013f, (Color){ 250, 220, 150, 255 });
}

void station_voice(void)
{
    if (!VO.init) init();
    station_sign(X0, "Voice commands", "one set of orders, four ways to give them:\nwhich suits what?");
    vrui_box(on_table(0, -0.025f, 0), (Vector3){ 1.4f, 0.05f, 0.7f }, (Color){ 120, 92, 66, 255 });
    for (int h = 0; h < 2; h++) {
        VO.pointed[h] = pointed_bug(h);
        if (VO.pointed[h] >= 0) VO.last_pointed = VO.pointed[h];
    }
    minions();
    bind_point_and_radial();   // (before the bumper: an open ring hides the bumper's hint)
    bind_point_and_bumper();
    bind_world_button();
    bind_hands_free();
    results();
    placards();
    // the transcript board, standing behind the TALK button
    vrui_sign(on_table(0.3f, 0.35f, -0.2f), 0.6f, "What was heard", VO.board[0] ? VO.board :
              (sfxr_voice_available() ? "hold TALK and speak" : "no recognizer: run scripts/get-speech.sh"), (Color){ 40, 44, 56, 255 });
    panel();
}
