// vrui.c - context, targeting arbitration, draw queue, lasers, text.

#include "vrui_internal.h"

#include <stdio.h>

VruiCtx vrui_ctx;

// ---------------------------------------------------------------------------
// Style / lifecycle
// ---------------------------------------------------------------------------

static VruiStyle default_style(void)
{
    VruiStyle s;
    s.px_per_m      = 1000.0f;
    s.font_size     = 20;
    s.title_height  = 36;
    s.panel_bg      = (Color){  28,  31,  38, 255 };
    s.panel_border  = (Color){  70,  78,  96, 255 };
    s.title_bg      = (Color){  44,  50,  64, 255 };
    s.title_text    = (Color){ 220, 226, 240, 255 };
    s.text          = (Color){ 225, 228, 235, 255 };
    s.text_dim      = (Color){ 140, 146, 160, 255 };
    s.widget        = (Color){  52,  58,  72, 255 };
    s.widget_hot    = (Color){  70,  80, 100, 255 };
    s.widget_active = (Color){  90, 104, 132, 255 };
    s.accent        = (Color){  80, 170, 255, 255 };
    s.laser         = (Color){ 255, 255, 255, 110 };
    s.laser_hit     = (Color){ 120, 200, 255, 255 };
    s.laser_far     = (Color){ 255, 180, 70, 255 };
    s.cursor        = (Color){ 255, 255, 255, 255 };
    s.prop_hover    = (Color){ 255, 255, 180, 255 };
    s.haptic_hover  = 0.15f;
    s.haptic_click  = 0.5f;
    s.haptic_scale  = 1.0f;
    s.show_hints    = true;
    s.pull          = SFXR_PULL_FIRM;
    return s;
}

void vrui_init(void)
{
    memset(&C, 0, sizeof(C));
    C.style = default_style();
    C.font = GetFontDefault();
    C.show_controllers = true;
    C.controller_models = true;
}

void vrui_shutdown(void)
{
    for (int i = 0; i < VRUI_MAX_PANELS; i++)
        if (C.panels[i].id) UnloadRenderTexture(C.panels[i].rt);
    memset(&C, 0, sizeof(C));
}

VruiStyle *vrui_style(void)              { return &C.style; }
void vrui_set_font(Font font)            { C.font = font; C.custom_font = true; }
void vrui_show_controllers(bool on)      { C.show_controllers = on; }
void vrui_controller_models(bool on)     { C.controller_models = on; }
void vrui_hand_joints_always(bool on)    { C.joints_always = on; }

bool vrui_hand_busy(SfxrHandId h)        { return C.ray_active[h] || C.grab_active[h]; }
bool vrui_hand_hovering(SfxrHandId h)    { return C.ray_hot[h] || C.grab_hot[h]; }

void vrui_push_pull(SfxrPull level)
{
    if (C.npull >= VRUI_PULL_STACK) { TraceLog(LOG_WARNING, "VRUI: vrui_push_pull nested too deep"); return; }
    C.pull_stack[C.npull++] = level;
}

void vrui_pop_pull(void)
{
    if (C.npull > 0) C.npull--;
    else TraceLog(LOG_WARNING, "VRUI: vrui_pop_pull without a push");
}

SfxrPull vrui_current_pull(void)
{
    SfxrPull l = C.npull > 0 ? C.pull_stack[C.npull - 1] : C.style.pull;
    return (unsigned)l < SFXR_PULL_COUNT ? l : SFXR_PULL_FIRM;
}

bool vrui__grab_pressed(int h)
{
    VruiGrabStyle g = SFXR_BREAK(vrui_grab_style_ignored) ? VRUI_GRAB_GRIP : C.style.grab;
    if (g == VRUI_GRAB_CLOSE) return C.close_pressed[h];
    if (g == VRUI_GRAB_GRIP_OR_TRIGGER) return vrui__squeeze(h)->pressed || vrui__trigger(h)->pressed;
    return vrui__squeeze(h)->pressed;
}

bool vrui__grab_down(int h)
{
    VruiGrabStyle g = SFXR_BREAK(vrui_grab_style_ignored) ? VRUI_GRAB_GRIP : C.style.grab;
    if (g == VRUI_GRAB_CLOSE) return C.close_down[h];
    if (g == VRUI_GRAB_GRIP_OR_TRIGGER) return vrui__squeeze(h)->down || vrui__trigger(h)->down;
    return vrui__squeeze(h)->down;
}

const SfxrButton *vrui__trigger(int hand) { return &sfxr_hand((SfxrHandId)hand)->trigger_at[vrui_current_pull()]; }
const SfxrButton *vrui__squeeze(int hand) { return &sfxr_hand((SfxrHandId)hand)->squeeze_at[vrui_current_pull()]; }

SfxrPose vrui_facing(Vector3 position, Vector3 viewer)
{
    Vector3 d = Vector3Subtract(viewer, position);
    SfxrPose p;
    p.position = position;
    p.orientation = QuaternionFromAxisAngle((Vector3){0, 1, 0}, atan2f(d.x, d.z));
    return p;
}

SfxrPose vrui_in_front_of_head(float distance, float drop)
{
    SfxrPose h = sfxr_head();
    Vector3 f = sfxr_pose_forward(h);
    f.y = 0;
    if (Vector3Length(f) < 1e-3f) f = (Vector3){0, 0, -1};
    f = Vector3Normalize(f);
    Vector3 pos = Vector3Add(h.position, Vector3Scale(f, distance));
    pos.y -= drop;
    return vrui_facing(pos, h.position);
}

// ---------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------

void vrui__name(VruiId id, const char *label)
{
    if (!label || !*label) return;
    VruiItem *it = vrui__item(id);
    if (strncmp(it->name, label, sizeof it->name - 1)) snprintf(it->name, sizeof it->name, "%s", label);
}

const char *vrui__who(VruiId id)
{
    static char buf[4][24];
    static int k;
    VruiItem *it = vrui__item(id);
    if (it->name[0] && !SFXR_BREAK(vrui_events_unnamed)) return it->name;
    char *b = buf[k++ & 3];
    snprintf(b, sizeof buf[0], "#%08x", (unsigned)id);
    return b;
}

VruiItem *vrui__item(VruiId id)
{
    uint32_t hsh = (id * 2654435761u) % VRUI_MAX_ITEMS;
    for (int n = 0; n < VRUI_MAX_ITEMS; n++) {
        VruiItem *it = &C.items[(hsh + n) % VRUI_MAX_ITEMS];
        if (it->used && it->id == id) { it->last_frame = C.frame; return it; }
        if (!it->used) {
            memset(it, 0, sizeof(*it));
            it->used = true;
            it->id = id;
            it->rel = sfxr_pose_identity();
            it->hold_hand = -1;
            it->last_frame = C.frame;
            return it;
        }
    }
    static VruiItem overflow;
    TraceLog(LOG_WARNING, "VRUI: item table full (raise VRUI_MAX_ITEMS)");
    memset(&overflow, 0, sizeof(overflow));
    return &overflow;
}

VruiItem *vrui__widget_item(VruiId owner, VruiId local)
{
    // mix both ids into one (splitmix32 finalizer) so panel-local ids from
    // different panels land on different items
    uint32_t x = owner * 0x9E3779B9u ^ (local + 0x7F4A7C15u);
    x ^= x >> 16; x *= 0x85EBCA6Bu; x ^= x >> 13; x *= 0xC2B2AE35u; x ^= x >> 16;
    return vrui__item(x ? x : 1);
}

// ---------------------------------------------------------------------------
// Arbitration
// ---------------------------------------------------------------------------

void vrui__ray_offer(int h, VruiId id, float dist)
{
    if (dist < 0 || dist > VRUI_RAY_LENGTH) return;
    if (C.ray_active[h] && C.ray_active[h] != id) return;
    if (C.grab_active[h]) return;
    if (!C.ray_offer_id[h] || dist < C.ray_offer_dist[h]) {
        C.ray_offer_id[h] = id;
        C.ray_offer_dist[h] = dist;
    }
}

void vrui__grab_offer(int h, VruiId id, float score)
{
    if (C.grab_active[h] && C.grab_active[h] != id) return;
    if (C.ray_active[h]) return;
    if (!C.grab_offer_id[h] || score < C.grab_offer_score[h]) {
        C.grab_offer_id[h] = id;
        C.grab_offer_score[h] = score;
    }
}

bool vrui__ray_hot(int h, VruiId id)  { return id && C.ray_hot[h] == id; }
bool vrui__grab_hot(int h, VruiId id) { return id && C.grab_hot[h] == id; }
bool vrui__hand_free(int h)           { return !C.ray_active[h] && !C.grab_active[h]; }

void vrui__hover_tick(int h, VruiId id)
{
    if (C.last_hover_tick[h] == id) return;
    C.last_hover_tick[h] = id;
    if (id && C.style.haptic_hover > 0) vrui_haptic_pulse((SfxrHandId)h, C.style.haptic_hover, 0.01f, 0);
}

void vrui__click_pulse(int h)
{
    if (C.style.haptic_click > 0) vrui_haptic_pulse((SfxrHandId)h, C.style.haptic_click, 0.03f, 0);
}

Ray vrui__hand_ray(int h)
{
    const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
    Ray r = { hand->aim.position, sfxr_pose_forward(hand->aim) };
    return r;
}

Vector3 vrui__tip(int h)
{
    // The runtime's poke pose: the controller tip, or the index fingertip
    // with bare hands (sfxr derives it from the aim pose when unavailable).
    return sfxr_hand((SfxrHandId)h)->poke.position;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void vrui_claim_input(SfxrHandId h) { C.claim_cur[h == SFXR_RIGHT] = true; }
bool vrui_input_claimed(SfxrHandId h) { int i = h == SFXR_RIGHT; return C.claim_cur[i] || C.claim_prev[i]; }
void vrui_panel_capture(VruiCapture mode) { C.next_capture = mode; }

void vrui__hint(VruiId id, const char *how)
{
    if (!C.style.show_hints || C.nhints >= (int)(sizeof C.hints / sizeof C.hints[0])) return;
    C.hints[C.nhints].id = id;
    snprintf(C.hints[C.nhints].text, sizeof C.hints[0].text, "%s", how);
    C.nhints++;
}

const char *vrui__grab_words(void)
{
    switch (C.style.grab) {
    case VRUI_GRAB_CLOSE:           return "close your hand on it";
    case VRUI_GRAB_GRIP_OR_TRIGGER: return "grab it (grip or trigger)";
    default:                        return "grab it (grip)";
    }
}

const char *vrui__pull_suffix(void)
{
    SfxrPull p = vrui_current_pull();
    return p == SFXR_PULL_SOFT ? " (light pull)" : p == SFXR_PULL_FULL ? " (full pull)" : "";
}

static const char *hint_for(VruiId id)
{
    for (int i = 0; i < C.nhints; i++) if (C.hints[i].id == id) return C.hints[i].text;
    return NULL;
}

void vrui_begin(void)
{
    C.frame++;
    C.nhints = 0;
    for (int h = 0; h < 2; h++) {
        C.claim_prev[h] = C.claim_cur[h];
        C.claim_cur[h] = false;
        // VRUI_GRAB_CLOSE: the grip fingers' curl, with hysteresis; arms only
        // once the hand has been open
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        float g = (hand->curl[SFXR_FINGER_MIDDLE] + hand->curl[SFXR_FINGER_RING] + hand->curl[SFXR_FINGER_LITTLE]) / 3.0f;
        C.close_pressed[h] = false;
        if (!hand->active) { C.close_armed[h] = C.close_down[h] = false; continue; }
        if (g < 0.35f) C.close_armed[h] = true;
        if (!C.close_down[h] && C.close_armed[h] && g > 0.6f) { C.close_down[h] = C.close_pressed[h] = true; C.close_armed[h] = false; }
        else if (C.close_down[h] && g < 0.4f) C.close_down[h] = false;
    }
    C.ncmds = 0;
    C.ntext = 0;
    C.fade = 0.0f;
    C.tint = (Color){ 0 };
    C.on_top = 0;
    vrui__body_update();
    for (int h = 0; h < 2; h++) {
        C.ray_offer_id[h] = VRUI_ID_NONE;
        C.ray_offer_dist[h] = 0;
        C.grab_offer_id[h] = VRUI_ID_NONE;
        C.grab_offer_score[h] = 0;
        if (!sfxr_hand((SfxrHandId)h)->active) {
            C.ray_hot[h] = C.ray_active[h] = VRUI_ID_NONE;
            C.grab_hot[h] = C.grab_active[h] = VRUI_ID_NONE;
        }
    }
}

void vrui_end(void)
{
    if (C.npull) { TraceLog(LOG_WARNING, "VRUI: %d vrui_push_pull without vrui_pop_pull this frame", C.npull); C.npull = 0; }
    if (C.on_top) { TraceLog(LOG_WARNING, "VRUI: vrui_on_top_begin without vrui_on_top_end this frame"); C.on_top = 0; }
    for (int h = 0; h < 2; h++) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        // Release captures once the hand has let go completely. Widgets
        // normally release at their own pull level; this only catches widgets
        // that vanished, so it uses the lightest level.
        const SfxrButton *trig = &hand->trigger_at[SFXR_PULL_SOFT], *sq = &hand->squeeze_at[SFXR_PULL_SOFT];
        if (C.ray_active[h] && !trig->down) C.ray_active[h] = VRUI_ID_NONE;
        if (C.grab_active[h] && !sq->down && !trig->down) C.grab_active[h] = VRUI_ID_NONE;

        C.grab_hot[h] = C.grab_active[h] ? C.grab_active[h] : C.grab_offer_id[h];
        if (C.ray_active[h]) {
            C.ray_hot[h] = C.ray_active[h];
            if (C.ray_offer_id[h] == C.ray_active[h]) C.ray_hot_dist[h] = C.ray_offer_dist[h];
        } else if (C.grab_hot[h]) {
            C.ray_hot[h] = VRUI_ID_NONE;   // touching something beats pointing
        } else {
            C.ray_hot[h] = C.ray_offer_id[h];
            C.ray_hot_dist[h] = C.ray_offer_dist[h];
        }
        vrui__hover_tick(h, C.grab_hot[h] ? C.grab_hot[h] : C.ray_hot[h]);

        Ray r = vrui__hand_ray(h);
        C.laser_on[h] = hand->active && !C.grab_hot[h] && !C.grab_active[h];
        C.laser_from[h] = r.position;
        C.laser_hit[h] = C.ray_hot[h] != VRUI_ID_NONE;
        // 2D and 3D look different: blue on panels, amber on physical things
        C.laser_on_panel[h] = false;
        for (int i = 0; i < VRUI_MAX_PANELS && C.laser_hit[h]; i++)
            if (C.panels[i].id && C.panels[i].id == C.ray_hot[h]) C.laser_on_panel[h] = true;
        float len = C.laser_hit[h] ? C.ray_hot_dist[h] : 0.6f;
        C.laser_to[h] = Vector3Add(r.position, Vector3Scale(r.direction, len));

        // how to use what this hand is on: above the hand when touching, above
        // the laser spot when pointing
        const char *how = C.grab_hot[h] ? hint_for(C.grab_hot[h]) : C.ray_hot[h] ? hint_for(C.ray_hot[h]) : NULL;
        if (how && !C.grab_active[h] && !C.ray_active[h]) {
            Vector3 at = C.grab_hot[h] ? Vector3Add(hand->grip.position, (Vector3){ 0, 0.09f, 0 })
                                       : Vector3Add(C.laser_to[h], (Vector3){ 0, 0.05f, 0 });
            vrui_tag(at, how, 0.014f, C.style.text, (Color){ 20, 22, 28, 200 });
        }
    }
    vrui__haptics_flush();
}

// ---------------------------------------------------------------------------
// Draw queue
// ---------------------------------------------------------------------------

VruiCmd *vrui__cmd(VruiCmdKind kind, Color color)
{
    static VruiCmd dummy;
    if (C.ncmds >= VRUI_MAX_CMDS) return &dummy;
    VruiCmd *c = &C.cmds[C.ncmds++];
    memset(c, 0, sizeof(*c));
    c->kind = kind;
    c->color = color;
    c->on_top = C.on_top > 0;
    return c;
}

void vrui_box(SfxrPose pose, Vector3 size, Color color)
{
    VruiCmd *c = vrui__cmd(CMD_BOX, color);
    c->pose = pose;
    c->size = size;
}

void vrui_line(Vector3 a, Vector3 b, Color color)
{
    VruiCmd *c = vrui__cmd(CMD_LINE, color);
    c->a = a;
    c->b = b;
}

void vrui__sphere(Vector3 p, float r, Color color)
{
    VruiCmd *c = vrui__cmd(CMD_SPHERE, color);
    c->a = p;
    c->size.x = r;
}

void vrui__cylinder(Vector3 a, Vector3 b, float r, Color color)
{
    VruiCmd *c = vrui__cmd(CMD_CYLINDER, color);
    c->a = a;
    c->b = b;
    c->size = (Vector3){ r, r, 0 };
}

void vrui__triangle(Vector3 a, Vector3 b, Vector3 c3, Color color)
{
    VruiCmd *c = vrui__cmd(CMD_TRI, color);
    c->a = a;
    c->b = b;
    c->size = c3;
}

void vrui__ring(SfxrPose pose, float r, Color color)
{
    VruiCmd *c = vrui__cmd(CMD_RING, color);
    c->pose = pose;
    c->size.x = r;
}

void vrui_fade(float alpha) { if (alpha > C.fade) C.fade = alpha; }
float vrui_faded(void) { return Clamp(C.fade, 0, 1); }
void vrui_tint(Color color, float alpha)
{
    if (alpha <= C.tint.a / 255.0f) return;
    C.tint = color;
    C.tint.a = (unsigned char)(Clamp(alpha, 0, 1) * 255);
}

void vrui_on_top_begin(void) { C.on_top++; }
void vrui_on_top_end(void)
{
    if (C.on_top > 0) C.on_top--;
    else TraceLog(LOG_WARNING, "VRUI: vrui_on_top_end without a begin");
}

// ---------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------

float vrui__ray_box(Ray ray, SfxrPose pose, Vector3 half)
{
    Ray local;
    local.position = sfxr_pose_apply_inv(pose, ray.position);
    local.direction = Vector3RotateByQuaternion(ray.direction, QuaternionInvert(pose.orientation));
    BoundingBox bb = { Vector3Negate(half), half };
    RayCollision rc = GetRayCollisionBox(local, bb);
    return rc.hit ? rc.distance : -1.0f;
}

float vrui__point_box_dist(Vector3 p, SfxrPose pose, Vector3 half)
{
    Vector3 l = sfxr_pose_apply_inv(pose, p);
    Vector3 q = { fmaxf(fabsf(l.x) - half.x, 0), fmaxf(fabsf(l.y) - half.y, 0), fmaxf(fabsf(l.z) - half.z, 0) };
    return Vector3Length(q);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

static void draw_panel_quad(const VruiCmd *c)
{
    float hw = c->size.x * 0.5f, hh = c->size.y * 0.5f;
    Vector3 tl = sfxr_pose_apply(c->pose, (Vector3){ -hw,  hh, 0 });
    Vector3 bl = sfxr_pose_apply(c->pose, (Vector3){ -hw, -hh, 0 });
    Vector3 br = sfxr_pose_apply(c->pose, (Vector3){  hw, -hh, 0 });
    Vector3 tr = sfxr_pose_apply(c->pose, (Vector3){  hw,  hh, 0 });

    rlSetTexture(c->tex);
    rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        // Render-texture content is bottom-up: panel top = v 1.
        rlTexCoord2f(0, 1); rlVertex3f(tl.x, tl.y, tl.z);
        rlTexCoord2f(0, 0); rlVertex3f(bl.x, bl.y, bl.z);
        rlTexCoord2f(1, 0); rlVertex3f(br.x, br.y, br.z);
        rlTexCoord2f(1, 1); rlVertex3f(tr.x, tr.y, tr.z);
    rlEnd();
    rlSetTexture(0);

    // Back side: plain slab so the panel doesn't vanish from behind.
    Color b = C.style.panel_border;
    rlBegin(RL_QUADS);
        rlColor4ub(b.r, b.g, b.b, 255);
        rlVertex3f(tr.x, tr.y, tr.z); rlVertex3f(br.x, br.y, br.z);
        rlVertex3f(bl.x, bl.y, bl.z); rlVertex3f(tl.x, tl.y, tl.z);
    rlEnd();
}

// One quad, corners in order top-left, bottom-left, bottom-right, top-right
// (counter-clockwise seen from the front, so its back is culled).
static void quad(Vector3 tl, Vector3 right, Vector3 down, Color col)
{
    Vector3 bl = Vector3Add(tl, down), br = Vector3Add(bl, right), tr = Vector3Add(tl, right);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlVertex3f(tl.x, tl.y, tl.z); rlVertex3f(bl.x, bl.y, bl.z);
    rlVertex3f(br.x, br.y, br.z); rlVertex3f(tr.x, tr.y, tr.z);
}

// Text laid out on a plane: facing the head (billboard, yaw only) or lying
// on the command's pose (facing its +Z). The block is centered on the
// anchor; lines are left-aligned inside it.
static void draw_text3d(const VruiCmd *c)
{
    const char *text = C.text + c->text_off;
    Font f = C.font;
    float scale = c->size.x / (float)f.baseSize;
    Vector2 m = MeasureTextEx(f, text, (float)f.baseSize, 1.0f);
    Vector3 up, right, normal, origin = c->a;
    if (c->billboard) {
        Vector3 d = Vector3Subtract(sfxr_head().position, c->a);
        d.y = 0;
        if (Vector3Length(d) < 1e-4f) d = (Vector3){0, 0, 1};
        normal = Vector3Normalize(d);
        up = (Vector3){ 0, 1, 0 };
        right = Vector3CrossProduct(up, normal);
    } else {
        origin = c->pose.position;
        up = sfxr_pose_up(c->pose);
        right = sfxr_pose_right(c->pose);
        normal = Vector3CrossProduct(right, up);
    }

    if (c->bg.a > 0) {   // backing plate, a hair behind the glyphs
        float pad = c->size.x * 0.35f;
        float w = m.x * scale + 2 * pad, h = m.y * scale + 2 * pad;
        Vector3 tl = Vector3Add(origin, Vector3Add(Vector3Scale(right, -w * 0.5f), Vector3Scale(up, h * 0.5f)));
        tl = Vector3Add(tl, Vector3Scale(normal, -0.002f));
        rlSetTexture(0);
        rlBegin(RL_QUADS);
        quad(tl, Vector3Scale(right, w), Vector3Scale(up, -h), c->bg);
        rlEnd();
    }

    float x = -m.x * 0.5f, y = m.y * 0.5f;   // centered, in font px
    rlSetTexture(f.texture.id);
    rlBegin(RL_QUADS);
    for (const char *s = text; *s;) {
        int bytes = 0;
        int cp = GetCodepointNext(s, &bytes);
        s += bytes;
        if (cp == '\n') { x = -m.x * 0.5f; y -= (float)f.baseSize; continue; }
        int gi = GetGlyphIndex(f, cp);
        Rectangle rec = f.recs[gi];
        GlyphInfo g = f.glyphs[gi];
        if (cp != ' ' && cp != '\t') {
            float gx = x + (float)g.offsetX, gy = y - (float)g.offsetY;
            Vector3 o = Vector3Add(origin, Vector3Add(Vector3Scale(right, gx * scale), Vector3Scale(up, gy * scale)));
            Vector3 w = Vector3Scale(right, rec.width * scale);
            Vector3 hgt = Vector3Scale(up, -rec.height * scale);
            float u0 = rec.x / f.texture.width, v0 = rec.y / f.texture.height;
            float u1 = (rec.x + rec.width) / f.texture.width, v1 = (rec.y + rec.height) / f.texture.height;
            Vector3 p0 = o, p1 = Vector3Add(o, hgt), p2 = Vector3Add(p1, w), p3 = Vector3Add(o, w);
            rlColor4ub(c->color.r, c->color.g, c->color.b, c->color.a);
            rlTexCoord2f(u0, v0); rlVertex3f(p0.x, p0.y, p0.z);
            rlTexCoord2f(u0, v1); rlVertex3f(p1.x, p1.y, p1.z);
            rlTexCoord2f(u1, v1); rlVertex3f(p2.x, p2.y, p2.z);
            rlTexCoord2f(u1, v0); rlVertex3f(p3.x, p3.y, p3.z);
        }
        x += (float)(g.advanceX ? g.advanceX : (int)rec.width) + 1.0f;
    }
    rlEnd();
    rlSetTexture(0);
}

static void draw_ring(const VruiCmd *c)
{
    const int seg = 32;
    rlBegin(RL_LINES);
    rlColor4ub(c->color.r, c->color.g, c->color.b, c->color.a);
    for (int i = 0; i < seg; i++) {
        float a0 = (float)i / seg * 2.0f * PI, a1 = (float)(i + 1) / seg * 2.0f * PI;
        Vector3 p0 = sfxr_pose_apply(c->pose, (Vector3){ cosf(a0) * c->size.x, 0, sinf(a0) * c->size.x });
        Vector3 p1 = sfxr_pose_apply(c->pose, (Vector3){ cosf(a1) * c->size.x, 0, sinf(a1) * c->size.x });
        rlVertex3f(p0.x, p0.y, p0.z);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();
}

// Bones between hand joints, as pairs (OpenXR joint order, see SfxrJoint).
static const unsigned char BONES[][2] = {
    { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 },                      // thumb
    { 1, 6 }, { 6, 7 }, { 7, 8 }, { 8, 9 }, { 9, 10 },            // index
    { 1, 11 }, { 11, 12 }, { 12, 13 }, { 13, 14 }, { 14, 15 },    // middle
    { 1, 16 }, { 16, 17 }, { 17, 18 }, { 18, 19 }, { 19, 20 },    // ring
    { 1, 21 }, { 21, 22 }, { 22, 23 }, { 23, 24 }, { 24, 25 },    // little
};

static void draw_hand_joints(const SfxrHandJoints *j, Color c)
{
    for (int i = 1; i < SFXR_JOINT_COUNT; i++)
        DrawSphereEx(j->joint[i].position, fmaxf(j->radius[i], 0.004f) * 0.8f, 4, 6, c);
    for (size_t b = 0; b < sizeof BONES / sizeof BONES[0]; b++)
        DrawLine3D(j->joint[BONES[b][0]].position, j->joint[BONES[b][1]].position, ColorBrightness(c, 0.4f));
}

static void draw_controllers(void)
{
    for (int h = 0; h < 2; h++) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        const SfxrHandJoints *joints = sfxr_hand_joints((SfxrHandId)h);
        Color body = h ? (Color){ 70, 120, 200, 255 } : (Color){ 200, 110, 70, 255 };
        // Joints: always for bare hands; while holding controllers only on request.
        bool bare = joints->valid && joints->source == SFXR_SOURCE_HAND;
        if (joints->valid && (bare || C.joints_always)) draw_hand_joints(joints, bare ? body : ColorAlpha(body, 0.6f));
        if (!hand->active || bare || hand->source == SFXR_SOURCE_HAND) continue;
        if (hand->squeeze_btn.down) body = ColorBrightness(body, 0.35f);
        SfxrPose mp;
        const Model *m = C.controller_models ? sfxr_controller_model((SfxrHandId)h, &mp) : NULL;
        if (m) {
            // the headset's own model of this controller
            sfxr_push_pose(mp);
                DrawModel(*m, (Vector3){ 0 }, 1.0f, WHITE);
            sfxr_pop_pose();
        } else {
            sfxr_push_pose(hand->grip);
                DrawCube((Vector3){ 0, -0.005f, 0.035f }, 0.032f, 0.036f, 0.11f, body);
                DrawCube((Vector3){ 0, 0.012f, -0.02f }, 0.05f, 0.012f, 0.05f, ColorBrightness(body, -0.3f));
            sfxr_pop_pose();
        }
        // The tip shows how far the trigger is pulled: white (released),
        // pale yellow (soft), yellow (firm), orange (full).
        Color tip = RAYWHITE;
        if (hand->trigger_at[SFXR_PULL_FULL].down) tip = ORANGE;
        else if (hand->trigger_at[SFXR_PULL_FIRM].down) tip = YELLOW;
        else if (hand->trigger_at[SFXR_PULL_SOFT].down) tip = (Color){ 255, 245, 170, 255 };
        DrawSphereEx(hand->aim.position, 0.006f, 6, 8, tip);
    }
}

static void draw_cmd(const VruiCmd *c)
{
    switch (c->kind) {
    case CMD_BOX:
        sfxr_push_pose(c->pose);
        DrawCube((Vector3){0}, c->size.x, c->size.y, c->size.z, c->color);
        sfxr_pop_pose();
        break;
    case CMD_BOX_WIRES:
        sfxr_push_pose(c->pose);
        DrawCubeWires((Vector3){0}, c->size.x, c->size.y, c->size.z, c->color);
        sfxr_pop_pose();
        break;
    case CMD_SPHERE:   DrawSphereEx(c->a, c->size.x, 8, 12, c->color); break;
    case CMD_CYLINDER: DrawCylinderEx(c->a, c->b, c->size.x, c->size.y, 16, c->color); break;
    case CMD_LINE:     DrawLine3D(c->a, c->b, c->color); break;
    case CMD_PANEL:    draw_panel_quad(c); break;
    case CMD_TEXT:     draw_text3d(c); break;
    case CMD_RING:     draw_ring(c); break;
    case CMD_TRI:
        DrawTriangle3D(c->a, c->b, c->size, c->color);
        DrawTriangle3D(c->a, c->size, c->b, c->color);
        break;
    }
}

// A sphere around the head, drawn over everything (teleport blinks, flashes).
static void view_sphere(Color col)
{
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableBackfaceCulling();
    DrawSphereEx(sfxr_head().position, 0.25f, 8, 12, col);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthTest();
}

void vrui_draw(void)
{
    bool any_on_top = false;
    for (int i = 0; i < C.ncmds; i++) {
        if (C.cmds[i].on_top) any_on_top = true;
        else draw_cmd(&C.cmds[i]);
    }

    if (C.show_controllers) draw_controllers();

    for (int h = 0; h < 2; h++) {
        if (!C.laser_on[h]) continue;
        Color col = !C.laser_hit[h] ? C.style.laser : C.laser_on_panel[h] ? C.style.laser_hit : C.style.laser_far;
        DrawLine3D(C.laser_from[h], C.laser_to[h], col);
        if (C.laser_hit[h]) DrawSphereEx(C.laser_to[h], 0.006f, 6, 8, C.style.cursor);
    }

    // On-top pass (HUDs, pointers): no depth test, so nothing in the world
    // can hide them; they draw in the order they were queued.
    if (any_on_top) {
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        for (int i = 0; i < C.ncmds; i++) if (C.cmds[i].on_top) draw_cmd(&C.cmds[i]);
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
    }

    if (C.tint.a > 0) view_sphere(C.tint);
    if (C.fade > 0.001f) view_sphere((Color){ 0, 0, 0, (unsigned char)(Clamp(C.fade, 0, 1) * 255) });
}
