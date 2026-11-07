/* DirectDraw 1, 2 and 4 with Direct3D 1-3 (DirectX 3-6), included by ddraw.c: the interfaces the GOG releases of
 * Resident Evil 1-3 and Dino Crisis ask for, over the DirectDraw 7 / Direct3D 7 objects there. Each older interface is
 * its own vtable over the same object (obj() takes any version where a method's arguments are the same); what differs
 * is here:
 *   - IDirectDraw (SetDisplayMode without refresh rate and flags), IDirectDraw2 and IDirectDraw4: prefixes of
 *     IDirectDraw7's vtable; IDirectDrawSurface 1-4: prefixes of IDirectDrawSurface7's. The 108-byte DDSURFACEDESC and
 *     4-byte DDSCAPS of versions 1-3 are handled in ddraw.c (sd_bytes_for, caps_bytes_for).
 *   - IDirect3D, IDirect3D2, IDirect3D3: device enumeration with D3DDEVICEDESC, materials, lights, viewports.
 *   - IDirect3DDevice2 and IDirect3DDevice3: viewports as objects, the immediate-mode Begin/Vertex/End, vertex types
 *     (Device2) or FVF codes (Device3), light states, and the render states of before texture stages (texture handles,
 *     TEXTUREMAPBLEND, the filters, COLORKEYENABLE), translated into stage 0's state.
 *   - IDirect3DTexture and IDirect3DTexture2 on surfaces (handles, Load).
 * Colour keys: a surface's source key makes those pixels upload with alpha 0 (ddraw.c row_up); a keyed copy discards
 * them and a draw with COLORKEYENABLE adds an alpha test that does. */

enum { D3DTA_DIFFUSE = 0, D3DTA_CURRENT = 1, D3DTA_TEXTURE = 2 };
enum { TOP_DISABLE = 1, TOP_SELECTARG1 = 2, TOP_SELECTARG2 = 3, TOP_MODULATE = 4, TOP_ADD = 7, TOP_BLENDTEXTUREALPHA = 13 };

typedef struct DVp DVp;
typedef struct DOld {
    int ver;                                    /* 1, 2 or 3 */
    uint32_t g_dev;                             /* this device's IDirect3DDevice / 2 / 3 */
    DVp *vps[32]; int nvp; DVp *cur;            /* viewports added, the current one */
    uint32_t ls[16];                            /* light states */
    uint32_t mapblend, legacy_tex;              /* TEXTUREMAPBLEND; 1 once the game uses texture handles or it */
    uint32_t ck_enable, atest, aref, afunc;     /* COLORKEYENABLE; the game's own alpha test */
    uint32_t sent_at[3];                        /* the alpha test last sent (with the key's) */
    int lit;                                    /* the lighting last sent (D3DRENDERSTATE_LIGHTING) */
    uint8_t *imm; uint32_t imm_n, imm_cap, imm_prim, imm_fvf, imm_stride; int imm_on;   /* Begin/Vertex/End */
    uint16_t *idx; uint32_t idx_n, idx_cap; int indexed;
} DOld;
struct DVp { uint32_t g; int ref; DDevice *dev; uint8_t v1[44], v2[44]; int have2; uint32_t bg; float bg_z; };
typedef struct DMat { uint32_t g; int ref; uint8_t m[80]; uint32_t handle; } DMat;
typedef struct DLight { uint32_t g; int ref; uint8_t l[104]; } DLight;
typedef struct DClip { uint32_t g; int ref; uint32_t hwnd; } DClip;

static DSurface *tex_handles[4096]; static uint32_t ntex_handles = 1;
static DMat *mat_handles[1024]; static uint32_t nmat_handles = 1;
void tex_handle_drop(DSurface *s) { if (s->handle && s->handle < 4096 && tex_handles[s->handle] == s) tex_handles[s->handle] = 0; s->handle = 0; }
static uint32_t tex_handle(DSurface *s)
{
    if (!s->handle) { if (ntex_handles >= 4096) { for (uint32_t k = 1; k < 4096; k++) if (!tex_handles[k]) { s->handle = k; break; } }
                      else s->handle = ntex_handles++;
                      if (s->handle) tex_handles[s->handle] = s; }
    return s->handle;
}
static DSurface *tex_of_handle(uint32_t h) { return h && h < 4096 ? tex_handles[h] : 0; }

/* ---- the device state the old interfaces change ---- */
static void send_rs(DDevice *d, uint32_t s, uint32_t v) { if (s < 256) d->rs[s] = v; uint32_t *p = cmd_begin(DSR_RENDER_STATE, 8); p[0] = s; p[1] = v; cmd_end(); }
static void send_tss(DDevice *d, uint32_t st, uint32_t t, uint32_t v) { if (st < 8 && t < 32) d->tss[st][t] = v; uint32_t *p = cmd_begin(DSR_TSS, 12); p[0] = st; p[1] = t; p[2] = v; cmd_end(); }
static void set_tex(DDevice *d, uint32_t stage, DSurface *s)
{
    DSurface *old = d->tex[stage]; if (s) surf_addref(s); d->tex[stage] = s; if (old) surf_release(old);
    uint32_t *p = cmd_begin(DSR_TEXTURE, 8); p[0] = stage; p[1] = s ? s->id : 0; cmd_end();
}
static int tex_has_alpha(const DSurface *t)
{
    return t && (t->has_ck || t->pk == PK_1555 || t->pk == PK_4444 || (t->pk == PK_32 && (U32(t->desc, SD_PF + PF_FLAGS) & DDPF_ALPHAPIXELS)));
}
/* TEXTUREMAPBLEND (and no texture) as stage 0's operations */
static void update_blend(DDevice *d)
{
    DOld *o = d->old; if (!o || !o->legacy_tex) return;
    DSurface *t = d->tex[0]; uint32_t cop, aop;
    if (!t) { cop = TOP_SELECTARG2; aop = TOP_SELECTARG2; }
    else switch (o->mapblend) {
        case 1: case 5: case 7: cop = TOP_SELECTARG1; aop = TOP_SELECTARG1; break;               /* DECAL, DECALMASK, COPY */
        case 3: cop = TOP_BLENDTEXTUREALPHA; aop = TOP_SELECTARG2; break;                         /* DECALALPHA */
        case 4: cop = TOP_MODULATE; aop = TOP_MODULATE; break;                                    /* MODULATEALPHA */
        case 8: cop = TOP_ADD; aop = TOP_SELECTARG2; break;                                       /* ADD */
        default: cop = TOP_MODULATE; aop = tex_has_alpha(t) ? TOP_SELECTARG1 : TOP_SELECTARG2;   /* MODULATE, MODULATEMASK */
    }
    if (d->tss[0][1] != cop) send_tss(d, 0, 1, cop);
    if (d->tss[0][2] != D3DTA_TEXTURE) send_tss(d, 0, 2, D3DTA_TEXTURE);
    if (d->tss[0][3] != D3DTA_DIFFUSE) send_tss(d, 0, 3, D3DTA_DIFFUSE);
    if (d->tss[0][4] != aop) send_tss(d, 0, 4, aop);
    if (d->tss[0][5] != D3DTA_TEXTURE) send_tss(d, 0, 5, D3DTA_TEXTURE);
    if (d->tss[0][6] != D3DTA_DIFFUSE) send_tss(d, 0, 6, D3DTA_DIFFUSE);
    if (d->tss[1][1] != TOP_DISABLE) send_tss(d, 1, 1, TOP_DISABLE);
}
/* the alpha test: the game's own, or one that drops a keyed texture's key pixels (COLORKEYENABLE) */
static void sync_alpha_test(DDevice *d)
{
    DOld *o = d->old; if (!o) return;
    int key = o->ck_enable && d->tex[0] && d->tex[0]->has_ck;
    uint32_t en = o->atest || key, ref = o->atest ? o->aref : 0, fn = o->atest ? o->afunc : 5;   /* key alone: alpha > 0 */
    if (en != o->sent_at[0]) { send_rs(d, 15, en); o->sent_at[0] = en; }
    if (en && ref != o->sent_at[1]) { send_rs(d, 24, ref); o->sent_at[1] = ref; }
    if (en && fn != o->sent_at[2]) { send_rs(d, 25, fn); o->sent_at[2] = fn; }
}
static void pre_draw(DDevice *d, uint32_t fvf, uint32_t flags)
{
    DOld *o = d->old; if (!o) return;
    int lit = (fvf & 0xe) == 2 && (fvf & 0x10) && !(flags & 0x10);    /* untransformed with normals, unless D3DDP_DONOTLIGHT */
    if (lit != o->lit) { send_rs(d, 137, (uint32_t)lit); o->lit = lit; }
    update_blend(d); sync_alpha_test(d);
}
static uint32_t vtype_fvf(uint32_t vt) { return vt == 1 ? 0x112u : vt == 2 ? 0x1e2u : vt == 3 ? 0x1c4u : 0; }   /* D3DVT_VERTEX, LVERTEX, TLVERTEX */
/* a draw in an FVF the renderer takes: D3DLVERTEX's reserved dword (D3DFVF_RESERVED1) after the position is dropped */
static void draw_old(DDevice *d, uint32_t prim, uint32_t fvf, const uint8_t *v, uint32_t nv, const uint16_t *idx, uint32_t ni, uint32_t flags)
{
    if (!nv) return;
    pre_draw(d, fvf & ~0x20u, flags);
    {   /* development (DSR_DRAWSTATE=1): each new combination of a draw's texture and its blending and keying state */
        static int on = -1; static uint64_t seen[256]; static int ns;
        if (on < 0) on = getenv("DSR_DRAWSTATE") != 0;
        DSurface *t = d->tex[0]; DOld *o = d->old;
        if (on && ns < 256) {
            uint64_t k = (uint64_t)(t ? t->pk : 9) | (uint64_t)(t ? t->has_ck : 0) << 4 | (uint64_t)(o ? o->ck_enable : 0) << 5 | (uint64_t)(d->rs[27] & 1) << 6 |
                         (uint64_t)(d->rs[15] & 1) << 7 | (uint64_t)(o ? o->mapblend : 0) << 8 | (uint64_t)(d->rs[19] & 15) << 12 | (uint64_t)(d->rs[20] & 15) << 16 | (uint64_t)fvf << 20;
            uint32_t dif = 0;
            if (fvf & 0x40) { uint32_t off = ((fvf & 0xe) == 4 ? 16 : 12) + ((fvf & 0x10) ? 12 : 0) + ((fvf & 0x20) ? 4 : 0); memcpy(&dif, v + off, 4); }
            if (atoi(getenv("DSR_DRAWSTATE")) >= 2) k ^= (uint64_t)(dif >> 28) << 60 ^ (uint64_t)(t ? U32(t->desc, SD_WIDTH) : 0) << 50;   /* 2: the vertex alpha and texture size too */
            int have = 0; for (int j = 0; j < ns; j++) if (seen[j] == k) have = 1;
            if (!have) { seen[ns++] = k; fprintf(stderr, "dsr: draw state: diffuse %08x tex %s %ux%u pk %d key %d (%08x) | ck enable %u blend %u (%u/%u) atest %u ref %u fn %u | mapblend %u | fvf %#x prim %u n %u\n", dif,
                t ? "yes" : "none", t ? U32(t->desc, SD_WIDTH) : 0, t ? U32(t->desc, SD_HEIGHT) : 0, t ? t->pk : -1, t ? t->has_ck : 0, t ? t->ck[0] : 0,
                o ? o->ck_enable : 0, d->rs[27], d->rs[19], d->rs[20], o ? o->atest : d->rs[15], o ? o->aref : d->rs[24], o ? o->afunc : d->rs[25], o ? o->mapblend : 0, fvf, prim, nv); }
        }
    }
    if (fvf & 0x20) {
        uint32_t out_fvf = fvf & ~0x20u, so = fvf_stride(out_fvf), si = so + 4;
        uint8_t *t = malloc((size_t)so * nv); if (!t) return;
        for (uint32_t k = 0; k < nv; k++) { memcpy(t + k * so, v + k * si, 12); memcpy(t + k * so + 12, v + k * si + 16, so - 12); }
        emit_draw(prim, out_fvf, t, nv, (const uint8_t *)idx, ni); free(t);
    } else emit_draw(prim, fvf, v, nv, (const uint8_t *)idx, ni);
}
static uint32_t fvf_stride_any(uint32_t fvf) { return fvf_stride(fvf & ~0x20u) + ((fvf & 0x20) ? 4 : 0); }

/* ---- D3DDEVICEDESC (252 bytes in DirectX 6; 204 in 5, 172 in 3): a HAL that takes everything these games ask ---- */
static void dev_desc(uint8_t *b, uint32_t size, int hal)
{
    memset(b, 0, 252); U32(b, 0) = size;
    if (!hal) return;
    U32(b, 4) = 0x7ff;                                               /* every field valid */
    U32(b, 8) = 2;                                                   /* D3DCOLOR_RGB */
    U32(b, 12) = 0x1 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400 | 0x800 | 0x2000 | 0x8000 | 0x80000;   /* FLOATTLVERTEX .. HWRASTERIZATION */
    U32(b, 16) = 8; U32(b, 20) = 1;                                  /* transform caps: clip */
    U32(b, 24) = 1;                                                  /* bClipping */
    U32(b, 28) = 16; U32(b, 32) = 7; U32(b, 36) = 1; U32(b, 40) = 8;  /* lighting: point, spot, directional; RGB; 8 lights */
    for (int k = 0; k < 2; k++) {                                    /* D3DPRIMCAPS: lines, triangles */
        uint8_t *p = b + 44 + 56 * k;
        U32(p, 0) = 56; U32(p, 4) = 0x76; U32(p, 8) = 0x1 | 0x10 | 0x20 | 0x80 | 0x100 | 0x2000 | 0x4000;
        U32(p, 12) = 0xff; U32(p, 16) = 0x1fff; U32(p, 20) = 0x1fff; U32(p, 24) = 0xff; U32(p, 28) = 0x000fffff;
        U32(p, 32) = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x40 | 0x80 | 0x100;      /* perspective, pow2, alpha, transparency (colour key), border, .. */
        U32(p, 36) = 0x0303033f; U32(p, 40) = 0xff; U32(p, 44) = 0x1f;
    }
    U32(b, 156) = 0x500; U32(b, 160) = 0x700;                        /* render 16/32 bits; depth 16/24/32 */
    U32(b, 164) = 0; U32(b, 168) = 65535;
    if (size >= 204) { U32(b, 172) = 1; U32(b, 176) = 1; U32(b, 180) = 2048; U32(b, 184) = 2048; }
    if (size >= 252) { U32(b, 204) = 2048; U32(b, 208) = 2048; U32(b, 212) = 1; U32(b, 240) = 8; U32(b, 244) = 0x3ffffff; b[248] = 8; b[250] = 2; }
}
static const uint8_t GUID_HAL[16] = {0xe0, 0x3d, 0xe6, 0x84, 0xaa, 0x46, 0xcf, 0x11, 0x81, 0x6f, 0x00, 0x00, 0xc0, 0x20, 0x15, 0x6e};
static const uint8_t GUID_RGB[16] = {0x60, 0x5c, 0x66, 0xa4, 0x73, 0x26, 0xcf, 0x11, 0xa3, 0x1a, 0x00, 0xaa, 0x00, 0xb9, 0x33, 0x56};
static uint32_t desc_size_for(int ver) { return ver >= 3 ? 252 : ver == 2 ? 204 : 172; }
static const uint8_t GUID_RAMP[16] = {0x20, 0x6b, 0x08, 0xf2, 0x9f, 0x25, 0xcf, 0x11, 0xa3, 0x1a, 0x00, 0xaa, 0x00, 0xb9, 0x33, 0x56};
static const uint8_t GUID_MMX[16] = {0xa1, 0x49, 0x19, 0x88, 0xf3, 0xd6, 0xd0, 0x11, 0x89, 0xab, 0x00, 0xa0, 0xc9, 0x05, 0x41, 0x29};
/* The devices of DirectX 5-6 in their order: Ramp, RGB and MMX emulation, then the HAL. Every one draws on the GPU here;
 * the order matters because Resident Evil 2 picks by position: GOG's settings (Display Driver 0x13) name the fourth. */
static void enum_devices(Ctx *c, uint32_t cb, uint32_t ctx, int ver)
{
    static const struct { const uint8_t *guid; const char *desc, *name; int hal, model; } dev[4] = {
        {GUID_RAMP, "Microsoft Direct3D Mono(Ramp) Software Emulation", "Ramp Emulation", 0, 1},
        {GUID_RGB, "Microsoft Direct3D RGB Software Emulation", "RGB Emulation", 0, 2},
        {GUID_MMX, "Microsoft Direct3D MMX Software Emulation", "MMX Emulation", 0, 2},
        {GUID_HAL, "Microsoft Direct3D Hardware acceleration through Direct3D HAL", "Direct3D HAL", 1, 2} };
    uint32_t b = scratch(16 + 64 + 64 + 252 * 2); uint32_t size = desc_size_for(ver);
    for (int k = 0; k < 4; k++) {
        memcpy(GP(b), dev[k].guid, 16);
        snprintf((char *)GP(b + 16), 64, "%s", dev[k].desc); snprintf((char *)GP(b + 80), 64, "%s", dev[k].name);
        dev_desc(GP(b + 144), size, dev[k].hal); dev_desc(GP(b + 144 + 252), size, 1);
        U32(GP(b + 144 + 252), 8) = (uint32_t)dev[k].model;
        uint32_t a[6] = {b, b + 16, b + 80, b + 144, b + 144 + 252, ctx};
        if (w32_callback(c, cb, 6, a) == 0) break;
    }
    scratch_free(b);
}

/* ================= IDirectDraw, IDirectDraw2, IDirectDraw4 ================= */
static void dd_mode_set(Ctx *c, DDraw *me, uint32_t w, uint32_t h, uint32_t bpp)
{
    dsr_log("SetDisplayMode %ux%ux%u", w, h, bpp);
    me->mode_w = w; me->mode_h = h; me->mode_bpp = bpp;
    if (me->hwnd && (me->coop & DDSCL_EXCLUSIVE)) w32_set_window_pos(c, me->hwnd, 0, 0, (int)w, (int)h, 0x40 | 0x10);
    uint32_t *p = cmd_begin(DSR_MODE, 8); p[0] = w; p[1] = h; cmd_end();
}
M(dd1_SetDisplayMode, 4) ME(DDraw, O_DD) dd_mode_set(c, me, ARG(1), ARG(2), ARG(3)); RET(DD_OK, 4); END
M(dd4_GetDeviceIdentifier, 3)
    const SnapRec *r = snap_first("DID0");
    if (r) memcpy(GP(ARG(1)), r->data, r->size < 1064 ? r->size : 1064);   /* DDDEVICEIDENTIFIER: no WHQL level */
    RET(DD_OK, 3);
END
static uint32_t dd_old_iface(DDraw *dd, int type)
{
    uint32_t *g = &dd->g_old[type - O_DD1];
    if (!*g) *g = make_iface(type, dd);
    return *g;
}
uint32_t dd_old_qi(DDraw *dd, uint32_t iid)
{
    switch (iid) {
    case 0x6c14db80u: return dd_old_iface(dd, O_DD1);     /* IDirectDraw */
    case 0xb3a6f3e0u: return dd_old_iface(dd, O_DD2);     /* IDirectDraw2 */
    case 0x9c59509au: return dd_old_iface(dd, O_DD4);     /* IDirectDraw4 */
    case 0x3bba0080u: return dd_old_iface(dd, O_D3D1);    /* IDirect3D */
    case 0x6aae1ec1u: return dd_old_iface(dd, O_D3D2);    /* IDirect3D2 */
    case 0xbb223240u: return dd_old_iface(dd, O_D3D3);    /* IDirect3D3 */
    }
    return 0;
}
/* a clipper: kept for the window it names; the copies onto the primary surface go where the window is anyway */
M(clip_QueryInterface, 3) rt_w32(G_MEM, ARG(2), 0); RET(E_NOINTERFACE, 3); END
M(clip_AddRef, 1) DClip *me = obj(ARG(0), O_CLIP); RET(me ? (uint32_t)++me->ref : 0, 1); END
M(clip_Release, 1) DClip *me = obj(ARG(0), O_CLIP); if (!me) RET(0, 1); uint32_t r = (uint32_t)--me->ref; if (!r) { obj_drop(me->g); free(me); } RET(r, 1); END
M(clip_GetClipList, 4) if (ARG(3)) rt_w32(G_MEM, ARG(3), 0); RET(DDERR_UNSUPPORTED, 4); END
M(clip_GetHWnd, 2) DClip *me = obj(ARG(0), O_CLIP); rt_w32(G_MEM, ARG(1), me ? me->hwnd : 0); RET(DD_OK, 2); END
OK_M(clip_Initialize, 3) OK_M(clip_IsClipListChanged, 2) OK_M(clip_SetClipList, 3)
M(clip_SetHWnd, 3) DClip *me = obj(ARG(0), O_CLIP); if (me) me->hwnd = ARG(2); RET(DD_OK, 3); END
M(dd_old_CreateClipper, 4)
    DClip *k = calloc(1, sizeof *k); k->ref = 1; k->g = make_iface(O_CLIP, k); rt_w32(G_MEM, ARG(2), k->g); RET(DD_OK, 4);
END
#ifdef RE_EDITION
IMPL(ddraw, DirectDrawCreateClipper) { DClip *k = calloc(1, sizeof *k); k->ref = 1; k->g = make_iface(O_CLIP, k); rt_w32(G_MEM, ARG(1), k->g); RET(DD_OK, 3); }
#endif

/* ================= textures (IDirect3DTexture, IDirect3DTexture2) ================= */
M(tex_GetHandle, 3) ME(DSurface, O_SURF)
    uint32_t h = tex_handle(me); rt_w32(G_MEM, ARG(2), h);
    dsr_log("texture handle %#x: surface %u %ux%u %u-bit (pf flags %#x masks %x %x %x %x) key %d caps %#x", h, me->id, U32(me->desc, SD_WIDTH), U32(me->desc, SD_HEIGHT),
            bpp_of(me->desc), U32(me->desc, SD_PF + PF_FLAGS), U32(me->desc, SD_PF + PF_R), U32(me->desc, SD_PF + PF_G), U32(me->desc, SD_PF + PF_B), U32(me->desc, SD_PF + PF_A), me->has_ck, U32(me->desc, SD_CAPS));
    RET(DD_OK, 3);
END
/* Load: the source texture's pixels (and key) into this one, as the drivers' copy from system memory did */
M(tex_Load, 2) ME(DSurface, O_SURF)
    DSurface *src = obj(ARG(1), O_SURF); if (!src) RET(DDERR_INVALIDPARAMS, 2);
    for (DSurface *d = me, *s2 = src; d && s2; d = d->nattached ? d->attached[0] : 0, s2 = s2->nattached ? s2->attached[0] : 0) {
        uint32_t h = U32(d->desc, SD_HEIGHT) < U32(s2->desc, SD_HEIGHT) ? U32(d->desc, SD_HEIGHT) : U32(s2->desc, SD_HEIGHT);
        uint32_t row = (uint32_t)(d->pitch < s2->pitch ? d->pitch : s2->pitch);
        for (uint32_t y = 0; y < h; y++) memcpy(G_MEM + d->mem + y * (uint32_t)d->pitch, G_MEM + s2->mem + y * (uint32_t)s2->pitch, row);
        d->has_ck = s2->has_ck; d->ck[0] = s2->ck[0]; d->ck[1] = s2->ck[1]; d->upload_over = 0; d->gpu_dirty = 0;
        int32_t u[4] = {0, 0, (int32_t)U32(d->desc, SD_WIDTH), (int32_t)h}; upload_rect(d, u);
        if (d == me && !(U32(me->desc, SD_CAPS) & DDSCAPS_MIPMAP)) break;
    }
    RET(DD_OK, 2);
END
OK_M(tex_PaletteChanged, 3) OK_M(tex1_Initialize, 3) OK_M(tex1_Unload, 1)

/* ================= materials, lights ================= */
static uint32_t mat_new(int type) { DMat *m = calloc(1, sizeof *m); m->ref = 1; U32(m->m, 0) = 80; m->g = make_iface(type, m); return m->g; }
static DMat *mat_of(uint32_t g) { DMat *m = obj(g, O_MAT); return m ? m : obj(g, O_MAT1); }
M(mat_QueryInterface, 3) rt_w32(G_MEM, ARG(2), 0); RET(E_NOINTERFACE, 3); END
M(mat_AddRef, 1) DMat *me = mat_of(ARG(0)); RET(me ? (uint32_t)++me->ref : 0, 1); END
M(mat_Release, 1)
    DMat *me = mat_of(ARG(0)); if (!me) RET(0, 1);
    uint32_t r = (uint32_t)--me->ref;
    if (!r) { if (me->handle && me->handle < 1024) mat_handles[me->handle] = 0; obj_drop(me->g); free(me); }
    RET(r, 1);
END
M(mat_SetMaterial, 2) DMat *me = mat_of(ARG(0)); if (!me) RET(DDERR_INVALIDPARAMS, 2);
    uint32_t sz = rt_r32(G_MEM, ARG(1)); if (!sz || sz > 80) sz = 80; memcpy(me->m, GP(ARG(1)), sz);
    { static int n; static uint32_t last = 0xffffffffu; float f[4]; memcpy(f, me->m + 4, 16);
      if (log_on > 0 && n < 20 && U32(me->m, 72) != last) { n++; last = U32(me->m, 72); dsr_log("SetMaterial size %u diffuse %.2f %.2f %.2f %.2f texture %u", sz, f[0], f[1], f[2], f[3], U32(me->m, 72)); } }
    RET(DD_OK, 2); END
M(mat_GetMaterial, 2) DMat *me = mat_of(ARG(0)); if (!me) RET(DDERR_INVALIDPARAMS, 2);
    uint32_t sz = rt_r32(G_MEM, ARG(1)); if (!sz || sz > 80) sz = 80; memcpy(GP(ARG(1)), me->m, sz); RET(DD_OK, 2); END
M(mat_GetHandle, 3) DMat *me = mat_of(ARG(0)); if (!me) RET(DDERR_INVALIDPARAMS, 3);
    if (!me->handle) for (uint32_t k = 1; k < 1024; k++) if (!mat_handles[k]) { mat_handles[k] = me; me->handle = k; break; }
    rt_w32(G_MEM, ARG(2), me->handle); RET(DD_OK, 3); END
OK_M(mat1_Initialize, 2) OK_M(mat1_Reserve, 1) OK_M(mat1_Unreserve, 1)
static uint32_t mat_color(uint32_t handle)                        /* a material's diffuse colour, as a clear colour */
{
    DMat *m = handle && handle < 1024 ? mat_handles[handle] : 0; if (!m) return 0;
    float f[4]; memcpy(f, m->m + 4, 16); uint32_t c = 0;
    for (int k = 0; k < 4; k++) { float v = f[k] < 0 ? 0 : f[k] > 1 ? 1 : f[k]; uint32_t b = (uint32_t)(v * 255 + 0.5f); c |= b << (k == 3 ? 24 : 16 - 8 * k); }
    return c;
}
M(light_QueryInterface, 3) rt_w32(G_MEM, ARG(2), 0); RET(E_NOINTERFACE, 3); END
M(light_AddRef, 1) DLight *me = obj(ARG(0), O_LIGHT); RET(me ? (uint32_t)++me->ref : 0, 1); END
M(light_Release, 1) DLight *me = obj(ARG(0), O_LIGHT); if (!me) RET(0, 1); uint32_t r = (uint32_t)--me->ref; if (!r) { obj_drop(me->g); free(me); } RET(r, 1); END
OK_M(light_Initialize, 2)
M(light_SetLight, 2) DLight *me = obj(ARG(0), O_LIGHT); if (me) { uint32_t sz = rt_r32(G_MEM, ARG(1)); if (!sz || sz > 104) sz = 104; memcpy(me->l, GP(ARG(1)), sz); } RET(DD_OK, 2); END
M(light_GetLight, 2) DLight *me = obj(ARG(0), O_LIGHT); if (me) { uint32_t sz = rt_r32(G_MEM, ARG(1)); if (!sz || sz > 104) sz = 104; memcpy(GP(ARG(1)), me->l, sz); } RET(DD_OK, 2); END

/* ================= viewports (IDirect3DViewport 1-3) ================= */
static void vp_apply(DVp *v)
{
    DDevice *d = v->dev; if (!d) return;
    uint32_t q[6]; float mn, mx;
    if (v->have2) { memcpy(q, v->v2 + 4, 16); memcpy(&mn, v->v2 + 36, 4); memcpy(&mx, v->v2 + 40, 4); }
    else { memcpy(q, v->v1 + 4, 16); memcpy(&mn, v->v1 + 36, 4); memcpy(&mx, v->v1 + 40, 4); }
    if (!(mx > mn)) { mn = 0; mx = 1; }
    memcpy(&q[4], &mn, 4); memcpy(&q[5], &mx, 4);
    memcpy(d->vp, q, 24); uint32_t *p = cmd_begin(DSR_VIEWPORT, 24); memcpy(p, q, 24); cmd_end();
}
M(vp_QueryInterface, 3) DVp *me = obj(ARG(0), O_VP); if (!me) RET(DDERR_INVALIDPARAMS, 3);
    uint32_t iid = rt_r32(G_MEM, ARG(1));
    if (iid == 0 || iid == 0x4417c146u || iid == 0x93281500u || iid == 0xb0ab3b61u) { rt_w32(G_MEM, ARG(2), me->g); me->ref++; RET(DD_OK, 3); }
    rt_w32(G_MEM, ARG(2), 0); RET(E_NOINTERFACE, 3); END
M(vp_AddRef, 1) DVp *me = obj(ARG(0), O_VP); RET(me ? (uint32_t)++me->ref : 0, 1); END
M(vp_Release, 1) DVp *me = obj(ARG(0), O_VP); if (!me) RET(0, 1);
    uint32_t r = (uint32_t)--me->ref;
    if (!r) { if (me->dev && me->dev->old) { DOld *o = me->dev->old; for (int k = 0; k < o->nvp; k++) if (o->vps[k] == me) { memmove(&o->vps[k], &o->vps[k + 1], (size_t)(o->nvp - k - 1) * sizeof *o->vps); o->nvp--; break; } if (o->cur == me) o->cur = 0; }
              obj_drop(me->g); free(me); }
    RET(r, 1); END
OK_M(vp_Initialize, 2)
M(vp_GetViewport, 2) DVp *me = obj(ARG(0), O_VP); if (!me) RET(DDERR_INVALIDPARAMS, 2); memcpy(GP(ARG(1)) + 4, me->v1 + 4, 40); RET(DD_OK, 2); END
M(vp_SetViewport, 2) DVp *me = obj(ARG(0), O_VP); if (!me) RET(DDERR_INVALIDPARAMS, 2);
    memcpy(me->v1, GP(ARG(1)), 44); me->have2 = 0;
    memcpy(me->v2 + 4, me->v1 + 4, 16); memcpy(me->v2 + 36, me->v1 + 36, 8);            /* the same rectangle and depth range in the other form */
    if (me->dev && me->dev->old && me->dev->old->cur == me) vp_apply(me);
    RET(DD_OK, 2); END
M(vp_GetViewport2, 2) DVp *me = obj(ARG(0), O_VP); if (!me) RET(DDERR_INVALIDPARAMS, 2); memcpy(GP(ARG(1)) + 4, me->v2 + 4, 40); RET(DD_OK, 2); END
M(vp_SetViewport2, 2) DVp *me = obj(ARG(0), O_VP); if (!me) RET(DDERR_INVALIDPARAMS, 2);
    memcpy(me->v2, GP(ARG(1)), 44); me->have2 = 1;
    memcpy(me->v1 + 4, me->v2 + 4, 16); memcpy(me->v1 + 36, me->v2 + 36, 8);
    if (me->dev && me->dev->old && me->dev->old->cur == me) vp_apply(me);
    RET(DD_OK, 2); END
M(vp_SetBackground, 2) DVp *me = obj(ARG(0), O_VP); if (me) me->bg = ARG(1); RET(DD_OK, 2); END
M(vp_GetBackground, 3) DVp *me = obj(ARG(0), O_VP); if (me) { rt_w32(G_MEM, ARG(1), me->bg); if (ARG(2)) rt_w32(G_MEM, ARG(2), me->bg != 0); } RET(DD_OK, 3); END
OK_M(vp_SetBackgroundDepth, 2) OK_M(vp_SetBackgroundDepth2, 2)
M(vp_GetBackgroundDepth, 3) rt_w32(G_MEM, ARG(1), 0); if (ARG(2)) rt_w32(G_MEM, ARG(2), 0); RET(DD_OK, 3); END
M(vp_GetBackgroundDepth2, 3) rt_w32(G_MEM, ARG(1), 0); if (ARG(2)) rt_w32(G_MEM, ARG(2), 0); RET(DD_OK, 3); END
static void vp_clear(DVp *v, uint32_t n, uint32_t rects, uint32_t flags, uint32_t color, float z, uint32_t stencil)
{
    if (!v->dev) return;
    if (v->dev->old && v->dev->old->cur != v) vp_apply(v);
    if (!rects || n > 4096) n = 0;
    { static int k; if (log_on > 0 && k++ % 200 == 0) { int32_t r4[4] = {0, 0, 0, 0}; if (n) memcpy(r4, GP(rects), 16);
        dsr_log("viewport clear flags %u colour %08x z %.3f rects %u: %d,%d-%d,%d (rt %u)", flags, color, z, n, r4[0], r4[1], r4[2], r4[3], v->dev->rt ? v->dev->rt->id : 0); } }
    uint32_t *p = cmd_begin(DSR_CLEAR, 20 + 16 * n);
    p[0] = flags; p[1] = color; memcpy(&p[2], &z, 4); p[3] = stencil; p[4] = n; if (n) memcpy(p + 5, GP(rects), 16 * n);
    cmd_end();
    /* a background material with a texture (D3DMATERIAL.hTexture): clearing the target fills the viewport with that
     * texture, stretched over it. Resident Evil 2's rooms are drawn so, its pre-rendered picture as the background. */
    DMat *m = v->bg && v->bg < 1024 ? mat_handles[v->bg] : 0; DSurface *t = m ? tex_of_handle(U32(m->m, 72)) : 0;
    if ((flags & 1) && t && v->dev->rt) {
        const uint8_t *q = v->have2 ? v->v2 : v->v1; int32_t dr[4], sr[4] = {0, 0, (int32_t)U32(t->desc, SD_WIDTH), (int32_t)U32(t->desc, SD_HEIGHT)};
        dr[0] = (int32_t)U32(q, 4); dr[1] = (int32_t)U32(q, 8); dr[2] = dr[0] + (int32_t)U32(q, 12); dr[3] = dr[1] + (int32_t)U32(q, 16);
        if (dr[2] > dr[0] && dr[3] > dr[1]) emit_blt(v->dev->rt, dr, t, sr, 0, 0);
    }
    if (v->dev->old && v->dev->old->cur && v->dev->old->cur != v) vp_apply(v->dev->old->cur);
}
M(vp_Clear, 4) DVp *me = obj(ARG(0), O_VP); if (!me) RET(DDERR_INVALIDPARAMS, 4);
    vp_clear(me, ARG(1), ARG(2), ARG(3), mat_color(me->bg), 1.0f, 0); RET(DD_OK, 4); END
M(vp_Clear2, 7) DVp *me = obj(ARG(0), O_VP); if (!me) RET(DDERR_INVALIDPARAMS, 7);
    float z; uint32_t zb = ARG(5); memcpy(&z, &zb, 4); vp_clear(me, ARG(1), ARG(2), ARG(3), ARG(4), z, ARG(6)); RET(DD_OK, 7); END
UNIMPL_M(vp_TransformVertices, 5) UNIMPL_M(vp_LightElements, 3)
OK_M(vp_AddLight, 2) OK_M(vp_DeleteLight, 2)
M(vp_NextLight, 4) rt_w32(G_MEM, ARG(2), 0); RET(DDERR_INVALIDPARAMS, 4); END
M(d3d_old_CreateViewport, 3) DVp *v = calloc(1, sizeof *v); v->ref = 1; U32(v->v1, 0) = 44; U32(v->v2, 0) = 44; v->g = make_iface(O_VP, v);
    rt_w32(G_MEM, ARG(1), v->g); RET(DD_OK, 3); END
M(d3d_old_CreateLight, 3) DLight *l = calloc(1, sizeof *l); l->ref = 1; l->g = make_iface(O_LIGHT, l); rt_w32(G_MEM, ARG(1), l->g); RET(DD_OK, 3); END
M(d3d_old_CreateMaterial, 3) rt_w32(G_MEM, ARG(1), mat_new(O_MAT)); RET(DD_OK, 3); END
M(d3d1_CreateMaterial, 3) rt_w32(G_MEM, ARG(1), mat_new(O_MAT1)); RET(DD_OK, 3); END

/* ================= IDirect3D, IDirect3D2, IDirect3D3 ================= */
M(d3d1_EnumDevices, 3) enum_devices(c, ARG(1), ARG(2), 1); RET(DD_OK, 3); END
M(d3d2_EnumDevices, 3) enum_devices(c, ARG(1), ARG(2), 2); RET(DD_OK, 3); END
M(d3d3_EnumDevices, 3) enum_devices(c, ARG(1), ARG(2), 3); RET(DD_OK, 3); END
OK_M(d3d1_Initialize, 2)
M(d3d_old_FindDevice, 3)
    uint32_t res = ARG(2), sz = rt_r32(G_MEM, res), half = sz > 20 ? (sz - 20) / 2 : 0; uint8_t b[252];
    memcpy(GP(res + 4), GUID_HAL, 16);
    if (half) { if (half > 252) half = 252; dev_desc(b, half, 1); memcpy(GP(res + 20), b, half); memcpy(GP(res + 20 + half), b, half); }
    RET(DD_OK, 3);
END
static uint32_t old_device_create(Ctx *c, DDraw *dd, DSurface *rt, int type, int ver)
{
    uint32_t g7 = device_create(dd, rt); DDevice *d = obj(g7, O_DEV);
    DOld *o = calloc(1, sizeof *o); d->old = o; o->ver = ver; o->mapblend = 2; o->afunc = 8; o->lit = -1;
    o->ck_enable = ver == 1;                             /* Direct3D 1: a texture's colour key always applies (no COLORKEYENABLE yet) */
    o->g_dev = make_iface(type, d);
    if (!(dd->coop & 0x1000u) && !getenv("W32_NO_FPUSETUP")) rt_fcw_set(c, (c->fcw & ~0xf3fu) | 0x3fu);   /* as Direct3D: single precision unless FPUPRESERVE */
    /* Direct3D 2/3's defaults where they differ from 7's: no lighting of their pre-lit vertices, point filtering */
    send_rs(d, 137, 0); o->lit = 0;
    send_tss(d, 0, 16, 1); send_tss(d, 0, 17, 1); send_tss(d, 0, 18, 1);
    dsr_log("Direct3D %d device on surface %u", ver, rt ? rt->id : 0);
    return o->g_dev;
}
M(d3d2_CreateDevice, 4) ME(DDraw, O_D3D)
    DSurface *rt = obj(ARG(2), O_SURF); rt_w32(G_MEM, ARG(3), old_device_create(c, me, rt, O_DEV2, 2)); RET(DD_OK, 4); END
M(d3d3_CreateDevice, 5) ME(DDraw, O_D3D)
    DSurface *rt = obj(ARG(2), O_SURF); rt_w32(G_MEM, ARG(3), old_device_create(c, me, rt, O_DEV3, 3)); RET(DD_OK, 5); END
M(d3d3_CreateVertexBuffer, 5) rt_w32(G_MEM, ARG(2), vb_create(ARG(1))); RET(DD_OK, 5); END
/* Direct3D 1: a device is the render target surface's interface (QueryInterface with the device's GUID) */
uint32_t d3d1_device_qi(Ctx *c, DSurface *s, uint32_t iid_ptr)
{
    if (memcmp(GP(iid_ptr), GUID_HAL, 16) && memcmp(GP(iid_ptr), GUID_RGB, 16) && memcmp(GP(iid_ptr), GUID_MMX, 16) && memcmp(GP(iid_ptr), GUID_RAMP, 16)) return 0;
    return old_device_create(c, s->dd, s, O_DEV1, 1);
}

/* ================= IDirect3DDevice, IDirect3DDevice2, IDirect3DDevice3 ================= */
#define MD(name, n) M(name, n) DDevice *me = obj(ARG(0), O_DEV); if (!me || !me->old) RET(DDERR_INVALIDPARAMS, n); DOld *o = me->old; (void)o;
M(dev_old_QueryInterface, 3) DDevice *me = obj(ARG(0), O_DEV); if (!me || !me->old) RET(DDERR_INVALIDPARAMS, 3);
    uint32_t iid = rt_r32(G_MEM, ARG(1));
    if (iid == 0 || iid == 0x64108800u || iid == 0x93281501u || iid == 0xb0ab3b60u) { rt_w32(G_MEM, ARG(2), me->old->g_dev); me->ref++; RET(DD_OK, 3); }
    if (iid == 0xf5049e79u) { rt_w32(G_MEM, ARG(2), me->g); me->ref++; RET(DD_OK, 3); }
    rt_w32(G_MEM, ARG(2), 0); RET(E_NOINTERFACE, 3); END
MD(dev_old_GetCaps, 3)
    for (int k = 1; k <= 2; k++) if (ARG(k)) { uint32_t sz = rt_r32(G_MEM, ARG(k)); uint8_t b[252]; if (!sz || sz > 252) sz = 252; dev_desc(b, sz, 1); memcpy(GP(ARG(k)), b, sz); }
    RET(DD_OK, 3); END
MD(dev_old_GetStats, 2) uint32_t sz = rt_r32(G_MEM, ARG(1)); if (sz > 4 && sz <= 64) memset(GP(ARG(1)) + 4, 0, sz - 4); RET(DD_OK, 2); END
MD(dev_old_AddViewport, 2) DVp *v = obj(ARG(1), O_VP); if (!v) RET(DDERR_INVALIDPARAMS, 2);
    if (o->nvp < 32) { o->vps[o->nvp++] = v; v->ref++; v->dev = me; } RET(DD_OK, 2); END
MD(dev_old_DeleteViewport, 2) DVp *v = obj(ARG(1), O_VP); if (!v) RET(DDERR_INVALIDPARAMS, 2);
    for (int k = 0; k < o->nvp; k++) if (o->vps[k] == v) { memmove(&o->vps[k], &o->vps[k + 1], (size_t)(o->nvp - k - 1) * sizeof *o->vps); o->nvp--; if (o->cur == v) o->cur = 0; v->dev = 0; v->ref--; break; }
    RET(DD_OK, 2); END
MD(dev_old_NextViewport, 4) DVp *v = obj(ARG(1), O_VP); uint32_t fl = ARG(3); DVp *r = 0;
    if (fl == 2) r = o->nvp ? o->vps[0] : 0;                         /* D3DNEXT_HEAD */
    else if (fl == 4) r = o->nvp ? o->vps[o->nvp - 1] : 0;           /* D3DNEXT_TAIL */
    else for (int k = 0; k + 1 < o->nvp; k++) if (o->vps[k] == v) { r = o->vps[k + 1]; break; }
    rt_w32(G_MEM, ARG(2), r ? r->g : 0); RET(r ? DD_OK : DDERR_INVALIDPARAMS, 4); END
MD(dev_old_SetCurrentViewport, 2) DVp *v = obj(ARG(1), O_VP); if (!v) RET(DDERR_INVALIDPARAMS, 2);
    o->cur = v; v->dev = me; vp_apply(v); RET(DD_OK, 2); END
MD(dev_old_GetCurrentViewport, 2) rt_w32(G_MEM, ARG(1), o->cur ? o->cur->g : 0); if (o->cur) o->cur->ref++; RET(o->cur ? DD_OK : DDERR_INVALIDPARAMS, 2); END
MD(dev_old_GetDirect3D, 2) rt_w32(G_MEM, ARG(1), dd_old_iface(me->dd, o->ver >= 3 ? O_D3D3 : o->ver == 2 ? O_D3D2 : O_D3D1)); me->dd->ref++; RET(DD_OK, 2); END
MD(dev_old_GetRenderTarget, 2) rt_w32(G_MEM, ARG(1), surf_iface(me->rt, o->ver >= 3 ? O_S4 : O_S1)); surf_addref(me->rt); RET(DD_OK, 2); END
static void enum_tex_formats(Ctx *c, uint32_t cb, uint32_t ctx, int as_sd)
{
    /* 16-bit formats first (what these games were made for), then 32-bit */
    static const uint32_t f[][6] = {   /* flags, bits, r, g, b, a */
        {0x40, 16, 0xf800, 0x7e0, 0x1f, 0}, {0x40, 16, 0x7c00, 0x3e0, 0x1f, 0}, {0x41, 16, 0x7c00, 0x3e0, 0x1f, 0x8000},
        {0x41, 16, 0xf00, 0xf0, 0xf, 0xf000}, {0x41, 32, 0xff0000, 0xff00, 0xff, 0xff000000u}, {0x40, 32, 0xff0000, 0xff00, 0xff, 0} };
    uint32_t b = scratch(SD_BYTES);
    for (size_t k = 0; k < sizeof f / sizeof *f; k++) {
        uint8_t *pf = GP(b) + (as_sd ? SD_PF : 0);
        memset(GP(b), 0, SD_BYTES);
        if (as_sd) { U32(GP(b), 0) = 108; U32(GP(b), SD_FLAGS) = DDSD_CAPS | DDSD_PIXELFORMAT; U32(GP(b), SD_CAPS) = DDSCAPS_TEXTURE; }
        U32(pf, PF_SIZE) = 32; U32(pf, PF_FLAGS) = f[k][0]; U32(pf, PF_BITS) = f[k][1]; U32(pf, PF_R) = f[k][2]; U32(pf, PF_G) = f[k][3]; U32(pf, PF_B) = f[k][4]; U32(pf, PF_A) = f[k][5];
        uint32_t a[2] = {b, ctx};
        if (w32_callback(c, cb, 2, a) == 0) break;
    }
    scratch_free(b);
}
MD(dev2_EnumTextureFormats, 3) enum_tex_formats(c, ARG(1), ARG(2), 1); RET(DD_OK, 3); END
MD(dev3_EnumTextureFormats, 3) enum_tex_formats(c, ARG(1), ARG(2), 0); RET(DD_OK, 3); END
/* render states: the ones from before texture stages become stage 0's state */
static void old_render_state(DDevice *me, uint32_t s, uint32_t v)
{
    DOld *o = me->old;
    { static int lg = -1; if (lg < 0) lg = getenv("DSR_RSLOG") != 0;                /* development: each new (state, value) */
      static uint64_t seen[512]; static int ns; uint64_t k = (uint64_t)s << 32 | v; int have = 0;
      if (lg) { for (int i = 0; i < ns; i++) if (seen[i] == k) { have = 1; break; } if (!have && ns < 512) { seen[ns++] = k; fprintf(stderr, "dsr: render state %u = %#x\n", s, v); } } }
    switch (s) {
    case 1: o->legacy_tex = 1; set_tex(me, 0, tex_of_handle(v)); me->rs[1] = v; update_blend(me); return;    /* TEXTUREHANDLE */
    case 21: o->legacy_tex = 1; o->mapblend = v; me->rs[21] = v; update_blend(me); return;                     /* TEXTUREMAPBLEND */
    case 17: me->rs[17] = v; send_tss(me, 0, 16, v == 1 ? 1 : 2); return;                                      /* TEXTUREMAG */
    case 18: me->rs[18] = v;                                                                                    /* TEXTUREMIN */
        send_tss(me, 0, 17, (v == 1 || v == 3 || v == 5) ? 1 : 2); send_tss(me, 0, 18, v <= 2 ? 1 : v <= 4 ? 2 : 3); return;
    case 3: me->rs[3] = v; send_tss(me, 0, 12, v); return;                                                     /* TEXTUREADDRESS */
    case 44: me->rs[44] = v; send_tss(me, 0, 13, v); return;                                                   /* TEXTUREADDRESSU */
    case 45: me->rs[45] = v; send_tss(me, 0, 14, v); return;                                                   /* TEXTUREADDRESSV */
    case 41: me->rs[41] = v; o->ck_enable = v; return;                                                          /* COLORKEYENABLE */
    case 15: me->rs[15] = v; o->atest = v; return;                                                              /* the alpha test: with the key's (sync_alpha_test) */
    case 24: me->rs[24] = v; o->aref = v; return;
    case 25: me->rs[25] = v; o->afunc = v; return;
    }
    send_rs(me, s, v);
}
MD(dev_old_SetRenderState, 3) old_render_state(me, ARG(1), ARG(2)); RET(DD_OK, 3); END
MD(dev_old_GetLightState, 3) rt_w32(G_MEM, ARG(2), ARG(1) < 16 ? o->ls[ARG(1)] : 0); RET(DD_OK, 3); END
MD(dev_old_SetLightState, 3)
    uint32_t t = ARG(1), v = ARG(2); if (t < 16) o->ls[t] = v;
    switch (t) {
    case 1: { DMat *m = v && v < 1024 ? mat_handles[v] : 0; if (m) memcpy(me->material, m->m + 4, 68); break; }   /* MATERIAL */
    case 2: send_rs(me, 139, v); break;                              /* AMBIENT */
    case 4: send_rs(me, 140, v); break;                              /* FOGMODE: the vertex fog mode */
    case 5: send_rs(me, 36, v); break; case 6: send_rs(me, 37, v); break; case 7: send_rs(me, 38, v); break;   /* FOGSTART, END, DENSITY */
    case 8: send_rs(me, 141, v); break;                              /* COLORVERTEX */
    }
    RET(DD_OK, 3); END
/* drawing */
MD(dev2_DrawPrimitive, 6) uint32_t fvf = vtype_fvf(ARG(2)); if (!fvf) RET(DDERR_INVALIDPARAMS, 6);
    draw_old(me, ARG(1), fvf, GP(ARG(3)), ARG(4), 0, 0, ARG(5)); RET(DD_OK, 6); END
MD(dev2_DrawIndexedPrimitive, 8) uint32_t fvf = vtype_fvf(ARG(2)); if (!fvf) RET(DDERR_INVALIDPARAMS, 8);
    draw_old(me, ARG(1), fvf, GP(ARG(3)), ARG(4), (const uint16_t *)GP(ARG(5)), ARG(6), ARG(7)); RET(DD_OK, 8); END
MD(dev3_DrawPrimitive, 6) draw_old(me, ARG(1), ARG(2), GP(ARG(3)), ARG(4), 0, 0, ARG(5)); RET(DD_OK, 6); END
MD(dev3_DrawIndexedPrimitive, 8) draw_old(me, ARG(1), ARG(2), GP(ARG(3)), ARG(4), (const uint16_t *)GP(ARG(5)), ARG(6), ARG(7)); RET(DD_OK, 8); END
MD(dev3_DrawPrimitiveVB, 6) DVB *v = obj(ARG(2), O_VB); if (!v) RET(DDERR_INVALIDPARAMS, 6);
    draw_old(me, ARG(1), v->desc[2], G_MEM + v->mem + ARG(3) * v->stride, ARG(4), 0, 0, ARG(5)); RET(DD_OK, 6); END
MD(dev3_DrawIndexedPrimitiveVB, 6) DVB *v = obj(ARG(2), O_VB); if (!v) RET(DDERR_INVALIDPARAMS, 6);
    draw_old(me, ARG(1), v->desc[2], G_MEM + v->mem, v->desc[3], (const uint16_t *)GP(ARG(3)), ARG(4), ARG(5)); RET(DD_OK, 6); END
/* Begin / Vertex / End: the vertices gathered, drawn at End */
static void imm_begin(DOld *o, uint32_t prim, uint32_t fvf, int indexed)
{
    o->imm_on = 1; o->imm_prim = prim; o->imm_fvf = fvf; o->imm_stride = fvf_stride_any(fvf); o->imm_n = 0; o->idx_n = 0; o->indexed = indexed;
}
static void imm_vertex(DOld *o, const uint8_t *v)
{
    if (o->imm_n >= 65536) return;
    if ((o->imm_n + 1) * o->imm_stride > o->imm_cap) { o->imm_cap = (o->imm_cap ? o->imm_cap * 2 : 4096) + o->imm_stride; o->imm = realloc(o->imm, o->imm_cap); }
    memcpy(o->imm + o->imm_n * o->imm_stride, v, o->imm_stride); o->imm_n++;
}
MD(dev2_Begin, 4) uint32_t fvf = vtype_fvf(ARG(2)); if (!fvf) RET(DDERR_INVALIDPARAMS, 4); imm_begin(o, ARG(1), fvf, 0); RET(DD_OK, 4); END
MD(dev3_Begin, 4) imm_begin(o, ARG(1), ARG(2), 0); RET(DD_OK, 4); END
MD(dev2_BeginIndexed, 6) uint32_t fvf = vtype_fvf(ARG(2)); if (!fvf) RET(DDERR_INVALIDPARAMS, 6);
    imm_begin(o, ARG(1), fvf, 1); for (uint32_t k = 0; k < ARG(4); k++) imm_vertex(o, GP(ARG(3)) + k * o->imm_stride); RET(DD_OK, 6); END
MD(dev3_BeginIndexed, 6) imm_begin(o, ARG(1), ARG(2), 1); for (uint32_t k = 0; k < ARG(4); k++) imm_vertex(o, GP(ARG(3)) + k * o->imm_stride); RET(DD_OK, 6); END
MD(dev_old_Vertex, 2) if (o->imm_on) imm_vertex(o, GP(ARG(1))); RET(DD_OK, 2); END
MD(dev_old_Index, 2) if (o->imm_on) { if (o->idx_n >= o->idx_cap) { o->idx_cap = o->idx_cap ? o->idx_cap * 2 : 1024; o->idx = realloc(o->idx, o->idx_cap * 2); } o->idx[o->idx_n++] = (uint16_t)ARG(1); } RET(DD_OK, 2); END
MD(dev_old_End, 2) if (o->imm_on) { o->imm_on = 0; draw_old(me, o->imm_prim, o->imm_fvf, o->imm, o->imm_n, o->indexed ? o->idx : 0, o->indexed ? o->idx_n : 0, ARG(1)); } RET(DD_OK, 2); END
OK_M(dev2_SwapTextureHandles, 3)


/* ================= Direct3D 1: IDirect3DDevice and execute buffers (Resident Evil) =================
 * The game fills a buffer with vertices and an instruction stream (D3DINSTRUCTION: opcode, size, count, then count
 * records): process vertices into the device's vertex array (copied as they are, or transformed by the world, view and
 * projection matrices onto the viewport), set render, light and transform states, draw triangles, lines and points
 * indexed into that array, load textures. Execute runs it here: each run of triangles is one indexed draw of the
 * transformed-and-lit vertices (D3DTLVERTEX: the renderer's FVF 0x1c4). */
typedef struct DExb { uint32_t g; int ref; uint32_t mem, size; uint8_t data[48]; uint32_t status; } DExb;
static float mat1[512][16]; static uint8_t mat1_used[512];
static void mat_mul4(float *d, const float *a, const float *b)       /* d = a x b, row vectors */
{
    float t[16]; for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) t[r * 4 + k] = a[r * 4] * b[k] + a[r * 4 + 1] * b[4 + k] + a[r * 4 + 2] * b[8 + k] + a[r * 4 + 3] * b[12 + k];
    memcpy(d, t, 64);
}
static const float *mat1_of(uint32_t h) { static const float id[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; return h && h < 512 && mat1_used[h] ? mat1[h] : id; }
typedef struct DOld1 { uint32_t xf[4]; uint8_t *tlv; uint32_t ntlv; } DOld1;
static DOld1 *old1(DDevice *d) { static DOld1 *tab[8]; static DDevice *who[8]; for (int k = 0; k < 8; k++) if (who[k] == d) return tab[k];
    for (int k = 0; k < 8; k++) if (!who[k]) { who[k] = d; tab[k] = calloc(1, sizeof(DOld1)); return tab[k]; } return tab[0]; }
static void light_state(DDevice *me, uint32_t t, uint32_t v)
{
    DOld *o = me->old; if (t < 16) o->ls[t] = v;
    switch (t) {
    case 1: { DMat *m = v && v < 1024 ? mat_handles[v] : 0; if (m) memcpy(me->material, m->m + 4, 68); break; }
    case 2: send_rs(me, 139, v); break; case 4: send_rs(me, 140, v); break;
    case 5: send_rs(me, 36, v); break; case 6: send_rs(me, 37, v); break; case 7: send_rs(me, 38, v); break; case 8: send_rs(me, 141, v); break;
    }
}
M(exb_QueryInterface, 3) DExb *me = obj(ARG(0), O_EXB); if (me) { me->ref++; rt_w32(G_MEM, ARG(2), me->g); RET(DD_OK, 3); } RET(E_NOINTERFACE, 3); END
M(exb_AddRef, 1) DExb *me = obj(ARG(0), O_EXB); RET(me ? (uint32_t)++me->ref : 0, 1); END
M(exb_Release, 1) DExb *me = obj(ARG(0), O_EXB); if (!me) RET(0, 1); uint32_t r = (uint32_t)--me->ref;
    if (!r) { heap_free(w32_process_heap, me->mem); obj_drop(me->g); free(me); } RET(r, 1); END
OK_M(exb_Initialize, 3) OK_M(exb_Unlock, 1) OK_M(exb_Validate, 2) OK_M(exb_Optimize, 2)
M(exb_Lock, 2) DExb *me = obj(ARG(0), O_EXB); if (!me) RET(DDERR_INVALIDPARAMS, 2);
    uint32_t d = ARG(1); rt_w32(G_MEM, d + 4, rt_r32(G_MEM, d + 4) | 0x2 | 0x10); rt_w32(G_MEM, d + 12, me->size); rt_w32(G_MEM, d + 16, me->mem); RET(DD_OK, 2); END
M(exb_SetExecuteData, 2) DExb *me = obj(ARG(0), O_EXB); if (!me) RET(DDERR_INVALIDPARAMS, 2);
    uint32_t sz = rt_r32(G_MEM, ARG(1)); if (!sz || sz > 48) sz = 48; memcpy(me->data, GP(ARG(1)), sz); RET(DD_OK, 2); END
M(exb_GetExecuteData, 2) DExb *me = obj(ARG(0), O_EXB); if (!me) RET(DDERR_INVALIDPARAMS, 2);
    uint32_t sz = rt_r32(G_MEM, ARG(1)); if (!sz || sz > 48) sz = 48; memcpy(GP(ARG(1)), me->data, sz); rt_w32(G_MEM, ARG(1), sz); RET(DD_OK, 2); END
MD(dev1_CreateExecuteBuffer, 4)
    uint32_t d = ARG(1), size = rt_r32(G_MEM, d + 12); if (!size || size > (16u << 20)) RET(DDERR_INVALIDPARAMS, 4);
    DExb *e = calloc(1, sizeof *e); e->ref = 1; e->size = size; e->mem = heap_alloc(w32_process_heap, 8, size + 16); U32(e->data, 0) = 48;
    e->g = make_iface(O_EXB, e); rt_w32(G_MEM, ARG(2), e->g); RET(DD_OK, 4); END
/* the vertex records into the device's array of transformed vertices */
static void process_vertices(DDevice *me, DExb *e, uint32_t flags, uint32_t start, uint32_t dest, uint32_t count)
{
    DOld1 *o1 = old1(me); uint32_t need = dest + count;
    if (need > 65536) return;
    if (need > o1->ntlv) { o1->tlv = realloc(o1->tlv, (size_t)need * 32); memset(o1->tlv + (size_t)o1->ntlv * 32, 0, (size_t)(need - o1->ntlv) * 32); o1->ntlv = need; }
    const uint8_t *src = G_MEM + e->mem + U32(e->data, 4) + (size_t)start * 32;
    if (U32(e->data, 4) + (size_t)(start + count) * 32 > e->size) return;
    uint8_t *dst = o1->tlv + (size_t)dest * 32;
    if ((flags & 7) == 2) { memcpy(dst, src, (size_t)count * 32); return; }    /* D3DPROCESSVERTICES_COPY: already D3DTLVERTEX */
    float wvp[16], wv[16]; mat_mul4(wv, mat1_of(o1->xf[1]), mat1_of(o1->xf[2])); mat_mul4(wvp, wv, mat1_of(o1->xf[3]));
    DVp *v = me->old->cur; float vx = 0, vy = 0, vw = 640, vh = 480, sx = 320, sy = 240;
    if (v) { vx = (float)U32(v->v1, 4); vy = (float)U32(v->v1, 8); vw = (float)U32(v->v1, 12); vh = (float)U32(v->v1, 16); memcpy(&sx, v->v1 + 20, 4); memcpy(&sy, v->v1 + 24, 4); }
    for (uint32_t k = 0; k < count; k++, src += 32, dst += 32) {
        float p[3], t[4]; memcpy(p, src, 12);
        for (int j = 0; j < 4; j++) t[j] = p[0] * wvp[j] + p[1] * wvp[4 + j] + p[2] * wvp[8 + j] + wvp[12 + j];
        float rhw = t[3] != 0 ? 1.0f / t[3] : 1.0f, out[4] = {vx + vw * 0.5f + t[0] * rhw * sx, vy + vh * 0.5f - t[1] * rhw * sy, t[2] * rhw, rhw};
        memcpy(dst, out, 16);
        if ((flags & 7) == 1) memcpy(dst + 16, src + 16, 16);              /* TRANSFORM: D3DLVERTEX's colours and coordinates */
        else { uint32_t white = 0xffffffffu, none = 0; memcpy(dst + 16, &white, 4); memcpy(dst + 20, &none, 4); memcpy(dst + 24, src + 24, 8); }   /* D3DVERTEX: not lit here */
    }
}
static void draw_tl(DDevice *me, uint32_t prim, const uint16_t *idx, uint32_t ni)
{
    DOld1 *o1 = old1(me); uint32_t mx = 0; for (uint32_t k = 0; k < ni; k++) if (idx[k] > mx) mx = idx[k];
    if (!ni || mx >= o1->ntlv) return;
    draw_old(me, prim, 0x1c4, o1->tlv, mx + 1, idx, ni, 0);
}
MD(dev1_Execute, 4)
    DExb *e = obj(ARG(1), O_EXB); DVp *v = obj(ARG(2), O_VP); if (!e) RET(DDERR_INVALIDPARAMS, 4);
    if (v) { v->dev = me; if (o->cur != v) { o->cur = v; vp_apply(v); } }
    DOld1 *o1 = old1(me);
    uint32_t ins = U32(e->data, 12), len = U32(e->data, 16); if (ins > e->size || len > e->size - ins) RET(DDERR_INVALIDPARAMS, 4);
    const uint8_t *p = G_MEM + e->mem + ins, *end = p + len; uint16_t *idx = 0; uint32_t cap = 0;
    while (p + 4 <= end) {
        uint32_t op = p[0], sz = p[1], cnt = (uint32_t)(p[2] | p[3] << 8); const uint8_t *d = p + 4; p = d + (size_t)sz * cnt;
        if (p > end) break;
        if (op == 11) break;                                                     /* EXIT */
        switch (op) {
        case 3: {                                                                /* TRIANGLE: v1 v2 v3 flags (WORDs) */
            if (cap < cnt * 3) { cap = cnt * 3; idx = realloc(idx, cap * 2); }
            for (uint32_t k = 0; k < cnt; k++) { memcpy(idx + k * 3, d + (size_t)k * sz, 6); }
            draw_tl(me, 4, idx, cnt * 3); break; }
        case 2: {                                                                /* LINE */
            if (cap < cnt * 2) { cap = cnt * 2; idx = realloc(idx, cap * 2); }
            for (uint32_t k = 0; k < cnt; k++) memcpy(idx + k * 2, d + (size_t)k * sz, 4);
            draw_tl(me, 2, idx, cnt * 2); break; }
        case 1:                                                                  /* POINT: count, first */
            for (uint32_t k = 0; k < cnt; k++) { uint16_t n = (uint16_t)(d[k * sz] | d[k * sz + 1] << 8), f0 = (uint16_t)(d[k * sz + 2] | d[k * sz + 3] << 8);
                if ((uint32_t)f0 + n <= o1->ntlv && n) draw_old(me, 1, 0x1c4, o1->tlv + (size_t)f0 * 32, n, 0, 0, 0); }
            break;
        case 4: for (uint32_t k = 0; k < cnt; k++) { uint32_t a = U32(d, k * sz), b = U32(d, k * sz + 4); if (a < 512) { memcpy(mat1[a], mat1_of(b), 64); mat1_used[a] = 1; } } break;   /* MATRIXLOAD */
        case 5: for (uint32_t k = 0; k < cnt; k++) { uint32_t a = U32(d, k * sz), b = U32(d, k * sz + 4), cc = U32(d, k * sz + 8); if (a < 512) { mat_mul4(mat1[a], mat1_of(b), mat1_of(cc)); mat1_used[a] = 1; } } break;   /* MATRIXMULTIPLY */
        case 6: for (uint32_t k = 0; k < cnt; k++) { uint32_t t = U32(d, k * sz), h = U32(d, k * sz + 4); if (t >= 1 && t <= 3) o1->xf[t] = h; } break;   /* STATETRANSFORM */
        case 7: for (uint32_t k = 0; k < cnt; k++) light_state(me, U32(d, k * sz), U32(d, k * sz + 4)); break;           /* STATELIGHT */
        case 8: for (uint32_t k = 0; k < cnt; k++) old_render_state(me, U32(d, k * sz), U32(d, k * sz + 4)); break;    /* STATERENDER */
        case 9: for (uint32_t k = 0; k < cnt; k++) { const uint8_t *q = d + (size_t)k * sz;                                 /* PROCESSVERTICES */
                    process_vertices(me, e, U32(q, 0), (uint32_t)(q[4] | q[5] << 8), (uint32_t)(q[6] | q[7] << 8), U32(q, 8)); } break;
        case 10: for (uint32_t k = 0; k < cnt; k++) { DSurface *dt = tex_of_handle(U32(d, k * sz)), *st = tex_of_handle(U32(d, k * sz + 4));   /* TEXTURELOAD */
                    if (dt && st) { int32_t u[4] = {0, 0, (int32_t)U32(dt->desc, SD_WIDTH), (int32_t)U32(dt->desc, SD_HEIGHT)};
                        uint32_t h = U32(dt->desc, SD_HEIGHT) < U32(st->desc, SD_HEIGHT) ? U32(dt->desc, SD_HEIGHT) : U32(st->desc, SD_HEIGHT), row = (uint32_t)(dt->pitch < st->pitch ? dt->pitch : st->pitch);
                        for (uint32_t y = 0; y < h; y++) memcpy(G_MEM + dt->mem + y * (uint32_t)dt->pitch, G_MEM + st->mem + y * (uint32_t)st->pitch, row);
                        dt->has_ck = st->has_ck; dt->ck[0] = st->ck[0]; dt->ck[1] = st->ck[1]; dt->upload_over = 0; upload_rect(dt, u); } } break;
        case 12: {                                                               /* BRANCHFORWARD: mask, value, negate, offset */
            uint32_t mask = U32(d, 0), val = U32(d, 4), neg = U32(d, 8), off = U32(d, 12); int take = ((e->status & mask) == val) != (neg != 0);
            if (take) { if (!off) p = end; else p = d + off; } break; }
        case 14: e->status = U32(d, 4); break;                                   /* SETSTATUS */
        default: break;                                                          /* SPAN: not drawn */
        }
    }
    free(idx); RET(DD_OK, 4);
END
MD(dev1_CreateMatrix, 2) uint32_t h = 0; for (uint32_t k = 1; k < 512; k++) if (!mat1_used[k]) { h = k; break; }
    if (h) { mat1_used[h] = 1; memcpy(mat1[h], mat1_of(0), 64); } rt_w32(G_MEM, ARG(1), h); RET(h ? DD_OK : DDERR_INVALIDPARAMS, 2); END
MD(dev1_SetMatrix, 3) uint32_t h = ARG(1); if (h && h < 512) { memcpy(mat1[h], GP(ARG(2)), 64); mat1_used[h] = 1; } RET(DD_OK, 3); END
MD(dev1_GetMatrix, 3) uint32_t h = ARG(1); memcpy(GP(ARG(2)), mat1_of(h), 64); RET(DD_OK, 3); END
MD(dev1_DeleteMatrix, 2) uint32_t h = ARG(1); if (h && h < 512) mat1_used[h] = 0; RET(DD_OK, 2); END
OK_M(dev1_Initialize, 4)
M(dev1_Pick, 5) RET(DD_OK, 5); END
M(dev1_GetPickRecords, 3) if (ARG(1)) rt_w32(G_MEM, ARG(1), 0); RET(DD_OK, 3); END
/* ================= the vtables ================= */
static void old_vtables(const Fn *dd, const Fn *surf, const Fn *dev)
{
    (void)dev;
    Fn d1[23]; memcpy(d1, dd, sizeof d1); d1[4] = dd_old_CreateClipper; d1[21] = dd1_SetDisplayMode;
    Fn d2[24]; memcpy(d2, dd, sizeof d2); d2[4] = dd_old_CreateClipper;
    Fn d4[28]; memcpy(d4, dd, sizeof d4); d4[4] = dd_old_CreateClipper; d4[27] = dd4_GetDeviceIdentifier;
    vt[O_DD1] = build_vtable("IDirectDraw", d1, 23); vt[O_DD2] = build_vtable("IDirectDraw2", d2, 24); vt[O_DD4] = build_vtable("IDirectDraw4", d4, 28);
    vt[O_S1] = build_vtable("IDirectDrawSurface", surf, 36); vt[O_S2] = build_vtable("IDirectDrawSurface2", surf, 39);
    vt[O_S3] = build_vtable("IDirectDrawSurface3", surf, 40); vt[O_S4] = build_vtable("IDirectDrawSurface4", surf, 45);
    static const Fn clip[] = { clip_QueryInterface, clip_AddRef, clip_Release, clip_GetClipList, clip_GetHWnd, clip_Initialize, clip_IsClipListChanged, clip_SetClipList, clip_SetHWnd };
    vt[O_CLIP] = build_vtable("IDirectDrawClipper", clip, 9);
    static const Fn t1[] = { s_QueryInterface, s_AddRef, s_Release, tex1_Initialize, tex_GetHandle, tex_PaletteChanged, tex_Load, tex1_Unload };
    static const Fn t2[] = { s_QueryInterface, s_AddRef, s_Release, tex_GetHandle, tex_PaletteChanged, tex_Load };
    vt[O_T1] = build_vtable("IDirect3DTexture", t1, 8); vt[O_T2] = build_vtable("IDirect3DTexture2", t2, 6);
    static const Fn m1[] = { mat_QueryInterface, mat_AddRef, mat_Release, mat1_Initialize, mat_SetMaterial, mat_GetMaterial, mat_GetHandle, mat1_Reserve, mat1_Unreserve };
    static const Fn m2[] = { mat_QueryInterface, mat_AddRef, mat_Release, mat_SetMaterial, mat_GetMaterial, mat_GetHandle };
    vt[O_MAT1] = build_vtable("IDirect3DMaterial", m1, 9); vt[O_MAT] = build_vtable("IDirect3DMaterial3", m2, 6);
    static const Fn li[] = { light_QueryInterface, light_AddRef, light_Release, light_Initialize, light_SetLight, light_GetLight };
    vt[O_LIGHT] = build_vtable("IDirect3DLight", li, 6);
    static const Fn vp[] = { vp_QueryInterface, vp_AddRef, vp_Release, vp_Initialize, vp_GetViewport, vp_SetViewport, vp_TransformVertices,
        vp_LightElements, vp_SetBackground, vp_GetBackground, vp_SetBackgroundDepth, vp_GetBackgroundDepth, vp_Clear, vp_AddLight,
        vp_DeleteLight, vp_NextLight, vp_GetViewport2, vp_SetViewport2, vp_SetBackgroundDepth2, vp_GetBackgroundDepth2, vp_Clear2 };
    vt[O_VP] = build_vtable("IDirect3DViewport3", vp, 21);
    static const Fn d3d1[] = { d3d_QueryInterface, d3d_AddRef, d3d_Release, d3d1_Initialize, d3d1_EnumDevices, d3d_old_CreateLight,
        d3d1_CreateMaterial, d3d_old_CreateViewport, d3d_old_FindDevice };
    static const Fn d3d2[] = { d3d_QueryInterface, d3d_AddRef, d3d_Release, d3d2_EnumDevices, d3d_old_CreateLight, d3d_old_CreateMaterial,
        d3d_old_CreateViewport, d3d_old_FindDevice, d3d2_CreateDevice };
    static const Fn d3d3[] = { d3d_QueryInterface, d3d_AddRef, d3d_Release, d3d3_EnumDevices, d3d_old_CreateLight, d3d_old_CreateMaterial,
        d3d_old_CreateViewport, d3d_old_FindDevice, d3d3_CreateDevice, d3d3_CreateVertexBuffer, d3d_EnumZBufferFormats, d3d_EvictManagedTextures };
    vt[O_D3D1] = build_vtable("IDirect3D", d3d1, 9); vt[O_D3D2] = build_vtable("IDirect3D2", d3d2, 9); vt[O_D3D3] = build_vtable("IDirect3D3", d3d3, 12);
    static const Fn dev2[] = { dev_old_QueryInterface, dev_AddRef, dev_Release, dev_old_GetCaps, dev2_SwapTextureHandles, dev_old_GetStats,
        dev_old_AddViewport, dev_old_DeleteViewport, dev_old_NextViewport, dev2_EnumTextureFormats, dev_BeginScene, dev_EndScene,
        dev_old_GetDirect3D, dev_old_SetCurrentViewport, dev_old_GetCurrentViewport, dev_SetRenderTarget, dev_old_GetRenderTarget,
        dev2_Begin, dev2_BeginIndexed, dev_old_Vertex, dev_old_Index, dev_old_End, dev_GetRenderState, dev_old_SetRenderState,
        dev_old_GetLightState, dev_old_SetLightState, dev_SetTransform, dev_GetTransform, dev_MultiplyTransform, dev2_DrawPrimitive,
        dev2_DrawIndexedPrimitive, dev_SetClipStatus, dev_GetClipStatus };
    static const Fn dev3[] = { dev_old_QueryInterface, dev_AddRef, dev_Release, dev_old_GetCaps, dev_old_GetStats, dev_old_AddViewport,
        dev_old_DeleteViewport, dev_old_NextViewport, dev3_EnumTextureFormats, dev_BeginScene, dev_EndScene, dev_old_GetDirect3D,
        dev_old_SetCurrentViewport, dev_old_GetCurrentViewport, dev_SetRenderTarget, dev_old_GetRenderTarget, dev3_Begin, dev3_BeginIndexed,
        dev_old_Vertex, dev_old_Index, dev_old_End, dev_GetRenderState, dev_old_SetRenderState, dev_old_GetLightState, dev_old_SetLightState,
        dev_SetTransform, dev_GetTransform, dev_MultiplyTransform, dev3_DrawPrimitive, dev3_DrawIndexedPrimitive, dev_SetClipStatus,
        dev_GetClipStatus, dev_DrawPrimitiveStrided, dev_DrawIndexedPrimitiveStrided, dev3_DrawPrimitiveVB, dev3_DrawIndexedPrimitiveVB,
        dev_ComputeSphereVisibility, dev_GetTexture, dev_SetTexture, dev_GetTextureStageState, dev_SetTextureStageState, dev_ValidateDevice };
    _Static_assert(sizeof dev2 / sizeof *dev2 == 33 && sizeof dev3 / sizeof *dev3 == 42 && sizeof vp / sizeof *vp == 21, "old vtable sizes");
    vt[O_DEV2] = build_vtable("IDirect3DDevice2", dev2, 33); vt[O_DEV3] = build_vtable("IDirect3DDevice3", dev3, 42);
    static const Fn dev1[] = { dev_old_QueryInterface, dev_AddRef, dev_Release, dev1_Initialize, dev_old_GetCaps, dev2_SwapTextureHandles,
        dev1_CreateExecuteBuffer, dev_old_GetStats, dev1_Execute, dev_old_AddViewport, dev_old_DeleteViewport, dev_old_NextViewport,
        dev1_Pick, dev1_GetPickRecords, dev2_EnumTextureFormats, dev1_CreateMatrix, dev1_SetMatrix, dev1_GetMatrix, dev1_DeleteMatrix,
        dev_BeginScene, dev_EndScene, dev_old_GetDirect3D };
    static const Fn exb[] = { exb_QueryInterface, exb_AddRef, exb_Release, exb_Initialize, exb_Lock, exb_Unlock, exb_SetExecuteData,
        exb_GetExecuteData, exb_Validate, exb_Optimize };
    _Static_assert(sizeof dev1 / sizeof *dev1 == 22 && sizeof exb / sizeof *exb == 10, "Direct3D 1 vtable sizes");
    vt[O_DEV1] = build_vtable("IDirect3DDevice", dev1, 22); vt[O_EXB] = build_vtable("IDirect3DExecuteBuffer", exb, 10);
}

/* ---- exports: DirectDraw before DirectDrawCreateEx (the GOG Resident Evil games and Dino Crisis; the other games
 * that call DirectDrawCreate draw in software: ddraw_pal.c) ---- */
static DDraw *dd_new(void);
#ifdef RE_EDITION
IMPL(ddraw, DirectDrawCreate)
{
    uint32_t out = ARG(1);
    dsr_log("DirectDrawCreate");
    DDraw *dd = dd_new();
    rt_w32(G_MEM, out, dd_old_iface(dd, O_DD1)); RET(DD_OK, 3);
}
IMPL(ddraw, DirectDrawEnumerateA)
{
    uint32_t cb = ARG(0), ctx = ARG(1), buf = scratch(96);
    strcpy((char *)GP(buf), "Primary Display Driver"); strcpy((char *)GP(buf + 64), "display");
    uint32_t a[4] = {0, buf, buf + 64, ctx}; w32_callback(c, cb, 4, a);
    scratch_free(buf); RET(DD_OK, 2);
}
#endif
