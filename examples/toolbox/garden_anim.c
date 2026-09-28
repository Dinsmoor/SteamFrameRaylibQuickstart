// garden_anim.c - named animation clips, cross-faded, and where a bone is.
//
// A character exported from Blender carries its clips inside the .glb, one
// per Action, named ("idle", "walk"). raylib 6 loads them with
// LoadModelAnimations, poses the model with UpdateModelAnimation, and blends
// two clips with UpdateModelAnimationEx; this adds playing a clip BY NAME and
// a short cross-fade when switching, so the gardener doesn't pop from
// standing to walking.
//
// anim_bone_pose() is attachment to a bone: it takes the bone's animated
// transform and places it where the model is drawn. The result is a parent
// pose like any other (docs/ATTACHING.md): the hammer rides the gardener's
// hand through his idle sway. (raylib 6 keeps each bone's current pose in
// MODEL space already -- the skinning matrix is inverse(bind) * pose -- so
// there's no walking up the parent chain; doing so applies the parents twice.)

#include "garden.h"

#include <string.h>
#include <strings.h>

#define ANIM_FPS  60.0f   // raylib bakes glTF clips to 60 frames a second
#define ANIM_FADE 0.2f    // cross-fade when switching clips (s)

bool anim_load(AnimModel *a, const char *path)
{
    memset(a, 0, sizeof *a);
    a->cur = a->prev = -1;
    a->blend = 1;
    a->model = LoadModel(path);
    if (a->model.meshCount == 0) { UnloadModel(a->model); a->model = (Model){ 0 }; return false; }
    a->clips = LoadModelAnimations(path, &a->clip_count);
    anim_play(a, "idle");
    return true;
}

void anim_unload(AnimModel *a)
{
    if (a->clips) UnloadModelAnimations(a->clips, a->clip_count);
    if (a->model.meshCount) UnloadModel(a->model);
    memset(a, 0, sizeof *a);
}

void anim_play(AnimModel *a, const char *name)
{
    int idx = -1;
    for (int i = 0; i < a->clip_count; i++)
        if (strcasecmp(a->clips[i].name, name) == 0) idx = i;
    if (idx < 0 || (idx == a->cur && a->clips)) return;   // no such clip, or already playing
    a->prev = a->cur;
    a->prev_frame = a->cur_frame;
    a->cur = idx;
    a->cur_frame = 0;
    a->blend = a->prev >= 0 ? 0.0f : 1.0f;
}

void anim_update(AnimModel *a, float dt)
{
    if (!a->clips || a->cur < 0 || a->cur >= a->clip_count) return;   // no clips (or not loaded): nothing to play
    ModelAnimation cur = a->clips[a->cur];
    a->cur_frame = fmodf(a->cur_frame + dt * ANIM_FPS, (float)cur.keyframeCount);
    if (a->blend < 1) a->blend = fminf(1, a->blend + dt / ANIM_FADE);
    if (a->blend >= 1 || a->prev < 0) {
        UpdateModelAnimation(a->model, cur, a->cur_frame);
        a->prev = -1;
    } else {
        UpdateModelAnimationEx(a->model, a->clips[a->prev], a->prev_frame, cur, a->cur_frame, a->blend);
    }
}

void anim_draw(const AnimModel *a, SfxrPose pose, float scale)
{
    sfxr_push_pose(pose);
    DrawModel(a->model, (Vector3){ 0 }, scale, WHITE);
    sfxr_pop_pose();
}

static Matrix transform_matrix(Transform t)
{
    return MatrixMultiply(MatrixMultiply(MatrixScale(t.scale.x, t.scale.y, t.scale.z), QuaternionToMatrix(t.rotation)),
                          MatrixTranslate(t.translation.x, t.translation.y, t.translation.z));
}

SfxrPose anim_bone_pose(const AnimModel *a, const char *bone, SfxrPose pose, float scale)
{
    const ModelSkeleton *sk = &a->model.skeleton;
    int idx = -1;
    for (int i = 0; sk->bones && i < sk->boneCount; i++)
        if (strcmp(sk->bones[i].name, bone) == 0) idx = i;
    if (idx < 0 || !a->model.currentPose) return pose;   // no such bone: the model's origin

    Matrix m = transform_matrix(a->model.currentPose[idx]);   // the bone, in the model
    // as a pose (the model's scale applies to where the bone is, not to the
    // pose itself), then placed where the model is drawn
    Vector3 x = { m.m0, m.m1, m.m2 }, y = { m.m4, m.m5, m.m6 }, z = { m.m8, m.m9, m.m10 };
    x = Vector3Normalize(x); y = Vector3Normalize(y); z = Vector3Normalize(z);
    Matrix rot = { x.x, y.x, z.x, 0, x.y, y.y, z.y, 0, x.z, y.z, z.z, 0, 0, 0, 0, 1 };
    SfxrPose local = { Vector3Scale((Vector3){ m.m12, m.m13, m.m14 }, scale), QuaternionFromMatrix(rot) };
    return sfxr_pose_mul(pose, local);
}
