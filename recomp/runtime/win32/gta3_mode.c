/* The launch window's Resolution, GTA III and San Andreas. psSelectDevice takes the video mode saved in the settings file
 * (the menu's m_nPrefsVideoMode: GTA III 0x906c10 in gta3.set, San Andreas 0xb880c8 in gta_sa.set) when it is a usable
 * one, else looks for 640x480 (GTA III) or 800x600 (San Andreas). GTA_MODE=WxH (the launchers) puts RenderWare's index
 * of that 32-bit full-screen mode there first (tools/lift.py GTA3_HOOKS / GTASA_HOOKS), so the game opens at it and its
 * Display Setup shows it; without the variable (or empty) the saved choice stands ("As Set in Game"). */
#include "w32.h"
#include "edition.h"

#ifdef GTA_EDITION
#if defined(GTA3_EDITION) || defined(GTASA_EDITION) || defined(GTAVC_EDITION)
#ifdef GTA3_EDITION
enum { PICK_MODE = 0x005829f8u,             /* psSelectDevice: just before the saved mode is read */
       RW_NUM_MODES = 0x005a2460u,          /* RwEngineGetNumVideoModes(void) */
       RW_MODE_INFO = 0x005a24d0u,          /* RwEngineGetVideoModeInfo(RwVideoMode *, index): width, height, depth, flags */
       PREFS_MODE = 0x00906c10u };
#elif defined(GTAVC_EDITION)
enum { PICK_MODE = 0x00600a95u, RW_NUM_MODES = 0x00641af0u, RW_MODE_INFO = 0x00641b20u, PREFS_MODE = 0x00868698u };   /* Vice City (Steam) */
#else
enum { PICK_MODE = 0x0074daecu, RW_NUM_MODES = 0x007fa680u, RW_MODE_INFO = 0x007fa6b0u, PREFS_MODE = 0x00b880c8u };   /* San Andreas 3.0 */
#endif

/* a cdecl guest call from a hook: arguments, a return address the callee's ret pops, the registers kept */
static uint32_t guest_call(Ctx *c, uint32_t fn, int n, const uint32_t *args)
{
    uint32_t r[8] = {c->eax, c->ecx, c->edx, c->ebx, c->esp, c->ebp, c->esi, c->edi};
    for (int k = n - 1; k >= 0; k--) { c->esp -= 4; rt_w32(G_MEM, c->esp, args[k]); }
    c->esp -= 4; rt_w32(G_MEM, c->esp, 0);
    rt_call(c, fn);
    uint32_t v = c->eax;
    c->eax = r[0]; c->ecx = r[1]; c->edx = r[2]; c->ebx = r[3]; c->esp = r[4]; c->ebp = r[5]; c->esi = r[6]; c->edi = r[7];
    return v;
}

static void pick_mode(Ctx *c)
{
    static int done; const char *e = getenv("GTA_MODE"); unsigned w, h;
    if (done || !e || sscanf(e, "%ux%u", &w, &h) != 2) return;
    done = 1;
    uint32_t buf = (c->esp - 0x400) & ~15u, n = guest_call(c, RW_NUM_MODES, 0, 0);   /* scratch well below the frame */
    for (uint32_t i = 1; i < n && i < 512; i++) {
        uint32_t a[2] = {buf, i}; guest_call(c, RW_MODE_INFO, 2, a);
        if (rt_r32(G_MEM, buf) == w && rt_r32(G_MEM, buf + 4) == h && rt_r32(G_MEM, buf + 8) == 32 && (rt_r32(G_MEM, buf + 12) & 1)) {
            fprintf(stderr, "gta3: video mode %ux%u (RenderWare mode %u, was %u)\n", w, h, i, rt_r32(G_MEM, PREFS_MODE));
            rt_w32(G_MEM, PREFS_MODE, i); return;
        }
    }
    fprintf(stderr, "gta3: no %ux%u video mode: the game's own choice\n", w, h);
}

/* CdStreamSync's wait for a streaming read (San Andreas 3.0, 0x407a70; the same code in GTA III and Vice City). The game checks the channel's sector count, then
 * sets its "locked" flag and waits on the channel's semaphore; the streaming thread (0x407b70) clears the count, then
 * releases the semaphore only if the flag is set. Nothing orders the two: when the thread finishes between the game's
 * check and its flag (reads here take microseconds, not a disc's milliseconds), nobody releases the semaphore and the
 * game waits for ever (hang report: main thread in 0x407a70, streaming thread idle in 0x407b70). The hook waits a
 * millisecond at a time until the semaphore is released or the thread is done with the channel (count 0, and its "in
 * use" flag, cleared after the release step, 0); the game's own wait then has a timeout of 0 and only takes a release
 * that came in between. Channel (esi): +4 sectors to read, +0xd locked, +0xe in use, +0x14 semaphore. */
/* call WaitForSingleObject(semaphore, INFINITE): [esp] handle, [esp + 4] timeout; the channel in esi (SA) or ebx */
#if defined(GTA3_EDITION)
enum { CD_SYNC_WAIT = 0x0040604au };
#define CD_CHANNEL(c) ((c)->ebx)
#elif defined(GTAVC_EDITION)
enum { CD_SYNC_WAIT = 0x0040840au };
#define CD_CHANNEL(c) ((c)->ebx)
#else
enum { CD_SYNC_WAIT = 0x00407a9eu };
#define CD_CHANNEL(c) ((c)->esi)
#endif
int w32_wait_one(Ctx *c, uint32_t h, uint32_t ms);
static void cd_sync_wait(Ctx *c)
{
    uint32_t ch = CD_CHANNEL(c), sema = rt_r32(G_MEM, c->esp);
    if (rt_r32(G_MEM, c->esp + 4) != 0xffffffffu || sema != rt_r32(G_MEM, ch + 0x14)) return;   /* not the expected call */
    for (;;) {
        if (w32_wait_one(c, sema, 1) == 0) break;
        uint32_t left = __atomic_load_n((uint32_t *)(G_MEM + ch + 4), __ATOMIC_ACQUIRE);
        uint8_t busy = __atomic_load_n(G_MEM + ch + 0xe, __ATOMIC_ACQUIRE);
        if (!left && !busy) {
            static int n; if (n++ < 5) fprintf(stderr, "gta: streaming read on channel %08x finished without a release (the CdStreamSync race): not waiting\n", ch);
            break;
        }
    }
    rt_w32(G_MEM, c->esp + 4, 0);
}

int gta3_hook(Ctx *c, uint32_t addr)
{
    if (addr == PICK_MODE) { pick_mode(c); return 1; }
    if (addr == CD_SYNC_WAIT) { cd_sync_wait(c); return 1; }
    return 0;
}
#endif
#endif
