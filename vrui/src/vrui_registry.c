// vrui_registry.c - the widget registry: every widget says each frame where it
// is and what it's set to, under a readable name, so tests (and tools) can
// find "table.sky" or "toolbox.reset_blocks" instead of hard-coding where
// they are (docs/TESTING.md, "The widget registry").
//
// Names: the id's group name (vrui_group_name) + "." + the widget's label,
// lower-case, words joined by "_", anything from a "(" on dropped:
// VRUI_ID2(G_TABLE, 1) labelled "SPAWN (full pull)" in group "table" is
// "table.spawn". A panel's widgets are "<panel title>.<text>". Unlabelled
// widgets are "<group>.#<index>" until you name them (vrui_name_widget).
//
// Reports are collected during a frame and published at vrui_end, so lookups
// always see one complete frame.

#include "vrui_internal.h"

#include <ctype.h>
#include <stdio.h>

#define MAX_REG    512
#define MAX_GROUPS 64

static VruiWidgetInfo cur[MAX_REG], last[MAX_REG];
static int ncur, nlast;
static struct { unsigned group; char name[20]; } groups[MAX_GROUPS];
static int ngroups;

void vrui_group_name(unsigned group, const char *name)
{
    for (int i = 0; i < ngroups; i++)
        if (groups[i].group == group) { snprintf(groups[i].name, sizeof groups[i].name, "%s", name); return; }
    if (ngroups >= MAX_GROUPS) return;
    groups[ngroups].group = group;
    snprintf(groups[ngroups++].name, sizeof groups[0].name, "%s", name);
}

void vrui_name_widget(VruiId id, const char *label) { vrui__name(id, label); }

void vrui_mark(VruiId id, const char *label, SfxrPose pose)
{
    vrui__name(id, label);
    vrui__report(id, "mark", pose, pose, 0, false);
}

// "SPAWN (full pull)" -> "spawn", "Reset blocks" -> "reset_blocks"
void vrui__slug(const char *text, char *out, int size)
{
    int n = 0;
    bool gap = false;
    for (const char *s = text; *s && *s != '(' && n < size - 1; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c)) {
            if (gap && n > 0 && n < size - 1) out[n++] = '_';
            if (n < size - 1) out[n++] = (char)tolower(c);
            gap = false;
        } else {
            gap = true;
        }
    }
    out[n] = 0;
}

void vrui__report_named(const char *name, const char *label, VruiId id, const char *kind, SfxrPose base, SfxrPose part,
                        float value, bool has_value)
{
    VruiWidgetInfo *w = NULL;
    for (int i = 0; i < ncur; i++) if (cur[i].id == id) w = &cur[i];   // one entry per widget
    if (!w) {
        if (ncur >= MAX_REG) return;
        w = &cur[ncur++];
    }
    snprintf(w->name, sizeof w->name, "%s", name);
    snprintf(w->label, sizeof w->label, "%s", label);
    snprintf(w->kind, sizeof w->kind, "%s", kind);
    w->id = id;
    w->pose = base;
    w->part = part;
    w->value = value;
    w->has_value = has_value;
}

void vrui__report(VruiId id, const char *kind, SfxrPose base, SfxrPose part, float value, bool has_value)
{
    unsigned group = id >> 16;
    const char *g = NULL;
    for (int i = 0; i < ngroups; i++) if (groups[i].group == group) g = groups[i].name;
    char gbuf[16], slug[32], name[64];
    if (!g) { snprintf(gbuf, sizeof gbuf, "g%u", group); g = gbuf; }
    const char *label = vrui__item(id)->name;
    vrui__slug(label, slug, sizeof slug);
    if (slug[0]) snprintf(name, sizeof name, "%s.%s", g, slug);
    else snprintf(name, sizeof name, "%s.#%u", g, id & 0xFFFFu);
    vrui__report_named(name, vrui__who(id), id, kind, base, part, value, has_value);
}

void vrui__registry_publish(void)
{
    memcpy(last, cur, sizeof(VruiWidgetInfo) * (size_t)ncur);
    nlast = ncur;
    ncur = 0;
}

int vrui_widget_count(void) { return nlast; }
const VruiWidgetInfo *vrui_widget_at(int i) { return i >= 0 && i < nlast ? &last[i] : NULL; }

const VruiWidgetInfo *vrui_find_widget(const char *target, SfxrPose *pose)
{
    char name[64];
    snprintf(name, sizeof name, "%s", target);
    char *slash = strchr(name, '/');
    const char *part = slash ? slash + 1 : "";
    if (slash) *slash = 0;
    for (int i = 0; i < nlast; i++) {
        if (strcmp(last[i].name, name)) continue;
        if (pose) *pose = !*part || !strcmp(part, "base") ? last[i].pose : last[i].part;   // handle, cap, part...: the moving part
        return &last[i];
    }
    return NULL;
}
