// garden.c - Daddy Bug Smasher: a small real game made from the toolbox's pieces
// (docs/DADDY_BUG_SMASHER.md). Go through the gate at the right end of the row.
//
// The game: you are Daddy. Your hammer is waiting on a stump; pick it up and
// the Bugmaster's bugs come, crawling out of his tower and at you from all
// sides. Swing the hammer at them (a real
// swing: it's the speed of the hammer's head that counts, so tapping does
// nothing). Smash 8 before they bite you 10 times.
//
// What it uses, and where each piece is explained:
//   attaching      the hammer rests on its stump (the world), then rides
//                  your hand, then your belt (put it on your right hip), or
//                  nothing (drop or throw it: it tumbles as a rigid body)
//                  -- docs/ATTACHING.md
//   locomotion     walk with the left stick (smooth, the garden is a game),
//                  teleport or turn with the right; the ground is sculpted
//                  terrain, and tree trunks, rocks and the tower are solid
//                  (your head in one fades the view) -- docs/MOVEMENT.md
//   HUD and menus  the Menus & HUD station's choices apply here too: your
//                  health and score on the HUD, arrows to bugs behind you,
//                  and the hand menus offer Restart / Recall hammer / Leave
//   labels         callouts on tough bugs (hits left), the Bugmaster's words
//   smoothing      how the hammer follows whatever holds it: Snap (exactly
//                  on the hand or bone), Lag, Spring, Heavy (a weighty
//                  swing: a flick can't whip it round), Steady -- the
//                  Smoothing station's modes and settings (docs/SMOOTHING.md).
//                  Pick one on the game's board or from a hand menu.
//   haptics        a thump per hit, scaled by how hard you swung; a jolt
//                  when bitten
//   sound          every smash, hit and bite from where it happens; the
//                  Bugmaster heard from the top of his tower (docs/AUDIO.md).
//                  The original game's sounds and the Bugmaster's voice lines
//                  (resources/garden/sfx/ and voice/), the toolbox's sounds
//                  made in code for the rest
//   music          the original game's menu, combat, win and lose tunes
//   voice          hold the bumper on your less-used hand (the other one's
//                  opens the hand menu) and say "hammer" (it comes back to
//                  your belt) or "restart"
//   Steam          winning unlocks an achievement when Steam is there
//                  (docs/STEAM.md)
//   the event log  every hit, bite, pick-up and drop is in it
//
// The game logic is plain C in this one file; the world (terrain, props,
// the chair's rigid body) is garden_world.c and garden_rigidbody.c.

#include "garden.h"
#include "rlgl.h"
#include "sfxr_break.h"
#include "sfxr_steam.h"
#include "sfxr_audio.h"
#include "sfxr_voice.h"

#include <string.h>

// the garden's own break switches (tests/garden proves each one matters)
#define BREAK SFXR_BREAK_DECLARE
#include "garden_breaks.def"
#undef BREAK

#define ID(n) VRUI_ID2(G_GARDEN, (n))

#define SCORE_TO_WIN 8
#define MAX_HP       10
#define MAX_BUGS     24
#define BUG_R        0.3f     // a bug's body radius (m)
#define PLAYER_R     0.3f     // how close to your feet a bug must get to bite
#define SWING_SPEED  1.3f     // the hammer's head must move this fast to hit (m/s)

// The hammer, in its own frame: the handle along +Y from its end at the
// origin; we keep the pose of the handle's middle. The flat game's model is
// stretched along the shaft for VR (0.86 m of handle: two hands fit, and a
// swing has reach), its head only a little bigger.
#define HAMMER_XZ    GARDEN_SCALE
#define HAMMER_Y     0.95f
#define HANDLE_MID   (0.45f * HAMMER_Y)
#define HEAD_UP      (0.95f * HAMMER_Y - HANDLE_MID)   // handle middle -> head center
#define FEEL_WEIGHT  VRUI_SMOOTH_COUNT                  // GD.feel: vrui_wield's weight (the other values: a smoothing mode)
#define HEAD_REACH   0.16f

// The gate at the right end of the row (the toolbox side), and where you
// come back out.
#define GATE_X  19.5f
#define GATE_Z  0.3f

typedef struct { const char *name; Color color; float speed; int health, damage; } BugType;
static const BugType TYPES[] = {
    { "red",   { 210, 60, 60, 255 },  0.75f, 1, 2 },
    { "blue",  { 60, 110, 230, 255 }, 1.0f,  1, 1 },
    { "green", { 70, 190, 80, 255 },  0.6f,  3, 3 },
};
#define NTYPES ((int)(sizeof TYPES / sizeof TYPES[0]))

typedef struct {
    bool alive;
    float squash;          // > 0: smashed, flattening out (s)
    Vector3 pos;           // on the ground
    int type, health;
    float bite_cd, hit_cd, wobble;
} Bug;

typedef enum { WAITING, PLAYING, WON, LOST } Round;
typedef enum { ON_STUMP, IN_HAND, ON_BELT, LOOSE } HammerAt;
// The Bugmaster's tower (the level's tower prop, in meters), and what he
// shouts from the top of it.
static const Vector3 TOWER = { 16 * GARDEN_SCALE, 0, -14 * GARDEN_SCALE };
#define TOWER_TOP (14.2f * GARDEN_SCALE)

static const char *const HAMMER_WORDS[] = { "on its stump", "in your hand", "on your belt", "on the ground" };

static struct {
    bool active;
    Round round;
    float t, drip, hurt;
    int score, hp, waves_done;
    Bug bugs[MAX_BUGS];
    uint32_t rng;

    HammerAt hammer_at;
    SfxrPose hammer;       // the handle's middle, exactly where its parent puts it
    SfxrPose shown;        // ...and after smoothing: where it's drawn and where it hits
    VruiSmooth smooth;
    int feel;              // VruiSmoothMode for the hammer
    RigidBody hammer_body; // while loose
    SfxrHandId hammer_hand;
    Vector3 head_prev;     // the hammer's head last frame, in tracking space
    bool head_prev_ok;
    Vector3 head_vel;      // its velocity this frame (world, m/s)
    bool test_mode;        // tests place their own bugs: no timed waves, and bugs stay put (they still bite)
    Model hammer_model;
    Texture2D bugmaster[2];   // his drawing from the flat game, two frames
    bool models_ok;


    VruiLocoConfig loco;
    SfxrPose board;        // the garden's panel (placed on entering; you can drag it)
    bool steam_sent;
    float gate;            // the gate's opening 0..1
    int spawned;           // bugs sent so far (every other one comes from the tower)
    const char *shout;     // what the Bugmaster is shouting, and for how long
    float shout_t;
    // the original game's recordings, when copied in (0 = not there: use the made-in-code ones)
    SfxrSound smash[3], scurry, ding_hi, ding_lo, bm_intro, bm_win, bm_lose, taunt[4];
    bool sounds_loaded, talking;
} GD = { .rng = 12345, .feel = FEEL_WEIGHT };

bool garden_active(void) { return GD.active; }
Color garden_sky(void) { return gw_sky(); }

// deterministic, so replays and tests of the garden repeat exactly
static float rnd(void) { GD.rng = GD.rng * 1664525u + 1013904223u; return (float)(GD.rng >> 8) / 16777216.0f; }

// --- where things are ---------------------------------------------------------------

static Vector3 on_ground(float x, float z) { return (Vector3){ x, gw_ground(x, z), z }; }

// The stump by the spawn point where your hammer waits, and the hammer
// standing on it (the handle's middle, leaning a little toward you).
static Vector3 stump_top(void) { Vector3 p = on_ground(0.7f, -0.7f); p.y += 0.5f; return p; }
static SfxrPose hammer_on_stump(void)
{
    return (SfxrPose){ Vector3Add(stump_top(), (Vector3){ 0, HANDLE_MID - 0.02f, 0.03f }),
                       QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, 12 * DEG2RAD) };
}
// The belt slot for the hammer: your right hip, head up.
static SfxrPose belt(void)
{
    return sfxr_pose_mul(vrui_body(), (SfxrPose){ { 0.24f, 0.55f * vrui_eye_height() - 0.05f, -0.02f },
                                                   QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -15 * DEG2RAD) });
}

static Vector3 hammer_head(void) { return sfxr_pose_apply(GD.shown, (Vector3){ 0, HEAD_UP, 0 }); }

static SfxrPose rig(void)
{
    return (SfxrPose){ sfxr_rig_position(), QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, sfxr_rig_yaw()) };
}

// How fast the hammer's head is moving. In your hand, it's measured from
// where the head was last frame in TRACKING space (your room), not the
// world: a flick of the wrist moves the head much faster than the hand, and
// a teleport or stick walking moves the world, not your swing.
static void track_head(void)
{
    Vector3 head = hammer_head();
    if (GD.hammer_at == LOOSE) { GD.head_vel = rb_point_velocity(&GD.hammer_body, head); GD.head_prev_ok = false; return; }
    if (GD.hammer_at != IN_HAND) { GD.head_vel = (Vector3){ 0 }; GD.head_prev_ok = false; return; }
    SfxrPose r = rig();
    Vector3 local = sfxr_pose_apply_inv(r, head);
    float dt = sfxr_dt();
    GD.head_vel = GD.head_prev_ok && dt > 0
        ? Vector3RotateByQuaternion(Vector3Scale(Vector3Subtract(local, GD.head_prev), 1.0f / dt), r.orientation)
        : (Vector3){ 0 };
    GD.head_prev = local;
    GD.head_prev_ok = true;
}

// --- the locomotion hooks -------------------------------------------------------------

static float ground_cb(Vector3 p, void *u) { (void)u; return gw_ground(p.x, p.z); }
static float solid_cb(Vector3 p, void *u) { (void)u; return gw_solid_depth(p); }
static bool inside_cb(Vector3 t, void *u)
{
    (void)u;
    float h = gw_half_size() - 1.0f;
    return fabsf(t.x) < h && fabsf(t.z) < h;
}

// --- the round --------------------------------------------------------------------------

static void spawn_bug(int type)
{
    int alive = 0;
    for (int i = 0; i < MAX_BUGS; i++) alive += GD.bugs[i].alive;
    if (alive >= 10) return;
    for (int i = 0; i < MAX_BUGS; i++) {
        Bug *b = &GD.bugs[i];
        if (b->alive) continue;
        if (type < 0) type = (int)(rnd() * NTYPES) % NTYPES;
        float a = rnd() * 2 * PI, r = fminf(gw_half_size() - 1.5f, 16.0f);
        Vector3 me = sfxr_head_floor_point();
        float x = Clamp(me.x + r * sinf(a), -gw_half_size() + 1, gw_half_size() - 1);
        float z = Clamp(me.z + r * cosf(a), -gw_half_size() + 1, gw_half_size() - 1);
        if (GD.spawned++ % 2 == 0) {   // every other one crawls out of the Bugmaster's tower door
            Vector3 to = Vector3Normalize((Vector3){ me.x - TOWER.x, 0, me.z - TOWER.z });
            x = TOWER.x + to.x * 2.2f + (rnd() - 0.5f);
            z = TOWER.z + to.z * 2.2f + (rnd() - 0.5f);
        }
        gw_resolve_circle(&x, &z, BUG_R);
        *b = (Bug){ .alive = true, .pos = on_ground(x, z), .type = type, .health = TYPES[type].health, .wobble = rnd() * 6 };
        if (GD.scurry) sfxr_sound_play(GD.scurry, b->pos, 0.7f);
        sfxr_event("bug", "a %s bug comes (%.1f %.1f)", TYPES[type].name, x, z);
        return;
    }
}

// --- sound -------------------------------------------------------------------------------

static void load_sounds(void)
{
    if (GD.sounds_loaded) return;
    GD.sounds_loaded = true;
    static const char *const SMASH[3] = { "sfx/death1.wav", "sfx/death2.wav", "sfx/death3.wav" };
    for (int i = 0; i < 3; i++) GD.smash[i] = sfxr_sound_load(gw_path(SMASH[i]), 3);
    GD.scurry = sfxr_sound_load(gw_path("sfx/scurry.wav"), 4);
    GD.ding_hi = sfxr_sound_load(gw_path("sfx/high ding.wav"), 1);
    GD.ding_lo = sfxr_sound_load(gw_path("sfx/low ding.wav"), 1);
    // the Bugmaster's lines: named from the player's side ("win" plays when you win)
    GD.bm_intro = sfxr_sound_load(gw_path("voice/bugmaster_intro.wav"), 1);
    GD.bm_win = sfxr_sound_load(gw_path("voice/bugmaster_win.wav"), 1);
    GD.bm_lose = sfxr_sound_load(gw_path("voice/bugmaster_lose.wav"), 1);
    for (int i = 0; i < 4; i++) GD.taunt[i] = sfxr_sound_load(gw_path(TextFormat("voice/taunt%d.wav", i + 1)), 1);
}

// A recording if there is one, else the sound made in code.
static void play_sound(SfxrSound recorded, SoundId made, Vector3 at, float volume)
{
    if (recorded) sfxr_sound_play(recorded, at, volume);
    else sound_play(made, at, volume);
}

static Vector3 tower_top(void) { return (Vector3){ TOWER.x, TOWER_TOP + 2.5f, TOWER.z }; }

// His lines, as recorded for the flat game (the subtitles are what Whisper
// heard in them). He says it from the top of his tower; the words hang over
// him for as long as the line lasts.
static const char *const TAUNT_TEXT[4] = { "Heh! My bugs, too strong!", "Go, bugs, go!", "You weakling!", "Can't even stop a bug!" };

static void bugmaster_says(const char *line, SfxrSound voice)
{
    GD.shout = line;
    GD.shout_t = fmaxf(3.0f, sfxr_sound_seconds(voice) + 0.5f);
    sfxr_event("bugmaster", "%s", line);
    play_sound(voice, SND_TRILL, tower_top(), 1.0f);   // no recording: a made-in-code trill
}

// The original game's music (made by the author and their kids): the menu
// tune while you pick up the hammer, combat while the bugs come, win or
// lose at the end. Quieter while the Bugmaster speaks.
static void music(const char *track, bool loop)
{
    sfxr_music_play(gw_path(TextFormat("music/%s.ogg", track)), loop, 0.35f);
}

static void round_reset(void)
{
    memset(GD.bugs, 0, sizeof GD.bugs);
    GD.round = WAITING;
    GD.t = GD.drip = GD.hurt = 0;
    GD.score = 0;
    GD.hp = MAX_HP;
    GD.waves_done = 0;
    GD.hammer_at = ON_STUMP;
    GD.rng = 12345;
    gw_reset();
    music("menu", true);
}

static void hit_bugs(void)
{
    if (GD.hammer_at != IN_HAND && GD.hammer_at != LOOSE) return;
    Vector3 v = GD.head_vel, head = hammer_head();
    if (SFXR_BREAK(garden_hit_at_hand) && GD.hammer_at == IN_HAND) head = sfxr_hand(GD.hammer_hand)->grip.position;
    float speed = Vector3Length(v);
    if (speed < SWING_SPEED && !SFXR_BREAK(garden_hit_any_speed)) return;
    if (gw_hit_loose(head, v, 1.2f, HEAD_REACH)) {   // the chair
        sound_play(SND_THUMP, head, 0.6f);
        if (GD.hammer_at == IN_HAND) vrui_haptic_pulse(GD.hammer_hand, 0.4f, 0.03f, 0);
    }
    for (int i = 0; i < MAX_BUGS; i++) {
        Bug *b = &GD.bugs[i];
        if (!b->alive || b->squash > 0 || b->hit_cd > 0) continue;
        Vector3 c = Vector3Add(b->pos, (Vector3){ 0, BUG_R * 0.9f, 0 });
        if (Vector3Distance(head, c) > BUG_R + HEAD_REACH) continue;
        b->hit_cd = 0.35f;   // one hit per swing
        b->health--;
        // the thump: stronger the harder you swung
        if (GD.hammer_at == IN_HAND) vrui_haptic_pulse(GD.hammer_hand, Clamp(0.35f + speed * 0.12f, 0, 1), 0.05f, 0);
        if (b->health <= 0) {
            b->squash = 0.001f;
            sound_pitch(SND_SQUISH, 0.8f + 0.4f * rnd());
            play_sound(GD.smash[(int)(rnd() * 3) % 3], SND_SQUISH, c, 1.0f);
            GD.score++;
            sfxr_event("smash", "%s bug smashed at %.1f m/s (%d of %d)", TYPES[b->type].name, speed, GD.score, SCORE_TO_WIN);
        } else {
            Vector3 away = Vector3Normalize((Vector3){ c.x - head.x, 0, c.z - head.z });
            b->pos = on_ground(b->pos.x + away.x * 0.4f, b->pos.z + away.z * 0.4f);
            sfxr_event("hit", "%s bug hit, %d to go", TYPES[b->type].name, b->health);
            sound_play(SND_THUMP, c, 0.9f);
        }
    }
}

static void move_bugs(float dt)
{
    Vector3 me = sfxr_head_floor_point();
    for (int i = 0; i < MAX_BUGS; i++) {
        Bug *b = &GD.bugs[i];
        if (!b->alive) continue;
        if (b->squash > 0) {   // smashed: flatten, then gone
            b->squash += dt;
            if (b->squash > 0.5f) b->alive = false;
            continue;
        }
        b->hit_cd = fmaxf(0, b->hit_cd - dt);
        b->bite_cd = fmaxf(0, b->bite_cd - dt);
        b->wobble += dt * 9;
        if (GD.round != PLAYING) continue;
        Vector3 to = { me.x - b->pos.x, 0, me.z - b->pos.z };
        float d = Vector3Length(to);
        float x = b->pos.x, z = b->pos.z;
        Vector3 vel = { 0 };
        if (d > PLAYER_R + BUG_R && !GD.test_mode) {   // (tests put bugs exactly where they want them)
            vel = Vector3Scale(to, TYPES[b->type].speed / d);
            x += vel.x * dt;
            z += vel.z * dt;
        }
        gw_resolve_circle(&x, &z, BUG_R);
        gw_push_loose(&x, &z, BUG_R, vel, 1.0f);   // bugs nudge the chair too
        b->pos = on_ground(x, z);
        // a bite, then it backs off a little
        if (d < PLAYER_R + BUG_R + 0.08f && b->bite_cd <= 0) {
            GD.hp -= TYPES[b->type].damage;
            b->bite_cd = SFXR_BREAK(garden_bite_every_frame) ? 0 : 1.2f;
            GD.hurt = 0.35f;
            if (d > 1e-3f && !SFXR_BREAK(garden_bite_every_frame)) b->pos = on_ground(b->pos.x - to.x / d * 0.5f, b->pos.z - to.z / d * 0.5f);
            for (int h = 0; h < 2; h++) vrui_haptic_pulse((SfxrHandId)h, 0.6f, 0.12f, 0);
            sound_play(SND_CHOMP, b->pos, 1.0f);
            sfxr_event("bite", "a %s bug bit you: %d/%d", TYPES[b->type].name, GD.hp > 0 ? GD.hp : 0, MAX_HP);
        }
    }
}

static void play(float dt)
{
    if (GD.round != PLAYING) return;
    GD.t += dt;
    if (GD.test_mode) GD.waves_done = 2, GD.drip = 0;   // tests place their own bugs
    // the waves: three at once, two tough green ones, then one every 3.5 s
    if (GD.waves_done == 0 && GD.t > 0.5f) { for (int i = 0; i < 3; i++) spawn_bug(-1); GD.waves_done = 1; }
    if (GD.waves_done == 1 && GD.t > 4.0f) { spawn_bug(2); spawn_bug(2); GD.waves_done = 2; }
    if (GD.t > 6.0f && (GD.drip += dt) > 3.5f) { GD.drip = 0; spawn_bug(-1); }
    if (GD.score >= SCORE_TO_WIN) {
        GD.round = WON;
        sfxr_event("round", "won in %.0f s with %d/%d left", GD.t, GD.hp, MAX_HP);
        music("win", false);
        bugmaster_says("No, my precious bugs! My precious bugs! You may have defeated my bugs this time...", GD.bm_win);
        play_sound(GD.ding_hi, SND_BELL, sfxr_head().position, 1.0f);
        // Spacewar's (app 480) test achievement; your game's would be yours
        if (!GD.steam_sent) GD.steam_sent = sfxr_steam_unlock("ACH_WIN_ONE_GAME");
    } else if (GD.hp <= 0) {
        GD.hp = 0;
        GD.round = LOST;
        sfxr_event("round", "lost after %.0f s, %d smashed", GD.t, GD.score);
        music("lose", false);
        bugmaster_says("Yes, yes! My wonderful bugs have won!", GD.bm_lose);
        play_sound(GD.ding_lo, SND_STOP, sfxr_head().position, 1.0f);
    }
}

// --- the hammer: who it's attached to --------------------------------------------------

static void hammer(void)
{
    // Where the hammer is this frame comes from its parent (docs/ATTACHING.md):
    switch (GD.hammer_at) {
    case ON_STUMP: GD.hammer = hammer_on_stump(); break;
    case ON_BELT: if (!SFXR_BREAK(garden_belt_world_space)) GD.hammer = belt(); break;
    case LOOSE:
        rb_step(&GD.hammer_body, sfxr_dt(), 9.8f, gw_ground(GD.hammer_body.pos.x, GD.hammer_body.pos.z));
        GD.hammer = rb_pose(&GD.hammer_body);
        break;
    case IN_HAND: break;    // vrui_wield moves it with the hands
    }

    // Held, it's wielded (vrui_wield, docs/WIELDING.md): take it anywhere on
    // the handle and it settles into your fist, head up and a face forward;
    // loosen your grip to slide along the shaft, or add your other hand. The
    // game moves it the rest of the time (own_physics): stump, belt, falling.
    VruiWieldSpec ws = vrui_wield_spec(VRUI_WEIGHT_HEAVY);
    ws.mass = GD.feel == FEEL_WEIGHT ? 2.5f : 0.0f;
    ws.grip[0] = (VruiGrip){ { 0, -HANDLE_MID + 0.03f, 0 }, { 0, HEAD_UP - 0.13f, 0 }, { 1, 0, 0 }, 2, false };
    ws.ngrips = 1;
    ws.center = (Vector3){ 0, HEAD_UP - 0.05f, 0 };
    ws.half = (Vector3){ 0.16f, HANDLE_MID + 0.12f, 0.08f };
    ws.box_center = (Vector3){ 0, 0.1f, 0 };
    ws.own_physics = true;
    VruiWield w = vrui_wield(ID(1), &GD.hammer, &ws);
    vrui_name_widget(ID(1), "hammer");
    if (w.grabbed) {
        sfxr_event("hammer", "taken %s", HAMMER_WORDS[GD.hammer_at]);
        GD.hammer_at = IN_HAND;
        GD.hammer_hand = w.hand;
        if (GD.round == WAITING) {
            GD.round = PLAYING;
            GD.t = 0;
            music("combat", true);
            bugmaster_says("You there! You've been smashing my bugs for too long. This time, they will smash YOU!", GD.bm_intro);
        }
    }
    if (w.hands > 0) GD.hammer_hand = w.hand;
    if (w.released) {
        // on your hip: it rides on your belt; anywhere else: it falls (or flies)
        if (Vector3Distance(GD.hammer.position, belt().position) < 0.25f) {
            GD.hammer_at = ON_BELT;
        } else {
            GD.hammer_at = LOOSE;
            rb_init(&GD.hammer_body, GD.hammer.position, (Vector3){ 0.05f, HANDLE_MID + 0.08f, 0.05f }, GD.hammer.orientation, 1.0f);
            GD.hammer_body.vel = w.velocity;
            GD.hammer_body.ang_vel = w.angular_velocity;
        }
        sfxr_event("hammer", "let go: %s", HAMMER_WORDS[GD.hammer_at]);
    }
    // How the hammer follows its parent: its weight (vrui_wield, above), or
    // one of the Smoothing station's modes. It rides the player while in your
    // hand or on your belt, so teleports don't smear it; on the stump it's in
    // the world. Loose, its rigid body already moves it smoothly.
    bool smoothing = GD.feel != FEEL_WEIGHT && GD.hammer_at != LOOSE;
    VruiSmoothSpec feel = *smoothing_spec(smoothing ? (VruiSmoothMode)GD.feel : VRUI_SMOOTH_SNAP);
    feel.with_player = GD.hammer_at == IN_HAND || GD.hammer_at == ON_BELT;
    GD.shown = vrui_smooth_pose(&GD.smooth, GD.hammer, &feel);

    // show the belt slot while the hammer is near your hip
    if (GD.hammer_at == IN_HAND && Vector3Distance(GD.hammer.position, belt().position) < 0.4f)
        vrui_box(belt(), (Vector3){ 0.08f, 0.2f, 0.08f }, (Color){ 120, 200, 255, 90 });
    if (GD.hammer_at == LOOSE && Vector3Distance(GD.hammer.position, sfxr_head().position) > 8.0f)
        vrui_offscreen_arrow(GD.hammer.position, "hammer", (Color){ 255, 220, 120, 230 });
}

// "Recall hammer": out of whatever hand has it, onto your belt.
static void recall_hammer(void)
{
    if (GD.hammer_at == ON_STUMP) return;
    vrui_wield_drop(ID(1));
    GD.hammer_at = ON_BELT;
}

// --- the gate (in the toolbox) --------------------------------------------------------------

void garden_gate(void)
{
    // A garden gate at the right end of the row, in a low hedge. Walk through
    // it (open or not: nothing in VR can stop you) and you're in the garden.
    Color hedge = { 60, 120, 60, 255 }, wood = { 140, 100, 60, 255 };
    Quaternion I = QuaternionIdentity();
    vrui_box((SfxrPose){ { GATE_X, 0.6f, GATE_Z - 1.8f }, I }, (Vector3){ 0.5f, 1.2f, 2.6f }, hedge);
    vrui_box((SfxrPose){ { GATE_X, 0.6f, GATE_Z + 1.8f }, I }, (Vector3){ 0.5f, 1.2f, 2.6f }, hedge);
    vrui_box((SfxrPose){ { GATE_X, 0.65f, GATE_Z - 0.52f }, I }, (Vector3){ 0.08f, 1.3f, 0.08f }, wood);
    vrui_box((SfxrPose){ { GATE_X, 0.65f, GATE_Z + 0.52f }, I }, (Vector3){ 0.08f, 1.3f, 0.08f }, wood);
    // the gate opens away from you (toward +X): its +Z faces +X, handle toward -Z
    SfxrPose hinge = { { GATE_X, 0.02f, GATE_Z + 0.46f }, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, PI / 2) };
    vrui_door(ID(20), hinge, 0.92f, 1.1f, &GD.gate, NULL);
    vrui_sign((SfxrPose){ { GATE_X - 0.3f, 2.0f, GATE_Z }, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, -PI / 2) }, 1.4f,
              "Daddy Bug Smasher", "part three: smash the Bugmaster's bugs.\ngo through the gate to play", (Color){ 50, 80, 50, 255 });
    Vector3 head = sfxr_head().position;
    if (head.x > GATE_X + 0.3f && fabsf(head.z - GATE_Z) < 2.5f) garden_enter();
}

void garden_enter(void)
{
    if (GD.active) return;
    if (!gw_load()) {
        vrui_tag((Vector3){ GATE_X + 0.5f, 1.6f, GATE_Z }, "the garden's models are missing\n(examples/toolbox/resources/garden)",
                 0.042f, RAYWHITE, (Color){ 120, 30, 30, 220 });
        return;
    }
    if (!GD.models_ok) {
        GD.hammer_model = LoadModel(gw_path("hammer.glb"));
        gw_light(&GD.hammer_model);
        for (int i = 0; i < 2; i++) {
            GD.bugmaster[i] = LoadTexture(gw_path(TextFormat("sprites/bugmaster_%d.png", i + 1)));
            SetTextureFilter(GD.bugmaster[i], TEXTURE_FILTER_BILINEAR);
        }
        GD.models_ok = true;
    }
    GD.active = true;
    load_sounds();
    sound_play_here(SND_WHOOSH, 0.8f);
    round_reset();
    GD.board = vrui_facing(Vector3Add(on_ground(-1.3f, -0.6f), (Vector3){ 0, 1.35f, 0 }), Vector3Add(on_ground(0, 1.0f), (Vector3){ 0, 1.35f, 0 }));
    // arrive at the spawn point facing into the garden (-Z), your hammer ahead
    Vector3 f = sfxr_pose_forward(sfxr_head());
    sfxr_rig_turn(atan2f(f.x, -f.z));
    sfxr_rig_teleport(on_ground(0, 1.0f));
    vrui_fade(1.0f);
    sfxr_event("scene", "into the garden");
}

void garden_leave(void)
{
    if (!GD.active) return;
    GD.active = false;
    sfxr_music_stop();
    // back out through the gate, facing the row
    Vector3 f = sfxr_pose_forward(sfxr_head());
    sfxr_rig_turn(-(-PI / 2 - atan2f(f.x, -f.z)));
    sfxr_rig_teleport((Vector3){ GATE_X - 1.2f, 0, GATE_Z });
    vrui_fade(1.0f);
    sfxr_event("scene", "back to the toolbox");
}

// --- every frame in the garden ------------------------------------------------------------------

static const char *const MENU[] = { "Restart", "Recall hammer", "Leave garden", "HUD style", "Hammer feel" };

void garden_update(const VruiLocoConfig *toolbox_loco)
{
    float dt = sfxr_dt();

    // hand menus: the Menus & HUD station's choices apply here too
    switch (menus_update(MENU, 5, TextFormat("HP %d/%d  smashed %d", GD.hp, MAX_HP, GD.score))) {
    case 0: round_reset(); break;
    case 1: recall_hammer(); break;
    case 2: garden_leave(); return;
    case 3: menus_set_hud_style((HudStyle)((menus_hud_style() + 1) % HUD_COUNT)); break;
    case 4: GD.feel = (GD.feel + 1) % (VRUI_SMOOTH_COUNT + 1); break;
    default: break;
    }

    // voice: hold the bumper on your less-used hand, say "hammer" or "restart"
    // (the main hand's bumper is the hand menus' ring)
    static const char *const SAY[] = { "hammer", "restart" };
    SfxrHandId talk = menus_main_hand() == SFXR_RIGHT ? SFXR_LEFT : SFXR_RIGHT;
    const SfxrHand *rh = sfxr_hand(talk);
    if (rh->bumper.pressed && !vrui_input_claimed(talk)) { GD.talking = true; sfxr_voice_set_prompt("hammer, restart"); sfxr_voice_listen_begin(); }
    if (GD.talking && rh->bumper.released) { GD.talking = false; sfxr_voice_listen_end(); }
    char said[128];
    if (sfxr_voice_result(said, sizeof said)) {
        int c = sfxr_voice_match(said, SAY, 2);
        if (c == 0 && GD.hammer_at != ON_STUMP) { recall_hammer(); sound_play_here(SND_WHOOSH, 0.7f); }
        if (c == 1) { round_reset(); }
    }
    if (GD.talking) vrui_tag(Vector3Add(rh->grip.position, (Vector3){ 0, 0.12f, 0 }), "listening: hammer / restart", 0.028f, RAYWHITE,
                             (Color){ 150, 30, 30, 220 });

    // the board by the spawn point: what's going on, and the two buttons
    if (vrui_panel_begin(ID(2), &GD.board, 0.72f, 0.52f, "Daddy Bug Smasher")) {
        vrui_layout_begin(vrui_panel_content(), 4);
        static const char *const SAY[] = { "Pick up your hammer, Daddy!", "Smash 8 bugs before they get you!",
                                           "The garden is clear! Well smashed.", "The bugs got you. Try again?" };
        vrui_label(vrui_row(24), SAY[GD.round]);
        vrui_label(vrui_row(24), TextFormat("health %d/%d   smashed %d/%d", GD.hp, MAX_HP, GD.score, SCORE_TO_WIN));
        vrui_label(vrui_row(24), TextFormat("hammer: %s", HAMMER_WORDS[GD.hammer_at]));
        static const char *FEEL[VRUI_SMOOTH_COUNT + 1];
        for (int m = 0; m < VRUI_SMOOTH_COUNT; m++) FEEL[m] = vrui_smooth_name((VruiSmoothMode)m);
        FEEL[FEEL_WEIGHT] = "Weight";
        vrui_label(vrui_row(22), "The hammer follows your hand:");
        vrui_segmented(3, vrui_row(34), FEEL, VRUI_SMOOTH_COUNT + 1, &GD.feel);
        Rectangle cols[2];
        vrui_row_cols(36, 2, cols);
        if (vrui_button(1, cols[0], "Restart")) { round_reset(); }
        if (vrui_button(2, cols[1], "Back to the toolbox")) { vrui_panel_end(); garden_leave(); return; }
        vrui_panel_end();
    }

    hammer();
    track_head();
    hit_bugs();
    move_bugs(dt);
    play(dt);
    gw_step(dt);

    // you walk into the chair: it tips
    Vector3 me = sfxr_head_floor_point();
    float mx = me.x, mz = me.z;
    gw_push_loose(&mx, &mz, PLAYER_R, (Vector3){ 0 }, 6.0f);

    if (GD.round == WAITING)
        vrui_callout(Vector3Add(stump_top(), (Vector3){ 0, 0.75f, 0 }), "your hammer, Daddy: pick it up", 0.1f, RAYWHITE);

    // the Bugmaster, up on his tower: his name, and what he's shouting
    if (GD.round == PLAYING && GD.shout_t <= 0 && fmodf(GD.t, 15.0f) < dt && GD.t > 5) {
        int k = (int)(GD.t / 15.0f) % 4;
        bugmaster_says(TAUNT_TEXT[k], GD.taunt[k]);
    }
    Vector3 top = { TOWER.x, TOWER_TOP + 4.3f, TOWER.z };
    // (wrapped at about 28 letters a line: one long line would be meters wide up there)
    char wrapped[200] = "the Bugmaster";
    if (GD.shout_t > 0) {
        int n = 0, col = 0;
        for (const char *c = GD.shout; *c && n < (int)sizeof wrapped - 1; c++, col++) {
            if (*c == ' ' && col > 28) { wrapped[n++] = '\n'; col = 0; continue; }
            wrapped[n++] = *c;
        }
        wrapped[n] = 0;
    }
    vrui_tag(top, wrapped, 0.56f, GD.shout_t > 0 ? (Color){ 255, 220, 120, 255 } : RAYWHITE,
             (Color){ 60, 20, 70, 220 });
    GD.shout_t -= dt;
    sfxr_music_volume(GD.shout_t > 0 ? 0.15f : 0.35f);   // duck the music under his voice

    // labels on tough bugs: how many hits they have left
    for (int i = 0; i < MAX_BUGS; i++) {
        const Bug *b = &GD.bugs[i];
        if (b->alive && b->squash == 0 && b->health > 1)
            vrui_callout(Vector3Add(b->pos, (Vector3){ 0, BUG_R * 1.8f, 0 }), TextFormat("%d hits", b->health), 0.25f, TYPES[b->type].color);
    }

    // HUD, arrows to the two nearest bugs out of view, a red flash when bitten
    const char *hud = GD.round == WON ? "The garden is clear!\nRestart from a hand menu" :
                      GD.round == LOST ? "The bugs got you\nRestart from a hand menu" :
                      TextFormat("health %d/%d\nsmashed %d/%d", GD.hp, MAX_HP, GD.score, SCORE_TO_WIN);
    hud_show(menus_hud_style(), hud, GD.hp > 3 ? vrui_style()->accent : (Color){ 230, 70, 60, 255 });
    if (menus_edge_arrows()) {
        int shown = 0;
        bool used[MAX_BUGS] = { 0 };
        while (shown < 2) {
            int best = -1;
            float bd = 1e9f;
            for (int i = 0; i < MAX_BUGS; i++) {
                const Bug *b = &GD.bugs[i];
                float d = Vector3Distance(b->pos, me);
                if (b->alive && b->squash == 0 && !used[i] && d < bd) { bd = d; best = i; }
            }
            if (best < 0) break;
            used[best] = true;
            vrui_offscreen_arrow(Vector3Add(GD.bugs[best].pos, (Vector3){ 0, 0.3f, 0 }), "bug", TYPES[GD.bugs[best].type].color);
            shown++;
        }
    }
    if (GD.hurt > 0) { vrui_tint((Color){ 200, 20, 20, 255 }, 0.4f * GD.hurt / 0.35f); GD.hurt -= dt; }

    // moving: walk with the left stick, teleport or turn with the right; the
    // toolbox's turning choices carry over
    GD.loco = *toolbox_loco;
    GD.loco.smooth_move = true;
    GD.loco.ground_height = ground_cb;
    GD.loco.solid_depth = solid_cb;
    GD.loco.valid_target = inside_cb;
    GD.loco.pads = NULL;
    GD.loco.npads = 0;
    GD.loco.pads_only = false;
    vrui_locomotion(&GD.loco);
}

// --- for tests (tests/garden) ---------------------------------------------------------------------

GardenState garden_state(void)
{
    GardenState s = { (int)GD.round, GD.score, GD.hp, (int)GD.hammer_at, GD.hammer, hammer_head(), belt(), 0, { 0 } };
    for (int i = 0; i < MAX_BUGS; i++)
        if (GD.bugs[i].alive && GD.bugs[i].squash == 0) {
            if (s.bugs == 0) s.first_bug = Vector3Add(GD.bugs[i].pos, (Vector3){ 0, BUG_R * 0.9f, 0 });
            s.bugs++;
        }
    return s;
}

void garden_test_bug(int type, float x, float z)
{
    GD.test_mode = true;
    for (int i = 0; i < MAX_BUGS; i++) {
        Bug *b = &GD.bugs[i];
        if (b->alive) continue;
        *b = (Bug){ .alive = true, .pos = on_ground(x, z), .type = type, .health = TYPES[type].health };
        return;
    }
}

void garden_test_start(void) { GD.test_mode = true; GD.round = PLAYING; }
void garden_test_feel(int mode) { GD.feel = mode; }

// --- drawing -------------------------------------------------------------------------------------

static void draw_bug(const Bug *b)
{
    Color c = TYPES[b->type].color;
    float flat = b->squash > 0 ? fmaxf(0.1f, 1 - b->squash * 2.5f) : 1.0f;   // smashed: flattens
    float bob = b->squash > 0 ? 0 : sinf(b->wobble) * 0.03f;
    Vector3 body = Vector3Add(b->pos, (Vector3){ 0, BUG_R * 0.9f * flat + bob, 0 });
    rlPushMatrix();
    rlTranslatef(body.x, body.y, body.z);
    rlScalef(1 + (1 - flat) * 0.6f, flat, 1 + (1 - flat) * 0.6f);
    DrawSphereEx((Vector3){ 0 }, BUG_R, 8, 10, c);
    rlPopMatrix();
    if (b->squash > 0) return;
    // eyes toward you, and a dark underside
    Vector3 me = sfxr_head_floor_point();
    Vector3 f = Vector3Normalize((Vector3){ me.x - b->pos.x, 0, me.z - b->pos.z });
    Vector3 r = { -f.z, 0, f.x };
    for (int s = -1; s <= 1; s += 2) {
        Vector3 eye = Vector3Add(body, Vector3Add(Vector3Scale(f, BUG_R * 0.75f), Vector3Add(Vector3Scale(r, s * BUG_R * 0.35f), (Vector3){ 0, BUG_R * 0.35f, 0 })));
        DrawSphereEx(eye, BUG_R * 0.2f, 6, 6, RAYWHITE);
        DrawSphereEx(Vector3Add(eye, Vector3Scale(f, BUG_R * 0.12f)), BUG_R * 0.1f, 4, 4, BLACK);
    }
    DrawCylinder(Vector3Add(b->pos, (Vector3){ 0, 0.02f, 0 }), BUG_R * 0.9f, BUG_R * 0.9f, 0.04f, 10, ColorBrightness(c, -0.5f));
}

// The Bugmaster: his drawing from the flat game, standing on his tower as
// a flat cut-out that turns to face you (a billboard). It turns only about
// the vertical, so he stands upright however you tilt your head. Two frames
// swap to make him fidget -- faster while he's shouting.
static void draw_bugmaster(void)
{
    Texture2D tex = GD.bugmaster[(int)(sfxr_time() * (GD.shout_t > 0 ? 6.0 : 1.5)) % 2];
    if (tex.id == 0) return;
    const float px = 3.4f / 94.0f;                        // meters per pixel: he's 3.4 m tall up there
    float w = (float)tex.width * px, h = (float)tex.height * px;
    Vector3 feet = { TOWER.x, TOWER_TOP, TOWER.z };
    Vector3 to_me = Vector3Subtract(sfxr_head().position, feet);
    to_me.y = 0;
    if (Vector3Length(to_me) < 1e-3f) to_me = (Vector3){ 0, 0, 1 };
    to_me = Vector3Normalize(to_me);
    Vector3 right = { to_me.z, 0, -to_me.x };             // to your right, as you look at him
    Vector3 l = Vector3Subtract(feet, Vector3Scale(right, w * 0.5f));   // bottom corners, left and right
    Vector3 r = Vector3Add(feet, Vector3Scale(right, w * 0.5f));
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    rlNormal3f(to_me.x, 0, to_me.z);
    rlTexCoord2f(0, 1); rlVertex3f(l.x, l.y, l.z);        // counter-clockwise, seen from you
    rlTexCoord2f(1, 1); rlVertex3f(r.x, r.y, r.z);
    rlTexCoord2f(1, 0); rlVertex3f(r.x, r.y + h, r.z);
    rlTexCoord2f(0, 0); rlVertex3f(l.x, l.y + h, l.z);
    rlEnd();
    rlSetTexture(0);
}

void garden_draw(void)
{
    gw_draw();
    DrawCylinder(Vector3Subtract(stump_top(), (Vector3){ 0, 0.5f, 0 }), 0.2f, 0.22f, 0.5f, 12, (Color){ 110, 80, 50, 255 });   // the stump
    DrawCylinder(Vector3Subtract(stump_top(), (Vector3){ 0, 0.01f, 0 }), 0.19f, 0.19f, 0.012f, 12, (Color){ 190, 160, 110, 255 });
    // the hammer model: its origin is the handle's end, below the pose we keep
    sfxr_push_pose(sfxr_pose_mul(GD.shown, (SfxrPose){ { 0, -HANDLE_MID, 0 }, QuaternionIdentity() }));
    DrawModelEx(GD.hammer_model, (Vector3){ 0 }, (Vector3){ 0, 1, 0 }, 0, (Vector3){ HAMMER_XZ, HAMMER_Y, HAMMER_XZ }, WHITE);
    sfxr_pop_pose();
    for (int i = 0; i < MAX_BUGS; i++)
        if (GD.bugs[i].alive) draw_bug(&GD.bugs[i]);
    draw_bugmaster();   // last: his see-through edges blend with what's already drawn
}
