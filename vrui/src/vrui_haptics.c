// vrui_haptics.c - one haptic channel per hand, mixed.
//
// These controllers can't push back, so haptics carry all the "physical"
// feedback. A new vibration replaces whatever is playing, so everything goes
// through this mixer instead of calling sfxr_haptic() directly:
//
//   PULSES  short events: detent ticks, end-stop bumps, grab/release clicks.
//           The strongest pulse of a frame plays, and it isn't cut off by a
//           hum until it has finished.
//   HUMS    continuous feedback held for as long as it's requested each frame:
//           spring tension, strain against resistance. The strongest hum of a
//           frame plays; it is re-issued every frame and stopped when nobody
//           asks for it any more.
//
// vrui_style()->haptic_scale scales everything (0 turns haptics off).

#include "vrui_internal.h"

void vrui_haptic_pulse(SfxrHandId hand, float amplitude, float seconds, float frequency_hz)
{
    VruiHapticMix *m = &C.hap[hand == SFXR_RIGHT ? 1 : 0];
    if (amplitude <= 0 || (m->pulse_pending && m->pulse_amp >= amplitude)) return;
    m->pulse_pending = true;
    m->pulse_amp = amplitude;
    m->pulse_secs = seconds;
    m->pulse_hz = frequency_hz;
}

void vrui_haptic_hum(SfxrHandId hand, float amplitude, float frequency_hz)
{
    VruiHapticMix *m = &C.hap[hand == SFXR_RIGHT ? 1 : 0];
    if (amplitude <= m->hum_amp) return;
    m->hum_amp = amplitude;
    m->hum_hz = frequency_hz;
}

void vrui__haptics_flush(void)
{
    float scale = C.style.haptic_scale;
    double now = sfxr_time();
    for (int h = 0; h < 2; h++) {
        VruiHapticMix *m = &C.hap[h];
        if (m->pulse_pending && scale > 0) {
            sfxr_haptic((SfxrHandId)h, Clamp(m->pulse_amp * scale, 0, 1), m->pulse_secs, m->pulse_hz);
            m->pulse_until = now + m->pulse_secs;
            m->humming = false;
        } else if (m->hum_amp > 0 && scale > 0 && now >= m->pulse_until) {
            // re-issued every frame; 50 ms bridges any frame hitch
            sfxr_haptic((SfxrHandId)h, Clamp(m->hum_amp * scale, 0, 1), 0.05f, m->hum_hz);
            m->humming = true;
        } else if (m->humming && m->hum_amp <= 0) {
            sfxr_haptic((SfxrHandId)h, 0.0f, 0.001f, 0);   // stop the hum now, not in 50 ms
            m->humming = false;
        }
        m->pulse_pending = false;
        m->pulse_amp = 0;
        m->hum_amp = 0;
    }
}
