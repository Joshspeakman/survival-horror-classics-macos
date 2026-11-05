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

/* ---- Widescreen: Hor+ (the launchers' Widescreen option; GTA_WIDESCREEN=0 turns it off) ----
 * The games were made for 4:3: the camera's view window is tan(fov/2) wide and that over 4/3 tall, so a wider screen
 * either stretches the picture or (the games' own widescreen settings) crops its top and bottom. Here the 3D view keeps
 * the 4:3 view's height and gets wider with the screen, as FusionFix does (ThirteenAG, MIT; its patterns find the same
 * code in these builds):
 *   CameraSize (RenderWare skeleton: camera, rect, view window, aspect): for a camera that draws the whole screen, the
 *     view window is widened by aspect / (4/3) and the aspect is the screen's;
 *   CCamera::CalculateDerivedValues: the frustum's half angle (what is culled at the sides) widened the same way;
 *   the aspect the aiming code uses (Find3rdPersonCamTargetVector: III's constant, CDraw::ms_fAspectRatio in VC and SA)
 *     is the screen's. */
int w32_d3d_size(int *w, int *h);   /* d3d9.c: the back buffer */
#include <math.h>
#if defined(GTA3_EDITION)
enum { CAMERA_SIZE = 0x00527b00u, FRUSTUM_HALF = 0x0046f4c9u };
static const uint32_t aim_aspect[] = {0x005fe9c0u, 0x005fe9bcu};   /* Find3rdPersonCamTargetVector's 4/3 and 16/9 */
#elif defined(GTAVC_EDITION)
enum { CAMERA_SIZE = 0x00580690u, FRUSTUM_HALF = 0x0046c2dcu, VC_ASPECT_RET = 0x0054a1bau, VC_ASPECT = 0x0094cd40u };
#else
enum { CAMERA_SIZE = 0x007389a0u, FRUSTUM_HALF = 0x00518350u, SA_ASPECT = 0x00c24058u };
static const uint32_t sa_aspect_ret[] = {0x0070b0aau, 0x0070b0bbu, 0x0070b0c8u};   /* CDraw::CalculateAspectRatio's three returns */
#endif
static int wide_on(void) { static int on = -1; if (on < 0) { const char *e = getenv("GTA_WIDESCREEN"); on = !(e && *e == '0'); } return on; }
static double screen_aspect(void)   /* the screen's width over height; 4/3 when off or before there is a device */
{
    int w, h; if (!wide_on() || !w32_d3d_size(&w, &h)) return 4.0 / 3; return (double)w / h;
}
static int wide(double a) { return fabs(a - 4.0 / 3) > 1e-3; }
static float rdf(uint32_t a) { uint32_t v = rt_r32(G_MEM, a); float f; memcpy(&f, &v, 4); return f; }
static void wrf(uint32_t a, float f) { uint32_t v; memcpy(&v, &f, 4); rt_w32(G_MEM, a, v); }

static void camera_size(Ctx *c)   /* at CameraSize's entry: [esp+4] camera, +8 rect, +12 view window, +16 aspect */
{
    double a = screen_aspect(); int w, h;
#ifdef GTA3_EDITION
    for (unsigned k = 0; k < sizeof aim_aspect / sizeof *aim_aspect; k++) wrf(aim_aspect[k], (float)(wide(a) ? a : k ? 16.0 / 9 : 4.0 / 3));
#endif
    if (!wide(a) || !w32_d3d_size(&w, &h)) return;
    uint32_t cam = rt_r32(G_MEM, c->esp + 4), rect = rt_r32(G_MEM, c->esp + 8), ras = cam ? rt_r32(G_MEM, cam + 0x60) : 0;
    if (!ras || (int)rt_r32(G_MEM, ras + 0xc) != w || (int)rt_r32(G_MEM, ras + 0x10) != h) return;   /* not the screen's camera */
    if (rect && ((int)rt_r32(G_MEM, rect + 8) != w || (int)rt_r32(G_MEM, rect + 12) != h)) return;
    float vw = rdf(c->esp + 12); if (!(vw > 0)) return;
    wrf(c->esp + 12, (float)(vw * a * 0.75)); wrf(c->esp + 16, (float)a);
}
static void frustum_half(Ctx *c)   /* st(0): the half angle across the view (radians), from the 4:3 field of view */
{
    double a = screen_aspect(); if (wide(a)) ST(0) = atan(tan(ST(0)) * a * 0.75);
}

int gta3_hook(Ctx *c, uint32_t addr)
{
    if (addr == CAMERA_SIZE) { camera_size(c); return 1; }
    if (addr == FRUSTUM_HALF) { frustum_half(c); return 1; }
#if defined(GTAVC_EDITION)
    if (addr == VC_ASPECT_RET) { double a = screen_aspect(); if (wide(a)) wrf(VC_ASPECT, (float)a); return 1; }   /* fld [ms_fAspectRatio]; ret */
#elif defined(GTASA_EDITION)
    for (unsigned k = 0; k < 3; k++) if (addr == sa_aspect_ret[k]) {   /* st(0) and ms_fAspectRatio: the screen's */
        double a = screen_aspect(); if (wide(a)) { ST(0) = (double)(float)a; wrf(SA_ASPECT, (float)a); } return 1;
    }
#endif
    if (addr == PICK_MODE) { pick_mode(c); return 1; }
    if (addr == CD_SYNC_WAIT) { cd_sync_wait(c); return 1; }
    return 0;
}
#endif
#endif
