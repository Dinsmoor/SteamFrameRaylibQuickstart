// panels.c - the Toolbox panel: world settings, pull level, grab style (the
// 2D widgets, all of them). The wrist watch moved to station_menus.c.

#include "toolbox.h"

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
    station_sign(-1.6f, "Toolbox", "world settings, pull level, grab style");
    if (!P.placed) {
        P.pose = row_pose(-1.6f, 1.35f);
        P.placed = true;
    }
    if (!vrui_panel_begin(VRUI_ID2(G_PANEL, 0), &P.pose, 0.598f, 1.1f, "Toolbox")) return;
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
