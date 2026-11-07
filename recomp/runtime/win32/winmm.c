/* WINMM wave output on Core Audio (what the recompiled Miles Sound System mixes into), plus "no device" answers for
 * MIDI, mixer, aux, MCI and wave input. Buffers (WAVEHDR) play in submission order; finished buffers are marked
 * WHDR_DONE and reported through the client's chosen callback (function: on a guest service thread; event; window or
 * thread message). */
#include "w32.h"
#include "host_audio.h"

#include <unistd.h>
#include <math.h>

enum { WHDR_DONE = 1, WHDR_PREPARED = 2, WHDR_INQUEUE = 0x10, WOM_OPEN = 0x3bb, WOM_CLOSE = 0x3bc, WOM_DONE = 0x3bd };
enum { CB_NULL = 0, CB_WINDOW = 0x10000, CB_THREAD = 0x20000, CB_FUNCTION = 0x30000, CB_EVENT = 0x50000 };
typedef struct WaveOut {
    int used; uint32_t handle, cb, cbtype, inst; int ch, bits, rate;
    DsAudio * au; pthread_mutex_t lock;
    uint32_t q[256]; int qh, qn; uint32_t off;          /* queued WAVEHDRs (guest addresses), read offset in the head */
    float vol;
} WaveOut;
static WaveOut wos[8];
/* completion notifications are delivered by a guest service thread */
typedef struct Note { uint32_t wo, msg, hdr; } Note;
static Note notes[1024]; static int nnotes; static pthread_mutex_t nlock = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t ncv = PTHREAD_COND_INITIALIZER;
static int svc;
void w32_post(uint32_t tid, uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);
uint32_t w32_window_tid(uint32_t hwnd);
int w32_event_set(uint32_t h);
static void deliver(Ctx *c, Note n)
{
    WaveOut *w = &wos[n.wo];
    switch (w->cbtype) {
    case CB_FUNCTION: { uint32_t a[5] = {w->handle, n.msg, w->inst, n.hdr, 0}; w32_callback(c, w->cb, 5, a); break; }
    case CB_EVENT: w32_event_set(w->cb); break;
    case CB_WINDOW: w32_post(w32_window_tid(w->cb), w->cb, n.msg, w->handle, n.hdr); break;
    case CB_THREAD: w32_post(w->cb, 0, n.msg, w->handle, n.hdr); break;
    }
}
static void service(Ctx *c, void *arg)
{
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&nlock);
        while (!nnotes) pthread_cond_wait(&ncv, &nlock);
        Note q[1024]; int n = nnotes; memcpy(q, notes, sizeof(Note) * (size_t)n); nnotes = 0;
        pthread_mutex_unlock(&nlock);
        for (int k = 0; k < n; k++) deliver(c, q[k]);
    }
}
static void note(uint32_t wo, uint32_t msg, uint32_t hdr)
{
    if (wos[wo].cbtype == CB_NULL) return;
    pthread_mutex_lock(&nlock);
    if (nnotes < 1024) { notes[nnotes].wo = wo; notes[nnotes].msg = msg; notes[nnotes].hdr = hdr; nnotes++; }
    pthread_cond_signal(&ncv); pthread_mutex_unlock(&nlock);
}
/* every output path ends here: W32_AUDIOLOG prints the level once per second, W32_AUDIO_DUMP=<file.wav> records the
 * output (16-bit stereo); the caller then plays silence when W32_AUDIO_MUTE is set (test runs) */
void w32_audio_out(const float *lr, uint32_t frames, int rate, int source)
{
    static double acc[2]; static float peak[2]; static FILE *dump; static int dump_init, dump_rate;
    for (uint32_t i = 0; i < 2 * frames; i++) { float v = fabsf(lr[i]); if (v > peak[source]) peak[source] = v; }
    acc[source] += (double)frames / rate;
    if (acc[source] >= 1.0) {
        if (getenv("W32_AUDIOLOG")) fprintf(stderr, "audio: %s peak %.3f\n", source ? "DirectSound" : "waveOut", peak[source]);
        acc[source] = 0; peak[source] = 0;
    }
    if (!dump_init) { dump_init = 1; const char *p = getenv("W32_AUDIO_DUMP"); if (p && (dump = fopen(p, "wb"))) { uint8_t h[44] = {0}; fwrite(h, 1, 44, dump); dump_rate = rate; } }
    if (!dump || rate != dump_rate) return;
    int16_t s[2 * 1024];
    for (uint32_t at = 0; at < frames; at += 1024) {
        uint32_t n = frames - at < 1024 ? frames - at : 1024;
        for (uint32_t i = 0; i < 2 * n; i++) { float v = lr[2 * at + i] * 32768.0f; s[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : lrintf(v)); }
        fwrite(s, 4, n, dump);
    }
    long n = ftell(dump) - 44; uint32_t v, br = (uint32_t)rate * 4; uint16_t h16; uint8_t h[44];
    memcpy(h, "RIFF", 4); v = (uint32_t)n + 36; memcpy(h + 4, &v, 4); memcpy(h + 8, "WAVEfmt ", 8); v = 16; memcpy(h + 16, &v, 4);
    h16 = 1; memcpy(h + 20, &h16, 2); h16 = 2; memcpy(h + 22, &h16, 2); v = (uint32_t)rate; memcpy(h + 24, &v, 4); memcpy(h + 28, &br, 4);
    h16 = 4; memcpy(h + 32, &h16, 2); h16 = 16; memcpy(h + 34, &h16, 2); memcpy(h + 36, "data", 4); v = (uint32_t)n; memcpy(h + 40, &v, 4);
    fseek(dump, 0, SEEK_SET); fwrite(h, 1, 44, dump); fseek(dump, 0, SEEK_END);
}
static void render(void *u, void *buffer, uint32_t frames)
{

    uint32_t k = (uint32_t)(uintptr_t)u; WaveOut *w = &wos[k];
    uint8_t *out = buffer; uint32_t want = frames * (uint32_t)(w->ch * w->bits / 8), got = 0;
    uint32_t done[64]; int nd = 0;
    pthread_mutex_lock(&w->lock);
    while (got < want && w->qn) {
        uint32_t h = w->q[w->qh], data = rt_r32(G_MEM, h), len = rt_r32(G_MEM, h + 4);
        uint32_t n = len - w->off < want - got ? len - w->off : want - got;
        memcpy(out + got, G_MEM + data + w->off, n); got += n; w->off += n;
        if (w->off >= len) {
            rt_w32(G_MEM, h + 16, (rt_r32(G_MEM, h + 16) & ~(uint32_t)WHDR_INQUEUE) | WHDR_DONE);
            w->qh = (w->qh + 1) % 256; w->qn--; w->off = 0;
            if (nd < 64) done[nd++] = h;
        }
    }
    pthread_mutex_unlock(&w->lock);
    if (got < want) memset(out + got, w->bits == 8 ? 0x80 : 0, want - got);
    if (k == 0 && w->ch == 2 && w->bits == 16) {                /* the shared output tap (level log, dump) */
        float tmp[2 * 1024];
        for (uint32_t at = 0; at < frames; at += 1024) {
            uint32_t n = frames - at < 1024 ? frames - at : 1024;
            for (uint32_t i = 0; i < 2 * n; i++) tmp[i] = ((int16_t *)out)[2 * at + i] / 32768.0f;
            w32_audio_out(tmp, n, w->rate, 0);
        }
    }
    if (getenv("W32_AUDIO_MUTE")) memset(out, w->bits == 8 ? 0x80 : 0, want);
    for (int j = 0; j < nd; j++) note(k, WOM_DONE, done[j]);

}
IMPL(winmm, waveOutGetDevCapsA)
{
    uint32_t p = ARG(1), sz = ARG(2); uint8_t caps[52] = {0};
    caps[0] = 1; caps[2] = 1; caps[4] = 1; snprintf((char *)caps + 8, 32, "Native Audio Output");
    uint32_t fmts = 0xfff; memcpy(caps + 40, &fmts, 4); caps[44] = 2; uint32_t sup = 0x4 | 0x8; memcpy(caps + 48, &sup, 4);
    memcpy(GP(p), caps, sz < 52 ? sz : 52);
    RET(0, 3);
}
IMPL(winmm, waveOutOpen)
{
    uint32_t ph = ARG(0), fmt = ARG(2), cb = ARG(3), inst = ARG(4), fl = ARG(5);
    uint16_t tag = (uint16_t)rt_r16(G_MEM, fmt), ch = (uint16_t)rt_r16(G_MEM, fmt + 2), bits = (uint16_t)rt_r16(G_MEM, fmt + 14);
    uint32_t rate = rt_r32(G_MEM, fmt + 4);
    if (tag != 1 || (bits != 8 && bits != 16) || ch < 1 || ch > 2) RET(32, 6);   /* WAVERR_BADFORMAT */
    if (fl & 1) RET(0, 6);                                                        /* WAVE_FORMAT_QUERY */
    int k = 0; while (k < 8 && wos[k].used) k++;
    if (k == 8) RET(4, 6);                                                        /* MMSYSERR_ALLOCATED */
    WaveOut *w = &wos[k]; memset(w, 0, sizeof *w);
    w->used = 1; w->handle = 0x00c00000u + 4 * (uint32_t)k; w->cb = cb; w->cbtype = fl & 0x70000; w->inst = inst;
    w->ch = ch; w->bits = bits; w->rate = (int)rate; pthread_mutex_init(&w->lock, NULL); w->vol = 1;
    w->au = ds_audio_open((int)rate, ch, bits, 0, render, (void *)(uintptr_t)k);
    if (!w->au) { pthread_mutex_destroy(&w->lock); w->used = 0; RET(2, 6); }
    if (!svc) { svc = 1; extern int w32_spawn_service(void (*)(Ctx *, void *), void *); w32_spawn_service(service, 0); }
    rt_w32(G_MEM, ph, w->handle);
    if (getenv("W32_AUDIOLOG")) fprintf(stderr, "audio: waveOutOpen %u Hz, %u bits, %u ch, callback type %x\n", rate, bits, ch, w->cbtype);
    note((uint32_t)k, WOM_OPEN, 0);
    RET(0, 6);
}
static WaveOut *WO(uint32_t h) { uint32_t k = (h - 0x00c00000u) / 4; return h >= 0x00c00000u && k < 8 && wos[k].used ? &wos[k] : 0; }
IMPL(winmm, waveOutPrepareHeader) { rt_w32(G_MEM, ARG(1) + 16, rt_r32(G_MEM, ARG(1) + 16) | WHDR_PREPARED); RET(0, 3); }
IMPL(winmm, waveOutUnprepareHeader)
{
    uint32_t h = ARG(1), f = rt_r32(G_MEM, h + 16);
    if (f & WHDR_INQUEUE) RET(33, 3);                                             /* WAVERR_STILLPLAYING */
    rt_w32(G_MEM, h + 16, f & ~(uint32_t)WHDR_PREPARED); RET(0, 3);
}
IMPL(winmm, waveOutWrite)
{
    WaveOut *w = WO(ARG(0)); uint32_t h = ARG(1); if (!w) RET(5, 3);
    pthread_mutex_lock(&w->lock);
    if (w->qn < 256) { rt_w32(G_MEM, h + 16, (rt_r32(G_MEM, h + 16) & ~(uint32_t)WHDR_DONE) | WHDR_INQUEUE); w->q[(w->qh + w->qn) % 256] = h; w->qn++; }
    pthread_mutex_unlock(&w->lock);
    RET(0, 3);
}
IMPL(winmm, waveOutReset)
{
    WaveOut *w = WO(ARG(0)); if (!w) RET(5, 1);
    uint32_t done[256]; int nd = 0;
    pthread_mutex_lock(&w->lock);
    while (w->qn) { uint32_t h = w->q[w->qh]; rt_w32(G_MEM, h + 16, (rt_r32(G_MEM, h + 16) & ~(uint32_t)WHDR_INQUEUE) | WHDR_DONE); done[nd++] = h; w->qh = (w->qh + 1) % 256; w->qn--; }
    w->off = 0;
    pthread_mutex_unlock(&w->lock);
    for (int j = 0; j < nd; j++) note((uint32_t)(w - wos), WOM_DONE, done[j]);
    RET(0, 1);
}
IMPL(winmm, waveOutClose)
{
    WaveOut *w = WO(ARG(0)); if (!w) RET(5, 1);
    ds_audio_close(w->au);
    pthread_mutex_destroy(&w->lock);
    note((uint32_t)(w - wos), WOM_CLOSE, 0); w->used = 0;
    RET(0, 1);
}
IMPL(winmm, waveOutGetID) { if (ARG(1)) rt_w32(G_MEM, ARG(1), 0); RET(0, 2); }
/* no MIDI, mixer, aux, MCI or recording devices */
IMPL(winmm, auxGetNumDevs) { RET(0, 0); }
IMPL(winmm, auxGetDevCapsA) { RET(2, 3); }
IMPL(winmm, auxGetVolume) { RET(2, 2); }
IMPL(winmm, auxSetVolume) { RET(2, 2); }
/* MCI: the AVI video device ("avivideo"), as Resident Evil plays its movies: MCI_OPEN of the device, then of the movie
 * (MCI_OPEN_ELEMENT, a child window of the game's), MCI_STATUS of that window, MCI_PLAY with MCI_NOTIFY (the game's
 * window hears MM_MCINOTIFY when it ends), MCI_STOP / MCI_CLOSE when it is skipped. The host plays the movie
 * (main.m gta_movie_play: AVFoundation over the game): its "<file>.mpg" (a mod's first, else the installer's MPEG-1 copy
 * beside it), or the file itself. RE_NO_MOVIES / GTA_NO_MOVIES: each movie ends as it starts. Other MCI devices: none. */
#include <dlfcn.h>
#include <sys/stat.h>
enum { MCI_OPEN = 0x803, MCI_CLOSE = 0x804, MCI_PLAY = 0x806, MCI_SEEK = 0x807, MCI_STOP = 0x808, MCI_PAUSE = 0x809,
       MCI_STATUS = 0x814, MCI_RESUME = 0x855, MCI_NOTIFY_F = 1, MCI_OPEN_TYPE = 0x2000, MCI_OPEN_ELEMENT = 0x200,
       MCI_STATUS_ITEM = 0x100, MM_MCINOTIFY = 0x3b9, MCI_NOTIFY_SUCCESSFUL = 1, MCI_NOTIFY_ABORTED = 4 };
static struct { int open; char path[2048]; uint32_t notify_hwnd; volatile int playing, gen; } mci[8];   /* id 1: the device, 2..7: movies */
static int mci_log(void) { static int v = -1; if (v < 0) v = getenv("W32_MOVIELOG") != 0; return v; }
static void mci_done(void *user)                          /* the host's player: the movie ended (main thread) */
{
    uintptr_t u = (uintptr_t)user; int id = (int)(u & 7), gen = (int)(u >> 3);
    if (id < 2 || id > 7 || !mci[id].playing || mci[id].gen != gen) return;
    mci[id].playing = 0;
    uint32_t w32_window_tid(uint32_t); void w32_post(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    if (mci[id].notify_hwnd) w32_post(w32_window_tid(mci[id].notify_hwnd), mci[id].notify_hwnd, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, (uint32_t)id);
    if (mci_log()) fprintf(stderr, "mci: %d ended\n", id);
}
IMPL(winmm, mciSendCommandA)                         /* (device, message, flags, parameters) */
{
    uint32_t id = ARG(0), msg = ARG(1), flags = ARG(2), p = ARG(3);
    static int (*play)(const char *, void (*)(void *), void *); static void (*stop)(void); static int looked;
    if (!looked) { looked = 1; play = (int (*)(const char *, void (*)(void *), void *))dlsym(RTLD_DEFAULT, "gta_movie_play");
                   stop = (void (*)(void))dlsym(RTLD_DEFAULT, "gta_movie_stop"); }
    if (mci_log()) fprintf(stderr, "mci: device %u message %#x flags %#x\n", id, msg, flags);
    if (msg == MCI_OPEN) {
        if (flags & MCI_OPEN_ELEMENT) {                  /* a movie */
            const char *name = GS(rt_r32(G_MEM, p + 12)); char host[2048], conv[1100]; struct stat st;
            snprintf(conv, sizeof conv, "%s.mpg", name);
            if (!w32_host_path(conv, host, sizeof host, 0) && !stat(host, &st)) {}
            else if (w32_host_path(name, host, sizeof host, 0) || stat(host, &st)) RET(275 /* MCIERR_FILE_NOT_FOUND */, 4);
            int k = 2; while (k < 8 && mci[k].open) k++;
            if (k == 8) RET(257, 4);
            mci[k].open = 1; mci[k].playing = 0; snprintf(mci[k].path, sizeof mci[k].path, "%s", host);
            rt_w32(G_MEM, p + 4, (uint32_t)k);
            if (mci_log()) fprintf(stderr, "mci: open %s -> %d (%s)\n", name, k, host);
            RET(0, 4);
        }
        if (flags & MCI_OPEN_TYPE) {                     /* the device itself: "avivideo" only */
            uint32_t t = rt_r32(G_MEM, p + 8);
            if ((flags & 0x1000 /* MCI_OPEN_TYPE_ID */) || !t || strcasecmp(GS(t), "avivideo")) RET(266 /* MCIERR_INVALID_DEVICE_NAME */, 4);
            mci[1].open = 1; rt_w32(G_MEM, p + 4, 1); RET(0, 4);
        }
        RET(257, 4);
    }
    if (id < 1 || id > 7 || !mci[id].open) RET(257 /* MCIERR_INVALID_DEVICE_ID */, 4);
    switch (msg) {
    case MCI_PLAY:
        if (id < 2) RET(0, 4);
        mci[id].notify_hwnd = (flags & MCI_NOTIFY_F) ? rt_r32(G_MEM, p) : 0;
        mci[id].gen++; mci[id].playing = 1;
        { const char *off = getenv("RE_NO_MOVIES"); if (!off) off = getenv("GTA_NO_MOVIES");
          if ((off && strcmp(off, "0")) || !play || !play(mci[id].path, mci_done, (void *)(uintptr_t)((uint32_t)id | (uint32_t)mci[id].gen << 3)))
              mci_done((void *)(uintptr_t)((uint32_t)id | (uint32_t)mci[id].gen << 3)); }   /* nothing to show: over at once */
        RET(0, 4);
    case MCI_STOP: case MCI_CLOSE:
        if (id >= 2 && mci[id].playing) {
            mci[id].playing = 0; mci[id].gen++; if (stop) stop();
            if ((flags & MCI_NOTIFY_F) && mci[id].notify_hwnd) { uint32_t w32_window_tid(uint32_t); void w32_post(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
                w32_post(w32_window_tid(mci[id].notify_hwnd), mci[id].notify_hwnd, MM_MCINOTIFY, MCI_NOTIFY_ABORTED, id); }
        }
        if (msg == MCI_CLOSE) mci[id].open = 0;
        RET(0, 4);
    case MCI_STATUS:
        if (flags & MCI_STATUS_ITEM) {
            uint32_t item = rt_r32(G_MEM, p + 8), v = 0;
            if (item == 4) v = mci[id].playing ? 526 /* MCI_MODE_PLAY */ : 525 /* MCI_MODE_STOP */;
            else if (item == 0x4001) v = 0x7fff0001u;    /* the movie's window: one of the host's, not the game's */
            rt_w32(G_MEM, p + 4, v);
        }
        RET(0, 4);
    default: RET(0, 4);                                  /* MCI_WINDOW, MCI_PUT, MCI_SEEK, MCI_PAUSE, MCI_SET ...: accepted */
    }
}
IMPL(winmm, midiOutOpen) { RET(6, 5); }
IMPL(winmm, midiOutClose) { RET(5, 1); }
IMPL(winmm, midiOutLongMsg) { RET(5, 3); }
IMPL(winmm, midiOutShortMsg) { RET(5, 2); }
IMPL(winmm, midiOutPrepareHeader) { RET(5, 3); }
IMPL(winmm, midiOutUnprepareHeader) { RET(5, 3); }
IMPL(winmm, midiOutReset) { RET(5, 1); }
IMPL(winmm, mixerGetNumDevs) { RET(0, 0); }
IMPL(winmm, mixerOpen) { RET(2, 5); }
IMPL(winmm, mixerClose) { RET(5, 1); }
IMPL(winmm, mixerGetLineInfoA) { RET(5, 3); }
IMPL(winmm, mixerGetLineControlsA) { RET(5, 3); }
IMPL(winmm, mixerGetControlDetailsA) { RET(5, 3); }
IMPL(winmm, mixerSetControlDetails) { RET(5, 3); }
IMPL(winmm, waveInOpen) { RET(2, 6); }
IMPL(winmm, waveInClose) { RET(5, 1); }
IMPL(winmm, waveInAddBuffer) { RET(5, 3); }
IMPL(winmm, waveInPrepareHeader) { RET(5, 3); }
IMPL(winmm, waveInUnprepareHeader) { RET(5, 3); }
IMPL(winmm, waveInReset) { RET(5, 1); }
IMPL(winmm, waveInStart) { RET(5, 1); }
IMPL(winmm, timeEndPeriod) { RET(0, 1); }
IMPL(winmm, timeBeginPeriod) { RET(0, 1); }
