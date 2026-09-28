// vrui_label.c - text and labels in the world (vrui.h section 9).
//
// Every kind queues one text command; vrui.c's draw_text3d lays the glyphs
// out on a plane, either turned toward the head (billboards) or lying on a
// fixed pose (signs). Sizes are meters.
//
// How big text must be: the Frame shows about 16 pixels per degree at its
// default render size, and text reads comfortably at about 1 degree tall,
// which is 1.75 cm for every meter it is away from the eyes. Below about
// 0.6 degrees it turns to mush. vrui_text_height() does the arithmetic.

#include "vrui_internal.h"

static VruiCmd *text_cmd(const char *text, float height_m, Color color)
{
    int len = (int)strlen(text);
    if (C.ntext + len + 1 > VRUI_TEXT_ARENA) return NULL;
    VruiCmd *c = vrui__cmd(CMD_TEXT, color);
    c->size.x = height_m;
    c->text_off = C.ntext;
    memcpy(C.text + C.ntext, text, (size_t)len + 1);
    C.ntext += len + 1;
    return c;
}

float vrui_text_height(float distance_m, float degrees)
{
    return distance_m * tanf(degrees * DEG2RAD);
}

Vector2 vrui_text_size(const char *text, float height_m)
{
    Vector2 m = MeasureTextEx(C.font, text, (float)C.font.baseSize, 1.0f);
    return Vector2Scale(m, height_m / (float)C.font.baseSize);
}

void vrui_text3d(Vector3 position, const char *text, float height_m, Color color)
{
    VruiCmd *c = text_cmd(text, height_m, color);
    if (!c) return;
    c->billboard = true;
    c->a = position;
}

void vrui_text_at(SfxrPose pose, const char *text, float height_m, Color color)
{
    VruiCmd *c = text_cmd(text, height_m, color);
    if (!c) return;
    c->pose = pose;
}

void vrui_tag(Vector3 position, const char *text, float height_m, Color color, Color plate)
{
    VruiCmd *c = text_cmd(text, height_m, color);
    if (!c) return;
    c->billboard = true;
    c->a = position;
    c->bg = plate;
}

void vrui_callout(Vector3 anchor, const char *text, float lift_m, Color color)
{
    // Keep an apparent size of about 1.1 degrees as you step back, up to
    // 5 cm tall; past 8 m it isn't drawn at all (from across the room a
    // wall of callouts is clutter, not help).
    Vector3 at = Vector3Add(anchor, (Vector3){ 0, lift_m, 0 });
    float dist = Vector3Distance(sfxr_head().position, at);
    if (dist > 8.0f) return;
    float h = Clamp(vrui_text_height(dist, 1.1f), 0.012f, 0.05f);
    Vector2 sz = vrui_text_size(text, h);
    Vector3 tag = Vector3Add(at, (Vector3){ 0, sz.y * 0.5f + h * 0.35f, 0 });
    vrui__sphere(anchor, fmaxf(h * 0.18f, 0.003f), color);
    vrui_line(anchor, at, color);
    vrui_tag(tag, text, h, color, (Color){ 20, 22, 28, 210 });
}

void vrui_sign(SfxrPose pose, float width_m, const char *title, const char *body, Color board)
{
    // Sizes follow the width, so a bigger sign has bigger letters.
    float title_h = width_m * 0.07f, body_h = width_m * 0.042f, pad = width_m * 0.05f;
    Vector2 tsz = title ? vrui_text_size(title, title_h) : (Vector2){ 0 };
    Vector2 bsz = body ? vrui_text_size(body, body_h) : (Vector2){ 0 };
    float gap = title && body ? body_h : 0.0f;
    float height = pad + tsz.y + gap + bsz.y + pad;
    const float thick = 0.02f;

    // pose is the middle of the sign's face; the board sits just behind it
    vrui_box(sfxr_pose_mul(pose, (SfxrPose){ { 0, 0, -thick * 0.5f }, QuaternionIdentity() }),
             (Vector3){ width_m, height, thick }, board);
    float top = height * 0.5f - pad;
    if (title)
        vrui_text_at(sfxr_pose_mul(pose, (SfxrPose){ { 0, top - tsz.y * 0.5f, 0.002f }, QuaternionIdentity() }),
                     title, title_h, RAYWHITE);
    if (body)
        vrui_text_at(sfxr_pose_mul(pose, (SfxrPose){ { 0, top - tsz.y - gap - bsz.y * 0.5f, 0.002f }, QuaternionIdentity() }),
                     body, body_h, (Color){ 215, 220, 230, 255 });
}
