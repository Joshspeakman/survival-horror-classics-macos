/* ADVAPI32 registry: an in-memory tree (empty at start; the GOG game keeps its settings in INI files). */
#include "w32.h"
#include <dirent.h>
#include "edition.h"

typedef struct RVal { char name[128]; uint32_t type, size; uint8_t *data; } RVal;
typedef struct RKey { char path[512]; RVal *v; int nv; } RKey;
static RKey *keys[512]; static int nkeys;
static pthread_mutex_t rlock = PTHREAD_MUTEX_INITIALIZER;
typedef struct RHandle { RKey *k; } RHandle;

static const char *root_name(uint32_t h)
{
    switch (h) { case 0x80000000u: return "hkcr"; case 0x80000001u: return "hkcu"; case 0x80000002u: return "hklm"; case 0x80000003u: return "hku"; }
    return 0;
}
static RKey *key_of(uint32_t h);
static int full_path(uint32_t parent, const char *sub, char *out, size_t cap)
{
    const char *r = root_name(parent); RKey *pk = r ? 0 : key_of(parent);
    if (!r && !pk) return -1;
    snprintf(out, cap, "%s%s%s", r ? r : pk->path, sub && *sub ? "\\" : "", sub ? sub : "");
    for (char *s = out; *s; s++) { if (*s >= 'A' && *s <= 'Z') *s += 32; if (*s == '/') *s = '\\'; }
    size_t l = strlen(out); while (l && out[l - 1] == '\\') out[--l] = 0;
    return 0;
}
static RKey *lookup(const char *path, int create)
{
    for (int k = 0; k < nkeys; k++) if (!strcmp(keys[k]->path, path)) return keys[k];
    if (!create || nkeys == 512) return 0;
    RKey *k = calloc(1, sizeof *k); snprintf(k->path, sizeof k->path, "%s", path); keys[nkeys++] = k;
    return k;
}
/* a string value put in place before the game looks (install-time values; W32_REGISTRY="key|name|value;..." in tests) */
/* a value of any type (REG_SZ 1, REG_BINARY 3, REG_DWORD 4); keep: leave one that is already there */
static void reg_value(const char *path, const char *name, uint32_t type, const void *data, uint32_t size, int keep)
{
    char p[512]; snprintf(p, sizeof p, "%s", path);
    for (char *q = p; *q; q++) { if (*q >= 'A' && *q <= 'Z') *q += 32; if (*q == '/') *q = '\\'; }
    pthread_mutex_lock(&rlock);
    RKey *k = lookup(p, 1);
    if (k) {
        RVal *v = 0; for (int j = 0; j < k->nv; j++) if (!strcasecmp(k->v[j].name, name)) v = &k->v[j];
        if (v && keep) { pthread_mutex_unlock(&rlock); return; }
        if (!v) { k->v = realloc(k->v, (size_t)(k->nv + 1) * sizeof *k->v); v = &k->v[k->nv++]; memset(v, 0, sizeof *v); snprintf(v->name, sizeof v->name, "%s", name); }
        free(v->data); v->type = type; v->size = size; v->data = malloc(size ? size : 1); memcpy(v->data, data, size);
    }
    pthread_mutex_unlock(&rlock);
}
void w32_reg_string(const char *path, const char *name, const char *str) { reg_value(path, name, 1, str, (uint32_t)strlen(str) + 1, 0); }
/* The registry values a GOG installer writes (goggame-<id>.script in the game folder: "setRegistry" actions), so the
 * game finds what it would after GOG's setup. The GOG Resident Evil games keep their settings there (Resident Evil 2's
 * "Install Flag" carries two values GOG's fixes divide by); values the game has written since are kept. */
static const char *json_field(const char *obj, const char *end, const char *name, char *out, size_t cap)
{
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", name);
    const char *q = strstr(obj, pat); if (!q || q > end) return 0;
    q = strchr(q + strlen(pat), ':'); if (!q) return 0;
    q = strchr(q, '"'); if (!q || q > end) return 0;
    size_t n = 0; for (q++; *q && *q != '"' && n + 1 < cap; q++) { if (*q == '\\' && q[1]) q++; out[n++] = *q; }
    out[n] = 0; return out;
}
static int b64(char ch) { return ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch >= 'a' && ch <= 'z' ? ch - 'a' + 26 : ch >= '0' && ch <= '9' ? ch - '0' + 52 : ch == '+' ? 62 : ch == '/' ? 63 : -1; }
static void gog_script_registry(const char *game_dir, const char *app_win)
{
    char pat[1200]; snprintf(pat, sizeof pat, "%s", game_dir);
    DIR *d = opendir(pat); if (!d) return;
    struct dirent *e; char path[1400] = "";
    while ((e = readdir(d))) if (!strncasecmp(e->d_name, "goggame-", 8) && strlen(e->d_name) > 7 && !strcasecmp(e->d_name + strlen(e->d_name) - 7, ".script")) { snprintf(path, sizeof path, "%s/%s", game_dir, e->d_name); break; }
    closedir(d);
    FILE *f = *path ? fopen(path, "rb") : 0; if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *js = malloc((size_t)n + 1); if (!js || fread(js, 1, (size_t)n, f) != (size_t)n) { free(js); fclose(f); return; }
    js[n] = 0; fclose(f); int count = 0;
    for (const char *a = strstr(js, "\"setRegistry\""); a; a = strstr(a + 1, "\"setRegistry\"")) {
        /* this action's fields: up to the next action ("{app}" in a value holds a brace of its own) */
        const char *end = strstr(a + 1, "\"setRegistry\""); if (!end) end = js + n;
        char root[64], sub[512], data[4096], name[256], type[32];
        if (!json_field(a, end, "root", root, sizeof root) || !json_field(a, end, "subkey", sub, sizeof sub) || !json_field(a, end, "valueName", name, sizeof name) ||
            !json_field(a, end, "valueType", type, sizeof type) || !json_field(a, end, "valueData", data, sizeof data)) continue;
        { const char *skip = getenv("W32_GOGREG_SKIP"); if (skip && strstr(skip, name)) continue; }   /* development: leave these out */
        char key[700]; snprintf(key, sizeof key, "%s\\%s", !strcasecmp(root, "HKEY_LOCAL_MACHINE") ? "hklm" : "hkcu", sub);
        if (!strcasecmp(type, "string")) {
            char v[4600] = ""; const char *s2 = data; size_t o = 0;
            while (*s2 && o + 1 < sizeof v) { if (!strncmp(s2, "{app}", 5)) { o += (size_t)snprintf(v + o, sizeof v - o, "%s", app_win); s2 += 5; if (o >= sizeof v) o = sizeof v - 1; } else v[o++] = *s2++; }
            v[o] = 0; reg_value(key, name, 1, v, (uint32_t)strlen(v) + 1, 1);
        } else if (!strcasecmp(type, "dword")) {
            uint32_t v = (uint32_t)strtoul(data + (data[0] == '$'), 0, data[0] == '$' ? 16 : 10);
            { const char *m = getenv("W32_GOGREG_AND"); if (m && !strcasecmp(name, "Install Flag")) v &= (uint32_t)strtoul(m, 0, 16); }   /* development */
            reg_value(key, name, 4, &v, 4, 1);
        } else if (!strcasecmp(type, "binary")) {
            uint8_t b[3072]; uint32_t nb = 0, acc = 0; int bits = 0;
            for (const char *s2 = data; *s2 && nb < sizeof b; s2++) { int x = b64(*s2); if (x < 0) continue; acc = acc << 6 | (uint32_t)x; bits += 6; if (bits >= 8) { bits -= 8; b[nb++] = (uint8_t)(acc >> bits); } }
            reg_value(key, name, 3, b, nb, 1);
        } else continue;
        count++;
    }
    free(js);
    if (count) fprintf(stderr, "w32: %d registry values from %s\n", count, path);
}
/* What the game's installer would have written. The GOG and Steam releases never run that installer, so these are
 * filled in from the data folder at start-up:
 *   cdkey-hash.txt  DoubleHash, the 8-byte value the retail installer derives from the player's own product key
 *                   (install.sh --cd-key computes it the way the installer does). The game shows it to GameSpy (now
 *                   OpenSpy) as its CD key for multiplayer; without it multiplayer stops at "Could not find a valid
 *                   CDKey". Only the hash is kept, never the key. */
void w32_registry_gog(const char *game_dir)                 /* (main.m, with the host's game folder) */
{
#ifdef RE_EDITION
    /* Windows' own install paths (Resident Evil 2 reads ProgramFilesDir at start: "Reg Open Error" without it) */
    w32_reg_string("hklm\\software\\microsoft\\windows\\currentversion", "ProgramFilesDir", "C:\\Program Files");
    w32_reg_string("hklm\\software\\microsoft\\windows\\currentversion", "CommonFilesDir", "C:\\Program Files\\Common Files");
    gog_script_registry(game_dir, GAME_DIR_WIN);
#ifdef RE2_EDITION
    /* what Capcom's own installer recorded and GOG's leaves out: with the key there but no path, the game finds none
     * of its data (and stops with "Failed to initialize DIRECTX") */
    reg_value("hkcu\\software\\capcom\\resident evil2", "Install Path", 1, GAME_DIR_WIN "\\", (uint32_t)sizeof GAME_DIR_WIN + 1, 1);
    reg_value("hkcu\\software\\capcom\\resident evil2", "Install Path2", 1, GAME_DIR_WIN "\\", (uint32_t)sizeof GAME_DIR_WIN + 1, 1);
#endif
#else
    (void)game_dir;
#endif
}
void w32_registry_install(const char *data_dir)
{
    /* the user's Documents, as Windows records it (GTA III keeps "GTA3 User Files" there: settings, saves; without the
     * value it falls back to its own data folder) */
    w32_reg_string("hkcu\\software\\microsoft\\windows\\currentversion\\explorer\\shell folders", "Personal", "C:\\Users\\player\\Documents");
    w32_reg_string("hkcu\\software\\microsoft\\windows\\currentversion\\explorer\\user shell folders", "Personal", "C:\\Users\\player\\Documents");
#ifdef FNV_EDITION
    /* where Fallout NV (and its tools) look for the installation; the virtual game folder is the same for every game */
    w32_reg_string("hklm\\software\\bethesda softworks\\falloutnv", "Installed Path", "C:\\GOG Games\\Dungeon Siege\\");
    w32_reg_string("hklm\\software\\wow6432node\\bethesda softworks\\falloutnv", "Installed Path", "C:\\GOG Games\\Dungeon Siege\\");
#endif
#ifdef DS2_BROKEN_WORLD
    /* BW loads the original campaign resources from this installer location.
     * The native asset view contains both sets of files under the same virtual
     * game directory. No host registry or Windows installation is required. */
    const char *install_keys[] = {
        "hklm\\software\\microsoft\\microsoft games\\dungeonsiege2",
        "hklm\\software\\2k games\\dungeon siege 2 broken world"
    };
    for (unsigned i = 0; i < sizeof install_keys / sizeof install_keys[0]; i++) {
        w32_reg_string(install_keys[i], "AppPath", "C:\\GOG Games\\Dungeon Siege");
        w32_reg_string(install_keys[i], "InstallationDirectory", "C:\\GOG Games\\Dungeon Siege");
    }
#endif
    char p[1200], hex[64] = ""; snprintf(p, sizeof p, "%s/cdkey-hash.txt", data_dir);
    FILE *f = fopen(p, "r"); if (!f) return;
    if (!fgets(hex, sizeof hex, f)) hex[0] = 0;
    fclose(f);
    uint8_t b[8]; int n = 0;
    for (const char *q = hex; n < 8 && q[0] && q[1]; q += 2) { unsigned v; if (sscanf(q, "%2x", &v) != 1) break; b[n++] = (uint8_t)v; }
    if (n != 8) { fprintf(stderr, "w32: %s: expected 16 hex digits\n", p); return; }
    pthread_mutex_lock(&rlock);
    RKey *k = lookup("hklm\\software\\microsoft\\microsoft games\\dungeonsiege2", 1);
    if (k) {
        RVal *v = 0; for (int j = 0; j < k->nv; j++) if (!strcasecmp(k->v[j].name, "DoubleHash")) v = &k->v[j];
        if (!v) { k->v = realloc(k->v, (size_t)(k->nv + 1) * sizeof *k->v); v = &k->v[k->nv++]; memset(v, 0, sizeof *v); snprintf(v->name, sizeof v->name, "DoubleHash"); }
        free(v->data); v->type = 3; v->size = 8; v->data = malloc(8); memcpy(v->data, b, 8);   /* REG_BINARY */
    }
    pthread_mutex_unlock(&rlock);
}
static void reg_seed(void)
{
    static int done; if (done) return; done = 1;
    const char *e = getenv("W32_REGISTRY"); if (!e || !getenv("DS_TEST")) return;
    char *s = strdup(e), *save, *t = strtok_r(s, ";", &save);
    for (; t; t = strtok_r(0, ";", &save)) { char *a = strchr(t, '|'), *b = a ? strchr(a + 1, '|') : 0; if (!b) continue; *a = *b = 0; w32_reg_string(t, a + 1, b + 1); }
    free(s);
}
#define HREG_BASE 0x00a00000u
static RKey *rh[256];
static RKey *key_of(uint32_t h) { uint32_t i = (h - HREG_BASE) / 4; return (h >= HREG_BASE && i < 256) ? rh[i] : 0; }
static uint32_t new_handle(RKey *k) { for (int i = 1; i < 256; i++) if (!rh[i]) { rh[i] = k; return HREG_BASE + 4 * (uint32_t)i; } return 0; }

static uint32_t reg_open(uint32_t parent, uint32_t subkey, uint32_t out)
{
    char p[1024] = "";
    reg_seed();
    pthread_mutex_lock(&rlock);
    RKey *k = full_path(parent, subkey ? GS(subkey) : "", p, sizeof p) ? 0 : lookup(p, 0);
    uint32_t h = k ? new_handle(k) : 0;
    pthread_mutex_unlock(&rlock);
    if (!k) { if (getenv("W32_REGLOG")) fprintf(stderr, "w32: registry key %s not found\n", p); return 2; }   /* ERROR_FILE_NOT_FOUND */
    rt_w32(G_MEM, out, h); return 0;
}
IMPL(advapi32, RegOpenKeyExA) { RET(reg_open(ARG(0), ARG(1), ARG(4)), 5); }
IMPL(advapi32, RegOpenKeyA) { RET(reg_open(ARG(0), ARG(1), ARG(2)), 3); }
IMPL(advapi32, RegCreateKeyExA)
{
    char p[1024]; uint32_t out = ARG(7), disp = ARG(8);
    pthread_mutex_lock(&rlock);
    int existed = 0; RKey *k = 0;
    if (!full_path(ARG(0), GS(ARG(1)), p, sizeof p)) { existed = lookup(p, 0) != 0; k = lookup(p, 1); }
    uint32_t h = k ? new_handle(k) : 0;
    pthread_mutex_unlock(&rlock);
    if (!k) RET(2, 9);
    rt_w32(G_MEM, out, h); if (disp) rt_w32(G_MEM, disp, existed ? 2 : 1);
    RET(0, 9);
}
IMPL(advapi32, RegCloseKey) { uint32_t i = (ARG(0) - HREG_BASE) / 4; if (ARG(0) >= HREG_BASE && i < 256) rh[i] = 0; RET(0, 1); }
IMPL(advapi32, RegQueryValueExA)
{
    uint32_t name = ARG(1), type = ARG(3), data = ARG(4), szp = ARG(5);
    RKey *k = key_of(ARG(0)); if (!k) RET(6, 6);
    const char *n = name ? GS(name) : "";
    for (int j = 0; j < k->nv; j++) if (!strcasecmp(k->v[j].name, n)) {
        RVal *v = &k->v[j];
        if (type) rt_w32(G_MEM, type, v->type);
        if (data) {
            if (!szp || rt_r32(G_MEM, szp) < v->size) { if (szp) rt_w32(G_MEM, szp, v->size); RET(234, 6); }   /* ERROR_MORE_DATA */
            memcpy(GP(data), v->data, v->size);
        }
        if (szp) rt_w32(G_MEM, szp, v->size);
        RET(0, 6);
    }
    RET(2, 6);
}
IMPL(advapi32, RegSetValueExA)
{
    uint32_t name = ARG(1), type = ARG(3), data = ARG(4), size = ARG(5);
    RKey *k = key_of(ARG(0)); if (!k) RET(6, 6);
    const char *n = name ? GS(name) : "";
    pthread_mutex_lock(&rlock);
    RVal *v = 0;
    for (int j = 0; j < k->nv; j++) if (!strcasecmp(k->v[j].name, n)) v = &k->v[j];
    if (!v) { k->v = realloc(k->v, (size_t)(k->nv + 1) * sizeof *k->v); v = &k->v[k->nv++]; memset(v, 0, sizeof *v); snprintf(v->name, sizeof v->name, "%s", n); }
    free(v->data); v->type = type; v->size = size; v->data = malloc(size ? size : 1); memcpy(v->data, GP(data), size);
    pthread_mutex_unlock(&rlock);
    RET(0, 6);
}
/* (added for Dungeon Siege II) */
static int is_child(const char *parent, const char *path, const char **name)   /* path is a direct subkey of parent */
{
    size_t l = strlen(parent);
    if (strncmp(path, parent, l) || path[l] != '\\' || strchr(path + l + 1, '\\')) return 0;
    *name = path + l + 1; return 1;
}
IMPL(advapi32, RegDeleteValueA)
{
    RKey *k = key_of(ARG(0)); if (!k) RET(6, 2);
    const char *n = ARG(1) ? GS(ARG(1)) : "";
    pthread_mutex_lock(&rlock);
    for (int j = 0; j < k->nv; j++) if (!strcasecmp(k->v[j].name, n)) {
        free(k->v[j].data); k->v[j] = k->v[--k->nv]; pthread_mutex_unlock(&rlock); RET(0, 2);
    }
    pthread_mutex_unlock(&rlock); RET(2, 2);
}
IMPL(advapi32, RegDeleteKeyA)                          /* only a key without subkeys, as on Windows */
{
    char p[1024]; const char *nm;
    pthread_mutex_lock(&rlock);
    RKey *k = full_path(ARG(0), GS(ARG(1)), p, sizeof p) ? 0 : lookup(p, 0);
    if (!k) { pthread_mutex_unlock(&rlock); RET(2, 2); }
    for (int j = 0; j < nkeys; j++) if (is_child(p, keys[j]->path, &nm)) { pthread_mutex_unlock(&rlock); RET(5, 2); }
    for (int j = 0; j < nkeys; j++) if (keys[j] == k) { keys[j] = keys[--nkeys]; break; }
    for (int i = 0; i < 256; i++) if (rh[i] == k) rh[i] = 0;
    for (int v = 0; v < k->nv; v++) free(k->v[v].data);
    free(k->v); free(k);
    pthread_mutex_unlock(&rlock); RET(0, 2);
}
IMPL(advapi32, RegEnumKeyExA)                          /* (h, index, name, *namelen, reserved, class, *classlen, *time) */
{
    RKey *k = key_of(ARG(0)); uint32_t idx = ARG(1), name = ARG(2), lenp = ARG(3); const char *nm;
    if (!k) RET(6, 8);
    pthread_mutex_lock(&rlock);
    uint32_t n = 0;
    for (int j = 0; j < nkeys; j++) if (is_child(k->path, keys[j]->path, &nm) && n++ == idx) {
        uint32_t l = (uint32_t)strlen(nm);
        if (rt_r32(G_MEM, lenp) <= l) { pthread_mutex_unlock(&rlock); RET(234, 8); }   /* ERROR_MORE_DATA */
        memcpy(GP(name), nm, l + 1); rt_w32(G_MEM, lenp, l);
        if (ARG(6)) rt_w32(G_MEM, ARG(6), 0);
        if (ARG(7)) { rt_w32(G_MEM, ARG(7), 0); rt_w32(G_MEM, ARG(7) + 4, 0); }
        pthread_mutex_unlock(&rlock); RET(0, 8);
    }
    pthread_mutex_unlock(&rlock); RET(259, 8);         /* ERROR_NO_MORE_ITEMS */
}
IMPL(advapi32, RegQueryInfoKeyA)                       /* (h, class, *classlen, reserved, *subkeys, *maxsubkeylen, *maxclasslen,
                                                        *values, *maxvaluenamelen, *maxvaluelen, *secdesc, *time) */
{
    RKey *k = key_of(ARG(0)); const char *nm; if (!k) RET(6, 12);
    pthread_mutex_lock(&rlock);
    uint32_t nsub = 0, maxsub = 0, maxvn = 0, maxv = 0;
    for (int j = 0; j < nkeys; j++) if (is_child(k->path, keys[j]->path, &nm)) { nsub++; if (strlen(nm) > maxsub) maxsub = (uint32_t)strlen(nm); }
    for (int v = 0; v < k->nv; v++) { if (strlen(k->v[v].name) > maxvn) maxvn = (uint32_t)strlen(k->v[v].name); if (k->v[v].size > maxv) maxv = k->v[v].size; }
    pthread_mutex_unlock(&rlock);
    uint32_t out[] = {ARG(4), nsub, ARG(5), maxsub, ARG(6), 0, ARG(7), (uint32_t)k->nv, ARG(8), maxvn, ARG(9), maxv, ARG(10), 0};
    for (int i = 0; i < 14; i += 2) if (out[i]) rt_w32(G_MEM, out[i], out[i + 1]);
    if (ARG(2)) rt_w32(G_MEM, ARG(2), 0);
    if (ARG(11)) { rt_w32(G_MEM, ARG(11), 0); rt_w32(G_MEM, ARG(11) + 4, 0); }
    RET(0, 12);
}
