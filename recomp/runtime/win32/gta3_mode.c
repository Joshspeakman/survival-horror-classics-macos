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

/* ---- Frame rate (the launchers' Frame Rate; GTA_FPS=<n> caps at n frames a second, GTA_FPS=0 uncapped; unset: as set
 * in the game). The games limit themselves to RsGlobal.maxFPS (30) while Display Setup's Frame Limiter is on; both are
 * set once a frame (from the D3D9 layer's Present), so the game's own menu can't undo the choice. */
#if defined(GTA3_EDITION)
enum { RS_MAX_FPS = 0x009055a8u, PREF_LIMITER = 0x00600e20u };
#elif defined(GTAVC_EDITION)
enum { RS_MAX_FPS = 0x009b38f4u, PREF_LIMITER = 0x0086865du };
#else
enum { RS_MAX_FPS = 0x00bf8504u, PREF_LIMITER = 0x00b8803cu };
#endif
void gta_frame_prefs(void)
{
    static int fps = -2;
    if (fps == -2) { const char *e = getenv("GTA_FPS"); fps = e && *e ? atoi(e) : -1;
        if (fps >= 0) fprintf(stderr, "gta: frame rate %s%d\n", fps ? "capped at " : "uncapped", fps ? fps : 0); }
    if (fps < 0) return;
    if (fps) { rt_w32(G_MEM, RS_MAX_FPS, (uint32_t)fps); G_MEM[PREF_LIMITER] = 1; }
    else G_MEM[PREF_LIMITER] = 0;
}

/* ---- High frame rates (above 30 a second): the parts of the game that count frames instead of time run as at 30,
 * as FusionFix's HighFPS does (ThirteenAG, MIT; the same sites, found with its patterns, tools noted in the commit):
 *   timers that add a frame's whole milliseconds (mission countdowns, script timers, HUD fades) read a time step that
 *     carries the fraction of a millisecond over; things done every 16th/32nd frame (money ticking, the upside-down car
 *     catching fire) and the HUD's flashing count frames of 30 a second; particles update 30 times a second with the
 *     time that passed; the radio station's name, the clouds' turn, the spin damping of cars and peds, and the
 *     objects the mission scripts rotate and slide (gates, lifts) go by the time step. */
#if defined(GTA3_EDITION)
enum { TIME_STEP = 0x008f3ee8u, FRAME_COUNTER = 0x00952624u, EVERY_16 = 0x004a066cu, EVERY_32 = 0x004a0f1cu,
       PARTICLES = 0x0050e630u, PARTICLES_RET = 0x0048cdb1u, RADIO_NAME = 0x0057f4d3u, RADIO_LEFT = 0x00661ba1u,
       CLOUDS = 0x004f7533u, CLOUDS_TURN = 0x00608dd4u, DAMPING = 0x00604814u,
       SCRIPT_ROTATE = 0x00449ea6u, SCRIPT_SLIDE = 0x0044a691u, SCRIPT_PARAMS = 0x006fe5a0u };
static const uint32_t hf_ms[] = {0x00429733u, 0x00438496u, 0x004f0e34u, 0x005075f4u, 0x0050766fu, 0x005076edu, 0x005077bbu,
    0x00507aa7u, 0x00507b22u, 0x00507b93u, 0x00507c50u, 0x005092b6u, 0x0050936eu, 0x00509596u, 0x005099b4u, 0x00509a2cu,
    0x00509aa7u, 0x00509b95u, 0x0050a335u, 0x0050a445u, 0x0050a790u, 0x0050a843u, 0x005350a0u, 0x0054045cu, 0x00552e76u};
static const uint32_t hf_flash[] = {0x00506c31u, 0x00506c87u, 0x00506d02u, 0x00506e3bu, 0x00506f6fu, 0x00507010u, 0x005071acu,
    0x005072e0u, 0x005074a3u, 0x00508079u, 0x005083ceu, 0x00508baau, 0x00421104u};   /* CHud::Draw, CDarkel::DrawMessages */
static const uint32_t hf_damp[] = {0x00496558u, 0x0049656au, 0x0049657cu};   /* CPhysical::ApplyAirResistance's fmuls */
#define HIGH_FPS 1
#elif defined(GTAVC_EDITION)
enum { TIME_STEP = 0x0097442cu, FRAME_COUNTER = 0x00a0c8a0u, EVERY_16 = 0x004bd51cu, EVERY_32 = 0x004bdc8fu,
       PARTICLES = 0x00561d00u, PARTICLES_RET = 0x004a43bau, RADIO_NAME = 0x005f9d80u, RADIO_LEFT = 0x007829c5u,
       CLOUDS = 0x00540db3u, CLOUDS_TURN = 0x006944ecu, DAMPING = 0x0068e664u,
       SCRIPT_ROTATE = 0x0045c023u, SCRIPT_SLIDE = 0x0045c814u, SCRIPT_PARAMS = 0x007d6440u };
static const uint32_t hf_ms[] = {0x00434db6u, 0x00451306u, 0x004bca4au, 0x004bcb12u, 0x004bcb5du, 0x004bcc21u, 0x004bcd48u,
    0x004bcd93u, 0x004bce5cu, 0x004bd015u, 0x004bd060u, 0x004bd124u, 0x004bd2a4u, 0x004bd2efu, 0x004bd3b3u, 0x005083fdu,
    0x005353a6u, 0x0053560au, 0x00537eb2u, 0x00556aecu, 0x00556c01u, 0x00556f5au, 0x00557022u, 0x0055949du, 0x00559518u,
    0x00559596u, 0x00559698u, 0x005599bbu, 0x00559a36u, 0x00559aa7u, 0x00559b64u, 0x0055b14eu, 0x0055b1cbu, 0x0055b254u,
    0x0055b35du, 0x0055b6f8u, 0x0055b7b0u, 0x0055b9b7u, 0x0055bcc6u, 0x0055bd35u, 0x0055bdcau, 0x0059582du, 0x00598a09u,
    0x005a0943u, 0x005b7e96u, 0x00612160u};
static const uint32_t hf_flash[] = {0x0055891du, 0x00558973u, 0x005589eeu, 0x00558b2fu, 0x00558bfbu, 0x00558d9fu, 0x00558f7du,
    0x00558fdbu, 0x00559f79u, 0x0055a207u, 0x0055a785u, 0x0042a2c4u};
static const uint32_t hf_damp[] = {0x004bab08u, 0x004bab17u, 0x004bab29u};
#define HIGH_FPS 1
#endif
#ifdef HIGH_FPS
static struct { uint32_t last; float whole_ms, ms_carry, carry30, scale, clouds, clouds_base, damp, damp_base, particles, saved;
                uint32_t frames30, gated, passed; int restore; } hf = {.last = 0xffffffffu, .gated = 0xffffffffu};
static int hf_on(void) { static int on = -1; if (on < 0) { const char *e = getenv("GTA_FPS"); on = e && *e && (atoi(e) > 30 || atoi(e) == 0); } return on; }
static void hf_update(void)   /* once a game frame (CTimer::m_FrameCounter changed) */
{
    uint32_t n = rt_r32(G_MEM, FRAME_COUNTER); if (n == hf.last) return;
    if (hf.last == 0xffffffffu) { hf.clouds_base = rdf(CLOUDS_TURN); hf.damp_base = rdf(DAMPING); }
    hf.last = n;
    const float step30 = 50.0f / 30, ms30 = 1000.0f / 30;
    float step = rdf(TIME_STEP), ms = step * 20;
    hf.ms_carry += ms; float whole = floorf(hf.ms_carry); hf.ms_carry -= whole; hf.whole_ms = (whole + 0.5f) / 20;
    hf.carry30 += ms; hf.passed = (uint32_t)(hf.carry30 / ms30); hf.carry30 -= (float)hf.passed * ms30;
    hf.frames30 += hf.passed; hf.gated = hf.passed ? hf.frames30 : 0xffffffffu;
    hf.scale = step / step30; hf.clouds = hf.clouds_base * hf.scale; hf.damp = powf(hf.damp_base, hf.scale);
}
static int hf_hook(Ctx *c, uint32_t addr)
{
    if (!hf_on()) return 0;
    for (unsigned k = 0; k < sizeof hf_ms / sizeof *hf_ms; k++) if (addr == hf_ms[k]) { hf_update(); ST(0) = hf.whole_ms; return 1; }
    for (unsigned k = 0; k < sizeof hf_flash / sizeof *hf_flash; k++) if (addr == hf_flash[k]) { hf_update(); c->eax = hf.frames30; return 1; }
    for (unsigned k = 0; k < sizeof hf_damp / sizeof *hf_damp; k++)
        if (addr == hf_damp[k]) { hf_update(); if (hf.damp_base > 0) ST(0) *= hf.damp / hf.damp_base; return 1; }
    switch (addr) {
    case EVERY_16: case EVERY_32: hf_update(); c->eax = hf.gated; return 1;
    case PARTICLES_RET: if (hf.restore) { wrf(TIME_STEP, hf.saved); hf.restore = 0; } return 1;
    case RADIO_NAME: { hf_update(); int left = G_MEM[RADIO_LEFT] + 1 - (int)hf.passed; G_MEM[RADIO_LEFT] = (uint8_t)(left < 1 ? 1 : left > 255 ? 255 : left); return 1; }
    case CLOUDS: hf_update(); ST(0) = hf.clouds; return 1;
    case SCRIPT_ROTATE: hf_update(); wrf(SCRIPT_PARAMS + 8, rdf(SCRIPT_PARAMS + 8) * hf.scale); return 1;
    case SCRIPT_SLIDE: hf_update(); for (int k = 4; k <= 6; k++) wrf(SCRIPT_PARAMS + 4 * k, rdf(SCRIPT_PARAMS + 4 * k) * hf.scale); return 1;
    }
    return 0;
}
/* CParticle::Update (no arguments): only when a 30 fps frame's time has passed, with all of it */
int gta3_override(Ctx *c, uint32_t addr)
{
    if (addr != PARTICLES || !hf_on()) return 0;
    hf_update(); float step = rdf(TIME_STEP); hf.particles += step;
    if (step >= 50.0f / 30 || hf.particles >= 50.0f / 30) { hf.saved = step; hf.restore = 1; wrf(TIME_STEP, hf.particles); hf.particles = 0; return 0; }
    c->esp += 4; return 1;   /* not yet: returned at once */
}
#else
int gta3_override(Ctx *c, uint32_t addr) { (void)c; (void)addr; return 0; }
#endif

int gta3_hook(Ctx *c, uint32_t addr)
{
#ifdef HIGH_FPS
    if (hf_hook(c, addr)) return 1;
#endif
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
