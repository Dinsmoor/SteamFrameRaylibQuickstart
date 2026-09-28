// hello - the smallest useful sfxr program.
//
// A floor grid, a few cubes, your controllers, and one interaction: pull the
// trigger while pointing at a cube to change its color (with a haptic tick).
// Runs in the headset via OpenXR, or in the desktop simulator without one.

#include "sfxr.h"

#include <stdio.h>

typedef struct { Vector3 pos; float size; Color color; } Box;

static bool ray_hits_box(Ray ray, Box b)
{
    float h = b.size * 0.5f;
    BoundingBox bb = { { b.pos.x - h, b.pos.y - h, b.pos.z - h }, { b.pos.x + h, b.pos.y + h, b.pos.z + h } };
    return GetRayCollisionBox(ray, bb).hit;
}

int main(void)
{
    SfxrConfig cfg = sfxr_default_config();
    cfg.app_name = "sfxr hello";
    if (!sfxr_init(&cfg)) return 1;

    Box boxes[] = {
        { { -0.6f, 1.0f, -1.5f }, 0.3f, RED },
        { {  0.0f, 1.2f, -1.8f }, 0.3f, GREEN },
        { {  0.6f, 1.0f, -1.5f }, 0.3f, BLUE },
    };
    const int nboxes = (int)(sizeof boxes / sizeof boxes[0]);
    const Color palette[] = { RED, ORANGE, GOLD, GREEN, SKYBLUE, BLUE, PURPLE, PINK };
    int next_color = 0;

    while (sfxr_frame_begin()) {
        // --- update
        for (int h = 0; h < SFXR_HAND_COUNT; h++) {
            const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
            if (!hand->active || !hand->trigger_btn.pressed) continue;
            Ray ray = { hand->aim.position, sfxr_pose_forward(hand->aim) };
            for (int i = 0; i < nboxes; i++) {
                if (ray_hits_box(ray, boxes[i])) {
                    boxes[i].color = palette[next_color++ % 8];
                    sfxr_haptic((SfxrHandId)h, 0.5f, 0.05f, 0);
                }
            }
        }

        // --- draw (once; sfxr renders both eyes)
        if (sfxr_draw_begin((Color){ 30, 34, 44, 255 })) {
            sfxr_draw_floor_grid(10, (Color){ 90, 90, 100, 255 }, (Color){ 55, 55, 65, 255 });
            for (int i = 0; i < nboxes; i++) {
                DrawCube(boxes[i].pos, boxes[i].size, boxes[i].size, boxes[i].size, boxes[i].color);
                DrawCubeWires(boxes[i].pos, boxes[i].size, boxes[i].size, boxes[i].size, BLACK);
            }
            sfxr_draw_controllers();
            sfxr_draw_end();
        }

        // --- mirror-window HUD (never reaches the headset)
        DrawText(TextFormat("%s | %s | %d fps", sfxr_backend_name(), sfxr_system_name(), GetFPS()),
                 10, GetScreenHeight() - 20, 10, RAYWHITE);
        sfxr_frame_end();
    }

    sfxr_shutdown();
    return 0;
}
