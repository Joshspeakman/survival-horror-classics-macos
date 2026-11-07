/* Windows calls the GOG releases of Resident Evil 1-3 and Dino Crisis make that the other ports did not: menus and
 * accelerators of their windowed mode, Resident Evil 3's settings property sheet, the audio codec manager's driver
 * list, joystick and MCI queries. Each answers as Windows would where the game goes on regardless, logged where a
 * game might want more (W32_RELOG=1: every call). */
#include "w32.h"
#include "edition.h"

static int relog(void) { static int on = -1; if (on < 0) on = getenv("W32_RELOG") != 0; return on; }
#define RLOG(...) do { if (relog()) fprintf(stderr, "re: " __VA_ARGS__); } while (0)

/* ---- user32: menus and accelerators (the windowed mode's menu bar is not shown) ---- */
IMPL(user32, LoadAcceleratorsA) { RLOG("LoadAcceleratorsA %#x\n", ARG(1)); RET(0x61000 + (ARG(1) & 0xfff), 2); }
IMPL(user32, LoadMenuA) { RLOG("LoadMenuA %#x\n", ARG(1)); RET(0x62000 + (ARG(1) & 0xfff), 2); }
IMPL(user32, GetSubMenu) { RET(ARG(0) ? ARG(0) + 0x100 + (ARG(1) & 0xff) : 0, 2); }
IMPL(user32, DrawMenuBar) { RET(1, 1); }
IMPL(user32, TrackPopupMenuEx) { RET(0, 6); }
IMPL(user32, CheckRadioButton) { RET(1, 4); }
IMPL(user32, RedrawWindow) { RET(1, 4); }
IMPL(user32, WaitForInputIdle) { RET(0, 2); }

/* ---- comctl32: Resident Evil 3's settings sheet (its launcher takes the settings now): closed at once ---- */
IMPL(comctl32, CreatePropertySheetPageA) { RLOG("CreatePropertySheetPageA\n"); RET(0x63000, 1); }
IMPL(comctl32, PropertySheetA) { fprintf(stderr, "re: PropertySheetA (the game's settings window): closed, nothing changed\n"); RET(0, 1); }

/* ---- gdi32 ---- */
IMPL(gdi32, GetPaletteEntries) { RET(0, 4); }

/* ---- kernel32, advapi32 ---- */
IMPL(kernel32, FreeResource) { RET(0, 1); }
IMPL(kernel32, LocalSize) { RET(ARG(0) ? heap_size(w32_process_heap, ARG(0)) : 0, 1); }
IMPL(advapi32, RegFlushKey) { RET(0, 1); }

/* ---- winmm: joystick count, MCI, wave output volume, mmio ---- */
IMPL(winmm, joyGetNumDevs) { RET(16, 0); }
IMPL(winmm, mciGetDeviceIDA) { RET(0, 1); }
IMPL(winmm, waveOutGetNumDevs) { RET(1, 0); }
IMPL(winmm, waveOutGetVolume) { if (ARG(1)) rt_w32(G_MEM, ARG(1), 0xffffffffu); RET(0, 2); }
IMPL(winmm, mmioSetInfo) { RET(0, 3); }

/* ---- msacm32: two codecs (Microsoft ADPCM, IMA ADPCM), whose streams df_imports.c decodes ---- */
static const struct { uint16_t tag; const char *s, *l; } ACM_DRV[2] = { {2, "MS-ADPCM", "Microsoft ADPCM CODEC"}, {0x11, "IMA ADPCM", "IMA ADPCM CODEC"} };
IMPL(msacm32, acmMetrics)                         /* (object, metric, out *) */
{
    uint32_t m = ARG(1), v = 0;
    switch (m) { case 1: case 2: v = 2; break;      /* ACM_METRIC_COUNT_DRIVERS, COUNT_CODECS */
                 case 50: v = 50; break;            /* ACM_METRIC_MAX_SIZE_FORMAT: an MS ADPCM WAVEFORMATEX with its coefficients */
                 default: v = 0; }
    RLOG("acmMetrics %u -> %u\n", m, v);
    if (ARG(2)) rt_w32(G_MEM, ARG(2), v); RET(0, 3);
}
IMPL(msacm32, acmDriverEnum)                      /* (callback(hadid, instance, support), instance, flags) */
{
    for (uint32_t k = 0; k < 2; k++) { uint32_t a[3] = {0x64000 + k, ARG(1), 1 /* ACMDRIVERDETAILS_SUPPORTF_CODEC */}; if (!w32_callback(c, ARG(0), 3, a)) break; }
    RET(0, 3);
}
IMPL(msacm32, acmDriverDetailsA)                  /* (hadid, ACMDRIVERDETAILSA *, flags) */
{
    uint32_t k = (ARG(0) & 0xfff) & 1, p = ARG(1), sz = rt_r32(G_MEM, p); if (!sz || sz > 920) sz = 920;
    memset(GP(p + 4), 0, sz - 4);
    rt_w32(G_MEM, p + 4, 0x63647561u); rt_w32(G_MEM, p + 8, 0);           /* 'audc' */
    rt_w16(G_MEM, p + 12, 1); rt_w16(G_MEM, p + 14, ACM_DRV[k].tag);
    rt_w32(G_MEM, p + 16, 0x03320000u); rt_w32(G_MEM, p + 20, 0x04000000u); rt_w32(G_MEM, p + 24, 1);
    rt_w32(G_MEM, p + 28, 1); rt_w32(G_MEM, p + 32, 0);
    if (sz >= 72) snprintf((char *)GP(p + 40), 32, "%s", ACM_DRV[k].s);
    if (sz >= 200) snprintf((char *)GP(p + 72), 128, "%s", ACM_DRV[k].l);
    RET(0, 3);
}
IMPL(msacm32, acmDriverOpen) { rt_w32(G_MEM, ARG(0), 0x65000 + (ARG(1) & 0xfff)); RET(0, 3); }
IMPL(msacm32, acmDriverClose) { RET(0, 2); }
IMPL(msacm32, acmFormatSuggest)                   /* (driver, source format, dest format, dest size, flags): 16-bit PCM */
{
    uint32_t s = ARG(1), d = ARG(2), n = ARG(3);
    uint16_t ch = rt_r16(G_MEM, s + 2); uint32_t rate = rt_r32(G_MEM, s + 4);
    if (n < 16) RET(11 /* MMSYSERR_INVALPARAM */, 5);
    rt_w16(G_MEM, d, 1); rt_w16(G_MEM, d + 2, ch); rt_w32(G_MEM, d + 4, rate); rt_w32(G_MEM, d + 8, rate * ch * 2u);
    rt_w16(G_MEM, d + 12, (uint16_t)(ch * 2)); rt_w16(G_MEM, d + 14, 16); if (n >= 18) rt_w16(G_MEM, d + 16, 0);
    RLOG("acmFormatSuggest %#x %u ch %u Hz -> PCM\n", rt_r16(G_MEM, s), ch, rate);
    RET(0, 5);
}

/* ---- winmm's joystick (Resident Evil, Resident Evil 2): the controller (dinput8.c's pad), as joystick 0 ---- */
#ifdef RE_EDITION
int w32_pad_state(float ax[6], uint32_t *buttons, int *pov);
IMPL(winmm, joyGetDevCapsA)                       /* (id, JOYCAPSA *, size) */
{
    float ax[6]; uint32_t b; int pov;
    { static int n; if (getenv("W32_JOYLOG") && n++ < 8) fprintf(stderr, "w32: joyGetDevCapsA(%u, size %u): pad %d\n", ARG(0), ARG(2), w32_pad_state(ax, &b, &pov)); }
    if (ARG(0) != 0 || getenv("W32_NO_PAD")) RET(167, 3);              /* JOYERR_UNPLUGGED */
    /* joystick 0 is always there (idle until a controller connects): the games look once, at start, often before macOS
     * has found the controller; this way one turned on later still plays */
    uint32_t p = ARG(1), n = ARG(2) < 404 ? ARG(2) : 404; memset(GP(p), 0, n);
    rt_w16(G_MEM, p, 0x045e); rt_w16(G_MEM, p + 2, 0x02ea); if (n >= 36) snprintf((char *)GP(p + 4), 32, "Controller");
    uint32_t v[] = {0, 65535, 0, 65535, 0, 65535, 16, 10, 1000, 0, 65535, 0, 65535, 0, 65535, 0x1 | 0x2 | 0x4 | 0x10 | 0x20, 6, 6, 16};   /* X Y Z, buttons, period, R U V, caps (Z R U V POV), axes, buttons */
    for (uint32_t k = 0; k < sizeof v / sizeof *v && 36 + 4 * k + 4 <= n; k++) rt_w32(G_MEM, p + 36 + 4 * k, v[k]);
    RET(0, 3);
}
IMPL(winmm, joyGetPosEx)                          /* (id, JOYINFOEX *): axes 0..65535, buttons, the hat in hundredths of a degree */
{
    float ax[6]; uint32_t b; int pov;
    { static int n; if (getenv("W32_JOYLOG") && n++ < 8) fprintf(stderr, "w32: joyGetPosEx(%u, flags %x): pad %d\n", ARG(0), rt_r32(G_MEM, ARG(1) + 4), w32_pad_state(ax, &b, &pov)); }
    if (ARG(0) != 0 || getenv("W32_NO_PAD")) RET(167, 2);
    if (!w32_pad_state(ax, &b, &pov)) { memset(ax, 0, sizeof ax); b = 0; pov = -1; }   /* none yet: centred, nothing pressed */
    uint32_t p = ARG(1);
    for (int k = 0; k < 6; k++) { float x = ax[k] < -1 ? -1 : ax[k] > 1 ? 1 : ax[k]; rt_w32(G_MEM, p + 8 + 4u * (uint32_t)k, (uint32_t)((x + 1) * 32767.5f)); }
    uint32_t bb = b & 0xfffu; if (b & (1u << 12)) pov = 0; else if (b & (1u << 13)) pov = 9000; else if (b & (1u << 14)) pov = 18000; else if (b & (1u << 15)) pov = 27000;
    rt_w32(G_MEM, p + 32, bb); uint32_t nb = 0; for (int k = 0; k < 32; k++) nb += (bb >> k) & 1; rt_w32(G_MEM, p + 36, nb);
    rt_w32(G_MEM, p + 40, pov >= 0 ? (uint32_t)pov : 0xffffu);
    RET(0, 2);
}
#endif

