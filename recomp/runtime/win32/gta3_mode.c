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
#else   /* San Andreas 3.0: the time step (and the one before the 3-unit clip), timers of both; no particles, radio name
         * or clouds hooks (FusionFix's San Andreas list); the flashing reads the counter's byte (test, mov bl/al) */
enum { TIME_STEP = 0x00b64414u, TIME_STEP_NC = 0x00b64410u, FRAME_COUNTER = 0x00b64404u, DAMPING = 0x0084a018u,
       SCRIPT_ROTATE = 0x00483cbcu, SCRIPT_SLIDE = 0x00484379u, SCRIPT_PARAMS = 0x00a25d90u };
static const uint32_t hf_ms[] = {
    0x00420811u, 0x00448508u, 0x00448630u, 0x00448740u, 0x0044e494u, 0x00467317u, 0x0049e7a9u, 0x0049e99du,
    0x0050d623u, 0x00525c06u, 0x0055500eu, 0x00555e9cu, 0x00555f1cu, 0x00555f69u, 0x00556037u, 0x00556084u,
    0x005560f4u, 0x00556286u, 0x005562d3u, 0x00556409u, 0x00556468u, 0x005564b5u, 0x005652b7u, 0x00565342u,
    0x005653d2u, 0x00565462u, 0x0056552cu, 0x00565622u, 0x005656fau, 0x0056577cu, 0x00565865u, 0x005658a4u,
    0x005659beu, 0x005659fbu, 0x00565b01u, 0x00565b43u, 0x00586ca6u, 0x00586d28u, 0x00586d8au, 0x00586e61u,
    0x005870d3u, 0x00587121u, 0x00587148u, 0x005871d3u, 0x00587a08u, 0x00587a5au, 0x00587aacu, 0x00587b96u,
    0x00588b26u, 0x00588cbcu, 0x00589131u, 0x00589219u, 0x005894e2u, 0x005897b9u, 0x00589818u, 0x0058988eu,
    0x00589ba6u, 0x00589c05u, 0x00589c7bu, 0x00589d1au, 0x00589d64u, 0x00589ddau, 0x0058ae67u, 0x0058aeadu,
    0x0058af07u, 0x0058af4bu, 0x0058af7fu, 0x0058afd9u, 0x0058b3ecu, 0x0058b44bu, 0x0058b4c1u, 0x0058b556u,
    0x0058b5a0u, 0x0058b616u, 0x0058b880u, 0x0058b8dfu, 0x0058b955u, 0x0058b9f5u, 0x0058ba3fu, 0x0058bab5u,
    0x005edbdfu, 0x005edc36u, 0x00613e98u, 0x006265d6u, 0x0062accau, 0x0062b738u, 0x0062fa3eu, 0x0062fb4au,
    0x0063041du, 0x00633a17u, 0x00633a9eu, 0x00639ed7u, 0x0064a999u, 0x0066cf37u, 0x0067ea5au, 0x0068659bu,
    0x00686607u, 0x006873e2u, 0x00687d51u, 0x0068e460u, 0x00691d02u, 0x0069b28au, 0x006ac9dcu, 0x006acaf2u,
    0x006acb23u, 0x006b7ecdu, 0x006c16f7u, 0x006c9ab1u, 0x006d45e4u, 0x006d6aabu, 0x006e1f1du, 0x006e946du,
    0x0073077au, 0x0073ede0u};
static const uint32_t hf_ms_nc[] = {0x0046c026u, 0x00510fc6u, 0x00611d61u, 0x00611e2eu};
static const uint32_t hf_swap[][3] = {   /* test byte [counter], n: swapped in at, put back at, gated (1) or not */
    {0x00557098u, 0x0055709fu, 1}, {0x0043e9a8u, 0x0043e9afu, 0}, {0x0058521du, 0x00585224u, 0}, {0x0058530du, 0x00585314u, 0},
    {0x005864e1u, 0x005864e8u, 0}, {0x00587672u, 0x00587679u, 0}, {0x00589f6au, 0x00589f71u, 0}};
static const uint32_t hf_movb[][2] = {{0x005853f2u, 1}, {0x00587506u, 0}};   /* after mov bl (1) / al (0), [counter] */
static const uint32_t hf_damp[] = {0x0054925cu, 0x00549268u, 0x00549274u};
#define HIGH_FPS 1
#define HIGH_FPS_SA 1
#endif
#ifdef HIGH_FPS
static struct { uint32_t last, swapped; float whole_ms, whole_ms_nc, ms_carry, ms_carry_nc, carry30, scale, clouds, clouds_base,
                damp, damp_base, particles, saved; double game_ms; uint32_t frames30, gated, passed; int restore; }
    hf = {.last = 0xffffffffu, .gated = 0xffffffffu};
static int hf_in(const uint32_t *a, unsigned n, uint32_t addr)   /* the lists are in address order */
{
    unsigned lo = 0, hi = n; while (lo < hi) { unsigned m = (lo + hi) / 2; if (a[m] < addr) lo = m + 1; else hi = m; }
    return lo < n && a[lo] == addr;
}
static int hf_on(void) { static int on = -1; if (on < 0) { const char *e = getenv("GTA_FPS"); on = e && *e && (atoi(e) > 30 || atoi(e) == 0); } return on; }
static void hf_update(void)   /* once a game frame (CTimer::m_FrameCounter changed) */
{
    uint32_t n = rt_r32(G_MEM, FRAME_COUNTER); if (n == hf.last) return;
#ifndef HIGH_FPS_SA
    if (hf.last == 0xffffffffu) hf.clouds_base = rdf(CLOUDS_TURN);
#endif
    if (hf.last == 0xffffffffu) hf.damp_base = rdf(DAMPING);
    hf.last = n;
    const float step30 = 50.0f / 30, ms30 = 1000.0f / 30;
    float step = rdf(TIME_STEP), ms = step * 20; hf.game_ms += ms;
    hf.ms_carry += ms; float whole = floorf(hf.ms_carry); hf.ms_carry -= whole; hf.whole_ms = (whole + 0.5f) / 20;
#ifdef HIGH_FPS_SA
    hf.ms_carry_nc += rdf(TIME_STEP_NC) * 20; whole = floorf(hf.ms_carry_nc); hf.ms_carry_nc -= whole; hf.whole_ms_nc = (whole + 0.5f) / 20;
#endif
    hf.carry30 += ms; hf.passed = (uint32_t)(hf.carry30 / ms30); hf.carry30 -= (float)hf.passed * ms30;
    hf.frames30 += hf.passed; hf.gated = hf.passed ? hf.frames30 : 0xffffffffu;
    hf.scale = step / step30; hf.clouds = hf.clouds_base * hf.scale; hf.damp = powf(hf.damp_base, hf.scale);
}
#ifdef HIGH_FPS_SA
/* ROTATE_OBJECT and SLIDE_OBJECT run once a frame in a script's loop: each move scaled by the time since the same
 * script last moved the same object, up to a 30 fps frame's */
static float script_move_scale(uint32_t script, uint32_t handle)
{
    static struct { uint32_t script, handle; double at; } last[64]; static int n;
    float scale = 1; int k;
    for (k = 0; k < n; k++) if (last[k].script == script && last[k].handle == handle) break;
    if (k < n) { double d = (hf.game_ms - last[k].at) / (1000.0 / 30); scale = (float)(d < 0 ? 0 : d > 1 ? 1 : d); }
    else { k = n < 64 ? n++ : (int)(handle % 64); last[k].script = script; last[k].handle = handle; }
    last[k].at = hf.game_ms; return scale;
}
#endif
static int hf_hook(Ctx *c, uint32_t addr)
{
    if (!hf_on()) return 0;
    if (hf_in(hf_ms, sizeof hf_ms / sizeof *hf_ms, addr)) { hf_update(); ST(0) = hf.whole_ms; return 1; }
#ifdef HIGH_FPS_SA
    if (hf_in(hf_ms_nc, sizeof hf_ms_nc / sizeof *hf_ms_nc, addr)) { hf_update(); ST(0) = hf.whole_ms_nc; return 1; }
    for (unsigned k = 0; k < sizeof hf_swap / sizeof *hf_swap; k++) {
        if (addr == hf_swap[k][0]) { hf_update(); hf.swapped = rt_r32(G_MEM, FRAME_COUNTER); rt_w32(G_MEM, FRAME_COUNTER, hf_swap[k][2] ? hf.gated : hf.frames30); return 1; }
        if (addr == hf_swap[k][1]) { rt_w32(G_MEM, FRAME_COUNTER, hf.swapped); return 1; }
    }
    for (unsigned k = 0; k < sizeof hf_movb / sizeof *hf_movb; k++) if (addr == hf_movb[k][0]) {
        hf_update(); uint32_t b = hf.frames30 & 0xff;
        if (hf_movb[k][1]) c->ebx = (c->ebx & ~0xffu) | b; else c->eax = (c->eax & ~0xffu) | b;
        return 1;
    }
    for (unsigned k = 0; k < sizeof hf_damp / sizeof *hf_damp; k++)
        if (addr == hf_damp[k]) { hf_update(); if (hf.damp_base > 0) ST(0) *= hf.damp / hf.damp_base; return 1; }
    if (addr == SCRIPT_ROTATE) { hf_update(); wrf(SCRIPT_PARAMS + 8, rdf(SCRIPT_PARAMS + 8) * script_move_scale(c->esi, rt_r32(G_MEM, SCRIPT_PARAMS))); return 1; }
    if (addr == SCRIPT_SLIDE) { hf_update(); float m = script_move_scale(c->esi, rt_r32(G_MEM, SCRIPT_PARAMS));
        for (int k = 4; k <= 6; k++) wrf(SCRIPT_PARAMS + 4 * k, rdf(SCRIPT_PARAMS + 4 * k) * m); return 1; }
    return 0;
#else
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
#endif
}
/* CParticle::Update (no arguments): only when a 30 fps frame's time has passed, with all of it */
#ifdef HIGH_FPS_SA
int gta3_override(Ctx *c, uint32_t addr) { (void)c; (void)addr; return 0; }
#else
int gta3_override(Ctx *c, uint32_t addr)
{
    if (addr != PARTICLES || !hf_on()) return 0;
    hf_update(); float step = rdf(TIME_STEP); hf.particles += step;
    if (step >= 50.0f / 30 || hf.particles >= 50.0f / 30) { hf.saved = step; hf.restore = 1; wrf(TIME_STEP, hf.particles); hf.particles = 0; return 0; }
    c->esp += 4; return 1;   /* not yet: returned at once */
}
#endif
#else
int gta3_override(Ctx *c, uint32_t addr) { (void)c; (void)addr; return 0; }
#endif

/* ---- PS2 colour filter (San Andreas; the launchers' Lighting: PS2, GTA_LIGHTING=ps2) ----
 * CPostEffects::ColourFilter (3.0: 0x7189b0) draws the frame's copy over it twice, with the time cycle's two filter
 * colours, both added (source alpha, one): the PC's look, the frame only ever brightened and washed out. The PS2
 * replaced the frame with it times twice the first colour, then added it times twice the second (its alpha in the PS2's
 * range, half the PC time cycle's). Here the D3D9 layer draws the first pass without blending and both with the
 * texture times the colour doubled (d3d9.c gta_filter_pass), as aap's SkyGfx describes the PS2's filter. */
#ifdef GTASA_EDITION
enum { FILTER_PASS1 = 0x00718a6cu, FILTER_PASS2 = 0x00718abdu, FILTER_DONE = 0x00718ac0u };
int gta_filter_pass;
static int ps2_light(void) { static int on = -1; if (on < 0) { const char *e = getenv("GTA_LIGHTING"); on = e && !strcmp(e, "ps2"); } return on; }
#endif

int gta3_hook(Ctx *c, uint32_t addr)
{
#ifdef GTASA_EDITION
    if (addr == FILTER_PASS1 || addr == FILTER_PASS2 || addr == FILTER_DONE) {
        gta_filter_pass = !ps2_light() ? 0 : addr == FILTER_PASS1 ? 1 : addr == FILTER_PASS2 ? 2 : 0;
        return 1;
    }
#endif
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
