/* Direct3D 8 on the Direct3D 9 layer above (included at the end of d3d9.c, so it shares its objects and setters):
 * Grand Theft Auto III (RenderWare's Direct3D 8 driver). Every interface the game gets is a Direct3D 8 one: the
 * vtables of d3d9.c are swapped for these when Direct3DCreate8 is called (the game never loads Direct3D 9).
 * Methods whose arguments are the same in both versions are d3d9.c's own; the others convert:
 *   - texture-stage states 13-21 and 25 are Direct3D 9's sampler states (address, border, filters, LOD, anisotropy)
 *   - SetStreamSource has no offset; SetIndices carries the base vertex that DrawIndexedPrimitive then adds
 *   - SetVertexShader takes an FVF (Direct3D 8 has no SetFVF); the game's vertex/pixel shaders are not supported
 *   - present parameters, adapter identifier, caps and surface descriptions have Direct3D 8 layouts
 *   - state blocks are DWORD tokens (here the state block object's guest pointer)
 *   - surfaces derive from IUnknown (no priority/preload/type), CopyRects and CreateImageSurface exist */

static int d3d8_mode;
void w32_window_fullscreen(Ctx *c, uint32_t hwnd, int w, int h);   /* user32.c */
static void fullscreen_window(Ctx *c, uint32_t pp8, uint32_t focus)   /* Windowed = FALSE: the window takes the mode's size */
{
    uint32_t v[13]; memcpy(v, GP(pp8), 52);
    if (!v[7]) w32_window_fullscreen(c, v[6] ? v[6] : focus, (int)(v[0] ? v[0] : (uint32_t)w32_screen_w), (int)(v[1] ? v[1] : (uint32_t)w32_screen_h));
}
static uint32_t pp9_from8(uint32_t pp8)                 /* D3DPRESENT_PARAMETERS (13 DWORDs) -> Direct3D 9's (14) */
{
    static uint32_t buf; if (!buf) buf = gmem(64);
    uint32_t v8[13], v9[14]; memcpy(v8, GP(pp8), 52);
    v9[0] = v8[0]; v9[1] = v8[1]; v9[2] = v8[2]; v9[3] = v8[3]; v9[4] = v8[4]; v9[5] = 0;   /* MultiSampleQuality */
    if (v9[2] == 23) v9[2] = 22;                        /* a 16-bit mode (R5G6B5, listed for Vice City's 640x480x16 search): drawn in 32 */
    for (int k = 5; k < 13; k++) v9[k + 1] = v8[k];      /* SwapEffect, hDeviceWindow, Windowed, EnableAutoDepthStencil, AutoDepthStencilFormat, Flags, refresh, interval */
    memcpy(GP(buf), v9, 56); return buf;
}
static void surf_desc8(Surf9 *s, uint32_t out)          /* D3DSURFACE_DESC (Direct3D 8): Format, Type, Usage, Pool, Size, MultiSampleType, Width, Height */
{
    uint32_t v[8] = {s->fmt, RT_SURFACE, s->usage, s->pool, s->size, 0, s->w, s->h}; memcpy(GP(out), v, 32);
}
/* ---- IDirect3D8 ---- */
M(d8_GetAdapterIdentifier, 4)                          /* (adapter, flags, D3DADAPTER_IDENTIFIER8 *): Direct3D 9's without DeviceName */
    uint32_t o = ARG(3); memset(GP(o), 0, 1068);
    strcpy((char *)GP(o), "ati2dvag.dll"); strcpy((char *)GP(o + 512), "Apple Silicon (Metal)");   /* Driver[512], Description[512] */
    rt_w32(G_MEM, o + 1024, 0x00020000u | 6614); rt_w32(G_MEM, o + 1028, 0x00060006u);
    rt_w32(G_MEM, o + 1032, gpu_vendor()); rt_w32(G_MEM, o + 1036, gpu_device()); rt_w32(G_MEM, o + 1040, 0); rt_w32(G_MEM, o + 1044, 0);
    static const uint8_t guid[16] = {0x5c, 0x1e, 0x8d, 0xd7, 0x6a, 0x3b, 0x11, 0xcf, 0x86, 0x42, 0x54, 0x13, 0x8e, 0xc2, 0x3a, 0x0b};
    memcpy(GP(o + 1048), guid, 16); rt_w32(G_MEM, o + 1064, 1);
    RET(D3D_OK, 4);
END
/* the modes in 32-bit colour, then the same in 16-bit (R5G6B5), as a Windows driver lists them: Vice City looks for
 * 640x480x16 when it has no saved mode (GTA III for more than 16 bits); a 16-bit device is drawn in 32 (pp9_from8) */
M(d8_GetAdapterModeCount, 2) uint32_t m[DS_DISPLAY_MODE_CAP][2]; RET(2 * (uint32_t)modes(m), 2); END
M(d8_EnumAdapterModes, 4)                              /* (adapter, index, D3DDISPLAYMODE *) */
    uint32_t m[DS_DISPLAY_MODE_CAP][2]; uint32_t n = (uint32_t)modes(m), i = ARG(2); if (i >= 2 * n) RET(D3DERR_INVALIDCALL, 4);
    uint32_t v[4] = {m[i % n][0], m[i % n][1], (uint32_t)w32_screen_hz, i < n ? 22u : 23u}; memcpy(GP(ARG(3)), v, 16); RET(D3D_OK, 4);
END
M(d8_CheckDeviceType, 6) RET(ARG(3) == 22 || ARG(3) == 21 || ARG(3) == 23 ? D3D_OK : D3DERR_NOTAVAILABLE, 6); END   /* (adapter, type, display, back buffer, windowed) */
M(d8_CheckDeviceMultiSampleType, 6) RET(ARG(5) == 0 ? D3D_OK : D3DERR_NOTAVAILABLE, 6); END   /* (adapter, type, format, windowed, multisample) */
static void fill_caps8(uint32_t out)                   /* D3DCAPS8: the first 53 fields of D3DCAPS9 */
{
    uint32_t tmp = gmem(304); fill_caps(tmp);
    rt_w32(G_MEM, tmp + 49 * 4, 0); rt_w32(G_MEM, tmp + 51 * 4, 0);   /* no shaders: RenderWare stays on the fixed-function path */
    memcpy(GP(out), GP(tmp), 212); gfree_(tmp, 304);
}
M(d8_GetDeviceCaps, 4) fill_caps8(ARG(3)); RET(D3D_OK, 4); END
M(d8_CreateDevice, 7) ME(D3D9, O9_D3D)                 /* (adapter, type, focus window, behaviour, present parameters, device *) */
    uint32_t pp = pp9_from8(ARG(5)); fullscreen_window(c, ARG(5), ARG(3));
    if (the_dev) { dev_reset(the_dev, pp); rt_w32(G_MEM, ARG(6), the_dev->g); the_dev->ref++; RET(D3D_OK, 7); }
    Dev9 *d = calloc(1, sizeof *d); d->ref = 1; d->g_d3d = me->g; d->hwnd = ARG(3); d->behavior = ARG(4);
    d->g = make_iface(O9_DEV, d); d->g_swap = make_iface(O9_SWAP, d); the_dev = d;
    dev_surfaces(d, pp); dev_defaults(d);
    fprintf(stderr, "d3d8: device %ux%u (format %u, depth %u, %s)\n", d->pp[0], d->pp[1], d->pp[2], d->pp[9] ? d->pp[10] : 0, d->pp[8] ? "windowed" : "full screen");
    rt_w32(G_MEM, ARG(6), d->g); RET(D3D_OK, 7);
END
/* ---- IDirect3DDevice8 ---- */
OK_M(d8_ResourceManagerDiscardBytes, 2)
M(d8_dvGetDeviceCaps, 2) fill_caps8(ARG(1)); RET(D3D_OK, 2); END
M(d8_GetDisplayMode, 2) uint32_t v[4] = {(uint32_t)w32_screen_w, (uint32_t)w32_screen_h, (uint32_t)w32_screen_hz, 22}; memcpy(GP(ARG(1)), v, 16); RET(D3D_OK, 2); END
M(d8_Reset, 2) DEV_ME fullscreen_window(c, ARG(1), me->hwnd); RET(dev_reset(me, pp9_from8(ARG(1))), 2); END
M(d8_GetBackBuffer, 4) DEV_ME                          /* (index, type, surface) */
    if (ARG(1)) RET(D3DERR_INVALIDCALL, 4); rt_w32(G_MEM, ARG(3), me->back->g); surf_addref(me->back); RET(D3D_OK, 4);
END
M(d8_GetRasterStatus, 2) uint32_t v[2] = {0, 0}; memcpy(GP(ARG(1)), v, 8); RET(D3D_OK, 2); END
M(d8_SetGammaRamp, 3) uint32_t *p = cmd_begin(DSR_GAMMA, 1536); memcpy(p, GP(ARG(2)), 1536); cmd_end(); RET(0, 3); END
M(d8_GetGammaRamp, 2) uint16_t r[768]; for (int k = 0; k < 768; k++) r[k] = (uint16_t)((k & 255) * 257); memcpy(GP(ARG(1)), r, 1536); RET(0, 2); END
M(d8_CreateTexture, 8) DEV_ME RET(create_tex(me, O9_TEX, ARG(1), ARG(2), 1, ARG(3), ARG(4), ARG(5), ARG(6), ARG(7)), 8); END
M(d8_CreateVolumeTexture, 9) DEV_ME RET(create_tex(me, O9_VOLTEX, ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8)), 9); END
M(d8_CreateCubeTexture, 7) DEV_ME RET(create_tex(me, O9_CUBE, ARG(1), ARG(1), 1, ARG(2), ARG(3), ARG(4), ARG(5), ARG(6)), 7); END
M(d8_CreateVertexBuffer, 6) DEV_ME RET(create_buf(me, 0, ARG(1), ARG(2), ARG(3), ARG(4), ARG(5)), 6); END   /* (length, usage, FVF, pool, buffer) */
M(d8_CreateIndexBuffer, 6) DEV_ME RET(create_buf(me, 1, ARG(1), ARG(2), ARG(3), ARG(4), ARG(5)), 6); END    /* (length, usage, format, pool, buffer) */
M(d8_CreateRenderTarget, 7) DEV_ME                     /* (w, h, format, multisample, lockable, surface) */
    new_samples = 0; Surf9 *s = surf_new(me, ARG(1), ARG(2), ARG(3), USAGE_RENDERTARGET, 0, 1, 0, 0, 0, 0, 0); rt_w32(G_MEM, ARG(6), s->g); RET(D3D_OK, 7);
END
M(d8_CreateDepthStencilSurface, 6) DEV_ME              /* (w, h, format, multisample, surface) */
    new_samples = 0; Surf9 *s = surf_new(me, ARG(1), ARG(2), ARG(3), USAGE_DEPTHSTENCIL, 0, 2, 0, 0, 0, 0, 0); rt_w32(G_MEM, ARG(5), s->g); RET(D3D_OK, 6);
END
M(d8_CreateImageSurface, 5) DEV_ME                     /* (w, h, format, surface): system memory */
    if (!d9_fmt_known(ARG(3))) RET(D3DERR_INVALIDCALL, 5);
    Surf9 *s = surf_new(me, ARG(1), ARG(2), ARG(3), 0, 2, 4, 0, 0, 0, 0, 0); rt_w32(G_MEM, ARG(4), s->g); RET(D3D_OK, 5);
END
M(d8_CopyRects, 6)                                     /* (src, src rects, count, dst, dst points): CPU copies of one format */
    Surf9 *s = obj(ARG(1), O9_SURF), *t = obj(ARG(4), O9_SURF);
    if (!s || !t || s->fmt != t->fmt) RET(D3DERR_INVALIDCALL, 6);
    if (s->gpu_newer) surf_readback(s);
    uint32_t n = ARG(2) ? ARG(3) : 1, bpp = (uint32_t)d9_fmt_bytes(s->fmt);
    if (d9_fmt_block(s->fmt)) { memcpy(G_MEM + t->mem, G_MEM + s->mem, s->size < t->size ? s->size : t->size); surf_upload(t, 0, t->h); RET(D3D_OK, 6); }
    for (uint32_t k = 0; k < n; k++) {
        int32_t r[4] = {0, 0, (int32_t)s->w, (int32_t)s->h}; if (ARG(2)) memcpy(r, GP(ARG(2) + 16 * k), 16);
        int32_t px = r[0], py = r[1]; if (ARG(5)) { px = (int32_t)rt_r32(G_MEM, ARG(5) + 8 * k); py = (int32_t)rt_r32(G_MEM, ARG(5) + 8 * k + 4); }
        for (int32_t y = r[1]; y < r[3]; y++) {
            int32_t dy = py + (y - r[1]); if (y < 0 || dy < 0 || (uint32_t)y >= s->h || (uint32_t)dy >= t->h) continue;
            int32_t x0 = r[0] < 0 ? 0 : r[0], x1 = r[2] > (int32_t)s->w ? (int32_t)s->w : r[2]; int32_t dx = px + (x0 - r[0]);
            if (x1 <= x0 || dx < 0) continue; uint32_t w = (uint32_t)(x1 - x0); if ((uint32_t)dx + w > t->w) w = t->w - (uint32_t)dx;
            memcpy(G_MEM + t->mem + (uint32_t)dy * t->pitch + (uint32_t)dx * bpp, G_MEM + s->mem + (uint32_t)y * s->pitch + (uint32_t)x0 * bpp, w * bpp);
        }
    }
    surf_upload(t, 0, t->h); RET(D3D_OK, 6);
END
M(d8_GetFrontBuffer, 2) DEV_ME                         /* (surface): what is on screen, as 32-bit */
    Surf9 *t = obj(ARG(1), O9_SURF); if (!t) RET(D3DERR_INVALIDCALL, 2);
    int32_t r[4] = {0, 0, (int32_t)me->back->w, (int32_t)me->back->h}; cmd_readback(me->back->id, r, G_MEM + t->mem, t->pitch, 4); RET(D3D_OK, 2);
END
M(d8_SetRenderTarget, 3) DEV_ME                        /* (render target or 0 to keep, depth stencil or 0 for none) */
    Surf9 *s = ARG(1) ? obj(ARG(1), O9_SURF) : me->rt, *z = ARG(2) ? obj(ARG(2), O9_SURF) : 0;
    if (!s || (ARG(2) && !z)) RET(D3DERR_INVALIDCALL, 3);
    if (s != me->rt) { surf_addref(s); if (me->rt) surf_release(me->rt); me->rt = s; }
    if (z) surf_addref(z); if (me->ds) surf_release(me->ds); me->ds = z; set_rt(me);
    uint32_t vp[6] = {0, 0, s->w, s->h, 0, u32f(1.0f)}; memcpy(me->vp, vp, 24); uint32_t *p = cmd_begin(DSR_VIEWPORT, 24); memcpy(p, vp, 24); cmd_end();
    RET(D3D_OK, 3);
END
M(d8_GetRenderTarget, 2) DEV_ME if (!me->rt) RET(D3DERR_NOTFOUND, 2); rt_w32(G_MEM, ARG(1), me->rt->g); surf_addref(me->rt); RET(D3D_OK, 2); END
/* render states: Direct3D 8's ZBIAS (47) is Direct3D 9's depth bias (as d3d8to9 converts it); LINEPATTERN, ZVISIBLE,
 * EDGEANTIALIAS, SOFTWAREVERTEXPROCESSING and PATCHSEGMENTS have no Direct3D 9 state and are kept only */
M(d8_SetRenderState, 3) DEV_ME
    uint32_t s = ARG(1), v = ARG(2);
    if (s == 47) { set_rs(me, 195, u32f((float)(int32_t)v * -0.000005f)); if (s < 256) me->rs[s] = v; RET(D3D_OK, 3); }
    if (s == 10 || s == 30 || s == 40 || s == 153 || s == 164) { me->rs[s] = v; RET(D3D_OK, 3); }
    set_rs(me, s, v); RET(D3D_OK, 3);
END
static int tss8_sampler(uint32_t t)                     /* a Direct3D 8 texture-stage state that is a Direct3D 9 sampler state, or 0 */
{
    switch (t) { case 13: return 1; case 14: return 2; case 25: return 3; case 15: return 4; case 16: return 5; case 17: return 6;
                 case 18: return 7; case 19: return 8; case 20: return 9; case 21: return 10; }
    return 0;
}
M(d8_SetTextureStageState, 4) DEV_ME
    int s = tss8_sampler(ARG(2)); if (s) set_ss(me, ARG(1), (uint32_t)s, ARG(3)); else set_tss(me, ARG(1), ARG(2), ARG(3)); RET(D3D_OK, 4);
END
M(d8_GetTextureStageState, 4) DEV_ME
    int s = tss8_sampler(ARG(2)); uint32_t v = 0;
    if (s) v = ARG(1) < 16 ? me->ss[ARG(1)][s] : 0; else v = ARG(1) < 8 && ARG(2) < 33 ? me->tss[ARG(1)][ARG(2)] : 0;
    rt_w32(G_MEM, ARG(3), v); RET(D3D_OK, 4);
END
M(d8_ApplyStateBlock, 2) DEV_ME SB9 *b = obj(ARG(1), O9_SB); if (!b) RET(D3DERR_INVALIDCALL, 2); sb_apply(me, b); RET(D3D_OK, 2); END
M(d8_CaptureStateBlock, 2) DEV_ME SB9 *b = obj(ARG(1), O9_SB); if (!b) RET(D3DERR_INVALIDCALL, 2); sb_capture(me, b); RET(D3D_OK, 2); END
M(d8_DeleteStateBlock, 2) SB9 *b = obj(ARG(1), O9_SB); if (b) sb_release(b); RET(D3D_OK, 2); END
M(d8_GetInfo, 4) RET(S_FALSE_, 4); END
M(d8_DrawIndexedPrimitive, 6) DEV_ME                   /* (type, min index, vertices, start index, primitives); base vertex from SetIndices */
    if ((!me->prog && !can_draw(me)) || !me->ib) RET(D3D_OK, 6);
    uint32_t ni = prim_verts(ARG(1), ARG(5)), is = me->ib->fmt == 102 ? 4 : 2;
    if ((uint64_t)(ARG(4) + ni) * is > me->ib->len) { static int w; if (!w++) fprintf(stderr, "d3d8: indices past the index buffer (skipped)\n"); RET(D3D_OK, 6); }
    (me->prog ? prog_draw_indexed : draw_indexed)(me, ARG(1), me->base8, ARG(2), ARG(3), G_MEM + me->ib->mem + ARG(4) * is, is == 4, ni, 0, 0); RET(D3D_OK, 6);
END
TODO_M(d8_ProcessVertices, 6, D3DERR_INVALIDCALL)
/* vertex shaders: Direct3D 8 passes the FVF where a shader handle would go; the game's own shaders are not run */
M(d8_CreateVertexShader, 5) { static int w; if (!w++) fprintf(stderr, "d3d8: CreateVertexShader is not supported (fixed function only)\n"); } RET(D3DERR_INVALIDCALL, 5); END
M(d8_SetVertexShader, 2) DEV_ME
    if (me->rec) { RecEnt e = {REC_FVF, ARG(1), 0}; rec_put(me->rec, e); RET(D3D_OK, 2); }
    me->fvf = ARG(1); me->decl = 0; me->vs = 0; RET(D3D_OK, 2);
END
M(d8_GetVertexShader, 2) DEV_ME rt_w32(G_MEM, ARG(1), me->fvf); RET(D3D_OK, 2); END
OK_M(d8_DeleteVertexShader, 2)
M(d8_SetVertexShaderConstant, 4) DEV_ME RET(set_consts(me->vsc, 256, ARG(1), ARG(2), ARG(3), 0), 4); END
M(d8_GetVertexShaderConstant, 4) DEV_ME RET(set_consts(me->vsc, 256, ARG(1), ARG(2), ARG(3), 1), 4); END
TODO_M(d8_GetVertexShaderDeclaration, 4, D3DERR_INVALIDCALL)
TODO_M(d8_GetVertexShaderFunction, 4, D3DERR_INVALIDCALL)
M(d8_SetStreamSource, 4) DEV_ME                        /* (stream, buffer, stride) */
    if (ARG(1) >= 16) RET(D3DERR_INVALIDCALL, 4);
    Buf9 *b = ARG(2) ? obj(ARG(2), O9_VB) : 0; if (ARG(2) && !b) RET(D3DERR_INVALIDCALL, 4);
    if (b) b->ref++; if (me->vb[ARG(1)]) buf_release(me->vb[ARG(1)]);
    me->vb[ARG(1)] = b; me->vb_off[ARG(1)] = 0; me->vb_stride[ARG(1)] = ARG(3); RET(D3D_OK, 4);
END
M(d8_GetStreamSource, 4) DEV_ME
    if (ARG(1) >= 16) RET(D3DERR_INVALIDCALL, 4);
    Buf9 *b = me->vb[ARG(1)]; rt_w32(G_MEM, ARG(2), b ? b->g : 0); if (b) b->ref++; rt_w32(G_MEM, ARG(3), me->vb_stride[ARG(1)]); RET(D3D_OK, 4);
END
M(d8_SetIndices, 3) DEV_ME                             /* (index buffer, base vertex index) */
    Buf9 *b = ARG(1) ? obj(ARG(1), O9_IB) : 0; if (ARG(1) && !b) RET(D3DERR_INVALIDCALL, 3);
    if (b) b->ref++; if (me->ib) buf_release(me->ib); me->ib = b; me->base8 = (int32_t)ARG(2); RET(D3D_OK, 3);
END
M(d8_GetIndices, 3) DEV_ME rt_w32(G_MEM, ARG(1), me->ib ? me->ib->g : 0); if (me->ib) me->ib->ref++; rt_w32(G_MEM, ARG(2), (uint32_t)me->base8); RET(D3D_OK, 3); END
M(d8_CreatePixelShader, 3) { static int w; if (!w++) fprintf(stderr, "d3d8: CreatePixelShader is not supported (fixed function only)\n"); } RET(D3DERR_INVALIDCALL, 3); END
OK_M(d8_SetPixelShader, 2)
M(d8_GetPixelShader, 2) rt_w32(G_MEM, ARG(1), 0); RET(D3D_OK, 2); END
OK_M(d8_DeletePixelShader, 2)
OK_M(d8_SetPixelShaderConstant, 4)
TODO_M(d8_GetPixelShaderConstant, 4, D3DERR_INVALIDCALL)
TODO_M(d8_GetPixelShaderFunction, 4, D3DERR_INVALIDCALL)
TODO_M(d8_DrawRectPatch, 4, D3DERR_INVALIDCALL)
TODO_M(d8_DrawTriPatch, 4, D3DERR_INVALIDCALL)
OK_M(d8_DeletePatch, 2)
TODO_M(d8_CreateAdditionalSwapChain, 3, D3DERR_INVALIDCALL)
/* ---- surfaces and textures ---- */
M(d8_sfGetDesc, 2) ME(Surf9, O9_SURF) surf_desc8(me, ARG(1)); RET(D3D_OK, 2); END
M(d8_txGetLevelDesc, 3) TEX_ME if (ARG(1) >= me->levels) RET(D3DERR_INVALIDCALL, 3); surf_desc8(me->lv[0][ARG(1)], ARG(2)); RET(D3D_OK, 3); END
TODO_M(d8_vtGetLevelDesc, 3, D3DERR_INVALIDCALL)
M(d8_swPresent, 5) Dev9 *d = the_dev; if (d) dev_present(c, d); RET(D3D_OK, 5); END
M(d8_swGetBackBuffer, 4) Dev9 *d = the_dev; if (!d || ARG(1)) RET(D3DERR_INVALIDCALL, 4); rt_w32(G_MEM, ARG(3), d->back->g); surf_addref(d->back); RET(D3D_OK, 4); END

static void init_vtables8(void)
{
    static int done; if (done) return; done = 1;
    init_vtables9();
    static const Fn9 d3d[] = { d9_QueryInterface, d9_AddRef, d9_Release, d9_RegisterSoftwareDevice, d9_GetAdapterCount, d8_GetAdapterIdentifier,
        d8_GetAdapterModeCount, d8_EnumAdapterModes, d9_GetAdapterDisplayMode, d8_CheckDeviceType, d9_CheckDeviceFormat,
        d8_CheckDeviceMultiSampleType, d9_CheckDepthStencilMatch, d8_GetDeviceCaps, d9_GetAdapterMonitor, d8_CreateDevice };
    static const Fn9 dev[] = { dv_QueryInterface, dv_AddRef, dv_Release, dv_TestCooperativeLevel, dv_GetAvailableTextureMem, d8_ResourceManagerDiscardBytes,
        dv_GetDirect3D, d8_dvGetDeviceCaps, d8_GetDisplayMode, dv_GetCreationParameters, dv_SetCursorProperties, dv_SetCursorPosition,
        dv_ShowCursor, d8_CreateAdditionalSwapChain, d8_Reset, dv_Present, d8_GetBackBuffer, d8_GetRasterStatus, d8_SetGammaRamp, d8_GetGammaRamp,
        d8_CreateTexture, d8_CreateVolumeTexture, d8_CreateCubeTexture, d8_CreateVertexBuffer, d8_CreateIndexBuffer, d8_CreateRenderTarget,
        d8_CreateDepthStencilSurface, d8_CreateImageSurface, d8_CopyRects, dv_UpdateTexture, d8_GetFrontBuffer, d8_SetRenderTarget,
        d8_GetRenderTarget, dv_GetDepthStencilSurface, dv_BeginScene, dv_EndScene, dv_Clear, dv_SetTransform, dv_GetTransform,
        dv_MultiplyTransform, dv_SetViewport, dv_GetViewport, dv_SetMaterial, dv_GetMaterial, dv_SetLight, dv_GetLight, dv_LightEnable,
        dv_GetLightEnable, dv_SetClipPlane, dv_GetClipPlane, d8_SetRenderState, dv_GetRenderState, dv_BeginStateBlock, dv_EndStateBlock,
        d8_ApplyStateBlock, d8_CaptureStateBlock, d8_DeleteStateBlock, dv_CreateStateBlock, dv_SetClipStatus, dv_GetClipStatus,
        dv_GetTexture, dv_SetTexture, d8_GetTextureStageState, d8_SetTextureStageState, dv_ValidateDevice, d8_GetInfo, dv_SetPaletteEntries,
        dv_GetPaletteEntries, dv_SetCurrentTexturePalette, dv_GetCurrentTexturePalette, dv_DrawPrimitive, d8_DrawIndexedPrimitive,
        dv_DrawPrimitiveUP, dv_DrawIndexedPrimitiveUP, d8_ProcessVertices, d8_CreateVertexShader, d8_SetVertexShader, d8_GetVertexShader,
        d8_DeleteVertexShader, d8_SetVertexShaderConstant, d8_GetVertexShaderConstant, d8_GetVertexShaderDeclaration, d8_GetVertexShaderFunction,
        d8_SetStreamSource, d8_GetStreamSource, d8_SetIndices, d8_GetIndices, d8_CreatePixelShader, d8_SetPixelShader, d8_GetPixelShader,
        d8_DeletePixelShader, d8_SetPixelShaderConstant, d8_GetPixelShaderConstant, d8_GetPixelShaderFunction, d8_DrawRectPatch,
        d8_DrawTriPatch, d8_DeletePatch };
    static const Fn9 surf[] = { sf_QueryInterface, sf_AddRef, sf_Release, sf_GetDevice, sf_SetPrivateData, sf_GetPrivateData, sf_FreePrivateData,
        sf_GetContainer, d8_sfGetDesc, sf_LockRect, sf_UnlockRect };
#define TEXBASE8 tx_QueryInterface, tx_AddRef, tx_Release, tx_GetDevice, tx_SetPrivateData, tx_GetPrivateData, tx_FreePrivateData, tx_SetPriority, \
        tx_GetPriority, tx_PreLoad, tx_GetType, tx_SetLOD, tx_GetLOD, tx_GetLevelCount
    static const Fn9 tex[] = { TEXBASE8, d8_txGetLevelDesc, tx_GetSurfaceLevel, tx_LockRect, tx_UnlockRect, tx_AddDirtyRect };
    static const Fn9 cube[] = { TEXBASE8, d8_txGetLevelDesc, cb_GetCubeMapSurface, cb_LockRect, cb_UnlockRect, cb_AddDirtyRect };
    static const Fn9 vol[] = { TEXBASE8, d8_vtGetLevelDesc, vt_GetVolumeLevel, vt_LockBox, vt_UnlockBox, vt_AddDirtyBox };
    static const Fn9 swap[] = { sw_QueryInterface, sw_AddRef, sw_Release, d8_swPresent, d8_swGetBackBuffer };
    _Static_assert(sizeof d3d / sizeof *d3d == 16 && sizeof dev / sizeof *dev == 97 && sizeof surf / sizeof *surf == 11 &&
                   sizeof tex / sizeof *tex == 19 && sizeof swap / sizeof *swap == 5, "d3d8 vtable sizes");
    vt[O9_D3D] = vtable9("IDirect3D8", d3d, 16); vt[O9_DEV] = vtable9("IDirect3DDevice8", dev, 97); vt[O9_SURF] = vtable9("IDirect3DSurface8", surf, 11);
    vt[O9_TEX] = vtable9("IDirect3DTexture8", tex, 19); vt[O9_CUBE] = vtable9("IDirect3DCubeTexture8", cube, 19);
    vt[O9_VOLTEX] = vtable9("IDirect3DVolumeTexture8", vol, 19); vt[O9_SWAP] = vtable9("IDirect3DSwapChain8", swap, 5);
    /* vertex and index buffers: the same methods and descriptions in both versions */
    d3d8_mode = 1;
}
