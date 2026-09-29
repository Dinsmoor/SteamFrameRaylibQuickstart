// sfxr_signals.c - the headset's other signals for apps: worn or not,
// refresh rate, blend mode, batteries, hand joints, performance counters,
// controller models. The XR backends fill them in (sfxr_xr_signals.c); the
// simulator, replays and tests provide their own values.

#include "sfxr_internal.h"

#include <math.h>
#include <string.h>

#define S sfxr_state

// ---------------------------------------------------------------------------
// Headset and system signals
// ---------------------------------------------------------------------------

static bool is_xr(void) { return S.backend == SFXR_BACKEND_XR_GL || S.backend == SFXR_BACKEND_XR_VK; }

bool sfxr_hand_joints_supported(void)
{
    return is_xr() ? sfxr_xr_hand_joints_supported() : (S.sig.joints[0].valid || S.sig.joints[1].valid);
}
const SfxrHandJoints *sfxr_hand_joints(SfxrHandId hand) { return &S.joints_world[hand == SFXR_RIGHT ? 1 : 0]; }

bool  sfxr_user_presence_known(void) { return S.sig.presence_known != 0; }
bool  sfxr_user_present(void) { return !S.sig.presence_known || S.sig.present; }
bool  sfxr_focused(void) { return !S.sig.unfocused; }   // (sampled each frame, so replays and tests have it too)

SfxrAttention sfxr_attention(void)
{
    if (!sfxr_user_present()) return SFXR_AWAY_HEADSET_OFF;
    if (!sfxr_focused()) return SFXR_AWAY_DASHBOARD;
    return SFXR_HERE;
}

const char *sfxr_attention_name(SfxrAttention a)
{
    return a == SFXR_AWAY_HEADSET_OFF ? "headset off" : a == SFXR_AWAY_DASHBOARD ? "dashboard open" : "here";
}
float sfxr_refresh_rate(void) { return S.sig.refresh_hz; }
int   sfxr_refresh_rates(float *out, int max) { return is_xr() ? sfxr_xr_refresh_rates(out, max) : 0; }
bool  sfxr_set_refresh_rate(float hz) { return is_xr() && sfxr_xr_set_refresh_rate(hz); }

bool sfxr_blend_supported(SfxrBlendMode m)
{
    if (m == SFXR_BLEND_OPAQUE) return true;
    return is_xr() && sfxr_xr_blend_supported(m);
}
void sfxr_set_blend_mode(SfxrBlendMode m) { if (sfxr_blend_supported(m)) S.blend = m; }
SfxrBlendMode sfxr_blend_mode(void) { return S.blend; }

SfxrBattery sfxr_battery(SfxrHandId hand) { return S.sig.battery[hand == SFXR_RIGHT ? 1 : 0]; }

void  sfxr_perf_enable(bool on) { if (is_xr()) sfxr_xr_perf_enable(on); }
int   sfxr_perf_count(void) { return is_xr() ? sfxr_xr_perf_count() : 0; }
const char *sfxr_perf_name(int i) { return is_xr() ? sfxr_xr_perf_name(i) : ""; }
bool  sfxr_perf_value(int i, float *value, const char **unit) { return is_xr() && sfxr_xr_perf_value(i, value, unit); }

const Model *sfxr_controller_model(SfxrHandId hand, SfxrPose *pose_out)
{
    if (!is_xr()) return NULL;
    SfxrPose stage;
    const Model *m = sfxr_xr_controller_model(hand, &stage);
    if (m && pose_out) *pose_out = sfxr_pose_mul(sfxr_rig_pose(), stage);
    return m;
}
