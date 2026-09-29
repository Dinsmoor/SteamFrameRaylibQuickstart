// sfxr_sim.c - desktop simulator backend.
//
// No headset: renders a mono view into the window and fakes the head and two
// controllers with mouse + keyboard. One hand is "active" and follows the
// mouse cursor; the other rests at your side. Good enough to build and test
// nearly every interaction before putting the headset on.
//
//   RMB drag      look around            WASD / Q E   walk / crouch-rise (Shift = fast)
//   mouse         aim active hand        wheel        hand distance
//   Shift+wheel   roll (twist) hand      Tab          switch active hand
//   LMB           trigger                F / MMB      grip (hold)   G = grip latch
//   1..5          right: A / B / menu / X / Y   left: D-pad down / up / view / left / right
//   6             bumper
//   arrows        thumbstick             Space        stick click
//   H             bare hands on/off (then LMB pinches, F/MMB/G makes a fist, P points)
//   F1            toggle this help
//
// Env: SFXR_SIM_POS="x,y,z" and SFXR_SIM_LOOK="yaw_deg,pitch_deg" set the start pose;
// SFXR_SIM_BARE=1 starts with bare hands.

#include "sfxr_internal.h"
#include "sfxr_gl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define S sfxr_state

void sfxr_texture_skip_srgb_decode(unsigned tex);
static void sim_bare_hand(int h, SfxrRawHand *o);

static struct {
    float yaw, pitch;          // head
    Vector3 head_pos;          // stage space
    float max_y;               // Q/E ceiling: 2.5 m, or higher when SFXR_SIM_POS starts above it (overhead shots)
    int active;                // active hand index
    float hand_dist;
    float hand_roll;
    bool grip_latch[2];
    bool bare;                 // H: bare hands (a procedural skeleton, like a runtime with a hand profile)
    SfxrPose prev_grip[2];
    bool have_prev;
    // render target
    unsigned tex, depth_rb, fbo;
    int w, h;
} M;

static const float SIM_VFOV = 70.0f * DEG2RAD;

static void destroy_target(void)
{
    if (M.fbo) sgl.DeleteFramebuffers(1, &M.fbo);
    if (M.tex) sgl.DeleteTextures(1, &M.tex);
    if (M.depth_rb) sgl.DeleteRenderbuffers(1, &M.depth_rb);
    M.fbo = M.tex = M.depth_rb = 0;
}

static bool ensure_target(int w, int h)
{
    if (M.fbo && M.w == w && M.h == h) return true;
    destroy_target();
    sgl.GenTextures(1, &M.tex);
    sgl.BindTexture(SGL_TEXTURE_2D, M.tex);
    // Same sRGB format the XR swapchains use, so MSAA resolves match.
    sgl.TexImage2D(SGL_TEXTURE_2D, 0, SGL_SRGB8_ALPHA8, w, h, 0, SGL_RGBA, SGL_UNSIGNED_BYTE, NULL);
    sgl.BindTexture(SGL_TEXTURE_2D, 0);
    sfxr_texture_skip_srgb_decode(M.tex);
    sgl.GenRenderbuffers(1, &M.depth_rb);
    sgl.BindRenderbuffer(SGL_RENDERBUFFER, M.depth_rb);
    sgl.RenderbufferStorage(SGL_RENDERBUFFER, SGL_DEPTH_COMPONENT24, w, h);
    sgl.BindRenderbuffer(SGL_RENDERBUFFER, 0);
    M.fbo = sfxr_make_fbo(M.tex, M.depth_rb);
    M.w = w;
    M.h = h;
    return M.fbo != 0;
}

static bool sim_init(void)
{
    memset(&M, 0, sizeof(M));
    M.head_pos = (Vector3){ 0, 1.6f, 0 };
    // The simulator is always "worn"; it runs at the window's 90 Hz.
    S.sig.presence_known = 1;
    S.sig.present = 1;
    S.sig.refresh_hz = 90.0f;
    M.pitch = -20.0f * DEG2RAD;   // looking a little down, like at a desk
    // Scriptable start pose for screenshots/tests:
    //   SFXR_SIM_POS="x,y,z"   SFXR_SIM_LOOK="yaw_deg,pitch_deg"
    const char *pos = sfxr_env_str("SFXR_SIM_POS");
    if (pos) sscanf(pos, "%f,%f,%f", &M.head_pos.x, &M.head_pos.y, &M.head_pos.z);
    M.max_y = fmaxf(2.5f, M.head_pos.y);
    const char *look = sfxr_env_str("SFXR_SIM_LOOK");
    if (look) {
        float yd = 0, pd = 0;
        if (sscanf(look, "%f,%f", &yd, &pd) == 2) { M.yaw = yd * DEG2RAD; M.pitch = pd * DEG2RAD; }
    }
    M.active = 1;
    M.hand_dist = 0.45f;
    M.bare = sfxr_env_flag("SFXR_SIM_BARE", false);   // start with bare hands (screenshots)
    snprintf(S.runtime_name, sizeof(S.runtime_name), "sfxr simulator");
    snprintf(S.system_name, sizeof(S.system_name), "Desktop (mouse + keyboard)");
    for (int h = 0; h < 2; h++)
        snprintf(S.raw[h].profile, sizeof(S.raw[h].profile), "/interaction_profiles/sfxr/simulator");
    S.eye_w = GetScreenWidth();
    S.eye_h = GetScreenHeight();
    return ensure_target(S.eye_w, S.eye_h);
}

static void sim_shutdown(void) { destroy_target(); }

static Quaternion yaw_pitch_roll(float yaw, float pitch, float roll)
{
    Quaternion qy = QuaternionFromAxisAngle((Vector3){0, 1, 0}, yaw);
    Quaternion qp = QuaternionFromAxisAngle((Vector3){1, 0, 0}, pitch);
    Quaternion qr = QuaternionFromAxisAngle((Vector3){0, 0, 1}, roll);
    return QuaternionMultiply(QuaternionMultiply(qy, qp), qr);
}

// Quaternion whose -Z axis points along dir, keeping +Y roughly up.
static Quaternion look_rotation(Vector3 dir)
{
    dir = Vector3Normalize(dir);
    float yaw = atan2f(-dir.x, -dir.z);
    float pitch = asinf(Clamp(dir.y, -1.0f, 1.0f));
    return yaw_pitch_roll(yaw, pitch, 0.0f);
}

static bool sim_frame_begin(void)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    if (sw < 16 || sh < 16) { S.should_render = false; return true; }
    S.eye_w = sw;
    S.eye_h = sh;
    ensure_target(sw, sh);
    S.should_render = true;
    float dt = S.dt > 0 ? S.dt : 1.0f / 90.0f;

    // --- head
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 d = GetMouseDelta();
        M.yaw -= d.x * 0.004f;
        M.pitch -= d.y * 0.004f;
        M.pitch = Clamp(M.pitch, -1.45f, 1.45f);
    }
    float speed = (IsKeyDown(KEY_LEFT_SHIFT) ? 4.0f : 1.5f) * dt;
    Vector3 fwd = { -sinf(M.yaw), 0, -cosf(M.yaw) };
    Vector3 right = { cosf(M.yaw), 0, -sinf(M.yaw) };
    if (IsKeyDown(KEY_W)) M.head_pos = Vector3Add(M.head_pos, Vector3Scale(fwd, speed));
    if (IsKeyDown(KEY_S)) M.head_pos = Vector3Subtract(M.head_pos, Vector3Scale(fwd, speed));
    if (IsKeyDown(KEY_D)) M.head_pos = Vector3Add(M.head_pos, Vector3Scale(right, speed));
    if (IsKeyDown(KEY_A)) M.head_pos = Vector3Subtract(M.head_pos, Vector3Scale(right, speed));
    if (IsKeyDown(KEY_E)) M.head_pos.y += speed;
    if (IsKeyDown(KEY_Q)) M.head_pos.y -= speed;
    M.head_pos.y = Clamp(M.head_pos.y, 0.3f, M.max_y);

    S.head_stage.position = M.head_pos;
    S.head_stage.orientation = yaw_pitch_roll(M.yaw, M.pitch, 0);
    S.eye_stage[0] = S.eye_stage[1] = S.head_stage;
    float aspect = (float)sw / (float)sh;
    float ty = tanf(SIM_VFOV * 0.5f), tx = ty * aspect;
    for (int e = 0; e < 2; e++) {
        S.fov[e][0] = -atanf(tx); S.fov[e][1] = atanf(tx);
        S.fov[e][2] = atanf(ty);  S.fov[e][3] = -atanf(ty);
    }
    S.views_valid = true;

    // --- hands
    if (IsKeyPressed(KEY_TAB)) M.active ^= 1;
    if (IsKeyPressed(KEY_H)) M.bare = !M.bare;
    float wheel = GetMouseWheelMove();
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) M.hand_roll += wheel * 0.2f;
    else M.hand_dist = Clamp(M.hand_dist + wheel * 0.05f, 0.15f, 2.0f);

    // Ray through the mouse cursor, in stage space.
    Vector2 mp = GetMousePosition();
    float nx = (mp.x / sw) * 2.0f - 1.0f, ny = 1.0f - (mp.y / sh) * 2.0f;
    Vector3 dir_view = Vector3Normalize((Vector3){ nx * tx, ny * ty, -1.0f });
    Vector3 dir = Vector3RotateByQuaternion(dir_view, S.head_stage.orientation);

    for (int h = 0; h < 2; h++) {
        SfxrRawHand *o = &S.raw[h];
        char profile[sizeof o->profile];
        memcpy(profile, o->profile, sizeof profile);
        memset(o, 0, sizeof *o);
        memcpy(o->profile, profile, sizeof profile);
        o->active = true;
        o->source = SFXR_SOURCE_CONTROLLER;
        o->pose_valid = RAW_POSE_GRIP | RAW_POSE_AIM;

        if (h == M.active) {
            // The aim ray IS the cursor ray, so the laser hits exactly what the
            // mouse is over; the aim point (controller tip) sits hand_dist out
            // along it. The grip (controller body) hangs a little below/behind
            // so it doesn't hide the target.
            Quaternion q = QuaternionMultiply(look_rotation(dir),
                                              QuaternionFromAxisAngle((Vector3){0, 0, 1}, M.hand_roll));
            o->aim.position = Vector3Add(M.head_pos, Vector3Scale(dir, M.hand_dist));
            o->aim.orientation = q;
            o->grip.orientation = q;
            o->grip.position = Vector3Add(o->aim.position,
                                          Vector3RotateByQuaternion((Vector3){ 0, -0.05f, 0.08f }, q));
        } else {
            float side = h == 0 ? -1.0f : 1.0f;
            Vector3 rest = { side * 0.22f, -0.45f, -0.25f };
            Quaternion body = QuaternionFromAxisAngle((Vector3){0, 1, 0}, M.yaw);
            o->aim.position = Vector3Add(M.head_pos, Vector3RotateByQuaternion(rest, body));
            o->aim.orientation = QuaternionMultiply(body, QuaternionFromAxisAngle((Vector3){1, 0, 0}, -0.3f));
            o->grip = o->aim;
            o->grip.position = Vector3Add(o->aim.position,
                                          Vector3RotateByQuaternion((Vector3){ 0, 0, 0.06f }, o->aim.orientation));
        }

        if (M.have_prev && dt > 0) {
            o->velocity = Vector3Scale(Vector3Subtract(o->grip.position, M.prev_grip[h].position), 1.0f / dt);
            o->has_velocity = true;
        }
        M.prev_grip[h] = o->grip;

        if (M.bare) { sim_bare_hand(h, o); continue; }
        memset(&S.sig.joints[h], 0, sizeof S.sig.joints[h]);
        if (h != M.active) {
            if (M.grip_latch[h]) { o->squeeze = 1.0f; }
            continue;
        }
        o->trigger = IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? 1.0f : 0.0f;
        if (IsKeyPressed(KEY_G)) M.grip_latch[h] = !M.grip_latch[h];
        bool grip = IsKeyDown(KEY_F) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || M.grip_latch[h];
        o->squeeze = grip ? 1.0f : 0.0f;
        o->stick.x = (IsKeyDown(KEY_RIGHT) ? 1.0f : 0.0f) - (IsKeyDown(KEY_LEFT) ? 1.0f : 0.0f);
        o->stick.y = (IsKeyDown(KEY_UP) ? 1.0f : 0.0f) - (IsKeyDown(KEY_DOWN) ? 1.0f : 0.0f);
        // Keys stand in for the controller under this hand (right: A/B/menu/X/Y,
        // left: D-pad down/up/view/left/right) -- the same physical spots.
        bool right = h == SFXR_RIGHT;
        static const int keys[] = { KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE };
        const SfxrControl rc[] = { SFXR_CTL_A, SFXR_CTL_B, SFXR_CTL_MENU, SFXR_CTL_X, SFXR_CTL_Y };
        const SfxrControl lc[] = { SFXR_CTL_DPAD_DOWN, SFXR_CTL_DPAD_UP, SFXR_CTL_VIEW, SFXR_CTL_DPAD_LEFT, SFXR_CTL_DPAD_RIGHT };
        for (int k = 0; k < 5; k++)
            if (IsKeyDown(keys[k])) o->click |= RAW_BIT(right ? rc[k] : lc[k]);
        if (IsKeyDown(KEY_SPACE)) o->click |= RAW_BIT(SFXR_CTL_STICK);
        if (IsKeyDown(KEY_SIX)) o->click |= RAW_BIT(SFXR_CTL_BUMPER);
        if (o->trigger >= 1.0f) o->click |= RAW_BIT(SFXR_CTL_TRIGGER);
        if (o->squeeze >= 1.0f) o->click |= RAW_BIT(SFXR_CTL_SQUEEZE);
        o->touch = o->click;
        if (o->stick.x != 0 || o->stick.y != 0) o->touch |= RAW_BIT(SFXR_CTL_STICK);
    }
    M.have_prev = true;

    // Gaze = the cursor ray (lets you test gaze UI with the mouse).
    S.gaze_valid = true;
    S.gaze_stage.position = M.head_pos;
    S.gaze_stage.orientation = look_rotation(dir);
    return true;
}

// A bare hand: the skeleton from the grip pose and a shape picked with the
// mouse and keys, and the input a runtime's hand profile would give (pinch
// strength as the trigger, grasp as the grip, the index tip as the poke).
// SFXR_HANDS=joints makes sfxr ignore this and build it from the joints.
static void sim_bare_hand(int h, SfxrRawHand *o)
{
    float curl[5] = { 0.15f, 0.1f, 0.15f, 0.2f, 0.25f }, pinch = 0;   // relaxed open hand
    if (h == M.active) {
        if (IsKeyPressed(KEY_G)) M.grip_latch[h] = !M.grip_latch[h];
        if (IsKeyDown(KEY_F) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || M.grip_latch[h])
            for (int f = 0; f < 5; f++) curl[f] = f ? 1.0f : 0.7f;
        else if (IsKeyDown(KEY_P)) { curl[0] = 0.7f; curl[1] = 0; curl[2] = curl[3] = curl[4] = 1; }
        else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { curl[0] = curl[1] = 0.3f; pinch = 1; }
    }
    SfxrHandJoints *j = &S.sig.joints[h];
    sfxr__hand_model(h, o->grip, curl, pinch, j);
    o->source = SFXR_SOURCE_HAND;
    snprintf(o->profile, sizeof o->profile, "/interaction_profiles/ext/hand_interaction_ext");
    o->pose_valid |= RAW_POSE_POKE | RAW_POSE_PINCH | RAW_POSE_PALM;
    o->poke = j->joint[SFXR_JOINT_INDEX_TIP];
    o->pinch = (SfxrPose){ Vector3Lerp(j->joint[SFXR_JOINT_INDEX_TIP].position, j->joint[SFXR_JOINT_THUMB_TIP].position, 0.5f),
                           o->aim.orientation };
    o->palm = j->joint[SFXR_JOINT_PALM];
    float d = Vector3Distance(j->joint[SFXR_JOINT_INDEX_TIP].position, j->joint[SFXR_JOINT_THUMB_TIP].position);
    o->trigger = Clamp((0.045f - d) / 0.03f, 0, 1);
    o->squeeze = Clamp(((curl[2] + curl[3] + curl[4]) / 3.0f - 0.25f) / 0.5f, 0, 1);
}

static bool sim_acquire(unsigned *fbo, unsigned *tex)
{
    if (!M.fbo) return false;
    *fbo = M.fbo;
    *tex = M.tex;
    return true;
}

static void sim_release(void) {}
static void sim_frame_end(bool rendered) { (void)rendered; }

static float haptic_until[2];
static void sim_haptic(SfxrHandId hand, float amplitude, float seconds, float freq)
{
    (void)amplitude; (void)freq;
    haptic_until[hand == SFXR_RIGHT ? 1 : 0] = (float)GetTime() + (seconds < 0.05f ? 0.05f : seconds);
}

void sfxr_sim_draw_help(int x, int y)
{
    static const char *lines[] = {
        "SIMULATOR  (F1 hides)",
        "RMB drag: look     WASD/QE: walk/rise (Shift fast)",
        "Mouse: aim hand    Wheel: hand distance   Shift+Wheel: twist",
        "Tab: switch hand   LMB: trigger   F/MMB: grip   G: grip latch",
        "1-5: A/B/menu/X/Y (left: D-pad dn/up/view/lt/rt)   6: bumper   Arrows: stick   Space: stick click",
        "H: bare hands (LMB pinch, F/MMB fist, P point)",
        "F2: take the headset off / put it on   F3: open / close the dashboard",
    };
    int n = (int)(sizeof lines / sizeof lines[0]);
    DrawRectangle(x - 4, y - 4, 520, n * 16 + 26, (Color){ 0, 0, 0, 150 });
    for (int i = 0; i < n; i++) DrawText(lines[i], x, y + i * 16, 10, i ? RAYWHITE : YELLOW);
    const char *hand = M.active ? "RIGHT" : "LEFT";
    float now = (float)GetTime();
    bool buzz = now < haptic_until[M.active];
    DrawText(TextFormat("active hand: %s%s  dist %.2fm%s%s", hand, M.bare ? " (bare)" : "", M.hand_dist,
                        M.grip_latch[M.active] ? "  [grip latched]" : "", buzz ? "  ~BUZZ~" : ""),
             x, y + n * 16 + 2, 10, buzz ? ORANGE : SKYBLUE);
}

const SfxrBackendVtbl sfxr_backend_sim = {
    sim_init, sim_shutdown, sim_frame_begin, sim_acquire, sim_release, sim_frame_end, sim_haptic,
};
