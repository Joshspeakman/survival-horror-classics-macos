/* DirectShow's multimedia streams (CLSID_AMMultiMediaStream), included by ddraw.c: how Resident Evil 2 and 3 and Dino
 * Crisis play their movies into DirectDraw surfaces. The game adds a video stream on its DirectDraw (and an audio
 * stream that plays itself), opens the file, makes a sample on a surface and calls Update for each frame, copying the
 * surface to the screen in between. The movies are MPEG-1 (Resident Evil 3, Dino Crisis; Resident Evil 2's Indeo AVIs
 * converted to MPEG-1 beside them, "<name>.mpg", by the installer): pl_mpeg (third_party, MIT) decodes them, the video
 * paced by the clock from Run, the sound from a second decoder on the host's audio output.
 * Objects: IAMMultiMediaStream (O_AMS), the video stream IDirectDrawMediaStream (O_AMV), the audio stream IMediaStream
 * (O_AMA), samples IDirectDrawStreamSample (O_AMSMP). */
#define PL_MPEG_IMPLEMENTATION
#include "../../third_party/pl_mpeg/pl_mpeg.h"
#include "../host_audio.h"

#define MS_S_ENDOFSTREAM 0x00040003u
#define MS_E_NOSTREAM 0x8004000bu
typedef struct AMs {
    uint32_t g_mm, g_vs, g_as; int ref; DDraw *dd; int video, audio;
    plm_t *pv, *pa; int w, h; double fps, duration;
    int state; uint64_t t0; double start_s;               /* running since t0 (us), from start_s in the movie */
    pthread_mutex_t m; DsAudio *out; float abuf[2304 * 2]; int an, ai, achannels, eos;
    uint32_t eos_event; uint32_t *bgra;
} AMs;
typedef struct AMSample { uint32_t g; int ref; AMs *s; DSurface *surf; int32_t r[4]; } AMSample;
static uint64_t ams_now(void) { return ds_clock_ns() / 1000; }
static int ams_muted(void) { const char *e = getenv("W32_AUDIO_MUTE"); return e && strcmp(e, "0"); }
static void ams_log(const char *fmt, ...) { if (!getenv("W32_MOVIELOG")) return; va_list ap; va_start(ap, fmt); fprintf(stderr, "movie: "); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap); }

/* ---- sound: the second decoder, read on the host's audio thread ---- */
static void ams_render(void *user, void *samples, uint32_t frames)
{
    AMs *s = user; int16_t *out = samples; uint32_t got = 0;
    pthread_mutex_lock(&s->m);
    while (got < frames && s->state == 1 && s->pa) {
        if (s->ai >= s->an) {
            plm_samples_t *p = plm_decode_audio(s->pa); if (!p) break;
            memcpy(s->abuf, p->interleaved, sizeof(float) * p->count * 2); s->an = (int)p->count; s->ai = 0;
        }
        for (; s->ai < s->an && got < frames; s->ai++, got++) for (int ch = 0; ch < 2; ch++) {
            float v = s->abuf[s->ai * 2 + ch] * 32767.0f; out[got * 2 + ch] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
    pthread_mutex_unlock(&s->m);
    memset(out + got * 2, 0, (frames - got) * 4);
}
static void ams_audio_stop(AMs *s) { if (s->out) { ds_audio_close(s->out); s->out = 0; } }
static void ams_audio_start(AMs *s)
{
    if (!s->pa || s->out || ams_muted() || !s->audio) return;
    s->out = ds_audio_open(plm_get_samplerate(s->pa), 2, 16, 0, ams_render, s);
}
static void ams_close(AMs *s)
{
    ams_audio_stop(s);
    pthread_mutex_lock(&s->m); if (s->pv) plm_destroy(s->pv); if (s->pa) plm_destroy(s->pa); s->pv = s->pa = 0; s->an = s->ai = 0; pthread_mutex_unlock(&s->m);
    free(s->bgra); s->bgra = 0;
}

/* ---- IAMMultiMediaStream ---- */
#define MA(name, n) M(name, n) AMs *me = obj(ARG(0), O_AMS); if (!me) RET(DDERR_INVALIDPARAMS, n);
static int guid_is(uint32_t p, uint32_t d1) { return p && rt_r32(G_MEM, p) == d1; }
M(ams_QueryInterface, 3) AMs *me = obj(ARG(0), O_AMS); if (!me) RET(E_NOINTERFACE, 3);
    uint32_t iid = rt_r32(G_MEM, ARG(1));
    if (iid == 0 || iid == 0xbebe595cu || iid == 0xb502d1bcu) { me->ref++; rt_w32(G_MEM, ARG(2), me->g_mm); RET(DD_OK, 3); }
    rt_w32(G_MEM, ARG(2), 0); RET(E_NOINTERFACE, 3); END
M(ams_AddRef, 1) AMs *me = obj(ARG(0), O_AMS); RET(me ? (uint32_t)++me->ref : 0, 1); END
M(ams_Release, 1) AMs *me = obj(ARG(0), O_AMS); if (!me) RET(0, 1);
    uint32_t r = (uint32_t)--me->ref;
    if (!r) { ams_close(me); obj_drop(me->g_mm); obj_drop(me->g_vs); obj_drop(me->g_as); free(me); }   /* (samples hold no reference here) */
    RET(r, 1); END
MA(ams_GetInformation, 3) if (ARG(1)) rt_w32(G_MEM, ARG(1), 0x3); if (ARG(2)) rt_w32(G_MEM, ARG(2), 0); RET(DD_OK, 3); END   /* seekable, has clock; STREAMTYPE_READ */
MA(ams_GetMediaStream, 3)
    uint32_t g = guid_is(ARG(1), 0xa35ff56au) ? (me->video ? me->g_vs : 0) : guid_is(ARG(1), 0xa35ff56bu) ? (me->audio ? me->g_as : 0) : 0;
    rt_w32(G_MEM, ARG(2), g); if (g) me->ref++; RET(g ? DD_OK : MS_E_NOSTREAM, 3); END
MA(ams_EnumMediaStreams, 3) uint32_t g = ARG(1) == 0 && me->video ? me->g_vs : (ARG(1) == (uint32_t)me->video && me->audio) ? me->g_as : 0;
    rt_w32(G_MEM, ARG(2), g); if (g) me->ref++; RET(g ? DD_OK : 1u, 3); END
MA(ams_GetState, 2) rt_w32(G_MEM, ARG(1), (uint32_t)me->state); RET(DD_OK, 2); END
static double ams_time(AMs *s) { return s->state == 1 ? s->start_s + (double)(ams_now() - s->t0) / 1e6 : s->start_s; }
MA(ams_SetState, 2)
    int st = ARG(1) == 1;
    if (st && me->state != 1) { me->t0 = ams_now(); me->state = 1; ams_audio_start(me); }
    else if (!st && me->state == 1) { me->start_s = ams_time(me); me->state = 0; ams_audio_stop(me); }
    ams_log("SetState %s at %.2f s", st ? "run" : "stop", me->start_s);
    RET(DD_OK, 2); END
MA(ams_GetTime, 2) uint64_t t = (uint64_t)(ams_time(me) * 1e7); memcpy(GP(ARG(1)), &t, 8); RET(DD_OK, 2); END
MA(ams_GetDuration, 2) uint64_t t = (uint64_t)(me->duration * 1e7); memcpy(GP(ARG(1)), &t, 8); RET(me->pv || me->pa ? DD_OK : 1u, 2); END
M(ams_Seek, 3) AMs *me = obj(ARG(0), O_AMS); if (!me) RET(DDERR_INVALIDPARAMS, 3);
    uint64_t t = (uint64_t)ARG(1) | (uint64_t)ARG(2) << 32; double sec = (double)t / 1e7;
    pthread_mutex_lock(&me->m);
    if (me->pv) plm_seek(me->pv, sec, 0); if (me->pa) { plm_seek(me->pa, sec, 0); me->an = me->ai = 0; }
    pthread_mutex_unlock(&me->m);
    me->start_s = sec; me->t0 = ams_now(); me->eos = 0; RET(DD_OK, 3); END
MA(ams_GetEndOfStreamEventHandle, 2)
    uint32_t w32_event_new(int manual, int signaled); if (!me->eos_event) me->eos_event = w32_event_new(1, me->eos);
    rt_w32(G_MEM, ARG(1), me->eos_event); RET(DD_OK, 2); END
MA(ams_Initialize, 4) RET(DD_OK, 4); END
/* the filter graph: a DirectShow graph object (dshow.c) the game can ask for its control, events and sound volume */
MA(ams_GetFilterGraph, 2)
    uint32_t w32_dshow_create(uint32_t clsid_first_dword); static uint32_t graph;
    if (!graph) graph = w32_dshow_create(0xe436ebb3u);                    /* CLSID_FilterGraph */
    rt_w32(G_MEM, ARG(1), graph); RET(graph ? DD_OK : E_NOINTERFACE, 2); END
MA(ams_GetFilter, 2) rt_w32(G_MEM, ARG(1), 0); RET(E_NOINTERFACE, 2); END
MA(ams_AddMediaStream, 5)
    uint32_t obj_ = ARG(1), purpose = ARG(2), out = ARG(4);
    if (guid_is(purpose, 0xa35ff56au)) { me->video = 1; DDraw *dd = obj(obj_, O_DD); if (dd) me->dd = dd; if (!me->dd) me->dd = the_dd; if (out) { rt_w32(G_MEM, out, me->g_vs); me->ref++; } }
    else if (guid_is(purpose, 0xa35ff56bu)) { me->audio = 1; if (out) { rt_w32(G_MEM, out, me->g_as); me->ref++; } }
    else RET(MS_E_NOSTREAM, 5);
    ams_log("AddMediaStream %s flags %#x", guid_is(purpose, 0xa35ff56au) ? "video" : "audio", ARG(3));
    RET(DD_OK, 5); END
static plm_t *ams_open_one(const char *host) { plm_t *p = plm_create_with_filename(host); if (p && !plm_probe(p, 5000 * 1024)) { plm_destroy(p); p = 0; } return p; }
MA(ams_OpenFile, 3)
    char a[1024]; int k = 0; for (; k < 1023 && rt_r16(G_MEM, ARG(1) + 2u * (uint32_t)k); k++) a[k] = (char)rt_r16(G_MEM, ARG(1) + 2u * (uint32_t)k); a[k] = 0;
    if (getenv("RE_NO_MOVIES") && strcmp(getenv("RE_NO_MOVIES"), "0")) { ams_log("OpenFile %s: movies off", a); RET(0x80070002u, 3); }   /* the launcher's Movies: Off */
    char host[2048], conv[2100]; if (w32_host_path(a, host, sizeof host, 0)) { ams_log("OpenFile %s: not found", a); RET(0x80070002u, 3); }
    snprintf(conv, sizeof conv, "%s.mpg", host); FILE *f = fopen(conv, "rb"); if (f) { fclose(f); snprintf(host, sizeof host, "%s", conv); }   /* the installer's MPEG-1 copy */
    ams_close(me);
    me->pv = ams_open_one(host); me->pa = ams_open_one(host);
    if (!me->pv) { ams_log("OpenFile %s: not a movie pl_mpeg reads", a); if (me->pa) plm_destroy(me->pa); me->pa = 0; RET(0x80040265u, 3); }   /* VFW_E_UNSUPPORTED_STREAM */
    plm_set_audio_enabled(me->pv, 0); plm_set_loop(me->pv, 0);
    if (me->pa) { plm_set_video_enabled(me->pa, 0); plm_set_loop(me->pa, 0); if (!plm_get_num_audio_streams(me->pa)) { plm_destroy(me->pa); me->pa = 0; } }
    me->w = plm_get_width(me->pv); me->h = plm_get_height(me->pv); me->fps = plm_get_framerate(me->pv); me->duration = plm_get_duration(me->pv);
    me->bgra = malloc((size_t)me->w * (size_t)me->h * 4); me->start_s = 0; me->eos = 0; me->state = 0;
    if (me->eos_event) w32_event_signal(me->eos_event, 0);
    ams_log("OpenFile %s: %dx%d %.2f fps %.1f s, sound %s", a, me->w, me->h, me->fps, me->duration, me->pa ? "yes" : "no");
    RET(DD_OK, 3); END
MA(ams_OpenMoniker, 4) RET(E_NOINTERFACE, 4); END
MA(ams_Render, 2) RET(DD_OK, 2); END

/* ---- the streams (video: IDirectDrawMediaStream; audio: IMediaStream) ---- */
static AMs *stream_owner(uint32_t g) { AMs *s = obj(g, O_AMV); return s ? s : obj(g, O_AMA); }
M(st_QueryInterface, 3) AMs *s = stream_owner(ARG(0)); if (!s) RET(E_NOINTERFACE, 3);
    uint32_t iid = rt_r32(G_MEM, ARG(1));
    if (iid == 0 || iid == 0xb502d1bdu || iid == 0xbebe595du || (iid == 0xf4104fceu && obj(ARG(0), O_AMV))) { s->ref++; rt_w32(G_MEM, ARG(2), ARG(0)); RET(DD_OK, 3); }
    rt_w32(G_MEM, ARG(2), 0); RET(E_NOINTERFACE, 3); END
M(st_AddRef, 1) AMs *s = stream_owner(ARG(0)); RET(s ? (uint32_t)++s->ref : 0, 1); END
M(st_Release, 1) AMs *s = stream_owner(ARG(0)); RET(s ? (uint32_t)(s->ref > 1 ? --s->ref : s->ref) : 0, 1); END
M(st_GetMultiMediaStream, 2) AMs *s = stream_owner(ARG(0)); rt_w32(G_MEM, ARG(1), s ? s->g_mm : 0); if (s) s->ref++; RET(DD_OK, 2); END
M(st_GetInformation, 3)
    if (ARG(1)) { uint8_t g[16] = {0x6a, 0xf5, 0x5f, 0xa3, 0xda, 0x9f, 0xd0, 0x11, 0x8f, 0xdf, 0x00, 0xc0, 0x4f, 0xd9, 0x18, 0x9d}; if (obj(ARG(0), O_AMA)) g[0] = 0x6b; memcpy(GP(ARG(1)), g, 16); }
    if (ARG(2)) rt_w32(G_MEM, ARG(2), 0); RET(DD_OK, 3); END
M(st_SetSameFormat, 3) RET(DD_OK, 3); END
M(st_SendEndOfStream, 2) RET(DD_OK, 2); END
/* the movie's format as a surface description (DDSURFACEDESC, 108 bytes): its size in the display's pixel format */
static void ams_format(AMs *s, uint32_t p)
{
    if (!p) return;
    uint32_t sz = rt_r32(G_MEM, p); if (sz < 108 || sz > SD_BYTES) sz = 108;
    uint8_t d[SD_BYTES]; memset(d, 0, sizeof d);
    U32(d, SD_SIZE) = sz; U32(d, SD_FLAGS) = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    U32(d, SD_WIDTH) = (uint32_t)s->w; U32(d, SD_HEIGHT) = (uint32_t)s->h; U32(d, SD_CAPS) = 0x40 | DDSCAPS_SYSTEMMEMORY;   /* DDSCAPS_OFFSCREENPLAIN */
    default_format(d + SD_PF);
    if (s->dd && s->dd->mode_bpp == 16) { uint8_t *pf = d + SD_PF; U32(pf, PF_BITS) = 16; U32(pf, PF_R) = 0xf800; U32(pf, PF_G) = 0x7e0; U32(pf, PF_B) = 0x1f; }
    memcpy(GP(p), d, sz);
}
M(vs_GetFormat, 5) AMs *s = obj(ARG(0), O_AMV); if (!s) RET(DDERR_INVALIDPARAMS, 5);
    ams_format(s, ARG(1)); if (ARG(2)) rt_w32(G_MEM, ARG(2), 0); ams_format(s, ARG(3)); if (ARG(4)) rt_w32(G_MEM, ARG(4), 0);
    RET(s->pv ? DD_OK : MS_E_NOSTREAM, 5); END
M(vs_SetFormat, 3) RET(DD_OK, 3); END
M(vs_GetDirectDraw, 2) AMs *s = obj(ARG(0), O_AMV); rt_w32(G_MEM, ARG(1), s && s->dd ? dd_old_iface(s->dd, O_DD1) : 0); if (s && s->dd) s->dd->ref++; RET(DD_OK, 2); END
M(vs_SetDirectDraw, 2) AMs *s = obj(ARG(0), O_AMV); DDraw *dd = obj(ARG(1), O_DD); if (s && dd) s->dd = dd; RET(DD_OK, 2); END
M(vs_GetTimePerFrame, 2) AMs *s = obj(ARG(0), O_AMV); uint64_t t = s && s->fps > 0 ? (uint64_t)(1e7 / s->fps) : 333333; memcpy(GP(ARG(1)), &t, 8); RET(DD_OK, 2); END
M(vs_CreateSample, 5) AMs *s = obj(ARG(0), O_AMV); if (!s) RET(DDERR_INVALIDPARAMS, 5);
    DSurface *surf = obj(ARG(1), O_SURF);
    if (!surf) {                                              /* none given: one of the movie's format */
        uint8_t d[SD_BYTES]; uint32_t tmp = scratch(SD_BYTES); rt_w32(G_MEM, tmp, 108); ams_format(s, tmp); memset(d, 0, sizeof d); memcpy(d, GP(tmp), 108); scratch_free(tmp);
        if (!s->dd || surface_create(s->dd, d, &surf)) RET(DDERR_INVALIDPARAMS, 5);
    } else surf_addref(surf);
    AMSample *sm = calloc(1, sizeof *sm); sm->ref = 1; sm->s = s; sm->surf = surf;
    if (ARG(2)) memcpy(sm->r, GP(ARG(2)), 16); else { sm->r[0] = sm->r[1] = 0; sm->r[2] = (int32_t)U32(surf->desc, SD_WIDTH); sm->r[3] = (int32_t)U32(surf->desc, SD_HEIGHT); }
    sm->g = make_iface(O_AMSMP, sm); s->ref++;
    ams_log("CreateSample on surface %u (%ux%u %u-bit) rect %d,%d-%d,%d", surf->id, U32(surf->desc, SD_WIDTH), U32(surf->desc, SD_HEIGHT), bpp_of(surf->desc), sm->r[0], sm->r[1], sm->r[2], sm->r[3]);
    rt_w32(G_MEM, ARG(4), sm->g); RET(DD_OK, 5); END

/* ---- samples ---- */
M(sm_QueryInterface, 3) AMSample *me = obj(ARG(0), O_AMSMP); if (!me) RET(E_NOINTERFACE, 3);
    uint32_t iid = rt_r32(G_MEM, ARG(1));
    if (iid == 0 || iid == 0xb502d1beu || iid == 0xf4104fcfu) { me->ref++; rt_w32(G_MEM, ARG(2), me->g); RET(DD_OK, 3); }
    rt_w32(G_MEM, ARG(2), 0); RET(E_NOINTERFACE, 3); END
M(sm_AddRef, 1) AMSample *me = obj(ARG(0), O_AMSMP); RET(me ? (uint32_t)++me->ref : 0, 1); END
M(sm_Release, 1) AMSample *me = obj(ARG(0), O_AMSMP); if (!me) RET(0, 1);
    uint32_t r = (uint32_t)--me->ref; if (!r) { surf_release(me->surf); if (me->s && me->s->ref > 1) me->s->ref--; obj_drop(me->g); free(me); }
    RET(r, 1); END
M(sm_GetMediaStream, 2) AMSample *me = obj(ARG(0), O_AMSMP); rt_w32(G_MEM, ARG(1), me ? me->s->g_vs : 0); if (me) me->s->ref++; RET(DD_OK, 2); END
M(sm_GetSampleTimes, 4) AMSample *me = obj(ARG(0), O_AMSMP);
    uint64_t t = me ? (uint64_t)(ams_time(me->s) * 1e7) : 0; for (int k = 1; k <= 3; k++) if (ARG(k)) memcpy(GP(ARG(k)), &t, 8); RET(DD_OK, 4); END
M(sm_SetSampleTimes, 3) RET(DD_OK, 3); END
/* Update: the next frame, when its time comes (frames already late are skipped), into the sample's surface */
static uint32_t sample_update(AMSample *sm)
{
    AMs *s = sm->s; if (!s->pv) return MS_S_ENDOFSTREAM;
    if (s->state != 1) { s->t0 = ams_now(); s->state = 1; ams_audio_start(s); }
    plm_frame_t *f = 0;
    for (;;) {
        pthread_mutex_lock(&s->m); f = plm_decode_video(s->pv); pthread_mutex_unlock(&s->m);
        if (!f) { s->eos = 1; if (s->eos_event) w32_event_signal(s->eos_event, 1); ams_log("end of stream"); return MS_S_ENDOFSTREAM; }
        double now = ams_time(s), late = now - f->time;
        if (late > 2.0 / (s->fps > 0 ? s->fps : 30)) continue;                       /* behind: drop it */
        if (late < 0) usleep((useconds_t)(-late * 1e6 > 200000 ? 200000 : -late * 1e6));
        break;
    }
    plm_frame_to_bgra(f, (uint8_t *)s->bgra, s->w * 4);
    DSurface *d = sm->surf; uint32_t bpp = bpp_of(d->desc) / 8;
    int32_t dw = sm->r[2] - sm->r[0], dh = sm->r[3] - sm->r[1];
    for (int32_t y = 0; y < dh; y++) {
        int sy = (int)((int64_t)y * s->h / (dh ? dh : 1)); uint8_t *row = G_MEM + d->mem + (uint32_t)(sm->r[1] + y) * (uint32_t)d->pitch + (uint32_t)sm->r[0] * bpp;
        for (int32_t x = 0; x < dw; x++) {
            uint32_t c = s->bgra[(size_t)sy * (size_t)s->w + (size_t)((int64_t)x * s->w / (dw ? dw : 1))] | 0xff000000u;
            if (bpp == 4) memcpy(row + 4 * x, &c, 4); else { uint32_t v = bgra_to_px(c, d->pk); row[2 * x] = (uint8_t)v; row[2 * x + 1] = (uint8_t)(v >> 8); }
        }
    }
    d->gpu_dirty = 0; d->upload_over = 0; upload_rect(d, sm->r);
    return DD_OK;
}
M(sm_Update, 5) AMSample *me = obj(ARG(0), O_AMSMP); if (!me) RET(DDERR_INVALIDPARAMS, 5);
    uint32_t r = sample_update(me);
    if (ARG(2)) w32_event_signal(ARG(2), 1);
    RET(r, 5); END
M(sm_CompletionStatus, 3) AMSample *me = obj(ARG(0), O_AMSMP); RET(me && me->s->eos ? MS_S_ENDOFSTREAM : DD_OK, 3); END
M(sm_GetSurface, 3) AMSample *me = obj(ARG(0), O_AMSMP); if (!me) RET(DDERR_INVALIDPARAMS, 3);
    if (ARG(1)) { rt_w32(G_MEM, ARG(1), surf_iface(me->surf, O_S1)); surf_addref(me->surf); }
    if (ARG(2)) memcpy(GP(ARG(2)), me->r, 16); RET(DD_OK, 3); END
M(sm_SetRect, 2) AMSample *me = obj(ARG(0), O_AMSMP); if (me && ARG(1)) memcpy(me->r, GP(ARG(1)), 16); RET(DD_OK, 2); END

static void ams_vtables(void)
{
    static const Fn mm[] = { ams_QueryInterface, ams_AddRef, ams_Release, ams_GetInformation, ams_GetMediaStream, ams_EnumMediaStreams,
        ams_GetState, ams_SetState, ams_GetTime, ams_GetDuration, ams_Seek, ams_GetEndOfStreamEventHandle, ams_Initialize,
        ams_GetFilterGraph, ams_GetFilter, ams_AddMediaStream, ams_OpenFile, ams_OpenMoniker, ams_Render };
    static const Fn vs[] = { st_QueryInterface, st_AddRef, st_Release, st_GetMultiMediaStream, st_GetInformation, st_SetSameFormat,
        st_SendEndOfStream, st_SendEndOfStream, st_SendEndOfStream,   /* AllocateSample, CreateSharedSample: not asked for */
        vs_GetFormat, vs_SetFormat, vs_GetDirectDraw, vs_SetDirectDraw, vs_CreateSample, vs_GetTimePerFrame };
    static const Fn sm[] = { sm_QueryInterface, sm_AddRef, sm_Release, sm_GetMediaStream, sm_GetSampleTimes, sm_SetSampleTimes,
        sm_Update, sm_CompletionStatus, sm_GetSurface, sm_SetRect };
    _Static_assert(sizeof mm / sizeof *mm == 19 && sizeof vs / sizeof *vs == 15 && sizeof sm / sizeof *sm == 10, "amstream vtables");
    vt[O_AMS] = build_vtable("IAMMultiMediaStream", mm, 19); vt[O_AMV] = build_vtable("IDirectDrawMediaStream", vs, 15);
    vt[O_AMA] = build_vtable("IMediaStream", vs, 9); vt[O_AMSMP] = build_vtable("IDirectDrawStreamSample", sm, 10);
}
/* CoCreateInstance (net_ole.c): CLSID_AMMultiMediaStream 49c47ce5-9ba4-11d0-8212-00c04fc32c45 */
uint32_t w32_amstream_create(uint32_t clsid_first)
{
    if (clsid_first != 0x49c47ce5u) return 0;
    init_vtables();
    AMs *s = calloc(1, sizeof *s); s->ref = 1; pthread_mutex_init(&s->m, 0); s->dd = the_dd;
    s->g_mm = make_iface(O_AMS, s); s->g_vs = make_iface(O_AMV, s); s->g_as = make_iface(O_AMA, s);
    ams_log("multimedia stream created");
    return s->g_mm;
}
