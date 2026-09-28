// sfxr_xr_input.c - OpenXR actions: which Steam Frame inputs are bound
// (every control's click and touch, five poses, bare hands, eye gaze), reading
// them each frame, and haptics.

#include "sfxr_xr_internal.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

enum {
    A_GRIP, A_AIM, A_POKE, A_PINCH, A_PALM, A_TRIGGER, A_SQUEEZE, A_STICK, A_HAPTIC, A_GAZE,
    A_CLICK0,                                   // + SfxrControl: that control's click
    A_TOUCH0 = A_CLICK0 + SFXR_CTL_COUNT,       // + SfxrControl: that control's touch
    A_COUNT  = A_TOUCH0 + SFXR_CTL_COUNT
};

static const struct { const char *name, *label; XrActionType type; } ACTION_DEFS[A_CLICK0] = {
    [A_GRIP]    = { "grip_pose",  "Grip pose",  XR_ACTION_TYPE_POSE_INPUT },
    [A_AIM]     = { "aim_pose",   "Aim pose",   XR_ACTION_TYPE_POSE_INPUT },
    [A_POKE]    = { "poke_pose",  "Poke pose",  XR_ACTION_TYPE_POSE_INPUT },
    [A_PINCH]   = { "pinch_pose", "Pinch pose", XR_ACTION_TYPE_POSE_INPUT },
    [A_PALM]    = { "palm_pose",  "Palm pose",  XR_ACTION_TYPE_POSE_INPUT },
    [A_TRIGGER] = { "trigger",    "Trigger",    XR_ACTION_TYPE_FLOAT_INPUT },
    [A_SQUEEZE] = { "squeeze",    "Grip",       XR_ACTION_TYPE_FLOAT_INPUT },
    [A_STICK]   = { "thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT },
    [A_HAPTIC]  = { "haptic",     "Haptics",    XR_ACTION_TYPE_VIBRATION_OUTPUT },
    [A_GAZE]    = { "eye_gaze",   "Eye gaze",   XR_ACTION_TYPE_POSE_INPUT },
};
static XrAction ACT[A_COUNT];

// OpenXR component names of each SfxrControl on the Frame controller profile.
static const char *const CTL_PATH[SFXR_CTL_COUNT] = {
    [SFXR_CTL_TRIGGER] = "trigger", [SFXR_CTL_SQUEEZE] = "squeeze", [SFXR_CTL_STICK] = "thumbstick",
    [SFXR_CTL_BUMPER] = "bumper", [SFXR_CTL_A] = "a", [SFXR_CTL_B] = "b", [SFXR_CTL_X] = "x",
    [SFXR_CTL_Y] = "y", [SFXR_CTL_MENU] = "menu", [SFXR_CTL_VIEW] = "view",
    [SFXR_CTL_DPAD_UP] = "dpad_up", [SFXR_CTL_DPAD_DOWN] = "dpad_down",
    [SFXR_CTL_DPAD_LEFT] = "dpad_left", [SFXR_CTL_DPAD_RIGHT] = "dpad_right",
};

// A profile's suggested bindings, built up with bind().
typedef struct { int n; XrActionSuggestedBinding b[128]; } Bindings;

static const char *const HAND_NAME[2] = { "left", "right" };

// hand: 0 left, 1 right, 2 both. suffix: e.g. "input/trigger/value".
static void bind(Bindings *bs, int action, int hand, const char *suffix)
{
    for (int h = 0; h < 2; h++) {
        if (hand != 2 && hand != h) continue;
        if (bs->n >= (int)(sizeof bs->b / sizeof bs->b[0])) return;
        char full[160];
        snprintf(full, sizeof full, "/user/hand/%s/%s", HAND_NAME[h], suffix);
        bs->b[bs->n].action = ACT[action];
        bs->b[bs->n].binding = sfxr_xr_path(full);
        bs->n++;
    }
}

static void suggest(const char *profile, const Bindings *bs)
{
    XrInteractionProfileSuggestedBinding s = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    s.interactionProfile = sfxr_xr_path(profile);
    s.countSuggestedBindings = (uint32_t)bs->n;
    s.suggestedBindings = bs->b;
    XrResult r = xrSuggestInteractionProfileBindings(X.instance, &s);
    if (XR_SUCCEEDED(r)) SFXR_LOG("bindings: %s (%d)", profile, bs->n);
    else sfxr_xr_check(r, profile, __FILE__, __LINE__);
}

// The Steam Frame controllers: every control, click + touch, every pose.
static void suggest_frame(void)
{
    Bindings bs = {0};
    bind(&bs, A_GRIP, 2, "input/grip/pose");
    bind(&bs, A_AIM, 2, "input/aim/pose");
    if (X.ext_hand_interaction) {
        bind(&bs, A_POKE, 2, "input/poke_ext/pose");
        bind(&bs, A_PINCH, 2, "input/pinch_ext/pose");
    }
    if (X.ext_palm_pose) bind(&bs, A_PALM, 2, "input/palm_ext/pose");
    bind(&bs, A_TRIGGER, 2, "input/trigger/value");
    bind(&bs, A_SQUEEZE, 2, "input/squeeze/value");
    bind(&bs, A_STICK, 2, "input/thumbstick");
    bind(&bs, A_HAPTIC, 2, "output/haptic");
    for (int c = 0; c < SFXR_CTL_COUNT; c++)
        for (int h = 0; h < 2; h++) {
            if (!sfxr_control_on_hand((SfxrControl)c, (SfxrHandId)h)) continue;
            char sfx[64];
            snprintf(sfx, sizeof sfx, "input/%s/click", CTL_PATH[c]);
            bind(&bs, A_CLICK0 + c, h, sfx);
            snprintf(sfx, sizeof sfx, "input/%s/touch", CTL_PATH[c]);
            bind(&bs, A_TOUCH0 + c, h, sfx);
        }
    suggest("/interaction_profiles/valve/frame_controller_valve", &bs);
}

// Bare hands (the Frame's hand tracking, when the controllers are put down):
// pinch drives the trigger, grasp drives the grip, so vrui works unchanged.
static void suggest_hands(void)
{
    Bindings bs = {0};
    bind(&bs, A_GRIP, 2, "input/grip/pose");
    bind(&bs, A_AIM, 2, "input/aim/pose");
    bind(&bs, A_POKE, 2, "input/poke_ext/pose");
    bind(&bs, A_PINCH, 2, "input/pinch_ext/pose");
    if (X.ext_palm_pose) bind(&bs, A_PALM, 2, "input/palm_ext/pose");
    bind(&bs, A_TRIGGER, 2, "input/pinch_ext/value");
    bind(&bs, A_SQUEEZE, 2, "input/grasp_ext/value");
    suggest("/interaction_profiles/ext/hand_interaction_ext", &bs);
}

// Other runtimes' controllers -- only offered when NOT on a Frame (e.g. the
// Monado test runtime), so SteamVR on the Frame never shows an emulation
// notice. Left-hand face buttons land on the Frame's left-hand equivalents
// (D-pad down/up, view), so sfxr_hand()->primary/secondary still work.
static void suggest_fallbacks(void)
{
    Bindings bs = {0};
    bind(&bs, A_GRIP, 2, "input/grip/pose"); bind(&bs, A_AIM, 2, "input/aim/pose");
    bind(&bs, A_TRIGGER, 2, "input/trigger/value"); bind(&bs, A_SQUEEZE, 2, "input/squeeze/value");
    bind(&bs, A_STICK, 2, "input/thumbstick"); bind(&bs, A_HAPTIC, 2, "output/haptic");
    bind(&bs, A_TOUCH0 + SFXR_CTL_TRIGGER, 2, "input/trigger/touch");
    bind(&bs, A_CLICK0 + SFXR_CTL_STICK, 2, "input/thumbstick/click");
    bind(&bs, A_TOUCH0 + SFXR_CTL_STICK, 2, "input/thumbstick/touch");
    bind(&bs, A_CLICK0 + SFXR_CTL_A, 1, "input/a/click"); bind(&bs, A_TOUCH0 + SFXR_CTL_A, 1, "input/a/touch");
    bind(&bs, A_CLICK0 + SFXR_CTL_B, 1, "input/b/click"); bind(&bs, A_TOUCH0 + SFXR_CTL_B, 1, "input/b/touch");
    bind(&bs, A_CLICK0 + SFXR_CTL_DPAD_DOWN, 0, "input/x/click"); bind(&bs, A_TOUCH0 + SFXR_CTL_DPAD_DOWN, 0, "input/x/touch");
    bind(&bs, A_CLICK0 + SFXR_CTL_DPAD_UP, 0, "input/y/click"); bind(&bs, A_TOUCH0 + SFXR_CTL_DPAD_UP, 0, "input/y/touch");
    bind(&bs, A_CLICK0 + SFXR_CTL_VIEW, 0, "input/menu/click");
    suggest("/interaction_profiles/oculus/touch_controller", &bs);

    Bindings ix = {0};
    bind(&ix, A_GRIP, 2, "input/grip/pose"); bind(&ix, A_AIM, 2, "input/aim/pose");
    bind(&ix, A_TRIGGER, 2, "input/trigger/value"); bind(&ix, A_SQUEEZE, 2, "input/squeeze/value");
    bind(&ix, A_STICK, 2, "input/thumbstick"); bind(&ix, A_HAPTIC, 2, "output/haptic");
    bind(&ix, A_CLICK0 + SFXR_CTL_TRIGGER, 2, "input/trigger/click");
    bind(&ix, A_TOUCH0 + SFXR_CTL_TRIGGER, 2, "input/trigger/touch");
    bind(&ix, A_CLICK0 + SFXR_CTL_STICK, 2, "input/thumbstick/click");
    bind(&ix, A_TOUCH0 + SFXR_CTL_STICK, 2, "input/thumbstick/touch");
    bind(&ix, A_CLICK0 + SFXR_CTL_A, 1, "input/a/click"); bind(&ix, A_CLICK0 + SFXR_CTL_B, 1, "input/b/click");
    bind(&ix, A_CLICK0 + SFXR_CTL_DPAD_DOWN, 0, "input/a/click"); bind(&ix, A_CLICK0 + SFXR_CTL_DPAD_UP, 0, "input/b/click");
    suggest("/interaction_profiles/valve/index_controller", &ix);

    Bindings sp = {0};
    bind(&sp, A_GRIP, 2, "input/grip/pose"); bind(&sp, A_AIM, 2, "input/aim/pose");
    bind(&sp, A_TRIGGER, 2, "input/select/click"); bind(&sp, A_HAPTIC, 2, "output/haptic");
    bind(&sp, A_CLICK0 + SFXR_CTL_MENU, 1, "input/menu/click");
    bind(&sp, A_CLICK0 + SFXR_CTL_VIEW, 0, "input/menu/click");
    suggest("/interaction_profiles/khr/simple_controller", &sp);
}

static XrSpace make_pose_space(int action, int h)
{
    XrActionSpaceCreateInfo sci = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
    sci.poseInActionSpace.orientation.w = 1.0f;
    sci.subactionPath = h >= 0 ? X.hand_path[h] : XR_NULL_PATH;
    sci.action = ACT[action];
    XrSpace sp = XR_NULL_HANDLE;
    XR_CHECK(xrCreateActionSpace(X.session, &sci, &sp));
    return sp;
}

bool sfxr_xr_create_actions(void)
{
    memset(ACT, 0, sizeof(ACT));
    XrActionSetCreateInfo asci = { XR_TYPE_ACTION_SET_CREATE_INFO };
    strcpy(asci.actionSetName, "gameplay");
    strcpy(asci.localizedActionSetName, "Gameplay");
    if (!XR_CHECK(xrCreateActionSet(X.instance, &asci, &X.action_set))) return false;

    X.hand_path[0] = sfxr_xr_path("/user/hand/left");
    X.hand_path[1] = sfxr_xr_path("/user/hand/right");

    for (int i = 0; i < A_COUNT; i++) {
        if (i == A_GAZE && !X.eye_gaze_supported) continue;
        XrActionCreateInfo aci = { XR_TYPE_ACTION_CREATE_INFO };
        if (i < A_CLICK0) {
            aci.actionType = ACTION_DEFS[i].type;
            strcpy(aci.actionName, ACTION_DEFS[i].name);
            strcpy(aci.localizedActionName, ACTION_DEFS[i].label);
        } else {
            bool click = i < A_TOUCH0;
            int c = click ? i - A_CLICK0 : i - A_TOUCH0;
            aci.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
            snprintf(aci.actionName, sizeof aci.actionName, "%s_%s", CTL_PATH[c], click ? "click" : "touch");
            snprintf(aci.localizedActionName, sizeof aci.localizedActionName, "%s %s",
                     sfxr_control_name((SfxrControl)c), click ? "press" : "touch");
        }
        if (i != A_GAZE) {
            aci.countSubactionPaths = 2;
            aci.subactionPaths = X.hand_path;
        }
        if (!XR_CHECK(xrCreateAction(X.action_set, &aci, &ACT[i]))) return false;
    }

    // Each profile is suggested separately so one unsupported path (or an
    // unknown profile on an older runtime) doesn't take the others down.
    // On a Frame we offer ONLY the Frame's own inputs: this quickstart is
    // Frame-specific, and offering other controllers' layouts invites SteamVR
    // to pick (and announce) an emulated one.
    bool frame = X.ext_frame_ctrl && !sfxr_env_flag("SFXR_FALLBACK_BINDINGS", false);
    if (X.ext_frame_ctrl) suggest_frame();
    if (X.ext_hand_interaction) suggest_hands();
    if (!frame) suggest_fallbacks();
    if (X.eye_gaze_supported) {
        XrActionSuggestedBinding g = { ACT[A_GAZE], sfxr_xr_path("/user/eyes_ext/input/gaze_ext/pose") };
        XrInteractionProfileSuggestedBinding s = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        s.interactionProfile = sfxr_xr_path("/interaction_profiles/ext/eye_gaze_interaction");
        s.countSuggestedBindings = 1;
        s.suggestedBindings = &g;
        XR_CHECK(xrSuggestInteractionProfileBindings(X.instance, &s));
    }
    SFXR_LOG("input: %s", frame ? "Steam Frame controllers + hand tracking only" : "Frame + fallback controller profiles");

    for (int h = 0; h < 2; h++) {
        X.grip_space[h]  = make_pose_space(A_GRIP, h);
        X.aim_space[h]   = make_pose_space(A_AIM, h);
        X.poke_space[h]  = make_pose_space(A_POKE, h);
        X.pinch_space[h] = make_pose_space(A_PINCH, h);
        X.palm_space[h]  = make_pose_space(A_PALM, h);
    }
    if (X.eye_gaze_supported) X.gaze_space = make_pose_space(A_GAZE, -1);

    XrSessionActionSetsAttachInfo ai = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    ai.countActionSets = 1;
    ai.actionSets = &X.action_set;
    return XR_CHECK(xrAttachSessionActionSets(X.session, &ai));
}

static bool get_bool(int a, int h, bool *active)
{
    XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
    gi.action = ACT[a];
    gi.subactionPath = X.hand_path[h];
    XrActionStateBoolean st = { XR_TYPE_ACTION_STATE_BOOLEAN };
    if (XR_FAILED(xrGetActionStateBoolean(X.session, &gi, &st))) { if (active) *active = false; return false; }
    if (active) *active = st.isActive;
    return st.isActive && st.currentState;
}

static float get_float(int a, int h)
{
    XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
    gi.action = ACT[a];
    gi.subactionPath = X.hand_path[h];
    XrActionStateFloat st = { XR_TYPE_ACTION_STATE_FLOAT };
    if (XR_FAILED(xrGetActionStateFloat(X.session, &gi, &st)) || !st.isActive) return 0.0f;
    return st.currentState;
}

static Vector2 get_vec2(int a, int h)
{
    XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
    gi.action = ACT[a];
    gi.subactionPath = X.hand_path[h];
    XrActionStateVector2f st = { XR_TYPE_ACTION_STATE_VECTOR2F };
    if (XR_FAILED(xrGetActionStateVector2f(X.session, &gi, &st)) || !st.isActive) return (Vector2){0};
    return (Vector2){ st.currentState.x, st.currentState.y };
}


void sfxr_xr_update_profiles(void)
{
    for (int h = 0; h < 2; h++) {
        XrInteractionProfileState ps = { XR_TYPE_INTERACTION_PROFILE_STATE };
        char now[sizeof S.raw[h].profile] = "";
        if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(X.session, X.hand_path[h], &ps)) &&
            ps.interactionProfile != XR_NULL_PATH) {
            uint32_t n = 0;
            xrPathToString(X.instance, ps.interactionProfile, sizeof now, &n, now);
        }
        if (strcmp(now, S.raw[h].profile) != 0) {   // runtimes send several events; log changes only
            memcpy(S.raw[h].profile, now, sizeof now);
            SFXR_LOG("%s hand profile: %s", h ? "right" : "left", now[0] ? now : "(none)");
        }
    }
}

void sfxr_xr_sample_input(void)
{
    XrActiveActionSet aas = { X.action_set, XR_NULL_PATH };
    XrActionsSyncInfo si = { XR_TYPE_ACTIONS_SYNC_INFO };
    si.countActiveActionSets = 1;
    si.activeActionSets = &aas;
    XrResult r = xrSyncActions(X.session, &si);
    bool focused = (r == XR_SUCCESS);

    for (int h = 0; h < 2; h++) {
        SfxrRawHand *o = &S.raw[h];
        char profile[sizeof o->profile];
        memcpy(profile, o->profile, sizeof profile);
        memset(o, 0, sizeof *o);
        memcpy(o->profile, profile, sizeof profile);
        if (!focused) continue;

        bool grip_ok = sfxr_xr_locate(X.grip_space[h], &o->grip, &o->velocity, &o->angular_velocity, &o->has_velocity);
        bool aim_ok  = sfxr_xr_locate(X.aim_space[h], &o->aim, NULL, NULL, NULL);
        if (!aim_ok && grip_ok) o->aim = o->grip;
        if (!grip_ok && aim_ok) o->grip = o->aim;
        o->active = grip_ok || aim_ok;
        if (!o->active) continue;
        o->source = strstr(o->profile, "hand_interaction") ? SFXR_SOURCE_HAND : SFXR_SOURCE_CONTROLLER;
        o->pose_valid = RAW_POSE_GRIP | RAW_POSE_AIM;
        if (sfxr_xr_locate(X.poke_space[h], &o->poke, NULL, NULL, NULL))   o->pose_valid |= RAW_POSE_POKE;
        if (sfxr_xr_locate(X.pinch_space[h], &o->pinch, NULL, NULL, NULL)) o->pose_valid |= RAW_POSE_PINCH;
        if (sfxr_xr_locate(X.palm_space[h], &o->palm, NULL, NULL, NULL))   o->pose_valid |= RAW_POSE_PALM;

        o->trigger = get_float(A_TRIGGER, h);
        o->squeeze = get_float(A_SQUEEZE, h);
        o->stick = get_vec2(A_STICK, h);
        for (int c = 0; c < SFXR_CTL_COUNT; c++) {
            if (get_bool(A_CLICK0 + c, h, NULL)) o->click |= RAW_BIT(c);
            if (get_bool(A_TOUCH0 + c, h, NULL)) o->touch |= RAW_BIT(c);
        }
    }

    sfxr_xr_locate_hand_joints(focused);
    sfxr_xr_poll_battery();

    S.gaze_valid = false;
    if (focused && X.eye_gaze_supported && X.gaze_space) {
        XrActionStatePose ps = { XR_TYPE_ACTION_STATE_POSE };
        XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
        gi.action = ACT[A_GAZE];
        if (XR_SUCCEEDED(xrGetActionStatePose(X.session, &gi, &ps)) && ps.isActive)
            S.gaze_valid = sfxr_xr_locate(X.gaze_space, &S.gaze_stage, NULL, NULL, NULL);
    }
}

void sfxr_xr_haptic(SfxrHandId hand, float amplitude, float seconds, float freq)
{
    if (!X.session || !X.running) return;
    XrHapticVibration v = { XR_TYPE_HAPTIC_VIBRATION };
    v.amplitude = amplitude;
    v.duration = (XrDuration)(seconds * 1e9);
    v.frequency = freq > 0.0f ? freq : XR_FREQUENCY_UNSPECIFIED;
    XrHapticActionInfo hi = { XR_TYPE_HAPTIC_ACTION_INFO };
    hi.action = ACT[A_HAPTIC];
    hi.subactionPath = X.hand_path[hand == SFXR_RIGHT ? 1 : 0];
    xrApplyHapticFeedback(X.session, &hi, (const XrHapticBaseHeader *)&v);
}
