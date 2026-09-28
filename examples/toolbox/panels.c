// panels.c - the Toolbox panel (world settings, pull level, grab style: the
// 2D widgets, all of them) and the small wrist panel.

#include "toolbox.h"
#include "onboarding.h"

static struct {
    SfxrPose pose;
    bool  placed;
    bool  smooth_turn;
    bool  real_falls;
    int   snap_choice;    // 0:15 1:30 2:45 degrees
    int   pull_choice;    // SfxrPull
    int   grab_choice;    // VruiGrabStyle
    float color_scroll;
} P = { .snap_choice = 1 };

void panels_toolbox(VruiLocoConfig *loco)
{
    if (!P.placed) {
        P.pose = station_pose(-55, 1.3f, 1.35f);
        P.placed = true;
    }
    if (!vrui_panel_begin(VRUI_ID2(G_PANEL, 0), &P.pose, 0.46f, 0.78f, "Toolbox")) return;
    vrui_layout_begin(vrui_panel_content(), 8);
    Rectangle cols[2];

    vrui_row_cols(40, 2, cols);
    if (vrui_button(1, cols[0], "Reset blocks")) world_reset_blocks();
    if (vrui_button(2, cols[1], "Spawn here")) {
        const SfxrHand *r = sfxr_hand(SFXR_RIGHT);
        world_spawn_block(Vector3Add(r->aim.position, Vector3Scale(sfxr_pose_forward(r->aim), 0.15f)));
    }

    vrui_row_cols(36, 2, cols);
    vrui_toggle(3, cols[0], "Gravity", &world.gravity);
    vrui_toggle(4, cols[1], "Grid", &world.show_grid);
    vrui_row_cols(36, 2, cols);
    vrui_toggle(5, cols[0], "Smooth turn", &P.smooth_turn);
    vrui_toggle(12, cols[1], "Real falls", &P.real_falls);   // both: less comfortable

    vrui_label(vrui_row(24), "Snap turn angle");
    static const char *const snaps[] = { "15", "30", "45" };
    vrui_segmented(6, vrui_row(36), snaps, 3, &P.snap_choice);

    P.pull_choice = vrui_style()->pull;    // the setup station may change these
    P.grab_choice = vrui_style()->grab;
    vrui_label(vrui_row(24), "Pull to use (trigger / grip)");
    static const char *const pulls[] = { "Soft", "Firm", "Full" };
    if (vrui_segmented(10, vrui_row(36), pulls, 3, &P.pull_choice)) vrui_style()->pull = (SfxrPull)P.pull_choice;

    vrui_label(vrui_row(24), "Grab things within reach by");
    static const char *const grabs[] = { "Grip", "Closing hand", "Grip/trigger" };
    if (vrui_segmented(11, vrui_row(36), grabs, 3, &P.grab_choice)) vrui_style()->grab = (VruiGrabStyle)P.grab_choice;

    vrui_slider(7, vrui_row(36), "Throw", &world.throw_power, 0.5f, 3.0f);
    vrui_slider(8, vrui_row(36), "Block size", &world.block_size, 0.5f, 2.0f);

    vrui_label(vrui_row(24), "Spawn color (drag or stick to scroll)");
    vrui_list(9, vrui_row(118), SPAWN_COLOR_NAMES, 6, &world.color_index, &P.color_scroll);

    vrui_label(vrui_row(24), TextFormat("Sky (lever): %.0f%%", world.sky * 100));
    vrui_progress(vrui_row(14), world.sky, vrui_style()->accent);
    vrui_panel_end();

    loco->smooth_turn = P.smooth_turn;
    loco->fall = P.real_falls ? VRUI_FALL_DROP : VRUI_FALL_BLINK;
    loco->snap_angle_deg = 15.0f * (float)(P.snap_choice + 1);
}

// Floats above the back of the hand you use less (the setup station learns
// which), tilted toward your face.
void panels_wrist(void)
{
    SfxrHandId off = onboarding_prefs()->valid && onboarding_prefs()->dominant == SFXR_LEFT ? SFXR_RIGHT : SFXR_LEFT;
    const SfxrHand *hand = sfxr_hand(off);
    if (!hand->active) return;
    SfxrPose offs = { { 0, 0.07f, 0.06f }, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -60.0f * DEG2RAD) };
    SfxrPose p = sfxr_pose_mul(hand->grip, offs);
    if (!vrui_panel_begin(VRUI_ID2(G_WRIST, 0), &p, 0.18f, 0.12f, NULL)) return;
    vrui_layout_begin(vrui_panel_content(), 2);
    // sfxr_dt(), not GetFPS(): keeps replays (regression tests) deterministic
    vrui_label(vrui_row(22), TextFormat("%.0f fps", sfxr_dt() > 0 ? 1.0f / sfxr_dt() : 0.0f));
    vrui_label(vrui_row(22), TextFormat("blocks %d", world.spawned));
    vrui_label(vrui_row(22), TextFormat("L %s  R %s", sfxr_hand_shape_name(sfxr_hand(SFXR_LEFT)->shape),
                                        sfxr_hand_shape_name(sfxr_hand(SFXR_RIGHT)->shape)));
    const SfxrHand *r = sfxr_hand(SFXR_RIGHT);
    const char *lvl = r->trigger_at[SFXR_PULL_FULL].down ? "full" : r->trigger_at[SFXR_PULL_FIRM].down ? "firm"
                    : r->trigger_at[SFXR_PULL_SOFT].down ? "soft" : "-";
    vrui_label(vrui_row(22), TextFormat("R trig %.2f %s", r->trigger, lvl));
    vrui_panel_end();
}
