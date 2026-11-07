/* Mods for Resident Evil 3 (the game and The Mercenaries). Resident Evil 1 and 2 and Dino Crisis read loose files, which
 * the file layer already takes from the enabled mods (files.c, GTA_MODS). Resident Evil 3 keeps its data packed in
 * Rofs1-15.dat (each file compressed and scrambled), read by its archive reader (the object at 0x5349dc) through the
 * reader's own methods: open(name), seek, read and close (thiscall: the reader in ecx, the arguments popped by the method).
 * The game's file loaders (through small cdecl wrappers) and its voice streams (through the sound object) both end there.
 * They are taken over here: a file the enabled mods have (at the name the game asks for, below its "bio19/" prefix, any
 * case: data_a/pld/pl00.pld, data_a/voice/m101a010.wav) is read from the mod, as plain bytes, which is what the reader
 * hands back once it has unpacked an entry. Anything else, and every file when no mod is on, goes to the game's reader.
 * The game folder is never written.
 *   the reader's methods (both programs): open 0x4214c0 (name, 0, 0: a handle or -1), close 0x414f10 (handle),
 *   read 0x414f70 (handle, buffer, length: the bytes read), seek 0x414fd0 (handle, offset, whence: the new position)
 *   the wrappers (cdecl, the same arguments; the archive dump below calls them): Resident Evil 3 open 0x507b00, seek
 *   0x507b20, read 0x507b40, close 0x507b60; The Mercenaries 0x507b70, 0x507b90, 0x507bb0, 0x507bd0
 * Development: W32_ROFSLOG=1 logs every open (and where a mod's file came from), seek and read. W32_ROFSDUMP=<folder> with
 * W32_ROFSDUMP_LIST=<file of names, one a line, as the game asks: /data_a/voice/M101A010.WAV>: at the game's first
 * archive open, each listed file is read through the game's own reader (unpacked) and saved below <folder> (the disc
 * import compares the PC's voices with the PlayStation's: tools/psx_import.py). W32_ROFSVOICE=<M101A010.WAV> (Resident
 * Evil 3): that voice line started at the first archive open, as the game's scenes start one. */
#include "w32.h"
#include "edition.h"
#ifdef RE3_EDITION
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef RE3_MERCE
enum { F_OPEN = 0x00507b70u, F_SEEK = 0x00507b90u, F_READ = 0x00507bb0u, F_CLOSE = 0x00507bd0u };
#else
enum { F_OPEN = 0x00507b00u, F_SEEK = 0x00507b20u, F_READ = 0x00507b40u, F_CLOSE = 0x00507b60u };
#endif
#ifndef RE3_MERCE
enum { VOICE_OPEN = 0x00506e70u, VOICE_PLAY = 0x00506e90u };   /* the sound object's stream open (channel, name) and play (channel) */
#endif
enum { M_OPEN = 0x004214c0u, M_CLOSE = 0x00414f10u, M_READ = 0x00414f70u, M_SEEK = 0x00414fd0u };   /* the reader's methods */
#define FAKE 0x4d4f0000u                         /* a mod file's handle: FAKE + slot (the game's own are small numbers) */
#define NSLOT 64
int w32_mod_count(void);
const char *w32_mod_dir(int k);

static int rlog(void) { static int on = -1; if (on < 0) on = getenv("W32_ROFSLOG") != 0; return on; }
static int fds[NSLOT]; static int fds_ready;
static pthread_mutex_t sm = PTHREAD_MUTEX_INITIALIZER;

/* rel (a/b/c, '/' separated) below base, each name in any case; 0 and the host path when it is a file */
static int resolve_ci(const char *base, const char *rel, char *out, size_t cap)
{
    char cur[2048], tmp[1024]; snprintf(cur, sizeof cur, "%s", base); snprintf(tmp, sizeof tmp, "%s", rel);
    char *save, *tok = strtok_r(tmp, "/", &save);
    while (tok) {
        char next[2048]; snprintf(next, sizeof next, "%s/%s", cur, tok);
        struct stat st;
        if (stat(next, &st)) {                   /* not in that case: look for it */
            DIR *d = opendir(cur); if (!d) return -1;
            struct dirent *e; int found = 0;
            while ((e = readdir(d))) if (!strcasecmp(e->d_name, tok)) { snprintf(next, sizeof next, "%s/%s", cur, e->d_name); found = 1; break; }
            closedir(d); if (!found) return -1;
        }
        snprintf(cur, sizeof cur, "%s", next); tok = strtok_r(0, "/", &save);
    }
    struct stat st; if (stat(cur, &st) || !S_ISREG(st.st_mode)) return -1;
    snprintf(out, cap, "%s", cur); return 0;
}
/* the game's name for a file ("Bio19/Data_a/pld/PL00.PLD", "d:/bio19/...") as a path below a mod: data_a/pld/PL00.PLD */
static void mod_rel(const char *name, char *out, size_t cap)
{
    char t[1024]; size_t k = 0;
    for (const char *s = name; *s && k + 1 < sizeof t; s++) t[k++] = *s == '\\' ? '/' : *s;
    t[k] = 0;
    const char *p = t;
    if (((p[0] | 32) >= 'a' && (p[0] | 32) <= 'z') && p[1] == ':') p += 2;
    while (*p == '/' || *p == '.') { if (p[0] == '.' && p[1] != '/') break; p++; }
    if (!strncasecmp(p, "bio19/", 6)) p += 6;
    snprintf(out, cap, "%s", p);
}

/* a cdecl call into the game from an override: its arguments pushed, a return address that ends the call, the caller's
 * registers as they were afterwards */
static uint32_t gcall(Ctx *c, uint32_t fn, int n, const uint32_t *args)
{
    Ctx save = *c;
    for (int k = n - 1; k >= 0; k--) { c->esp -= 4; rt_w32(G_MEM, c->esp, args[k]); }
    c->esp -= 4; rt_w32(G_MEM, c->esp, 0xffeff000u);
    rt_call(c, fn);
    uint32_t r = c->eax; *c = save; return r;
}
static int dumping;
static void dump_all(Ctx *c)
{
    const char *dir = getenv("W32_ROFSDUMP"), *list = getenv("W32_ROFSDUMP_LIST");
    int lf = list ? open(list, O_RDONLY) : -1; if (!dir || lf < 0) { if (lf >= 0) close(lf); return; }
    static char names[1 << 20]; ssize_t got_n = read(lf, names, sizeof names - 1); close(lf);
    names[got_n > 0 ? got_n : 0] = 0;
    mkdir(dir, 0755);
    dumping = 1; int n = 0, bad = 0;
    uint32_t gname = heap_alloc(w32_process_heap, 0, 512);
    for (char *line = strtok(names, "\r\n"); line; line = strtok(0, "\r\n")) {
        if (!*line || strlen(line) > 500) continue;
        memcpy(GP(gname), line, strlen(line) + 1);
        uint32_t a[3] = {gname, 0, 0}, h = gcall(c, F_OPEN, 3, a);
        if (h == 0xffffffffu) { bad++; continue; }
        uint32_t sk[3] = {h, 0, 2}, size = gcall(c, F_SEEK, 3, sk); sk[2] = 0; gcall(c, F_SEEK, 3, sk);
        uint32_t buf = size && size < (64u << 20) ? heap_alloc(w32_process_heap, 0, size) : 0;
        if (rlog() || !buf) fprintf(stderr, "re: archive dump %s: handle %x, size %u\n", line, h, size);
        if (buf) {
            uint32_t rd[3] = {h, buf, size}, got = gcall(c, F_READ, 3, rd);
            char out[2048]; snprintf(out, sizeof out, "%s/%s", dir, line[0] == '/' ? line + 1 : line);
            for (char *q = out + strlen(dir) + 1; *q; q++) if (*q == '/') { *q = 0; mkdir(out, 0755); *q = '/'; }
            FILE *o = fopen(out, "wb"); if (o) { fwrite(GP(buf), 1, got, o); fclose(o); n++; }
            heap_free(w32_process_heap, buf);
        }
        uint32_t cl[1] = {h}; gcall(c, F_CLOSE, 1, cl);
    }
    heap_free(w32_process_heap, gname); dumping = 0;
    fprintf(stderr, "re: archive dump: %d files to %s (%d not found)\n", n, dir, bad);
}

int re_mods_override(Ctx *c, uint32_t addr)
{
    if (addr != M_OPEN && addr != M_SEEK && addr != M_READ && addr != M_CLOSE) return 0;
    uint32_t pops = 4 + (addr == M_CLOSE ? 4 : 12);   /* the return address and the arguments (thiscall: the method pops them) */
    uint32_t a1 = rt_r32(G_MEM, c->esp + 4), a2 = rt_r32(G_MEM, c->esp + 8), a3 = rt_r32(G_MEM, c->esp + 12);
    pthread_mutex_lock(&sm);
    if (!fds_ready) { for (int k = 0; k < NSLOT; k++) fds[k] = -1; fds_ready = 1; }
    pthread_mutex_unlock(&sm);
    if (dumping) return 0;                       /* the dump's own calls: the game's reader */
    if (addr == M_OPEN) {
        static int dumped; if (!dumped) { dumped = 1; dump_all(c);
#ifndef RE3_MERCE
            const char *vt_ = getenv("W32_ROFSVOICE");      /* development: a voice line started as the game's scenes do
                                                               (0x427252: the sound object's stream open, then play) */
            if (vt_) { uint32_t gn = heap_alloc(w32_process_heap, 0, 256); snprintf((char *)GP(gn), 256, "/data_a/voice/%s", vt_);
                       dumping = 0; uint32_t a2[2] = {0, gn}; gcall(c, VOICE_OPEN, 2, a2); uint32_t a1[1] = {0}; gcall(c, VOICE_PLAY, 1, a1);
                       fprintf(stderr, "re: voice test %s started\n", vt_); }
#endif
            if (rlog()) { uint32_t o = rt_r32(G_MEM, 0x5349dcu), vt = o ? rt_r32(G_MEM, o) : 0;
                          fprintf(stderr, "rofs: reader %x vtable %x: open %x close %x read %x seek %x\n", o, vt, vt ? rt_r32(G_MEM, vt + 0x20) : 0,
                                  vt ? rt_r32(G_MEM, vt + 0x24) : 0, vt ? rt_r32(G_MEM, vt + 0x28) : 0, vt ? rt_r32(G_MEM, vt + 0x30) : 0);
                          uint32_t so = rt_r32(G_MEM, 0x5349e0u), sv = so ? rt_r32(G_MEM, so) : 0;
                          fprintf(stderr, "rofs: sound %x vtable %x: +18 %x\n", so, sv, sv ? rt_r32(G_MEM, sv + 0x18) : 0); } }
        const char *name = GS(a1);
        int n = w32_mod_count();
        if (rlog()) fprintf(stderr, "rofs: open %s\n", name);
        if (!n) return 0;
        char rel[1024], path[2048]; mod_rel(name, rel, sizeof rel);
        for (int m = 0; m < n; m++) {            /* the enabled mods, first wins (as the bio19 tree, or with bio19/ kept) */
            char alt[1100]; snprintf(alt, sizeof alt, "bio19/%s", rel);
            if (resolve_ci(w32_mod_dir(m), rel, path, sizeof path) && resolve_ci(w32_mod_dir(m), alt, path, sizeof path)) continue;
            int fd = open(path, O_RDONLY); if (fd < 0) continue;
            pthread_mutex_lock(&sm);
            int k = 0; while (k < NSLOT && fds[k] >= 0) k++;
            if (k < NSLOT) fds[k] = fd;
            pthread_mutex_unlock(&sm);
            if (k == NSLOT) { close(fd); return 0; }
            fprintf(stderr, "re: mod file %s <- %s\n", name, path);
            c->eax = FAKE + (uint32_t)k; c->esp += pops; return 1;
        }
        return 0;
    }
    if ((a1 & 0xffff0000u) != FAKE) {            /* the game's own file */
        if (rlog()) fprintf(stderr, "rofs: %s %x %x %x\n", addr == M_SEEK ? "seek" : addr == M_READ ? "read" : "close", a1, a2, a3);
        return 0;
    }
    uint32_t k = a1 & 0xffffu; int fd = k < NSLOT ? fds[k] : -1;
    if (fd < 0) { c->eax = 0xffffffffu; c->esp += pops; return 1; }
    if (addr == M_SEEK) {
        off_t r = lseek(fd, (off_t)(int32_t)a2, (int)a3); c->eax = r < 0 ? 0xffffffffu : (uint32_t)r;
    } else if (addr == M_READ) {
        uint32_t got = 0;
        while (got < a3) { ssize_t r = read(fd, GP(a2 + got), a3 - got); if (r <= 0) break; got += (uint32_t)r; }
        c->eax = got;
        if (rlog()) fprintf(stderr, "rofs: read mod %u: %u of %u\n", k, got, a3);
    } else {
        close(fd); pthread_mutex_lock(&sm); fds[k] = -1; pthread_mutex_unlock(&sm); c->eax = 0;
    }
    c->esp += pops; return 1;
}
#endif
