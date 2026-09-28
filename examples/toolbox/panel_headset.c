// panel_headset.c - the Headset panel, behind you on the left: the rest of
// the hardware -- worn or not, refresh rate, passthrough (alpha blend),
// controller batteries, hand joints, the runtime's controller models, and
// performance counters.

#include "toolbox.h"

#include <stdio.h>
#include <string.h>

static struct {
    SfxrPose pose;
    bool placed;
    int  takeoffs;         // how often the headset was taken off
    bool was_present;
    bool models, joints_always, perf;
    int  rate_choice;
} HS = { .models = true, .was_present = true };

static const char *joint_source(const SfxrHandJoints *j)
{
    return !j->valid ? "none" : j->source == SFXR_SOURCE_HAND ? "bare hand" : "from controller";
}

void panel_headset(void)
{
    if (!HS.placed) {
        HS.pose = station_pose(-160, 2.2f, 1.35f);
        HS.placed = true;
    }
    bool present = sfxr_user_present();
    if (HS.was_present && !present) HS.takeoffs++;
    HS.was_present = present;

    if (!vrui_panel_begin(VRUI_ID2(G_HEADSET, 0), &HS.pose, 0.56f, 0.62f, "Headset")) return;
    vrui_layout_begin(vrui_panel_content(), 6);
    Rectangle cols[2];

    vrui_label(vrui_row(24), TextFormat("Worn: %s   (taken off %d times)",
               !sfxr_user_presence_known() ? "unknown" : present ? "yes" : "no", HS.takeoffs));

    // refresh rate: whatever the runtime offers right now
    float rates[8];
    int nr = sfxr_refresh_rates(rates, 8);
    vrui_label(vrui_row(24), TextFormat("Refresh: %.0f Hz%s", sfxr_refresh_rate(), nr <= 1 ? "  (only rate offered)" : ""));
    if (nr > 1) {
        char names[8][8];
        const char *items[8];
        for (int i = 0; i < nr; i++) { snprintf(names[i], sizeof names[i], "%.0f", rates[i]); items[i] = names[i]; }
        if (vrui_segmented(1, vrui_row(34), items, nr, &HS.rate_choice)) sfxr_set_refresh_rate(rates[HS.rate_choice]);
    }

    if (sfxr_blend_supported(SFXR_BLEND_ALPHA)) {
        if (vrui_toggle(2, vrui_row(30), "Passthrough (alpha blend, hides the world)", &world.passthrough))
            sfxr_set_blend_mode(world.passthrough ? SFXR_BLEND_ALPHA : SFXR_BLEND_OPAQUE);
    } else {
        vrui_label(vrui_row(24), "Passthrough: not offered here");
    }

    for (int h = 0; h < 2; h++) {
        SfxrBattery b = sfxr_battery((SfxrHandId)h);
        const char *side = h ? "Right" : "Left";
        if (!b.valid) vrui_label(vrui_row(24), TextFormat("%s battery: unknown", side));
        else vrui_label(vrui_row(24), TextFormat("%s battery: %.0f%%%s%s", side, b.level * 100.0f,
                        b.charging ? " charging" : "", b.plugged_in ? " plugged in" : ""));
    }

    if (sfxr_hand_joints_supported())
        vrui_label(vrui_row(24), TextFormat("Hand joints: L %s, R %s", joint_source(sfxr_hand_joints(SFXR_LEFT)),
                                            joint_source(sfxr_hand_joints(SFXR_RIGHT))));
    else
        vrui_label(vrui_row(24), "Hand joints: not supported here");
    vrui_row_cols(30, 2, cols);
    if (vrui_toggle(3, cols[0], "Joints while holding", &HS.joints_always)) vrui_hand_joints_always(HS.joints_always);
    if (vrui_toggle(4, cols[1], "Real controller models", &HS.models)) vrui_controller_models(HS.models);

    if (vrui_toggle(5, vrui_row(30), "Performance counters", &HS.perf)) sfxr_perf_enable(HS.perf);
    if (HS.perf) {
        int n = sfxr_perf_count();
        if (!n) vrui_label(vrui_row(22), "(none offered here)");
        for (int i = 0; i < n && i < 8; i++) {
            float v;
            const char *unit;
            const char *name = sfxr_perf_name(i);
            const char *slash = strrchr(name, '/');   // "/perfmetrics_meta/app/gpu_frametime" -> "gpu_frametime"
            if (sfxr_perf_value(i, &v, &unit))
                vrui_label(vrui_row(22), TextFormat("%s: %.2f %s", slash ? slash + 1 : name, v, unit));
        }
    }
    vrui_panel_end();
}
