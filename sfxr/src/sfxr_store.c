// sfxr_store.c - saving: a small file of named values next to the app
// (sfxr.h, "Saving"). Plain text, one "key=value" a line, so it can be read
// and edited by hand. Replays and tests keep it in memory only.

#include "sfxr_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define S sfxr_state

#define MAX_KEYS 128

static struct {
    char path[512];
    int n;
    char key[MAX_KEYS][48];
    char val[MAX_KEYS][208];
} T;

static bool on_disk(void) { return S.backend != SFXR_BACKEND_REPLAY && S.backend != SFXR_BACKEND_SCRIPT; }

static int find(const char *key)
{
    for (int i = 0; i < T.n; i++) if (!strcmp(T.key[i], key)) return i;
    return -1;
}

bool sfxr_store_open(const char *name)
{
    T.n = 0;
    snprintf(T.path, sizeof T.path, "save/%s.cfg", name);
    if (!on_disk()) return false;
    FILE *f = fopen(T.path, "r");
    if (!f) return false;
    char line[300];
    while (fgets(line, sizeof line, f) && T.n < MAX_KEYS) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0;
        snprintf(T.key[T.n], sizeof T.key[0], "%.47s", line);
        snprintf(T.val[T.n], sizeof T.val[0], "%.207s", eq + 1);
        T.n++;
    }
    fclose(f);
    return true;
}

const char *sfxr_store_str(const char *key, const char *fallback)
{
    int i = find(key);
    return i >= 0 ? T.val[i] : fallback;
}
int   sfxr_store_int(const char *key, int fallback)     { const char *v = sfxr_store_str(key, NULL); return v ? atoi(v) : fallback; }
float sfxr_store_float(const char *key, float fallback) { const char *v = sfxr_store_str(key, NULL); return v ? (float)atof(v) : fallback; }

void sfxr_store_set_str(const char *key, const char *value)
{
    int i = find(key);
    if (i < 0) {
        if (T.n >= MAX_KEYS) return;
        i = T.n++;
        snprintf(T.key[i], sizeof T.key[0], "%s", key);
    }
    snprintf(T.val[i], sizeof T.val[0], "%s", value);
}
void sfxr_store_set_int(const char *key, int value)     { char b[32]; snprintf(b, sizeof b, "%d", value); sfxr_store_set_str(key, b); }
void sfxr_store_set_float(const char *key, float value) { char b[32]; snprintf(b, sizeof b, "%g", value); sfxr_store_set_str(key, b); }

bool sfxr_store_save(void)
{
    if (!on_disk() || !T.path[0]) return false;
    mkdir("save", 0755);
    // write a new file beside the old one, then swap it in: rename is atomic,
    // so the store is always either the old one or the new one, never half
    char tmp[520];
    snprintf(tmp, sizeof tmp, "%s.new", T.path);
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    fprintf(f, "# saved by sfxr_store_save\n");
    for (int i = 0; i < T.n; i++) fprintf(f, "%s=%s\n", T.key[i], T.val[i]);
    bool ok = fflush(f) == 0;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, T.path) != 0) { remove(tmp); return false; }
    return true;
}
