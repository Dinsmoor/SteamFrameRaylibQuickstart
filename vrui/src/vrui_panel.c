// vrui_panel.c - flat 2D UI on world-space panels.
//
// Each panel renders into its own RenderTexture (in pixels, top-left origin,
// just like desktop raylib 2D) while logic runs, then vrui_draw() puts the
// texture on a quad in the world. The laser that hits the panel becomes the
// "mouse": trigger = left button, thumbstick Y = scroll wheel.
//
// Moving panels: point at the title bar and hold trigger, or point anywhere on
// the panel and hold grip. The panel stays locked to your controller until
// you let go.

#include "vrui_internal.h"

#include <stdio.h>

#define PAD 12

typedef struct {
    VruiId active_widget;   // the widget holding the pointer (while the trigger is down)
} PanelState;               // (dragging the panel itself is the item's hold_hand)
VRUI_STATE_FITS(PanelState);

typedef struct {
    int   flags;            // L_* (vrui_list)
    float press_y;          // pointer y where the trigger went down
    float press_scroll;     // scroll at that moment
} ListState;
VRUI_STATE_FITS(ListState);

// Clip drawing to a rectangle in panel pixels (the texture has more texels).
void vrui__panel_scissor(int x, int y, int w, int h)
{
    BeginScissorMode(x * VRUI_PANEL_SS, y * VRUI_PANEL_SS, w * VRUI_PANEL_SS, h * VRUI_PANEL_SS);
}

static VruiPanelTex *panel_tex(VruiId id, int w, int h)
{
    VruiPanelTex *slot = NULL;
    for (int i = 0; i < VRUI_MAX_PANELS; i++) {
        if (C.panels[i].id == id) { slot = &C.panels[i]; break; }
        if (!slot && !C.panels[i].id) slot = &C.panels[i];
    }
    if (!slot) { TraceLog(LOG_WARNING, "VRUI: too many panels (raise VRUI_MAX_PANELS)"); return NULL; }
    if (slot->id != id || slot->w != w || slot->h != h) {
        if (slot->id) UnloadRenderTexture(slot->rt);
        slot->rt = LoadRenderTexture(w, h);
        slot->id = id;
        slot->w = w;
        slot->h = h;
    }
    return slot;
}

// Laser vs panel plane; returns distance and pixel coords (may be outside).
static float panel_hit(int h, SfxrPose pose, float w_m, float h_m, int w_px, int h_px, Vector2 *px, bool clamp_to_plane)
{
    Ray r = vrui__hand_ray(h);
    Vector3 o = sfxr_pose_apply_inv(pose, r.position);
    Vector3 d = Vector3RotateByQuaternion(r.direction, QuaternionInvert(pose.orientation));
    if (fabsf(d.z) < 1e-5f) return -1.0f;
    float t = -o.z / d.z;
    if (t < 0) return -1.0f;
    Vector3 p = Vector3Add(o, Vector3Scale(d, t));
    px->x = (p.x / w_m + 0.5f) * (float)w_px;
    px->y = (0.5f - p.y / h_m) * (float)h_px;
    if (!clamp_to_plane && (fabsf(p.x) > w_m * 0.5f || fabsf(p.y) > h_m * 0.5f || o.z < 0)) return -1.0f;
    return t;
}

void vrui_panel_passive(void) { C.next_passive = true; }

bool vrui_panel_begin(VruiId id, SfxrPose *pose, float width_m, float height_m, const char *title)
{
    if (C.p.open) { TraceLog(LOG_WARNING, "VRUI: vrui_panel_begin while another panel is open"); return false; }
    const VruiStyle *st = &C.style;
    int w_px = (int)(width_m * st->px_per_m), h_px = (int)(height_m * st->px_per_m);
    if (w_px > 2048) w_px = 2048;
    if (h_px > 2048) h_px = 2048;
    VruiPanelTex *pt = panel_tex(id, w_px * VRUI_PANEL_SS, h_px * VRUI_PANEL_SS);
    if (!pt) return false;

    VruiItem *it = vrui__item(id);
    PanelState *ps = VRUI_STATE(it, PanelState);
    int drag_hand = it->hold_hand;
    SfxrPull pull = vrui_current_pull();
    VruiId active_widget = ps->active_widget;

    // --- dragging the panel
    if (drag_hand >= 0) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)drag_hand);
        if (hand->active && (hand->trigger_at[pull].down || hand->squeeze_at[pull].down)) {
            *pose = sfxr_pose_mul(hand->aim, it->rel);
        } else {
            if (C.ray_active[drag_hand] == id) C.ray_active[drag_hand] = VRUI_ID_NONE;
            drag_hand = -1;
        }
    }

    // --- laser offers + pointer (passive panels are displays: no input at all)
    bool passive = C.next_passive;
    C.next_passive = false;
    int ptr_hand = -1;
    Vector2 ptr = { -1, -1 };
    for (int h = 0; h < 2 && !passive; h++) {
        if (!sfxr_hand((SfxrHandId)h)->active) continue;
        Vector2 px;
        bool captured = C.ray_active[h] == id;
        float t = panel_hit(h, *pose, width_m, height_m, w_px, h_px, &px, captured);
        if (t >= 0) vrui__ray_offer(h, id, t);
        if (vrui__ray_hot(h, id) && t >= 0 && (ptr_hand < 0 || captured)) { ptr_hand = h; ptr = px; }
    }

    // --- input ownership: pointing claims that hand; LOOK / MODAL claim both
    VruiCapture cap = C.next_capture;
    C.next_capture = VRUI_CAPTURE_POINT;
    for (int h = 0; h < 2 && !passive; h++) if (vrui__ray_hot(h, id)) vrui_claim_input((SfxrHandId)h);
    bool capturing = cap == VRUI_CAPTURE_MODAL;
    if (cap == VRUI_CAPTURE_LOOK) {
        // in front of your face (within 30 deg of where the head points, 3 m),
        // and you're looking at its FRONT: the back of a panel never captures
        SfxrPose head = sfxr_head();
        Vector3 to = Vector3Subtract(pose->position, head.position);
        float dist = Vector3Length(to);
        Vector3 facing = Vector3RotateByQuaternion((Vector3){ 0, 0, 1 }, pose->orientation);
        capturing = dist < 3.0f && dist > 1e-3f &&
                    Vector3DotProduct(Vector3Scale(to, 1.0f / dist), sfxr_pose_forward(head)) > 0.866f &&
                    Vector3DotProduct(facing, to) < 0.0f;
    }
    if (capturing) { vrui_claim_input(SFXR_LEFT); vrui_claim_input(SFXR_RIGHT); }

    if (!passive) vrui__hint(id, TextFormat("laser + trigger%s%s", vrui__pull_suffix(), title ? " | drag the title bar to move" : ""));

    bool has_title = title != NULL;
    C.p.open = true;
    C.p.capturing = capturing;
    C.p.id = id;
    C.p.w_m = width_m;
    C.p.h_m = height_m;
    C.p.w_px = w_px;
    C.p.h_px = h_px;
    C.p.has_title = has_title;
    C.p.hand = -1;
    C.p.item = it;
    C.p.down = C.p.pressed = C.p.released = false;
    C.p.stick = (Vector2){0};

    if (ptr_hand >= 0 && drag_hand < 0) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)ptr_hand);
        const SfxrButton *trig = &hand->trigger_at[pull];
        bool on_title = has_title && ptr.y >= 0 && ptr.y < st->title_height;
        if ((trig->pressed && on_title) || hand->squeeze_at[pull].pressed) {
            drag_hand = ptr_hand;
            it->rel = sfxr_pose_mul(sfxr_pose_inverse(hand->aim), *pose);
            C.ray_active[ptr_hand] = id;
            vrui__click_pulse(ptr_hand);
        } else {
            C.p.hand = ptr_hand;
            C.p.ptr = ptr;
            C.p.down = trig->down;
            C.p.pressed = trig->pressed;
            C.p.released = trig->released;
            C.p.stick = hand->stick;
            if (trig->pressed) C.ray_active[ptr_hand] = id;   // keep the pointer while held
            if (trig->released && C.ray_active[ptr_hand] == id) C.ray_active[ptr_hand] = VRUI_ID_NONE;
        }
    }
    // A widget keeps its capture only while the trigger is held (plus the
    // release frame, so it can report the click).
    if (C.p.hand < 0 || (!C.p.down && !C.p.released)) active_widget = VRUI_ID_NONE;
    it->hold_hand = drag_hand;
    ps->active_widget = active_widget;
    C.p.active_widget = active_widget;
    C.p.pose = *pose;
    C.p.slug[0] = 0;
    if (title) {   // the widget registry: titled panels and their widgets, by name
        vrui__slug(title, C.p.slug, sizeof C.p.slug);
        vrui__report_named(C.p.slug, title, id, "panel", *pose, *pose, 0, false);
    }

    // --- chrome (drawn in panel pixels, scaled up to the texture's texels)
    BeginTextureMode(pt->rt);
    rlPushMatrix();
    rlScalef((float)VRUI_PANEL_SS, (float)VRUI_PANEL_SS, 1.0f);
    ClearBackground(st->panel_bg);
    if (has_title) {
        Color tb = drag_hand >= 0 ? st->widget_active : st->title_bg;
        DrawRectangle(0, 0, w_px, st->title_height, tb);
        int fs = st->font_size;
        DrawTextEx(C.font, title, (Vector2){ PAD, (float)(st->title_height - fs) / 2 }, (float)fs, 2, st->title_text);
        // grip dots hint that the bar is draggable
        for (int i = 0; i < 3; i++)
            DrawCircle(w_px - PAD - 8 - i * 12, st->title_height / 2, 3, st->text_dim);
    }
    DrawRectangleLinesEx((Rectangle){ 0, 0, (float)w_px, (float)h_px }, capturing ? 5 : 3,
                         capturing ? st->accent : st->panel_border);
    if (capturing && has_title) {
        const char *msg = cap == VRUI_CAPTURE_LOOK ? "controls held - look away to release" : "controls held";
        int fs = st->font_size - 2;
        Vector2 sz = MeasureTextEx(C.font, msg, (float)fs, 1);
        DrawTextEx(C.font, msg, (Vector2){ w_px - PAD - 48 - sz.x, (float)(st->title_height - fs) / 2 }, (float)fs, 1, st->accent);
    }
    return true;
}

void vrui_panel_end(void)
{
    if (!C.p.open) return;
    VruiPanelTex *pt = NULL;
    for (int i = 0; i < VRUI_MAX_PANELS; i++) if (C.panels[i].id == C.p.id) pt = &C.panels[i];
    if (C.p.hand >= 0) {
        // pointer crosshair
        DrawCircleLines((int)C.p.ptr.x, (int)C.p.ptr.y, 7, C.style.cursor);
    }
    rlPopMatrix();
    EndTextureMode();
    VRUI_STATE(C.p.item, PanelState)->active_widget = C.p.active_widget;
    if (pt) {
        // Mipmaps keep small text readable when the panel is far away or at
        // a grazing angle (otherwise it shimmers as you move your head).
        GenTextureMipmaps(&pt->rt.texture);
        SetTextureFilter(pt->rt.texture, TEXTURE_FILTER_TRILINEAR);
        VruiCmd *c = vrui__cmd(CMD_PANEL, WHITE);
        c->pose = C.p.pose;
        c->size = (Vector3){ C.p.w_m, C.p.h_m, 0 };
        c->tex = pt->rt.texture.id;
    }
    C.p.open = false;
}

Rectangle vrui_panel_content(void)
{
    float top = C.p.has_title ? (float)C.style.title_height : 0.0f;
    return (Rectangle){ PAD, top + PAD, (float)C.p.w_px - 2 * PAD, (float)C.p.h_px - top - 2 * PAD };
}

bool vrui_panel_hovered(void) { return C.p.open && C.p.hand >= 0; }
bool vrui_panel_capturing(void) { return C.p.open && C.p.capturing; }

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void vrui_layout_begin(Rectangle area, float spacing)
{
    C.p.lay_area = area;
    C.p.lay_y = area.y;
    C.p.lay_spacing = spacing;
}

Rectangle vrui_row(float height)
{
    // never shorter than a line of text: layouts written for smaller text
    // (or a bigger font_size) don't crowd
    float line = (float)C.style.font_size + 4.0f;
    if (height >= 18.0f && height < line) height = line;   // (thin rows, bars and gaps, stay thin)
    Rectangle r = { C.p.lay_area.x, C.p.lay_y, C.p.lay_area.width, height };
    C.p.lay_y += height + C.p.lay_spacing;
    return r;
}

void vrui_row_cols(float height, int n, Rectangle *out)
{
    Rectangle row = vrui_row(height);
    float gap = C.p.lay_spacing;
    float w = (row.width - gap * (float)(n - 1)) / (float)n;
    for (int i = 0; i < n; i++) out[i] = (Rectangle){ row.x + (float)i * (w + gap), row.y, w, height };
}

void vrui_space(float height) { C.p.lay_y += height; }

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

// Where a spot on the panel (in panel pixels) is in the world.
static SfxrPose panel_spot(Vector2 px)
{
    Vector3 local = { px.x / (float)C.p.w_px * C.p.w_m - C.p.w_m * 0.5f, C.p.h_m * 0.5f - px.y / (float)C.p.h_px * C.p.h_m, 0 };
    return (SfxrPose){ sfxr_pose_apply(C.p.pose, local), C.p.pose.orientation };
}

// A panel widget in the widget registry: "<panel>.<its text>", posed at its
// middle (`part`: where its moving bit is, a slider's knob).
static void report(VruiId local, const char *kind, Rectangle r, Vector2 part, const char *text, float value)
{
    if (!C.p.slug[0] || !text) return;
    char slug[32], name[64];
    vrui__slug(text, slug, sizeof slug);
    snprintf(name, sizeof name, "%s.%s", C.p.slug, slug);
    vrui__report_named(name, text, vrui__widget_item(C.p.id, local)->id, kind,
                       panel_spot((Vector2){ r.x + r.width * 0.5f, r.y + r.height * 0.5f }), panel_spot(part), value, true);
}

static bool w_hot(VruiId id, Rectangle r)
{
    if (C.p.hand < 0) return false;
    if (C.p.active_widget && C.p.active_widget != id) return false;
    return CheckCollisionPointRec(C.p.ptr, r);
}

static void text_in(Rectangle r, const char *text, Color color, bool center)
{
    int fs = C.style.font_size;
    Vector2 m = MeasureTextEx(C.font, text, (float)fs, 2);
    float x = center ? r.x + (r.width - m.x) / 2 : r.x + 8;
    DrawTextEx(C.font, text, (Vector2){ floorf(x), floorf(r.y + (r.height - m.y) / 2) }, (float)fs, 2, color);
}

// Standard press/release-inside click. Returns true on click.
static bool w_click(VruiId id, Rectangle r, bool *hot_out, bool *active_out)
{
    bool hot = w_hot(id, r);
    bool click = false;
    if (hot && C.p.pressed) C.p.active_widget = id;
    bool active = C.p.active_widget == id;
    if (active && C.p.released) {
        if (hot) { click = true; vrui__click_pulse(C.p.hand); }
        C.p.active_widget = VRUI_ID_NONE;
    }
    if (hot_out) *hot_out = hot;
    if (active_out) *active_out = active && C.p.down;
    return click;
}

void vrui_label(Rectangle r, const char *text) { text_in(r, text, C.style.text, false); }

void vrui_paragraph(const char *text)
{
    // word-wrapped to the layout's width, a row per line
    int fs = C.style.font_size;
    float width = C.p.lay_area.width - 16;
    char line[256];
    int n = 0, last_space = -1;
    for (const char *s = text;; s++) {
        bool end = *s == 0 || *s == '\n';
        if (!end && n < (int)sizeof line - 1) {
            line[n] = *s;
            line[n + 1] = 0;
            if (*s == ' ') last_space = n;
            if (MeasureTextEx(C.font, line, (float)fs, 2).x <= width || last_space < 0) { n++; continue; }
            // too wide: this line ends at the last space; the rest carries on
            line[last_space] = 0;
            vrui_label(vrui_row((float)fs + 4), line);
            int rest = n - last_space;
            memmove(line, line + last_space + 1, (size_t)rest);
            n = rest;
            line[n] = 0;
            last_space = -1;
            for (int i = 0; i < n; i++) if (line[i] == ' ') last_space = i;
            continue;
        }
        line[n] = 0;
        if (n > 0 || *s == '\n') vrui_label(vrui_row((float)fs + 4), line);
        n = 0;
        last_space = -1;
        if (*s == 0) break;
    }
}
void vrui_label_center(Rectangle r, const char *text, Color color) { text_in(r, text, color, true); }

bool vrui_button(VruiId id, Rectangle r, const char *text)
{
    bool hot, active;
    bool click = w_click(id, r, &hot, &active);
    Color bg = active ? C.style.widget_active : hot ? C.style.widget_hot : C.style.widget;
    DrawRectangleRounded(r, 0.25f, 6, bg);
    if (hot) DrawRectangleRoundedLinesEx(r, 0.25f, 6, 2, C.style.accent);
    text_in(r, text, C.style.text, true);
    if (click) sfxr_event("click", "%s", text);
    report(id, "button", r, (Vector2){ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, text, click);
    return click;
}

bool vrui_toggle(VruiId id, Rectangle r, const char *text, bool *value)
{
    bool hot, active;
    bool click = w_click(id, r, &hot, &active);
    if (click) { *value = !*value; sfxr_event("toggle", "%s %s", text, *value ? "on" : "off"); }
    if (hot) DrawRectangleRec(r, C.style.widget);
    float s = r.height * 0.6f;
    Rectangle box = { r.x + 6, r.y + (r.height - s) / 2, s, s };
    DrawRectangleRounded(box, 0.3f, 6, *value ? C.style.accent : C.style.widget_hot);
    if (*value) {
        DrawLineEx((Vector2){ box.x + s * 0.22f, box.y + s * 0.52f }, (Vector2){ box.x + s * 0.42f, box.y + s * 0.72f }, 3, WHITE);
        DrawLineEx((Vector2){ box.x + s * 0.42f, box.y + s * 0.72f }, (Vector2){ box.x + s * 0.78f, box.y + s * 0.28f }, 3, WHITE);
    }
    text_in((Rectangle){ box.x + s + 4, r.y, r.width - s - 10, r.height }, text, C.style.text, false);
    report(id, "toggle", r, (Vector2){ box.x + s * 0.5f, box.y + s * 0.5f }, text, *value);
    return click;
}

bool vrui_slider(VruiId id, Rectangle r, const char *label, float *value, float min, float max)
{
    bool hot = w_hot(id, r);
    if (hot && C.p.pressed) C.p.active_widget = id;
    bool active = C.p.active_widget == id && C.p.down;
    if (C.p.active_widget == id && C.p.released) C.p.active_widget = VRUI_ID_NONE;

    float label_w = label ? r.width * 0.35f : 0;
    Rectangle track = { r.x + label_w + 8, r.y + r.height * 0.5f - 4, r.width - label_w - 16 - 70, 8 };
    bool changed = false;
    if (active && track.width > 0) {
        float t = Clamp((C.p.ptr.x - track.x) / track.width, 0, 1);
        float nv = min + t * (max - min);
        if (nv != *value) { *value = nv; changed = true; }
    }
    float t = (max > min) ? Clamp((*value - min) / (max - min), 0, 1) : 0;
    if (label) text_in((Rectangle){ r.x, r.y, label_w, r.height }, label, C.style.text, false);
    DrawRectangleRounded(track, 1.0f, 6, C.style.widget_hot);
    DrawRectangleRounded((Rectangle){ track.x, track.y, track.width * t, track.height }, 1.0f, 6, C.style.accent);
    float kr = (hot || active) ? 12.0f : 10.0f;
    DrawCircleV((Vector2){ track.x + track.width * t, track.y + track.height / 2 }, kr, active ? WHITE : C.style.text);
    text_in((Rectangle){ r.x + r.width - 70, r.y, 70, r.height }, TextFormat("%.2f", *value), C.style.text_dim, true);
    report(id, "slider", r, (Vector2){ track.x + track.width * t, track.y + track.height / 2 }, label, *value);
    return changed;
}

bool vrui_segmented(VruiId id, Rectangle r, const char *const *items, int count, int *selected)
{
    bool changed = false;
    float w = r.width / (float)count;
    for (int i = 0; i < count; i++) {
        Rectangle cell = { r.x + (float)i * w, r.y, w - 2, r.height };
        bool hot, active;
        VruiId cid = VRUI_ID2(id & 0xFFFF, 0x8000 | i);
        if (w_click(cid, cell, &hot, &active) && *selected != i) { *selected = i; changed = true; }
        Color bg = (*selected == i) ? C.style.accent : active ? C.style.widget_active : hot ? C.style.widget_hot : C.style.widget;
        DrawRectangleRec(cell, bg);
        text_in(cell, items[i], C.style.text, true);
        report(cid, "choice", cell, (Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.5f }, items[i], *selected == i);
    }
    return changed;
}

void vrui_progress(Rectangle r, float t, Color color)
{
    DrawRectangleRounded(r, 0.4f, 6, C.style.widget);
    DrawRectangleRounded((Rectangle){ r.x, r.y, r.width * Clamp(t, 0, 1), r.height }, 0.4f, 6, color);
}

bool vrui_list(VruiId id, Rectangle r, const char *const *items, int count, int *selected, float *scroll)
{
    // Three ways to scroll, because a laser has no mouse wheel:
    //   drag the list itself with the trigger held (like a phone),
    //   drag the scrollbar thumb on the right,
    //   or push the thumbstick while pointing at it.
    // A press that doesn't move more than a few pixels is a tap: it selects.
    const float bar_w = 16.0f;
    float row_h = (float)C.style.font_size * 1.8f;
    float content_h = row_h * (float)count;
    float max_scroll = fmaxf(0, content_h - r.height);
    ListState *ls = VRUI_STATE(vrui__widget_item(C.p.id, id), ListState);
    bool inside = C.p.hand >= 0 && CheckCollisionPointRec(C.p.ptr, r);
    bool on_bar = inside && max_scroll > 0 && C.p.ptr.x > r.x + r.width - bar_w - 4;
    enum { L_PRESSED = 1, L_DRAG = 2, L_BAR = 4 };
    int st = ls->flags;
    bool changed = false;

    if (inside && C.p.pressed) {
        st = L_PRESSED | (on_bar ? L_BAR : 0);
        ls->press_y = C.p.ptr.y;
        ls->press_scroll = *scroll;
        C.p.active_widget = id;
    }
    if ((st & L_PRESSED) && C.p.hand >= 0 && (C.p.down || C.p.released)) {
        float dy = C.p.ptr.y - ls->press_y;
        if (fabsf(dy) > 10.0f) st |= L_DRAG;
        if (st & L_DRAG) {
            if (st & L_BAR) *scroll = ls->press_scroll + dy * (content_h / r.height);
            else *scroll = ls->press_scroll - dy;
        }
        if (C.p.released) {
            if (!(st & L_DRAG) && inside && !(st & L_BAR)) {   // a tap: select the row under it
                int row = (int)((C.p.ptr.y - r.y + *scroll) / row_h);
                if (row >= 0 && row < count && *selected != row) { *selected = row; changed = true; }
                vrui__click_pulse(C.p.hand);
            }
            st = 0;
            if (C.p.active_widget == id) C.p.active_widget = VRUI_ID_NONE;
        }
    } else if (!(C.p.hand >= 0 && C.p.down)) {
        st = 0;
    }
    ls->flags = st;
    if (inside && !(st & L_DRAG) && fabsf(C.p.stick.y) > 0.2f) *scroll -= C.p.stick.y * 600.0f * sfxr_dt();
    *scroll = Clamp(*scroll, 0, max_scroll);

    DrawRectangleRec(r, C.style.widget);
    vrui__panel_scissor((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    int first = (int)(*scroll / row_h);
    int hover_row = inside && !on_bar ? (int)((C.p.ptr.y - r.y + *scroll) / row_h) : -1;
    for (int i = first; i < count; i++) {
        float y = r.y + (float)i * row_h - *scroll;
        if (y > r.y + r.height) break;
        Rectangle row = { r.x, y, r.width - (max_scroll > 0 ? bar_w : 0), row_h };
        Color bg = (*selected == i) ? C.style.accent : (i == hover_row && !(st & L_DRAG)) ? C.style.widget_hot
                 : ((i & 1) ? C.style.widget : C.style.panel_bg);
        DrawRectangleRec(row, bg);
        text_in(row, items[i], C.style.text, false);
    }
    EndScissorMode();
    if (max_scroll > 0) {
        Rectangle track = { r.x + r.width - bar_w, r.y, bar_w, r.height };
        DrawRectangleRec(track, C.style.panel_bg);
        float bar_h = fmaxf(24.0f, r.height * r.height / content_h);
        float bar_y = r.y + (r.height - bar_h) * (*scroll / max_scroll);
        Color thumb = (st & L_BAR) ? C.style.accent : on_bar ? C.style.widget_active : C.style.text_dim;
        DrawRectangleRounded((Rectangle){ track.x + 2, bar_y, bar_w - 4, bar_h }, 0.5f, 4, thumb);
        if (inside && !(st & L_DRAG)) {   // say how, the first time people meet it
            const char *hint = "drag to scroll";
            int fs = C.style.font_size - 2;
            Vector2 sz = MeasureTextEx(C.font, hint, (float)fs, 1);
            DrawTextEx(C.font, hint, (Vector2){ r.x + r.width - bar_w - sz.x - 6, r.y + r.height - sz.y - 4 }, (float)fs, 1,
                       C.style.text_dim);
        }
    }
    DrawRectangleLinesEx(r, 1, C.style.panel_border);
    return changed;
}
