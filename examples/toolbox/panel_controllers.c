// panel_controllers.c - the Controllers panel, to your left: every input the
// Steam Frame controllers give you, live, exactly as sfxr_hand() reports it,
// plus a haptics tester.
//
// While you look at it, it holds both controllers (VRUI_CAPTURE_LOOK), so you
// can try every button and the sticks without teleporting or turning.

#include "toolbox.h"

static struct {
    float amp, freq, dur;
    bool show_gaze;
    SfxrPose pose;
    bool placed;
} K = { .amp = 0.5f, .dur = 0.1f, .show_gaze = true };

// Touch ring (a finger resting on it) + press dot.
static void touch_press(float x, float y, SfxrButton b)
{
    const VruiStyle *st = vrui_style();
    DrawCircleLines((int)x, (int)y, 7, b.touched ? st->accent : st->text_dim);
    if (b.down) DrawCircle((int)x, (int)y, 5, st->accent);
}

// Analog bar with the pull-level thresholds marked.
static void pull_bar(Rectangle r, float v, const SfxrButton levels[SFXR_PULL_COUNT])
{
    const VruiStyle *st = vrui_style();
    DrawRectangleRec(r, st->widget);
    DrawRectangle((int)r.x, (int)r.y, (int)(r.width * Clamp(v, 0, 1)), (int)r.height, st->accent);
    static const float marks[SFXR_PULL_COUNT] = { 0.25f, 0.55f, 0.97f };   // the defaults (sfxr.h)
    for (int l = 0; l < SFXR_PULL_COUNT; l++) {
        int x = (int)(r.x + r.width * marks[l]);
        DrawRectangle(x - 1, (int)r.y - 3, 2, (int)r.height + 6, levels[l].down ? RAYWHITE : st->text_dim);
    }
}

static void hand_column(Rectangle col, SfxrHandId id)
{
    const VruiStyle *st = vrui_style();
    const SfxrHand *h = sfxr_hand(id);
    Font f = GetFontDefault();
    float y = col.y;
    const char *src = !h->active ? "not tracked" : h->source == SFXR_SOURCE_HAND ? "bare hand" : "controller";
    DrawTextEx(f, TextFormat("%s: %s, %s", id == SFXR_RIGHT ? "RIGHT" : "LEFT", src, sfxr_hand_shape_name(h->shape)),
               (Vector2){ col.x, y }, 20, 2, st->text);
    y += 30;

    // trigger and grip: how far pulled, with the soft / firm / full marks
    const char *names[2] = { "Trigger", "Grip" };
    for (int k = 0; k < 2; k++) {
        float v = k ? h->squeeze : h->trigger;
        DrawTextEx(f, names[k], (Vector2){ col.x, y }, 20, 2, st->text);
        touch_press(col.x + 100, y + 9, h->button[k ? SFXR_CTL_SQUEEZE : SFXR_CTL_TRIGGER]);
        pull_bar((Rectangle){ col.x + 118, y + 4, col.width - 170, 12 }, v, k ? h->squeeze_at : h->trigger_at);
        DrawTextEx(f, TextFormat("%.2f", v), (Vector2){ col.x + col.width - 46, y }, 20, 2, st->text_dim);
        y += 28;
    }

    // stick: position plot + click/touch; finger curl bars beside it
    Rectangle plot = { col.x, y, 64, 64 };
    DrawRectangleLinesEx(plot, 2, st->text_dim);
    DrawCircle((int)(plot.x + 32 + h->stick.x * 28), (int)(plot.y + 32 - h->stick.y * 28), 5,
               h->stick_btn.touched ? st->accent : st->text_dim);
    DrawTextEx(f, "Stick", (Vector2){ col.x + 76, y + 4 }, 20, 2, st->text);
    touch_press(col.x + 150, y + 13, h->stick_btn);
    DrawTextEx(f, TextFormat("%+.2f %+.2f", h->stick.x, h->stick.y), (Vector2){ col.x + 76, y + 32 }, 20, 2, st->text_dim);
    for (int k = 0; k < 5; k++) {   // thumb .. little
        Rectangle bar = { col.x + 200 + k * 12, y, 8, 60 };
        DrawRectangleRec(bar, st->widget);
        float c = Clamp(h->curl[k], 0, 1);
        DrawRectangle((int)bar.x, (int)(bar.y + bar.height * (1 - c)), (int)bar.width, (int)(bar.height * c + 0.5f), st->accent);
    }
    DrawTextEx(f, h->curl_from_joints ? "joints" : "touch", (Vector2){ col.x + 200, y + 62 }, 10, 1, st->text_dim);
    y += 74;

    // bare-hand gestures, measured from the joints (sfxr_hand_gestures)
    const SfxrHandGestures *g = sfxr_hand_gestures(id);
    if (g->valid) {
        DrawTextEx(f, TextFormat("pinch %.1f cm", g->pinch_dist[0] * 100), (Vector2){ col.x, y }, 20, 2, st->text);
        touch_press(col.x + 150, y + 9, g->pinch);
        DrawTextEx(f, TextFormat("%s%s", g->palm_up ? "palm up " : "", g->palm_to_head ? "palm to face" : ""),
                   (Vector2){ col.x + 170, y }, 20, 2, st->accent);
    } else {
        DrawTextEx(f, "no hand joints", (Vector2){ col.x, y }, 20, 2, st->text_dim);
    }
    y += 26;

    // every other control on this controller
    for (int c = SFXR_CTL_BUMPER; c < SFXR_CTL_COUNT; c++) {
        if (!sfxr_control_on_hand((SfxrControl)c, id)) continue;
        DrawTextEx(f, sfxr_control_name((SfxrControl)c), (Vector2){ col.x, y }, 20, 2, st->text);
        touch_press(col.x + 150, y + 9, h->button[c]);
        y += 26;
    }
}

void panel_controllers(void)
{
    if (!K.placed) {
        K.pose = station_pose(-110, 2.2f, 1.35f);
        K.placed = true;
    }
    vrui_panel_capture(VRUI_CAPTURE_LOOK);   // look at it: it owns both controllers
    if (!vrui_panel_begin(VRUI_ID2(G_CTRL, 0), &K.pose, 0.62f, 0.70f, "Controllers")) return;
    Rectangle area = vrui_panel_content();
    hand_column((Rectangle){ area.x, area.y, area.width * 0.5f - 8, 390 }, SFXR_LEFT);
    hand_column((Rectangle){ area.x + area.width * 0.5f + 8, area.y, area.width * 0.5f - 8, 390 }, SFXR_RIGHT);

    vrui_layout_begin((Rectangle){ area.x, area.y + 398, area.width, area.height - 398 }, 6);
    SfxrPose gaze;
    bool gz = sfxr_gaze(&gaze);
    Rectangle cols[2];
    vrui_row_cols(30, 2, cols);
    vrui_label(cols[0], TextFormat("Eye gaze: %s", gz ? "tracked" : "none"));
    vrui_toggle(1, cols[1], "Show gaze dot", &K.show_gaze);
    // The tester drives the motor directly (sfxr_haptic, not the vrui mixer):
    // it's for feeling the raw hardware.
    vrui_label(vrui_row(24), "Haptics tester");
    vrui_slider(2, vrui_row(30), "Strength", &K.amp, 0.0f, 1.0f);
    vrui_slider(3, vrui_row(30), "Length s", &K.dur, 0.01f, 1.0f);
    vrui_slider(4, vrui_row(30), "Freq Hz", &K.freq, 0.0f, 320.0f);
    vrui_row_cols(34, 2, cols);
    if (vrui_button(5, cols[0], "Buzz left")) sfxr_haptic(SFXR_LEFT, K.amp, K.dur, K.freq);
    if (vrui_button(6, cols[1], "Buzz right")) sfxr_haptic(SFXR_RIGHT, K.amp, K.dur, K.freq);
    vrui_panel_end();

    // a bare hand's steady ray (shoulder through knuckle), for comparing with the aim pose
    for (int i = 0; i < 2; i++) {
        const SfxrHandGestures *g = sfxr_hand_gestures((SfxrHandId)i);
        if (g->valid && sfxr_hand((SfxrHandId)i)->source == SFXR_SOURCE_HAND)
            vrui_line(g->ray.position, sfxr_pose_apply(g->ray, (Vector3){ 0, 0, -0.6f }), (Color){ 255, 255, 255, 90 });
    }

    if (gz && K.show_gaze) {   // a small cross 1.5 m out along your gaze
        Vector3 p = sfxr_pose_apply(gaze, (Vector3){ 0, 0, -1.5f });
        vrui_line(Vector3Add(p, (Vector3){ -0.01f, 0, 0 }), Vector3Add(p, (Vector3){ 0.01f, 0, 0 }), YELLOW);
        vrui_line(Vector3Add(p, (Vector3){ 0, -0.01f, 0 }), Vector3Add(p, (Vector3){ 0, 0.01f, 0 }), YELLOW);
    }
}
