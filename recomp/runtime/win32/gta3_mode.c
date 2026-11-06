/* The launch window's Resolution, GTA III and San Andreas. psSelectDevice takes the video mode saved in the settings file
 * (the menu's m_nPrefsVideoMode: GTA III 0x906c10 in gta3.set, San Andreas 0xb880c8 in gta_sa.set) when it is a usable
 * one, else looks for 640x480 (GTA III) or 800x600 (San Andreas). GTA_MODE=WxH (the launchers) puts RenderWare's index
 * of that 32-bit full-screen mode there first (tools/lift.py GTA3_HOOKS / GTASA_HOOKS), so the game opens at it and its
 * Display Setup shows it; without the variable (or empty) the saved choice stands ("As Set in Game"). */
#include "w32.h"
#include "edition.h"
#include <time.h>

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

#ifdef GTASA_EDITION
/* San Andreas lists only video modes of five shapes (height over width 0.8, 0.75, 0.6, 0.625, 0.5625): a Mac display
 * is often none of them (a 16-inch MacBook Pro below its notch, 2054x1290), and the launch window's mode was then
 * dropped for 800x600. At the first comparison (fld [0.8]; fld [esp+4] (the mode's shape); fucompp) the mode's shape
 * is made 0.8, so every mode is listed. */
enum { SA_MODE_SHAPE = 0x0074d3beu, SA_SHAPE_08 = 0x0083fe78u, SA_PREFS_AA = 0x00b880b8u };
#endif
/* The frame limiter (WinMain's loop, all three games) runs a frame once the time since CTimer's last update passes
 * 1000 / RsGlobal.maxFPS ms; that update comes partway through the frame, so every frame took the limit plus the work
 * before it (San Andreas at 60: 16.7 + ~6.7 ms, 43 fps; the PC game's "30" gave 25). With VSync that rounds up to the
 * next refresh: GTA III and Vice City capped at 120 on a 120 Hz display showed 60. The test here sees the time since the
 * last frame's due time by a steady clock instead, each frame due one interval after the last: the limit is kept
 * exactly. LIMIT_TEST: San Andreas' [esp+14] (the ms since, as a float), III's and VC's st(0) before the comparison
 * (reached only with the limiter on); LIMIT_RUN: where a frame runs. */
#if defined(GTASA_EDITION)
enum { LIMIT_TEST = 0x0075072du, LIMIT_RUN = 0x00750744u };
#elif defined(GTAVC_EDITION)
enum { LIMIT_TEST = 0x006000e0u, LIMIT_RUN = 0x006000fcu };
#else
enum { LIMIT_TEST = 0x00583be3u, LIMIT_RUN = 0x00583bffu };
#endif
static double lim_due;
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec * 1e-6; }
static void pick_mode(Ctx *c)
{
    static int done; const char *e = getenv("GTA_MODE"), *dp = getenv("GTA_DEPTH"); unsigned w, h, depth = dp && atoi(dp) == 16 ? 16 : 32;
#ifdef GTASA_EDITION
    /* the launcher's Antialiasing (GTA_MSAA=<samples>, 1 off) as the game's own (FrontEndMenuManager's applied level,
     * given to RwD3D9EngineSetMultiSamplingLevels just after this) */
    { const char *m = getenv("GTA_MSAA"); if (!done && m && *m) { int n = atoi(m) < 1 ? 1 : atoi(m); rt_w32(G_MEM, SA_PREFS_AA, (uint32_t)n); } }
#endif
    if (done || !e || sscanf(e, "%ux%u", &w, &h) != 2) { done = 1; return; }
    done = 1;
    uint32_t buf = (c->esp - 0x400) & ~15u, n = guest_call(c, RW_NUM_MODES, 0, 0);   /* scratch well below the frame */
    for (uint32_t i = 1; i < n && i < 512; i++) {
        uint32_t a[2] = {buf, i}; guest_call(c, RW_MODE_INFO, 2, a);
        if (rt_r32(G_MEM, buf) == w && rt_r32(G_MEM, buf + 4) == h && rt_r32(G_MEM, buf + 8) == depth && (rt_r32(G_MEM, buf + 12) & 1)) {
            fprintf(stderr, "gta3: video mode %ux%ux%u (RenderWare mode %u, was %u)\n", w, h, depth, i, rt_r32(G_MEM, PREFS_MODE));
            rt_w32(G_MEM, PREFS_MODE, i); return;
        }
    }
    fprintf(stderr, "gta3: no %ux%ux%u video mode: the game's own choice\n", w, h, depth);
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
int gta_wide(void);
static int wide_on(void) { return gta_wide(); }
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

/* ---- Controls (the launcher's Controls; GTA_CONTROLS=modern|classic) ----
 * Classic: the controller as the PS2's pad (host main.m: B A X Y, LT RT LB RB, View, L3, R3, the D-pad as buttons 1-16,
 * the order the games' default joypad layout expects). Modern: the Definitive Edition's Modern scheme (its control
 * tables: on foot, in vehicles, on bicycles, in aircraft), each of its buttons given the PS2 button its action is on.
 * The games' own bindings can't do it (they read fire in every situation: fire on the accelerator would fire as a car
 * drives), so it is decided here by where the player is: CWorld::Players[PlayerInFocus].m_pPed, its in-vehicle flag
 * and vehicle, and (San Andreas) the vehicle's class: a bicycle keeps the PS2's buttons as the Definitive Edition does
 * (hold A to pedal, tap it to pedal faster, LB bunny hop), aircraft have their own. Menus keep the PS2's layout. */
#if defined(GTA3_EDITION)
enum { PLAYERS = 0x00952628u, PLAYER_FOCUS = 0x0096e099u, PLAYER_SIZE = 0x13c, PED_IN_VEHICLE = 0x314, PED_VEHICLE = 0x310 };
#elif defined(GTAVC_EDITION)
enum { PLAYERS = 0x00949d30u, PLAYER_FOCUS = 0x00a0fb03u, PLAYER_SIZE = 0x170, PED_IN_VEHICLE = 0x3ac, PED_VEHICLE = 0x3a8 };
#else
enum { PLAYERS = 0x00b64788u, PLAYER_FOCUS = 0x00b6477cu, PLAYER_SIZE = 0x190, PED_IN_VEHICLE = 0x46d, PED_VEHICLE = 0x58c,   /* m_nPedFlags bit 8 */
       VEH_SUBCLASS = 0x594 };   /* CVehicle: 3 helicopter, 4 plane, 9 motorbike, 10 bicycle */
#endif
enum { CTX_FOOT, CTX_VEHICLE, CTX_BICYCLE, CTX_AIRCRAFT };
static int pad_context(void)
{
    uint8_t f = G_MEM[PLAYER_FOCUS]; if (f > 1) f = 0;
    uint32_t ped = rt_r32(G_MEM, PLAYERS + f * PLAYER_SIZE); if (ped < 0x10000u) return CTX_FOOT;
#ifdef GTASA_EDITION
    int in = G_MEM[ped + PED_IN_VEHICLE] & 1;
#else
    int in = G_MEM[ped + PED_IN_VEHICLE] != 0;
#endif
    uint32_t veh = rt_r32(G_MEM, ped + PED_VEHICLE);
    if (!in || veh < 0x10000u) return CTX_FOOT;
#ifdef GTASA_EDITION
    uint8_t k = G_MEM[veh + VEH_SUBCLASS];
    if (k == 10) return CTX_BICYCLE;
    if (k == 3 || k == 4) return CTX_AIRCRAFT;
#endif
    return CTX_VEHICLE;
}
/* Modern: the right stick drives the games' own mouse camera (a free orbit camera, as in today's games: up looks up),
 * not their joystick look (GTA III and Vice City's had up and down the other way; San Andreas, whose PC camera on foot
 * follows the mouse in its Mouse + Keys control method, ignored it on foot). Its axes are then not reported as the
 * joystick's. GTA III and Vice City have no mouse camera in vehicles: there the stick keeps the game's own look.
 * Counts a second at full tilt, after a dead zone and a curve (fine aim near the centre). */
void w32_di_mouse_move(int dx, int dy);
int gta_menu_frame(void);
uint32_t gta_pad_axes(float ax[6])   /* dinput8.c w32_di_pad, 250 times a second: ax as the host reports (right stick 2/3,
                                       4/5); returns PS2 buttons to add (GTA III and Vice City in vehicles: the looks) */
{
    static int modern = -1; if (modern < 0) { const char *e = getenv("GTA_CONTROLS"); modern = e && !strcmp(e, "modern"); }
    if (!modern) return 0;
    static double last, fx, fy; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); double now = ts.tv_sec + ts.tv_nsec * 1e-9;
    double dt = last > 0 ? now - last : 0; last = now; if (dt > 0.05) dt = 0.05;
    if (gta_menu_frame()) { fx = fy = 0; return 0; }   /* menus: the stick as the PS2's */
    int ctx = pad_context();
#if !defined(GTASA_EDITION)
    if (ctx != CTX_FOOT) {   /* no free camera in their vehicles: the stick looks left, right (L2, R2) or back (both) */
        float rx = ax[2], ry = ax[4]; ax[2] = ax[3] = ax[4] = ax[5] = 0; fx = fy = 0;
        if (ry > 0.6f) return 3u << 4;
        return rx < -0.6f ? 1u << 4 : rx > 0.6f ? 1u << 5 : 0;
    }
#else
    if (ctx == CTX_AIRCRAFT) { fx = fy = 0; return 0; }   /* the mouse would fly the plane: the stick's own controls */
#endif
    float rx = ax[2], ry = ax[4]; ax[2] = ax[3] = ax[4] = ax[5] = 0;
    double m = sqrt((double)rx * rx + (double)ry * ry), dz = 0.15;
    if (m <= dz) return 0;
    double k = (m - dz) / (1 - dz); if (k > 1) k = 1; k = k * k / m;        /* radial dead zone, then squared */
    static double sx = -1, sy; if (sx < 0) { const char *e = getenv("GTA_STICK_LOOK"); sx = e ? atof(e) : 1400; if (sx <= 0) sx = 1400; sy = sx * 0.7; }
#if defined(GTASA_EDITION)
    ry = -ry;   /* San Andreas' mouse cameras (on foot and in vehicles) pitch the other way to GTA III's and Vice City's, as
                   set by default */
#endif
    fx += rx * k * sx * dt; fy += ry * k * sy * dt;
    int dx = (int)fx, dy = (int)fy; fx -= dx; fy -= dy;
    if (dx || dy) w32_di_mouse_move(dx, dy);
    return 0;
}
static volatile uint32_t pad_mapped;   /* the PS2 buttons last reported (gta_pad_aiming) */
uint32_t gta_pad_layout_(uint32_t b);
uint32_t gta_pad_layout(uint32_t b) { uint32_t r = gta_pad_layout_(b); pad_mapped = r; return r; }
static int gta_pad_aiming(void) { return (pad_mapped >> 7) & 1; }   /* R1 (Modern: LT): targeting */
uint32_t gta_pad_layout_(uint32_t b)   /* the controller's buttons -> the PS2 pad's (same bit numbering) */
{
    static int modern = -1; if (modern < 0) { const char *e = getenv("GTA_CONTROLS"); modern = e && !strcmp(e, "modern"); }
    if (!modern) return b;
    enum { B = 1 << 0, A = 1 << 1, X = 1 << 2, Y = 1 << 3, LT = 1 << 4, RT = 1 << 5, LB = 1 << 6, RB = 1 << 7, VIEW = 1 << 8, L3 = 1 << 9, R3 = 1 << 10,
           UP = 1 << 12, RIGHT = 1 << 13, DOWN = 1 << 14, LEFT = 1 << 15 };
    enum { CIRCLE = B, CROSS = A, SQUARE = X, TRIANGLE = Y, L2 = LT, R2 = RT, L1 = LB, R1 = RB };   /* the PS2's, in the same bits */
    int gta_menu_frame(void);   /* d3d9.c: no 3D drawn (a menu or loading screen) */
    if (gta_menu_frame()) return b;
#ifdef GTASA_EDITION
    const uint32_t RADIO = UP;                          /* the radio: San Andreas' D-pad up, the others' L1 */
#else
    const uint32_t RADIO = L1;
#endif
    uint32_t o = 0; int ctx = pad_context();
    switch (ctx) {
    case CTX_FOOT:
        o = b & (VIEW | L3 | R3 | UP | RIGHT | DOWN | LEFT | A | X | Y);   /* sprint (tap: faster), jump, enter */
        if (b & (B | RT)) o |= CIRCLE;                   /* attack */
        if (b & LT) o |= R1;                             /* aim, target */
        if (b & LB) o |= R2;                             /* next weapon */
        if (b & RB) o |= L1;                             /* phone, swap weapons, reset camera */
        break;
    case CTX_VEHICLE:                                    /* cars, motorbikes, boats (and GTA III's and Vice City's aircraft) */
        o = b & (VIEW | L3 | DOWN | LEFT | Y);           /* horn, exit */
        if (b & RT) o |= CROSS;                          /* accelerate */
        if (b & LT) o |= SQUARE;                         /* brake, reverse */
        if (b & RB) o |= R1;                             /* handbrake */
        if (b & LB) o |= CIRCLE;                         /* fire */
        if (b & A) o |= CIRCLE | CROSS;                  /* nitrous (and what Cross does: skip a cutscene) */
        if (b & X) o |= L2;                              /* look left */
        if (b & B) o |= R2;                              /* look right */
        if (b & R3) o |= L2 | R2;                        /* look behind */
        if (b & UP) o |= R3;                             /* sub-missions */
        if (b & RIGHT) o |= RADIO;                       /* radio */
        break;
    case CTX_BICYCLE:                                    /* the PS2's buttons: hold A to pedal, tap to pedal faster */
        o = b & (VIEW | L3 | UP | RIGHT | DOWN | LEFT | A | X | Y | B | RB | LB);   /* pedal, brake, exit, fire, handbrake, bunny hop */
        if (b & R3) o |= L2 | R2;                        /* look behind */
        break;
    case CTX_AIRCRAFT:
        o = b & (VIEW | L3 | DOWN | LEFT | RIGHT | Y);   /* landing gear, radio, exit */
        if (b & RT) o |= CROSS;                          /* ascend, thrust */
        if (b & LT) o |= SQUARE;                         /* descend */
        if (b & LB) o |= L2;                             /* rudder left */
        if (b & RB) o |= R2;                             /* rudder right */
        if (b & A) o |= R1;                              /* main weapon */
        if (b & B) o |= CIRCLE;                          /* other weapon */
        if (b & R3) o |= L2 | R2;                        /* look behind */
        if (b & UP) o |= R3;                             /* sub-missions */
        break;
    }
    { static int lg = -1, was_c = -1; static uint32_t was_b; if (lg < 0) lg = getenv("GTA_PADLOG") != 0;   /* development */
      if (lg && (ctx != was_c || b != was_b)) { static const char *const n[] = {"on foot", "vehicle", "bicycle", "aircraft"};
          fprintf(stderr, "pad: %s, buttons %04x -> %04x\n", n[ctx], b, o); was_c = ctx; was_b = b; } }
    return o;
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
/* The game's own menu settings and the launcher's (each set at the start from the launch window, then the game's
 * Display Setup changes them for the session):
 *   Frame Limiter: on caps at GTA_FPS (RsGlobal.maxFPS, set every frame), off runs as fast as the display shows;
 *   Widescreen: the 3D view and HUD of this port (Hor+, above) on or off. The game's own widescreen (a 16:9 view
 *     cropped from the 4:3 one, a longer draw distance, no cinema bars) is never used: its setting holds this port's
 *     choice only while a menu or loading screen is up (no 3D drawn: d3d9.c), where Display Setup shows and changes
 *     it, and the game saves it; in play it is 0. */
#if defined(GTA3_EDITION)
enum { WIDE_PREF = 0x0096e05bu };      /* CMenuManager::m_PrefsUseWideScreen */
#elif defined(GTAVC_EDITION)
enum { WIDE_PREF = 0x0086865au };
#else
enum { WIDE_PREF = 0x00b8803bu };     /* FrontEndMenuManager.m_bWidescreenOn */
#endif
static int fps_env(void)   /* GTA_FPS: -1 unset (as in the game), 0 uncapped, else the cap */
{
    static int fps = -2;
    if (fps == -2) { const char *e = getenv("GTA_FPS"); fps = e && *e ? atoi(e) : -1;
        if (fps > 0) fprintf(stderr, "gta: frame rate capped at %d\n", fps); else if (!fps) fprintf(stderr, "gta: frame rate uncapped\n"); }
    return fps;
}
int gta_menu_cap(void) { int f = fps_env(); return f > 0 ? f : 0; }   /* ddraw.c: the menus' and loading screens' cap */
static int wide_state = -1;
int gta_wide(void) { if (wide_state < 0) { const char *e = getenv("GTA_WIDESCREEN"); wide_state = !(e && *e == '0'); } return wide_state; }
/* GTA III and San Andreas (the Mac releases) play their intro movies through TransGaming's TGPlayIntros (gta_imports.c),
 * so their own DirectShow graph is never made; but their window procedure, on WM_ACTIVATE / WM_ACTIVATEAPP while the
 * game state is still a movie's (GS_LOGO_MPEG 2, GS_INTRO_MPEG 4), calls that graph's IMediaControl (GetState, Run,
 * Pause) without checking it: the app made active during the intros (skipped late) crashed reading address 0 as the
 * movies ended. Such a message waits until the state has moved on (gta_frame_prefs posts it again). Vice City plays them
 * through DirectShow (dshow.c): its IMediaControl exists. */
#if defined(GTA3_EDITION)
enum { GAME_STATE = 0x00906a6cu, MEDIA_CONTROL = 0x008920ecu };
#elif defined(GTASA_EDITION)
enum { GAME_STATE = 0x00c6e1f0u, MEDIA_CONTROL = 0x00c72e08u };
#endif
static struct { uint32_t hwnd, wp, lp; int on; } held[2];   /* [0] WM_ACTIVATE, [1] WM_ACTIVATEAPP: the latest of each */
int gta_hold_message(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp)
{
#if defined(GTA3_EDITION) || defined(GTASA_EDITION)
    uint32_t st = rt_r32(G_MEM, GAME_STATE);
    if ((msg != 0x6 && msg != 0x1c) || (st != 2 && st != 4) || rt_r32(G_MEM, MEDIA_CONTROL)) return 0;
    int k = msg == 0x1c; held[k].hwnd = hwnd; held[k].wp = wp; held[k].lp = lp; held[k].on = 1;
    return 1;
#else
    (void)hwnd; (void)msg; (void)wp; (void)lp; return 0;
#endif
}
static void release_held(void)
{
#if defined(GTA3_EDITION) || defined(GTASA_EDITION)
    uint32_t st = rt_r32(G_MEM, GAME_STATE); if (st == 2 || st == 4) return;
    void w32_post(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t); uint32_t w32_window_tid(uint32_t);
    for (int k = 0; k < 2; k++) if (held[k].on) { held[k].on = 0; w32_post(w32_window_tid(held[k].hwnd), held[k].hwnd, k ? 0x1c : 0x6, held[k].wp, held[k].lp); }
#endif
}
/* development (test mode: the host's "tp:x,y,z"): the player, or the vehicle they're in, moved there by the game's own
 * CEntity::Teleport (San Andreas: virtual 14; thiscall (CVector, bool resetRotation)), on the game thread between frames */
static volatile int tp_pending; static float tp_to[3];
void gta_request_teleport(float x, float y, float z) { tp_to[0] = x; tp_to[1] = y; tp_to[2] = z; __atomic_store_n(&tp_pending, 1, __ATOMIC_RELEASE); }
static void teleport_now(void)
{
    if (!__atomic_exchange_n(&tp_pending, 0, __ATOMIC_ACQ_REL)) return;
#if defined(GTASA_EDITION)
    extern __thread Ctx *w32_cur_ctx; Ctx *c = w32_cur_ctx; if (!c) return;
    uint32_t ped = rt_r32(G_MEM, PLAYERS); if (ped < 0x10000u) return;
    uint32_t ent = (G_MEM[ped + PED_IN_VEHICLE] & 1) && rt_r32(G_MEM, ped + PED_VEHICLE) >= 0x10000u ? rt_r32(G_MEM, ped + PED_VEHICLE) : ped;
    uint32_t fn = rt_r32(G_MEM, rt_r32(G_MEM, ent) + 14 * 4);
    uint32_t a[4]; memcpy(&a[0], &tp_to[0], 4); memcpy(&a[1], &tp_to[1], 4); memcpy(&a[2], &tp_to[2], 4); a[3] = 0;
    uint32_t ecx = c->ecx; c->ecx = ent; w32_callback(c, fn, 4, a); c->ecx = ecx;
    fprintf(stderr, "gta: teleported %s to %.1f %.1f %.1f (%08x)\n", ent == ped ? "the player" : "the player's vehicle", tp_to[0], tp_to[1], tp_to[2], fn);
#endif
}
/* development (test mode: the host's "gta:verb,args"): the weather now (the game's forced, new and old weather types),
 * the player's vehicle's health (fire, explosions) and speed (held forward for some frames: the speed blur). On the game
 * thread between frames. */
#if defined(GTA3_EDITION)
static const uint32_t WEATHER[3] = {0x0096dfa8u, 0x0096dfb8u, 0x0096e024u}; enum { VEH_HEALTH = 0x200, VEH_SPEED = 0x78 };
#elif defined(GTAVC_EDITION)
static const uint32_t WEATHER[3] = {0x00a0f974u, 0x00a0fa36u, 0x00a0fab2u}; enum { VEH_HEALTH = 0x204, VEH_SPEED = 0x70 };
#else
static const uint32_t WEATHER[3] = {0x00c62180u, 0x00c62184u, 0x00c62188u}; enum { VEH_HEALTH = 0x4c0, VEH_SPEED = 0x44 };
#endif
static char dev_cmd[128]; static volatile int dev_pending; static float dev_speed; static int dev_speed_frames;
void gta_dev_command(const char *cmd) { snprintf(dev_cmd, sizeof dev_cmd, "%s", cmd); __atomic_store_n(&dev_pending, 1, __ATOMIC_RELEASE); }
static uint32_t player_vehicle(void)
{
    uint32_t ped = rt_r32(G_MEM, PLAYERS); if (ped < 0x10000u) return 0;
#if defined(GTASA_EDITION)
    if (!(G_MEM[ped + PED_IN_VEHICLE] & 1)) return 0;
#else
    if (!G_MEM[ped + PED_IN_VEHICLE]) return 0;
#endif
    uint32_t v = rt_r32(G_MEM, ped + PED_VEHICLE); return v >= 0x10000u ? v : 0;
}
static void dev_tick(void)
{
    if (__atomic_exchange_n(&dev_pending, 0, __ATOMIC_ACQ_REL)) {
        char verb[16] = ""; float a = 0, b = 0; sscanf(dev_cmd, "%15[a-z],%f,%f", verb, &a, &b);
        uint32_t v = player_vehicle();
        if (!strcmp(verb, "weather")) { for (int k = 0; k < 3; k++) { uint16_t w = (uint16_t)(int16_t)a; memcpy(G_MEM + WEATHER[k], &w, 2); } }
        else if (!strcmp(verb, "vhealth") && v) wrf(v + VEH_HEALTH, a);
        else if (!strcmp(verb, "vspeed") && v) { dev_speed = a; dev_speed_frames = b > 0 ? (int)b : 120; }
        fprintf(stderr, "gta: dev %s%s\n", dev_cmd, (!strcmp(verb, "vhealth") || !strcmp(verb, "vspeed")) && !v ? " (not in a vehicle)" : "");
    }
    if (dev_speed_frames > 0) {   /* along the vehicle's forward vector */
        uint32_t v = player_vehicle(); dev_speed_frames--; if (!v) { dev_speed_frames = 0; return; }
#if defined(GTASA_EDITION)
        uint32_t m = rt_r32(G_MEM, v + 0x14); if (m < 0x10000u) return; uint32_t fwd = m + 0x10;
#else
        uint32_t fwd = v + 0x14;
#endif
        for (int k = 0; k < 2; k++) { float f; memcpy(&f, G_MEM + fwd + 4 * k, 4); wrf(v + VEH_SPEED + 4 * k, f * dev_speed); }
    }
}
void gta_frame_prefs(int menu)   /* each Present (d3d9.c); menu: no 3D drawn in the frame */
{
    dev_tick();
    teleport_now();
    static int first = 1, was_menu;
    release_held();
    { static int on = -1; static char spec[512];   /* development: GTA_POKE="addr=byte,..." written every frame (force an effect on) */
      if (on < 0) { const char *e = getenv("GTA_POKE"); on = e && *e; if (on) snprintf(spec, sizeof spec, "%s", e); }
      if (on) { char buf[512], *sv, *t; snprintf(buf, sizeof buf, "%s", spec);
                for (t = strtok_r(buf, ",", &sv); t; t = strtok_r(0, ",", &sv)) { unsigned a, v; if (sscanf(t, "%x=%x", &a, &v) == 2 && a < 0x40000000u) G_MEM[a] = (uint8_t)v; } } }
    int fps = fps_env();
    if (fps >= 0) {
        if (first) G_MEM[PREF_LIMITER] = fps > 0;
        if (fps) rt_w32(G_MEM, RS_MAX_FPS, (uint32_t)fps);
    }
#ifdef GTASA_EDITION
#endif
#ifdef GTAVC_EDITION
    /* Vice City's car camera rolls with the steering (CCam::Process_Cam_On_A_String: f_Roll, eased towards the steering
     * times the speed times about 10-15 degrees). A keyboard steers by degrees; a stick is at full lock at once, so the
     * camera banked hard in every turn. The active camera's roll is kept at 0 in that mode and the boat's (a frame's easing from 0 is a
     * fraction of a degree); GTA_CAR_ROLL=1 keeps the original. TheCamera 0x7e3690: ActiveCam +0x76, Cams[3] +0x188
     * (0x1cc each: Mode +0xc, f_Roll +0x30, f_rollSpeed +0x34). */
    { static int keep = -1; if (keep < 0) keep = getenv("GTA_CAR_ROLL") && atoi(getenv("GTA_CAR_ROLL")) > 0;
      uint8_t a = G_MEM[0x7e3690u + 0x76];
      if (!keep && a < 3) { uint32_t cam = 0x7e3690u + 0x188 + a * 0x1ccu;
          uint16_t mode = rt_r16(G_MEM, cam + 0xc);
          if (mode == 18 || mode == 22) { wrf(cam + 0x30, 0); wrf(cam + 0x34, 0); } } }   /* MODE_CAM_ON_A_STRING, MODE_BEHINDBOAT */
#endif
    gta_wide();
    if (menu) { if (first || !was_menu) G_MEM[WIDE_PREF] = (uint8_t)wide_state; else wide_state = G_MEM[WIDE_PREF] != 0; }
    else { if (was_menu && !first) wide_state = G_MEM[WIDE_PREF] != 0; G_MEM[WIDE_PREF] = 0; }
    { static int last = -1; if (last != wide_state) { if (last >= 0) fprintf(stderr, "gta: widescreen %s (the game's menu)\n", wide_state ? "on" : "off"); last = wide_state; } }
    was_menu = menu; first = 0;
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
static int fps_env(void);
/* above 30 frames a second: a cap over 30, uncapped, or the game's Frame Limiter turned off */
static int hf_on(void) { int f = fps_env(); return f > 30 || f == 0 || (f > 0 && !G_MEM[PREF_LIMITER]); }
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
static int hf_known(uint32_t addr)   /* one of these hooks (taken, doing nothing, at 30 fps: not left to rt_hook's other cases) */
{
    if (hf_in(hf_ms, sizeof hf_ms / sizeof *hf_ms, addr)) return 1;
    for (unsigned k = 0; k < sizeof hf_damp / sizeof *hf_damp; k++) if (addr == hf_damp[k]) return 1;
    if (addr == SCRIPT_ROTATE || addr == SCRIPT_SLIDE) return 1;
#ifdef HIGH_FPS_SA
    if (hf_in(hf_ms_nc, sizeof hf_ms_nc / sizeof *hf_ms_nc, addr)) return 1;
    for (unsigned k = 0; k < sizeof hf_swap / sizeof *hf_swap; k++) if (addr == hf_swap[k][0] || addr == hf_swap[k][1]) return 1;
    for (unsigned k = 0; k < sizeof hf_movb / sizeof *hf_movb; k++) if (addr == hf_movb[k][0]) return 1;
#else
    for (unsigned k = 0; k < sizeof hf_flash / sizeof *hf_flash; k++) if (addr == hf_flash[k]) return 1;
    if (addr == EVERY_16 || addr == EVERY_32 || addr == PARTICLES_RET || addr == RADIO_NAME || addr == CLOUDS) return 1;
#endif
    return 0;
}
static int hf_hook(Ctx *c, uint32_t addr)
{
    if (!hf_on()) return hf_known(addr);
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
    if (addr != PARTICLES || !hf_on() || rt_r32(G_MEM, c->esp) != PARTICLES_RET) return 0;   /* only CGame::Process's call,
                                                                                         whose return puts the step back */
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
/* The PS2's radiosity, its soft glow of the bright parts of the picture (the "trails" of San Andreas): the PC game has it
 * (CPostEffects::m_bRadiosity 0xc3031c, CPostEffects::Radiosity 0x7173d0, called from Render 0x719a30) but off, switched
 * on only by some mission scripts with their own settings; with the PC defaults it only darkens. With PS2 lighting the
 * renderer draws it instead (DSR_GLOW: what is brighter than the limit, at a quarter size, blurred, added back) once the
 * colour filter is done, as on the PS2, except under night or infrared vision (CPostEffects 0xc30310, 0xc30311).
 * GTA_GLOW=<limit>,<strength> (0-255) changes it. */
enum { SA_NIGHT_VISION = 0x00c30310u, SA_INFRARED = 0x00c30311u };
static int glow_v(int k) { static int v[2] = {-1, -1}; if (v[0] < 0) { v[0] = 140; v[1] = 96; const char *e = getenv("GTA_GLOW"); if (e) sscanf(e, "%d,%d", &v[0], &v[1]); } return v[k]; }
static uint32_t glow_limit(void) { return (uint32_t)glow_v(0); }
static uint32_t glow_strength(void) { return (uint32_t)glow_v(1); }
int gta_filter_pass;
static int ps2_light(void) { static int on = -1; if (on < 0) { const char *e = getenv("GTA_LIGHTING"); on = e && !strcmp(e, "ps2"); } return on; }
#endif

#ifdef GTAVC_EDITION
/* Trails: the PS2's motion blur in the time cycle's colour (CMBlur, CCamera::RenderMotionBlur), which the PC release
 * switches off where it sets CMBlur::BlurOn (0x696d5c) to 0 (two places, the condition patched out). With PS2 lighting
 * (GTA_LIGHTING=ps2) those writes store 1: the copy of the screen is made and drawn over the next frame as on the PS2.
 * (GTA III's Trails are its own Display Setup setting.) */
enum { VC_BLUR_OFF1 = 0x0048fdbfu, VC_BLUR_OFF2 = 0x0049975fu };
static int vc_trails(void) { static int on = -1; if (on < 0) { const char *e = getenv("GTA_LIGHTING"); on = !e || !strcmp(e, "ps2"); } return on; }
#endif
int gta3_hook(Ctx *c, uint32_t addr)
{
#ifdef GTAVC_EDITION
    if (addr == VC_BLUR_OFF1 || addr == VC_BLUR_OFF2) { if (vc_trails()) c->eax = (c->eax & ~0xffu) | 1; return 1; }   /* mov [BlurOn], al */
#endif
#ifdef GTASA_EDITION
    if (addr == FILTER_PASS1 || addr == FILTER_PASS2 || addr == FILTER_DONE) {
        gta_filter_pass = !ps2_light() ? 0 : addr == FILTER_PASS1 ? 1 : addr == FILTER_PASS2 ? 2 : 0;
        if (addr == FILTER_DONE && ps2_light() && !G_MEM[SA_NIGHT_VISION] && !G_MEM[SA_INFRARED]) { void gta_glow(uint32_t, uint32_t); gta_glow(glow_limit(), glow_strength()); }
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
#ifdef GTASA_EDITION
    if (addr == LIMIT_TEST) { wrf(c->esp + 0x14, (float)(now_ms() - lim_due)); return 1; }   /* [esp+14]: the time since, ms */
#else
    if (addr == LIMIT_TEST) { ST(0) = now_ms() - lim_due; return 1; }                         /* st(0): the time since, ms */
#endif
#if defined(GTA3_EDITION)
    /* Modern: GTA III's player on foot with the mouse camera (the right stick) moves as on the PS2, toward where the
     * left stick points from the camera (CPlayerPed::PlayerControlZelda), not by turning with the stick as its PC
     * controls do (PlayerControl1stPersonRunAround); while aiming (LT) the PC control keeps him facing where the
     * camera looks. 0x4f0bca: the test of CCam::Using3rdPersonMouseCam()'s answer in CPlayerPed::ProcessControl. */
    if (addr == 0x004f0bcau) {
        static int modern = -1; if (modern < 0) { const char *e = getenv("GTA_CONTROLS"); modern = e && !strcmp(e, "modern"); }
        if (modern && !gta_pad_aiming()) c->eax &= ~0xffu;
        return 1;
    }
#endif
    if (addr == LIMIT_RUN) {
        double now = now_ms(), per = 1000.0 / (rt_r32(G_MEM, RS_MAX_FPS) ? rt_r32(G_MEM, RS_MAX_FPS) : 30);
        lim_due = now - lim_due > 2 * per ? now : lim_due + per;   /* behind by more than a frame: from now */
        return 1;
    }
#ifdef GTASA_EDITION
    if (addr == SA_MODE_SHAPE) { rt_w32(G_MEM, c->esp + 4, rt_r32(G_MEM, SA_SHAPE_08)); return 1; }
#endif
    if (addr == CD_SYNC_WAIT) { cd_sync_wait(c); return 1; }
    return 0;
}
#endif
#endif
