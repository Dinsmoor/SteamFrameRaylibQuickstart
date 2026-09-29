// viewer - look at a model from the asset pipeline (docs/ASSETS.md) in the
// desktop simulator or the headset, the way the game will draw it.
//
//   make view MODEL=examples/toolbox/resources/models/bug.glb        run it
//   make view-shot MODEL=...                                          headless PNG -> shots/viewer-sim.png
//   SFQ_MODEL=... build/host-debug/bin/viewer                         (what those do)
//
// The model stands on a turntable 1.5 m in front of you, over a 1 m grid,
// with its animations played in turn and the current one named over it.
// Trigger: the next animation. Grip: stop or start the turntable. Stick up
// or down: bigger or smaller. For screenshots: SFQ_ANIM=<n> starts on
// animation n, SFQ_TURN=0 holds the turntable still, SFQ_YAW=<deg> turns it
// (0: facing away from you, 180: toward you, the default).

#include "sfxr.h"
#include "vrui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *path = getenv("SFQ_MODEL");
    if (!path || !*path) { fprintf(stderr, "viewer: set SFQ_MODEL=<model.glb> (or make view MODEL=...)\n"); return 2; }

    SfxrConfig cfg = sfxr_default_config();
    cfg.app_name = "sfxr viewer";
    if (!sfxr_init(&cfg)) return 1;
    vrui_init();

    Model model = LoadModel(path);
    int nanim = 0;
    ModelAnimation *anims = LoadModelAnimations(path, &nanim);
    int tris = 0;
    for (int i = 0; i < model.meshCount; i++) tris += model.meshes[i].triangleCount;
    BoundingBox bb = GetModelBoundingBox(model);
    Vector3 size = Vector3Subtract(bb.max, bb.min);
    TraceLog(LOG_INFO, "VIEWER: %s: %d meshes, %d materials, %d triangles, %d bones, %d animations, %.2f x %.2f x %.2f m",
             path, model.meshCount, model.materialCount, tris, model.skeleton.boneCount, nanim, size.x, size.y, size.z);
    for (int i = 0; i < nanim; i++) TraceLog(LOG_INFO, "VIEWER: animation %d: \"%s\", %d keyframes", i, anims[i].name, anims[i].keyframeCount);

    // A little light, so shapes read (raylib's default material is unlit): one
    // sun from up-front-left, some ambient. Uses raylib's mvp, so it works in
    // sfxr's stereo pass; CPU-skinned animation just updates the vertex data.
    Shader lit = LoadShaderFromMemory(
        "#version 330\n"
        "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor;\n"
        "uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matNormal;\n"
        "out vec2 fragTexCoord; out vec4 fragColor; out vec3 fragNormal;\n"
        "void main() { fragTexCoord = vertexTexCoord; fragColor = vertexColor;\n"
        "  fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));\n"
        "  gl_Position = mvp * vec4(vertexPosition, 1.0); }\n",
        "#version 330\n"
        "in vec2 fragTexCoord; in vec4 fragColor; in vec3 fragNormal;\n"
        "uniform sampler2D texture0; uniform vec4 colDiffuse; out vec4 finalColor;\n"
        "void main() { vec4 base = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
        "  float sun = max(dot(normalize(fragNormal), normalize(vec3(-0.4, 0.8, 0.5))), 0.0);\n"
        "  finalColor = vec4(base.rgb * (0.45 + 0.6 * sun), base.a); }\n");
    for (int i = 0; i < model.materialCount; i++) model.materials[i].shader = lit;

    int cur = getenv("SFQ_ANIM") ? atoi(getenv("SFQ_ANIM")) : 0;
    if (cur < 0 || cur >= nanim) cur = 0;
    bool turning = !(getenv("SFQ_TURN") && strcmp(getenv("SFQ_TURN"), "0") == 0);
    float frame = 0, yaw = getenv("SFQ_YAW") ? (float)atof(getenv("SFQ_YAW")) : 180, scale = 1;   // 180: its front toward you
    const Vector3 at = { 0, 0, -2.0f };

    while (sfxr_frame_begin()) {
        vrui_begin();
        float dt = sfxr_dt();
        for (int h = 0; h < SFXR_HAND_COUNT; h++) {
            const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
            if (!hand->active) continue;
            if (hand->trigger_btn.pressed && nanim) { cur = (cur + 1) % nanim; frame = 0; }
            if (hand->squeeze_btn.pressed) turning = !turning;
            if (fabsf(hand->stick.y) > 0.3f) scale *= 1 + hand->stick.y * dt;
        }
        if (turning) yaw += dt * 30;
        if (nanim) {
            frame += dt * 60;   // raylib resamples glTF animations to 60 keyframes a second
            if (frame >= (float)anims[cur].keyframeCount) frame = fmodf(frame, (float)anims[cur].keyframeCount);
            UpdateModelAnimation(model, anims[cur], frame);
        }
        vrui_tag((Vector3){ at.x, bb.max.y * scale + 0.25f, at.z },
                 TextFormat("%s   %d tris   %s%s", GetFileName(path), tris,
                            nanim ? TextFormat("anim %d/%d: %s", cur + 1, nanim, anims[cur].name) : "no animations",
                            turning ? "" : "   (still)"),
                 0.03f, RAYWHITE, (Color){ 20, 20, 30, 220 });
        vrui_end();

        if (sfxr_draw_begin((Color){ 36, 40, 52, 255 })) {
            sfxr_draw_floor_grid(6, (Color){ 90, 90, 100, 255 }, (Color){ 55, 55, 65, 255 });
            DrawModelEx(model, at, (Vector3){ 0, 1, 0 }, yaw, (Vector3){ scale, scale, scale }, WHITE);
            // a 1 m bar beside it, for scale
            DrawCube((Vector3){ at.x + 1.4f, 0.5f, at.z }, 0.02f, 1.0f, 0.02f, (Color){ 220, 200, 80, 255 });
            sfxr_draw_controllers();
            vrui_draw();
            sfxr_draw_end();
        }
        DrawText(TextFormat("%s | %s | %d fps | trigger: next animation, grip: turntable, stick: size",
                            sfxr_backend_name(), sfxr_system_name(), GetFPS()),
                 10, GetScreenHeight() - 20, 10, RAYWHITE);
        sfxr_frame_end();
    }

    if (anims) UnloadModelAnimations(anims, nanim);
    UnloadShader(lit);
    UnloadModel(model);
    vrui_shutdown();
    sfxr_shutdown();
    return 0;
}
