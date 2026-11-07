/* DirectInput 8 for Fallout: New Vegas: the system keyboard (DIK scan codes) and mouse (relative motion, buttons,
 * wheel), in immediate (GetDeviceState) and buffered (GetDeviceData) mode. The host feeds it from its input events
 * (w32_di_key / w32_di_mouse_*). A game controller (the GTA games: the host's GameController pad, w32_di_pad) is one
 * gamepad: six axes (left stick X/Y, the right stick as Z/Rz and again as Rx/Ry), sixteen buttons in the order
 * the host gives them, and the D-pad as a point-of-view hat. */
#include "w32.h"
#include "edition.h"
#include <pthread.h>

#define DI_OK 0
#define DIERR_INVALIDPARAM 0x80070057u
#define DIERR_NOTACQUIRED 0x8007000cu
#define DIERR_DEVICENOTREG 0x80040154u
#define DI_BUFFEROVERFLOW 1u
#define E_NOINTERFACE 0x80004002u
#define QN 512

typedef struct { uint32_t ofs, data, time, seq; } DiEvent;
typedef struct Dev {
    int mouse, joy, acquired, buffer_size;
    DiEvent q[QN]; int qh, qt, overflow;
    int32_t dx, dy, dz;                    /* mouse motion since the last GetDeviceState */
} Dev;
static pthread_mutex_t di_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t keys[256], mbuttons[8];
static Dev *kbd, *mouse; static uint32_t seq;
static uint32_t vt_di, vt_dev, di_obj;
static int di_old;                         /* made by DirectInputCreateA (DirectInput 3-5): the device types of then */

static uint32_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000); }
static void enqueue(Dev *d, uint32_t ofs, uint32_t data)          /* di_lock held */
{
    if (!d || !d->acquired || !d->buffer_size) return;
    int n = (d->qt - d->qh + QN) % QN;
    if (n >= d->buffer_size || n >= QN - 1) { d->overflow = 1; return; }
    d->q[d->qt] = (DiEvent){ofs, data, now_ms(), ++seq}; d->qt = (d->qt + 1) % QN;
}
/* ---- host side ---- */
void w32_di_key(int dik, int down)
{
    if (dik <= 0 || dik > 255) return;
    pthread_mutex_lock(&di_lock);
    uint8_t v = down ? 0x80 : 0;
    if (keys[dik] != v) { keys[dik] = v; enqueue(kbd, (uint32_t)dik, v); }
    pthread_mutex_unlock(&di_lock);
}
void w32_di_mouse_move(int dx, int dy)
{
    pthread_mutex_lock(&di_lock);
    if (mouse && mouse->acquired) { mouse->dx += dx; mouse->dy += dy; if (dx) enqueue(mouse, 0, (uint32_t)dx); if (dy) enqueue(mouse, 4, (uint32_t)dy); }
    pthread_mutex_unlock(&di_lock);
}
void w32_di_mouse_wheel(int dz)
{
    pthread_mutex_lock(&di_lock);
    if (mouse && mouse->acquired) { mouse->dz += dz; enqueue(mouse, 8, (uint32_t)dz); }
    pthread_mutex_unlock(&di_lock);
}
void w32_di_mouse_button(int b, int down)
{
    if (b < 0 || b > 7) return;
    pthread_mutex_lock(&di_lock);
    uint8_t v = down ? 0x80 : 0;
    if (mbuttons[b] != v) { mbuttons[b] = v; enqueue(mouse, 12u + (uint32_t)b, v); }
    pthread_mutex_unlock(&di_lock);
}
void w32_di_release_all(void)                                       /* focus lost: every key and button up */
{
    for (int k = 1; k < 256; k++) if (keys[k]) w32_di_key(k, 0);
    for (int b = 0; b < 8; b++) if (mbuttons[b]) w32_di_mouse_button(b, 0);
}

/* ---- the game controller ---- */
#define JOY_GUID 0x6f1d2b70u                                  /* our instance GUID's first dword (the system devices' family) */
#define JOY_VIDPID 0x02ea045eu                                /* product GUID data1 = PID << 16 | VID: an Xbox controller */
static struct { int connected; float ax[6]; uint32_t buttons; int pov; int32_t lo[6], hi[6]; } pad = {.lo = {-32768, -32768, -32768, -32768, -32768, -32768}, .hi = {32767, 32767, 32767, 32767, 32767, 32767}};   /* centred on 0 unless the game sets a range: the GTA games read the axes signed */
static Dev *joy;
/* host: the pad's state (axes -1..1: X right, Y down; Z/Rz and Rx/Ry the right stick), buttons, the hat
 * in hundredths of a degree (-1 centred); connected 0: no controller */
uint32_t w32_di_pad_buttons(void) { return pad.connected ? pad.buttons : 0; }   /* (the intro movies' skip, gta_imports.c) */
#ifdef GTA_EDITION
uint32_t gta_pad_layout(uint32_t buttons);   /* gta3_mode.c: the launcher's Controls (Classic or Modern) */
#endif
void w32_di_pad(int connected, const float ax[6], uint32_t buttons, int pov)
{
    float axes[6]; memcpy(axes, ax, sizeof axes);
#ifdef GTA_EDITION
    buttons = gta_pad_layout(buttons);
    { uint32_t gta_pad_axes(float a[6]); buttons |= gta_pad_axes(axes); }   /* Modern: the right stick as the mouse camera (or looks) */
#endif
    pthread_mutex_lock(&di_lock);
    pad.connected = connected; memcpy(pad.ax, axes, sizeof pad.ax); pad.buttons = buttons; pad.pov = pov;
    pthread_mutex_unlock(&di_lock);
}
static void put_guid(uint32_t at, uint32_t d1, uint16_t d2, uint16_t d3, const char *d4)
{
    rt_w32(G_MEM, at, d1); rt_w16(G_MEM, at + 4, d2); rt_w16(G_MEM, at + 6, d3); memcpy(GP(at + 8), d4, 8);
}
static void joy_instance(uint32_t p)                         /* DIDEVICEINSTANCEA (580 bytes) */
{
    memset(GP(p), 0, 580); rt_w32(G_MEM, p, 580);
    put_guid(p + 4, JOY_GUID, 0, 0, "GTACPAD0"); put_guid(p + 20, JOY_VIDPID, 0, 0, "\0\0PIDVID");
    rt_w32(G_MEM, p + 36, di_old ? 0x0804u : 0x0215u);       /* DIDEVTYPE_JOYSTICK | GAMEPAD; DI8DEVTYPE_GAMEPAD, standard */
    snprintf((char *)GP(p + 40), 260, "Controller"); snprintf((char *)GP(p + 300), 260, "Controller");
}
static const uint32_t AXIS_GUID[6] = {0xa36d02e0u, 0xa36d02e1u, 0xa36d02e2u, 0xa36d02f4u, 0xa36d02f5u, 0xa36d02e3u};   /* X Y Z Rx Ry Rz */
static const char *AXIS_NAME[6] = {"X Axis", "Y Axis", "Z Axis", "X Rotation", "Y Rotation", "Z Rotation"};
static int32_t axis_value(int k)                             /* di_lock held: the axis in the range the game set */
{
    float v = pad.ax[k];
    if (v < -1) v = -1; if (v > 1) v = 1;
    return pad.lo[k] + (int32_t)((double)(v + 1) * 0.5 * ((double)pad.hi[k] - pad.lo[k]) + 0.5);
}

/* ---- IDirectInputDevice8A ---- */
static Dev *dev_of(Ctx *c) { return *(Dev **)GP(ARG(0) + 4); }
static int dilog(void) { static int on = -1; if (on < 0) on = getenv("W32_DILOG") != 0; return on; }   /* W32_DILOG: the controller's calls */
#define JLOG(...) do { if (d->joy && dilog()) { static int n_; if (n_++ < 6) fprintf(stderr, "dinput8: joystick " __VA_ARGS__); } } while (0)
static void d_QueryInterface(Ctx *c) { rt_w32(G_MEM, ARG(2), ARG(0)); RET(DI_OK, 3); }
static void d_AddRef(Ctx *c) { RET(1, 1); }
static void d_Release(Ctx *c) { RET(0, 1); }
static void d_GetCapabilities(Ctx *c)                 /* DIDEVCAPS: size, flags, type, axes, buttons, povs */
{
    Dev *d = dev_of(c); uint32_t p = ARG(1), n = rt_r32(G_MEM, p);
    memset(GP(p + 4), 0, n > 4 && n < 256 ? n - 4 : 40);
    rt_w32(G_MEM, p + 4, 5);                               /* DIDC_ATTACHED | DIDC_POLLEDDEVICE */
    rt_w32(G_MEM, p + 8, di_old ? (d->joy ? 0x804 : d->mouse ? 0x102 : 0x403) : (d->joy ? 0x215 : d->mouse ? 0x112 : 0x413));   /* gamepad / mouse / keyboard (PC enhanced) */
    rt_w32(G_MEM, p + 12, d->joy ? 6 : d->mouse ? 3 : 0); rt_w32(G_MEM, p + 16, d->joy ? 16 : d->mouse ? 8 : 128);
    if (d->joy) rt_w32(G_MEM, p + 20, 1);                  /* one point-of-view hat */
    RET(DI_OK, 2);
}
/* the controller's objects: 0-5 the axes, 6-21 the buttons, 22 the hat; a DIDEVICEOBJECTINSTANCEA of each */
static uint32_t obj_type(int k) { return k < 6 ? 2u | (uint32_t)k << 8 : k < 22 ? 4u | (uint32_t)(k - 6) << 8 : 0x10u; }   /* ABSAXIS / PSHBUTTON / POV, instance */
static uint32_t obj_offset(int k) { return k < 6 ? 4u * (uint32_t)k : k < 22 ? 48u + (uint32_t)(k - 6) : 32u; }          /* DIJOYSTATE offsets */
static void obj_fill(uint32_t o, uint32_t size, int k)
{
    int axis = k < 6, button = k >= 6 && k < 22;
    if (size < 32 || size > 316) size = 316;
    memset(GP(o + 4), 0, size - 4); rt_w32(G_MEM, o, size);
    put_guid(o + 4, axis ? AXIS_GUID[k] : button ? 0xa36d02f0u : 0xa36d02f2u, 0xc9f3, 0x11cf, "\xbf\xc7\x44\x45\x53\x54\x00\x00");
    rt_w32(G_MEM, o + 20, obj_offset(k)); rt_w32(G_MEM, o + 24, obj_type(k));
    if (size >= 32 + 260) { if (axis) snprintf((char *)GP(o + 32), 260, "%s", AXIS_NAME[k]); else if (button) snprintf((char *)GP(o + 32), 260, "Button %d", k - 5); else snprintf((char *)GP(o + 32), 260, "Hat Switch"); }
}
static void d_EnumObjects(Ctx *c)                     /* (callback, ref, flags): the controller's axes, buttons and hat */
{
    Dev *d = dev_of(c); uint32_t cb = ARG(1), ref = ARG(2), fl = ARG(3);
    if (!d->joy || !cb) RET(DI_OK, 4);
    uint32_t o = heap_alloc(w32_process_heap, 8, 320);
    for (int k = 0; k < 6 + 16 + 1; k++) {
        int axis = k < 6, button = k >= 6 && k < 22;
        if (fl && !(fl & (axis ? 3u : button ? 0x0cu : 0x10u))) continue;
        obj_fill(o, 316, k);
        uint32_t a[2] = {o, ref};
        if (!w32_callback(c, cb, 2, a)) break;            /* DIENUM_STOP */
    }
    heap_free(w32_process_heap, o);
    RET(DI_OK, 4);
}
static void d_GetProperty(Ctx *c)
{
    Dev *d = dev_of(c); uint32_t guid = ARG(1), p = ARG(2);
    if (guid == 1) { rt_w32(G_MEM, p + 16, (uint32_t)d->buffer_size); RET(DI_OK, 3); }     /* DIPROP_BUFFERSIZE */
    if (guid == 2) { rt_w32(G_MEM, p + 16, 0); RET(DI_OK, 3); }                             /* DIPROP_AXISMODE: relative */
    if (guid == 4 && d->joy) {                                                                 /* DIPROP_RANGE */
        uint32_t how = rt_r32(G_MEM, p + 12), obj = rt_r32(G_MEM, p + 8); int k = how == 1 ? (int)obj / 4 : 0;
        if (k < 0 || k > 5) k = 0;
        rt_w32(G_MEM, p + 16, (uint32_t)pad.lo[k]); rt_w32(G_MEM, p + 20, (uint32_t)pad.hi[k]); RET(DI_OK, 3);
    }
    if ((guid == 5 || guid == 6) && d->joy) { rt_w32(G_MEM, p + 16, guid == 5 ? 0 : 10000); RET(DI_OK, 3); }   /* dead zone, saturation */
    RET(DIERR_INVALIDPARAM, 3);
}
static void d_SetProperty(Ctx *c)                     /* DIPROPDWORD: header (16 bytes) then the value */
{
    Dev *d = dev_of(c); uint32_t guid = ARG(1), p = ARG(2);
    JLOG("SetProperty %u how %u obj %u values %d %d\n", guid, rt_r32(G_MEM, p + 12), rt_r32(G_MEM, p + 8), (int)rt_r32(G_MEM, p + 16), (int)rt_r32(G_MEM, p + 20));
    if (guid == 1) { pthread_mutex_lock(&di_lock); int n = (int)rt_r32(G_MEM, p + 16); d->buffer_size = n > QN - 1 ? QN - 1 : n; d->qh = d->qt = 0; pthread_mutex_unlock(&di_lock); }
    if (guid == 4 && d->joy) {                             /* DIPROP_RANGE: the whole device (DIPH_DEVICE) or one axis by offset / id */
        uint32_t how = rt_r32(G_MEM, p + 12), obj = rt_r32(G_MEM, p + 8); int32_t lo = (int32_t)rt_r32(G_MEM, p + 16), hi = (int32_t)rt_r32(G_MEM, p + 20);
        pthread_mutex_lock(&di_lock);
        for (int k = 0; k < 6; k++) {
            int match = how == 0 || (how == 1 && obj == 4u * (uint32_t)k) || (how == 2 && ((obj >> 8) & 0xffff) == (uint32_t)k);
            if (match) { pad.lo[k] = lo; pad.hi[k] = hi; }
        }
        pthread_mutex_unlock(&di_lock);
    }
    RET(DI_OK, 3);
}
static void d_Acquire(Ctx *c)
{
    Dev *d = dev_of(c); JLOG("Acquire\n"); pthread_mutex_lock(&di_lock);
    int was = d->acquired; d->acquired = 1; if (!was) { d->qh = d->qt = 0; d->dx = d->dy = d->dz = 0; }
    pthread_mutex_unlock(&di_lock); RET(was ? 1u /* S_FALSE */ : DI_OK, 1);
}
static void d_Unacquire(Ctx *c) { Dev *d = dev_of(c); pthread_mutex_lock(&di_lock); d->acquired = 0; pthread_mutex_unlock(&di_lock); RET(DI_OK, 1); }
static void d_GetDeviceState(Ctx *c)                  /* keyboard: 256 bytes; mouse: DIMOUSESTATE (16) or DIMOUSESTATE2 (20) */
{
    Dev *d = dev_of(c); uint32_t n = ARG(1), p = ARG(2);
    JLOG("GetDeviceState %u (acquired %d) buttons %x\n", n, d->acquired, pad.buttons);
    if (!d->acquired) RET(DIERR_NOTACQUIRED, 3);
    pthread_mutex_lock(&di_lock);
    if (d->joy) {                                         /* DIJOYSTATE (80) / DIJOYSTATE2 (272) */
        memset(GP(p), 0, n);
        if (n >= 80) {
            for (int k = 0; k < 6; k++) rt_w32(G_MEM, p + 4u * (uint32_t)k, (uint32_t)axis_value(k));
            for (int k = 0; k < 4; k++) rt_w32(G_MEM, p + 32 + 4u * (uint32_t)k, 0xffffffffu);
            if (pad.pov >= 0) rt_w32(G_MEM, p + 32, (uint32_t)pad.pov);
            for (int k = 0; k < 16; k++) G_MEM[p + 48 + (uint32_t)k] = pad.buttons >> k & 1 ? 0x80 : 0;
        }
    }
    else if (!d->mouse) memcpy(GP(p), keys, n < 256 ? n : 256);
    else {
        int32_t v[3] = {d->dx, d->dy, d->dz}; d->dx = d->dy = d->dz = 0;
        memset(GP(p), 0, n); memcpy(GP(p), v, n < 12 ? n : 12);
        if (n > 12) memcpy(GP(p + 12), mbuttons, n - 12 < 8 ? n - 12 : 8);
    }
    pthread_mutex_unlock(&di_lock); RET(DI_OK, 3);
}
static void d_GetDeviceData(Ctx *c)                   /* (object data size, DIDEVICEOBJECTDATA *, in/out count, flags) */
{
    Dev *d = dev_of(c); uint32_t sz = ARG(1), out = ARG(2), pn = ARG(3), flags = ARG(4);
    JLOG("GetDeviceData %u\n", sz);
    if (!d->acquired) RET(DIERR_NOTACQUIRED, 5);
    if (!d->buffer_size) RET(DIERR_INVALIDPARAM, 5);
    pthread_mutex_lock(&di_lock);
    uint32_t want = rt_r32(G_MEM, pn), got = 0; int peek = flags & 1;   /* DIGDD_PEEK */
    int h = d->qh;
    while (h != d->qt && got < want) {
        if (out) { memset(GP(out + got * sz), 0, sz); memcpy(GP(out + got * sz), &d->q[h], sz < 16 ? sz : 16); }
        h = (h + 1) % QN; got++;
    }
    if (!out && want == 0xffffffffu) { got = 0; for (h = d->qh; h != d->qt; h = (h + 1) % QN) got++; if (!peek) h = d->qt; }   /* flush */
    if (!peek) d->qh = h;
    uint32_t r = d->overflow ? DI_BUFFEROVERFLOW : DI_OK; if (!peek) d->overflow = 0;
    pthread_mutex_unlock(&di_lock);
    rt_w32(G_MEM, pn, got); RET(r, 5);
}
static void d_SetDataFormat(Ctx *c) { Dev *d = dev_of(c); JLOG("SetDataFormat size %u data %u objects %u\n", rt_r32(G_MEM, ARG(1)), rt_r32(G_MEM, ARG(1) + 12), rt_r32(G_MEM, ARG(1) + 16)); RET(DI_OK, 2); }
static void d_SetEventNotification(Ctx *c) { RET(DI_OK, 2); }
static void d_SetCooperativeLevel(Ctx *c) { RET(DI_OK, 3); }
/* (instance, object, how): DIPH_BYOFFSET 1 or DIPH_BYID 2. The GTA games ask it of each axis and only then set its
 * range (DIPROP_RANGE -2000..2000, which their stick code assumes: x 0.0005, then a 0.3 dead zone); without it the axes
 * came in at -32768..32767, 16 times too strong, so the slightest push up or down while steering read as a full one. */
static void d_GetObjectInfo(Ctx *c)
{
    Dev *d = dev_of(c); uint32_t o = ARG(1), obj = ARG(2), how = ARG(3);
    if (!d->joy || !o) RET(0x80070057u, 4);                                       /* DIERR_INVALIDPARAM */
    for (int k = 0; k < 23; k++)
        if ((how == 1 && obj == obj_offset(k)) || (how == 2 && (obj & 0xffff1fu) == obj_type(k))) {
            obj_fill(o, rt_r32(G_MEM, o), k); JLOG("GetObjectInfo %u how %u: object %d\n", obj, how, k); RET(DI_OK, 4);
        }
    RET(0x80070002u, 4);                                                               /* DIERR_OBJECTNOTFOUND */
}
static void d_GetDeviceInfo(Ctx *c)                   /* DIDEVICEINSTANCEA: size, instance and product GUIDs, type, names */
{
    Dev *d = dev_of(c); uint32_t p = ARG(1), n = rt_r32(G_MEM, p);
    if (d->joy) { joy_instance(p); rt_w32(G_MEM, p, n); RET(DI_OK, 2); }
    memset(GP(p + 4), 0, n > 4 && n < 1024 ? n - 4 : 0);
    rt_w32(G_MEM, p + 36, di_old ? (d->mouse ? 0x102 : 0x403) : (d->mouse ? 0x112 : 0x413));
    snprintf((char *)GP(p + 40), 260, "%s", d->mouse ? "Mouse" : "Keyboard"); snprintf((char *)GP(p + 300), 260, "%s", d->mouse ? "Mouse" : "Keyboard");
    RET(DI_OK, 2);
}
static void d_Poll(Ctx *c) { Dev *d = dev_of(c); JLOG("Poll\n"); RET(d->acquired ? DI_OK : DIERR_NOTACQUIRED, 1); }   /* as DirectInput: the game then acquires */
#define UNSUP(name, n) static void name(Ctx *c) { static int once; if (!once++) fprintf(stderr, "dinput8: %s asked for (not supported)\n", #name); RET(0x80004001u, n); }   /* E_NOTIMPL; logged once (does a game want force feedback?) */
UNSUP(d_RunControlPanel, 3) UNSUP(d_Initialize, 4) UNSUP(d_CreateEffect, 5) UNSUP(d_EnumEffects, 4) UNSUP(d_GetEffectInfo, 3)
UNSUP(d_GetForceFeedbackState, 2) UNSUP(d_SendForceFeedbackCommand, 2) UNSUP(d_EnumCreatedEffectObjects, 4) UNSUP(d_Escape, 2)
UNSUP(d_SendDeviceData, 5) UNSUP(d_EnumEffectsInFile, 5) UNSUP(d_WriteEffectToFile, 5) UNSUP(d_BuildActionMap, 4)
UNSUP(d_SetActionMap, 4) UNSUP(d_GetImageInfo, 2)

/* ---- IDirectInput8A ---- */
static void i_QueryInterface(Ctx *c) { rt_w32(G_MEM, ARG(2), ARG(0)); RET(DI_OK, 3); }
static void i_AddRef(Ctx *c) { RET(1, 1); }
static void i_Release(Ctx *c) { RET(0, 1); }
static void i_CreateDevice(Ctx *c)                    /* (this, GUID *, device **, outer): GUID_SysMouse 6f1d2b60, GUID_SysKeyboard 6f1d2b61 */
{
    uint32_t g = rt_r32(G_MEM, ARG(1)), out = ARG(2);
    if (g != 0x6f1d2b60u && g != 0x6f1d2b61u && g != JOY_GUID) RET(DIERR_DEVICENOTREG, 4);
    Dev *d = calloc(1, sizeof *d); d->mouse = g == 0x6f1d2b60u; d->joy = g == JOY_GUID;
    pthread_mutex_lock(&di_lock); if (d->joy) joy = d; else if (d->mouse) mouse = d; else kbd = d; pthread_mutex_unlock(&di_lock);
    uint32_t o = heap_alloc(w32_process_heap, 8, 4 + sizeof(Dev *)); rt_w32(G_MEM, o, vt_dev); memcpy(GP(o + 4), &d, sizeof d);
    rt_w32(G_MEM, out, o); RET(DI_OK, 4);
}
static void i_EnumDevices(Ctx *c)                    /* (type, callback, ref, flags): the controller, when one is connected */
{
    uint32_t type = ARG(1), cb = ARG(2), ref = ARG(3);
    int want = type == 0 || type == 4 || (type & 0xff) == 0x14 || (type & 0xff) == 0x15;   /* all, game controllers, joystick, gamepad */
    /* always there (idle until a controller connects): games look for controllers once, at start, before macOS has found
     * them; this way a controller turned on later still plays. W32_NO_PAD=1: none. */
    int on = !getenv("W32_NO_PAD");
    if (dilog()) fprintf(stderr, "dinput8: EnumDevices type %#x -> %s\n", type, want && on ? "the controller" : "none");
    if (want && on && cb) {
        uint32_t p = heap_alloc(w32_process_heap, 8, 580); joy_instance(p);
        uint32_t a[2] = {p, ref}; w32_callback(c, cb, 2, a); heap_free(w32_process_heap, p);
    }
    RET(DI_OK, 5);
}
static void i_GetDeviceStatus(Ctx *c)
{
    uint32_t g = rt_r32(G_MEM, ARG(1)); int on = !getenv("W32_NO_PAD");
    RET(g == 0x6f1d2b60u || g == 0x6f1d2b61u || (g == JOY_GUID && on) ? DI_OK : 1u, 2);
}
UNSUP(i_RunControlPanel, 3) UNSUP(i_Initialize, 3) UNSUP(i_FindDevice, 4) UNSUP(i_EnumDevicesBySemantics, 6) UNSUP(i_ConfigureDevices, 5)

static uint32_t vtable(const char *iface, void (*const *fns)(Ctx *), int n)
{
    uint32_t t = heap_alloc(w32_process_heap, 8, 4u * (uint32_t)n);
    for (int k = 0; k < n; k++) { char nm[96]; snprintf(nm, sizeof nm, "dinput8!%s.%d", iface, k); rt_w32(G_MEM, t + 4u * (uint32_t)k, w32_thunk_register(nm, fns[k])); }
    return t;
}
static void di_init(void);
IMPL(dinput8, DirectInput8Create)                     /* (hinstance, version, IID *, out **, outer) */
{
    di_init(); rt_w32(G_MEM, ARG(3), di_obj); RET(DI_OK, 5);
}
/* DirectInput 3-5 (the Resident Evil games, Dino Crisis): IDirectInputA / 2A and IDirectInputDeviceA / 2A are the first
 * 8 (9) and 18 (27) methods of the version 8 interfaces, the same methods; the device types are the old numbers */
IMPL(dinput, DirectInputCreateA)                      /* (hinstance, version, out **, outer) */
{
    di_old = 1; di_init(); rt_w32(G_MEM, ARG(2), di_obj); RET(DI_OK, 4);
}
static void di_init(void)
{
    if (!vt_di) {
        static void (*const di[])(Ctx *) = { i_QueryInterface, i_AddRef, i_Release, i_CreateDevice, i_EnumDevices, i_GetDeviceStatus,
            i_RunControlPanel, i_Initialize, i_FindDevice, i_EnumDevicesBySemantics, i_ConfigureDevices };
        static void (*const dv[])(Ctx *) = { d_QueryInterface, d_AddRef, d_Release, d_GetCapabilities, d_EnumObjects, d_GetProperty,
            d_SetProperty, d_Acquire, d_Unacquire, d_GetDeviceState, d_GetDeviceData, d_SetDataFormat, d_SetEventNotification,
            d_SetCooperativeLevel, d_GetObjectInfo, d_GetDeviceInfo, d_RunControlPanel, d_Initialize, d_CreateEffect, d_EnumEffects,
            d_GetEffectInfo, d_GetForceFeedbackState, d_SendForceFeedbackCommand, d_EnumCreatedEffectObjects, d_Escape, d_Poll,
            d_SendDeviceData, d_EnumEffectsInFile, d_WriteEffectToFile, d_BuildActionMap, d_SetActionMap, d_GetImageInfo };
        _Static_assert(sizeof di / sizeof *di == 11 && sizeof dv / sizeof *dv == 32, "vtable sizes");
        vt_di = vtable("IDirectInput8A", di, 11); vt_dev = vtable("IDirectInputDevice8A", dv, 32);
        di_obj = heap_alloc(w32_process_heap, 8, 8); rt_w32(G_MEM, di_obj, vt_di);
    }
}
