/* psx_seq: renders PlayStation sequenced music (a VAB sample bank and a SEQ/SEP sequence, libsnd's formats) to a WAV,
 * the way the PlayStation's sound chip plays it, for the disc import (psx_import.py music). Nothing from a disc is part
 * of the project; this only reads the player's own files.
 *
 *   psx_seq <bank.vh> <bank.vb> <sequence> <out.wav> [-n <sequence number in a SEP>] [-s <seconds>] [-r <reverb 0-9>]
 *           [-d <reverb depth 0-127>] [-b <the bank's sound RAM address, hex>] [-t <tempo scale>] [-i] [-v]
 *   (-i: print the sequences in the file and exit; -v: the loop markers' times, notes and programs used)
 *   psx_seq --samples <bank.vh> <bank.vb> <folder> [-b <address>]: each sample the bank's tones play, decoded once
 *           through (to its end flag) as <folder>/<offset in the .vb, hex>.raw (16-bit mono); one line each on stdout:
 *           offset, samples, looped (0/1), program, tone, centre note, fine tune (for the disc import's sound effects)
 *
 * Dino Crisis uses Capcom's own formats, read here too:
 *   - "Gian" banks: a 16-byte header (programs at 6, master volume at 12, pan at 13); 16 programs of 8 bytes (tones,
 *     volume, -, -, pan); then for each program 16 tones of 24 bytes laid out as a VAB tone, except that the last
 *     two bytes are the sample's sound RAM address / 8, so the bank's own address is needed (-b; its file says it).
 *   - sequences: an 8-byte header (00 01, ticks a beat and tempo, little-endian); no running status; an event's status
 *     byte has bit 7 set when a one-byte delta time follows it and clear when none does; A0 nn waits nn ticks;
 *     FF 51 + 3 bytes sets the tempo; FF 2F ends it. The loop markers are libsnd's.
 *
 * The sound chip (SPU) follows the PSX-SPX reference (psx-spx.consoledev.net, "Sound Processing Unit"):
 *   - SPU-ADPCM decoding (16-byte blocks, 28 samples, filters 0-4, loop start/end/repeat flags)
 *   - the pitch counter (4096 = 44100 Hz) and 4-point Gaussian interpolation with the hardware's table
 *   - ADSR envelopes exactly as the hardware steps them (attack, decay, sustain, release; linear or exponential)
 *   - the reverb unit at 22050 Hz with the documented presets (Room, Studio Small/Medium/Large, Hall, Half Echo,
 *     Space Echo, Chaos Echo, Delay)
 * The sequencer follows libsnd: VAB programs and tones (key ranges, centre note and fine tune, volume, pan, ADSR,
 * reverb flag in the tone's mode), note on/off, program change, volume (CC7), expression (CC11), pan (CC10), pitch
 * bend (with the tone's bend range), tempo, and libsnd's loop markers (NRPN 99 = 20 loop start, 30 loop end, with the
 * loop count in data entry CC6; 127 = for ever). Output: 44.1 kHz, 16-bit stereo. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int16_t gauss[512] = {
    -0x001,-0x001,-0x001,-0x001,-0x001,-0x001,-0x001,-0x001,-0x001,-0x001,-0x001,-0x001,-0x001,-0x001,-0x001,-0x001,
    0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0001,0x0001,0x0001,0x0001,0x0002,0x0002,0x0002,0x0003,0x0003,
    0x0003,0x0004,0x0004,0x0005,0x0005,0x0006,0x0007,0x0007,0x0008,0x0009,0x0009,0x000A,0x000B,0x000C,0x000D,0x000E,
    0x000F,0x0010,0x0011,0x0012,0x0013,0x0015,0x0016,0x0018,0x0019,0x001B,0x001C,0x001E,0x0020,0x0021,0x0023,0x0025,
    0x0027,0x0029,0x002C,0x002E,0x0030,0x0033,0x0035,0x0038,0x003A,0x003D,0x0040,0x0043,0x0046,0x0049,0x004D,0x0050,
    0x0054,0x0057,0x005B,0x005F,0x0063,0x0067,0x006B,0x006F,0x0074,0x0078,0x007D,0x0082,0x0087,0x008C,0x0091,0x0096,
    0x009C,0x00A1,0x00A7,0x00AD,0x00B3,0x00BA,0x00C0,0x00C7,0x00CD,0x00D4,0x00DB,0x00E3,0x00EA,0x00F2,0x00FA,0x0101,
    0x010A,0x0112,0x011B,0x0123,0x012C,0x0135,0x013F,0x0148,0x0152,0x015C,0x0166,0x0171,0x017B,0x0186,0x0191,0x019C,
    0x01A8,0x01B4,0x01C0,0x01CC,0x01D9,0x01E5,0x01F2,0x0200,0x020D,0x021B,0x0229,0x0237,0x0246,0x0255,0x0264,0x0273,
    0x0283,0x0293,0x02A3,0x02B4,0x02C4,0x02D6,0x02E7,0x02F9,0x030B,0x031D,0x0330,0x0343,0x0356,0x036A,0x037E,0x0392,
    0x03A7,0x03BC,0x03D1,0x03E7,0x03FC,0x0413,0x042A,0x0441,0x0458,0x0470,0x0488,0x04A0,0x04B9,0x04D2,0x04EC,0x0506,
    0x0520,0x053B,0x0556,0x0572,0x058E,0x05AA,0x05C7,0x05E4,0x0601,0x061F,0x063E,0x065C,0x067C,0x069B,0x06BB,0x06DC,
    0x06FD,0x071E,0x0740,0x0762,0x0784,0x07A7,0x07CB,0x07EF,0x0813,0x0838,0x085D,0x0883,0x08A9,0x08D0,0x08F7,0x091E,
    0x0946,0x096F,0x0998,0x09C1,0x09EB,0x0A16,0x0A40,0x0A6C,0x0A98,0x0AC4,0x0AF1,0x0B1E,0x0B4C,0x0B7A,0x0BA9,0x0BD8,
    0x0C07,0x0C38,0x0C68,0x0C99,0x0CCB,0x0CFD,0x0D30,0x0D63,0x0D97,0x0DCB,0x0E00,0x0E35,0x0E6B,0x0EA1,0x0ED7,0x0F0F,
    0x0F46,0x0F7F,0x0FB7,0x0FF1,0x102A,0x1065,0x109F,0x10DB,0x1116,0x1153,0x118F,0x11CD,0x120B,0x1249,0x1288,0x12C7,
    0x1307,0x1347,0x1388,0x13C9,0x140B,0x144D,0x1490,0x14D4,0x1517,0x155C,0x15A0,0x15E6,0x162C,0x1672,0x16B9,0x1700,
    0x1747,0x1790,0x17D8,0x1821,0x186B,0x18B5,0x1900,0x194B,0x1996,0x19E2,0x1A2E,0x1A7B,0x1AC8,0x1B16,0x1B64,0x1BB3,
    0x1C02,0x1C51,0x1CA1,0x1CF1,0x1D42,0x1D93,0x1DE5,0x1E37,0x1E89,0x1EDC,0x1F2F,0x1F82,0x1FD6,0x202A,0x207F,0x20D4,
    0x2129,0x217F,0x21D5,0x222C,0x2282,0x22DA,0x2331,0x2389,0x23E1,0x2439,0x2492,0x24EB,0x2545,0x259E,0x25F8,0x2653,
    0x26AD,0x2708,0x2763,0x27BE,0x281A,0x2876,0x28D2,0x292E,0x298B,0x29E7,0x2A44,0x2AA1,0x2AFF,0x2B5C,0x2BBA,0x2C18,
    0x2C76,0x2CD4,0x2D33,0x2D91,0x2DF0,0x2E4F,0x2EAE,0x2F0D,0x2F6C,0x2FCC,0x302B,0x308B,0x30EA,0x314A,0x31AA,0x3209,
    0x3269,0x32C9,0x3329,0x3389,0x33E9,0x3449,0x34A9,0x3509,0x3569,0x35C9,0x3629,0x3689,0x36E8,0x3748,0x37A8,0x3807,
    0x3867,0x38C6,0x3926,0x3985,0x39E4,0x3A43,0x3AA2,0x3B00,0x3B5F,0x3BBD,0x3C1B,0x3C79,0x3CD7,0x3D35,0x3D92,0x3DEF,
    0x3E4C,0x3EA9,0x3F05,0x3F62,0x3FBD,0x4019,0x4074,0x40D0,0x412A,0x4185,0x41DF,0x4239,0x4292,0x42EB,0x4344,0x439C,
    0x43F4,0x444C,0x44A3,0x44FA,0x4550,0x45A6,0x45FC,0x4651,0x46A6,0x46FA,0x474E,0x47A1,0x47F4,0x4846,0x4898,0x48E9,
    0x493A,0x498A,0x49D9,0x4A29,0x4A77,0x4AC5,0x4B13,0x4B5F,0x4BAC,0x4BF7,0x4C42,0x4C8D,0x4CD7,0x4D20,0x4D68,0x4DB0,
    0x4DF7,0x4E3E,0x4E84,0x4EC9,0x4F0E,0x4F52,0x4F95,0x4FD7,0x5019,0x505A,0x509A,0x50DA,0x5118,0x5156,0x5194,0x51D0,
    0x520C,0x5247,0x5281,0x52BA,0x52F3,0x532A,0x5361,0x5397,0x53CC,0x5401,0x5434,0x5467,0x5499,0x54CA,0x54FA,0x5529,
    0x5558,0x5585,0x55B2,0x55DE,0x5609,0x5632,0x565B,0x5684,0x56AB,0x56D1,0x56F6,0x571B,0x573E,0x5761,0x5782,0x57A3,
    0x57C3,0x57E2,0x57FF,0x581C,0x5838,0x5853,0x586D,0x5886,0x589E,0x58B5,0x58CB,0x58E0,0x58F4,0x5907,0x5919,0x592A,
    0x593A,0x5949,0x5958,0x5965,0x5971,0x597C,0x5986,0x598F,0x5997,0x599E,0x59A4,0x59A9,0x59AD,0x59B0,0x59B2,0x59B3,
};
/* reverb presets (PSX-SPX "Reverb Examples"): size in bytes, then the 32 registers 1F801DC0h..1F801DFEh */
static const struct { const char *name; int size; uint16_t r[32]; } presets[10] = {
    {"off", 0x10, {0,0,0,0,0,0,0,0, 0,0,1,1,1,1,1,1, 0,0,1,1,1,1,1,1, 0,0,1,1,1,1,0,0}},
    {"room", 0x26C0, {0x007D,0x005B,0x6D80,0x54B8,0xBED0,0x0000,0x0000,0xBA80, 0x5800,0x5300,0x04D6,0x0333,0x03F0,0x0227,0x0374,0x01EF,
                      0x0334,0x01B5,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x01B4,0x0136,0x00B8,0x005C,0x8000,0x8000}},
    {"studio small", 0x1F40, {0x0033,0x0025,0x70F0,0x4FA8,0xBCE0,0x4410,0xC0F0,0x9C00, 0x5280,0x4EC0,0x03E4,0x031B,0x03A4,0x02AF,0x0372,0x0266,
                      0x031C,0x025D,0x025C,0x018E,0x022F,0x0135,0x01D2,0x00B7, 0x018F,0x00B5,0x00B4,0x0080,0x004C,0x0026,0x8000,0x8000}},
    {"studio medium", 0x4840, {0x00B1,0x007F,0x70F0,0x4FA8,0xBCE0,0x4510,0xBEF0,0xB4C0, 0x5280,0x4EC0,0x0904,0x076B,0x0824,0x065F,0x07A2,0x0616,
                      0x076C,0x05ED,0x05EC,0x042E,0x050F,0x0305,0x0462,0x02B7, 0x042F,0x0265,0x0264,0x01B2,0x0100,0x0080,0x8000,0x8000}},
    {"studio large", 0x6FE0, {0x00E3,0x00A9,0x6F60,0x4FA8,0xBCE0,0x4510,0xBEF0,0xA680, 0x5680,0x52C0,0x0DFB,0x0B58,0x0D09,0x0A3C,0x0BD9,0x0973,
                      0x0B59,0x08DA,0x08D9,0x05E9,0x07EC,0x04B0,0x06EF,0x03D2, 0x05EA,0x031D,0x031C,0x0238,0x0154,0x00AA,0x8000,0x8000}},
    {"hall", 0xADE0, {0x01A5,0x0139,0x6000,0x5000,0x4C00,0xB800,0xBC00,0xC000, 0x6000,0x5C00,0x15BA,0x11BB,0x14C2,0x10BD,0x11BC,0x0DC1,
                      0x11C0,0x0DC3,0x0DC0,0x09C1,0x0BC4,0x07C1,0x0A00,0x06CD, 0x09C2,0x05C1,0x05C0,0x041A,0x0274,0x013A,0x8000,0x8000}},
    {"half echo", 0x3C00, {0x0017,0x0013,0x70F0,0x4FA8,0xBCE0,0x4510,0xBEF0,0x8500, 0x5F80,0x54C0,0x0371,0x02AF,0x02E5,0x01DF,0x02B0,0x01D7,
                      0x0358,0x026A,0x01D6,0x011E,0x012D,0x00B1,0x011F,0x0059, 0x01A0,0x00E3,0x0058,0x0040,0x0028,0x0014,0x8000,0x8000}},
    {"space echo", 0xF6C0, {0x033D,0x0231,0x7E00,0x5000,0xB400,0xB000,0x4C00,0xB000, 0x6000,0x5400,0x1ED6,0x1A31,0x1D14,0x183B,0x1BC2,0x16B2,
                      0x1A32,0x15EF,0x15EE,0x1055,0x1334,0x0F2D,0x11F6,0x0C5D, 0x1056,0x0AE1,0x0AE0,0x07A2,0x0464,0x0232,0x8000,0x8000}},
    {"chaos echo", 0x18040, {0x0001,0x0001,0x7FFF,0x7FFF,0x0000,0x0000,0x0000,0x8100, 0x0000,0x0000,0x1FFF,0x0FFF,0x1005,0x0005,0x0000,0x0000,
                      0x1005,0x0005,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x1004,0x1002,0x0004,0x0002,0x8000,0x8000}},
    {"delay", 0x18040, {0x0001,0x0001,0x7FFF,0x7FFF,0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x1FFF,0x0FFF,0x1005,0x0005,0x0000,0x0000,
                      0x1005,0x0005,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x1004,0x1002,0x0004,0x0002,0x8000,0x8000}},
};

/* ---- the VAB bank ---- */
typedef struct { uint8_t prior, mode, vol, pan, center, shift, min, max, pbmin, pbmax; uint16_t adsr1, adsr2; int16_t prog, vag; int32_t addr; } Tone;
typedef struct { int ntones; uint8_t mvol, mpan; Tone t[16]; } Prog;
static Prog progs[128]; static uint8_t vab_mvol = 127, vab_pan = 64;
static uint8_t *vb; static long vb_len; static uint32_t vag_off[256], vag_len[256]; static int nvags;

static uint8_t *load(const char *path, long *len)
{
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "psx_seq: cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*len + 16); if (fread(b, 1, (size_t)*len, f) != (size_t)*len) { fprintf(stderr, "psx_seq: short read %s\n", path); exit(2); }
    fclose(f); return b;
}
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static uint32_t bank_base; static int gian;
static int tone_ok(const Tone *t) { return gian ? t->addr >= 0 && t->addr < vb_len : t->vag > 0 && t->vag <= nvags; }
static void load_gian(const uint8_t *h, long hlen)
{
    int ps = le16(h + 6); gian = 1; vab_mvol = h[12]; vab_pan = h[13];
    for (int p = 0; p < 16 && p < ps; p++) {
        Prog *g = &progs[p]; const uint8_t *pa = h + 0x10 + p * 8; g->ntones = pa[0]; g->mvol = pa[1]; g->mpan = pa[4];
        if (g->ntones > 16) g->ntones = 16;
        for (int k = 0; k < g->ntones; k++) {
            long o = 0x90 + (long)p * 0x180 + k * 24; if (o + 24 > hlen) { g->ntones = k; break; }
            const uint8_t *t = h + o; Tone *x = &g->t[k];
            x->prior = t[0]; x->mode = t[1]; x->vol = t[2]; x->pan = t[3]; x->center = t[4]; x->shift = t[5]; x->min = t[6]; x->max = t[7];
            x->pbmin = t[12]; x->pbmax = t[13]; x->adsr1 = le16(t + 16); x->adsr2 = le16(t + 18); x->prog = (int16_t)le16(t + 20);
            x->addr = (int32_t)le16(t + 22) * 8 - (int32_t)bank_base; x->vag = 1;
        }
    }
}
static void load_vab(const uint8_t *h, long hlen)
{
    if (hlen >= 16 && !memcmp(h, "Gian", 4)) { load_gian(h, hlen); return; }
    if (hlen < 32 || memcmp(h, "pBAV", 4)) { fprintf(stderr, "psx_seq: not a VAB header (pBAV) or a Gian bank\n"); exit(2); }
    int ps = le16(h + 18); nvags = le16(h + 22); vab_mvol = h[24]; vab_pan = h[25];
    const uint8_t *pa = h + 32, *ta = pa + 128 * 16, *vs = ta + (long)ps * 16 * 32;
    if ((long)(vs - h) + 512 > hlen) { fprintf(stderr, "psx_seq: VAB header too short\n"); exit(2); }
    int block = 0;
    for (int p = 0; p < 128; p++) {
        Prog *g = &progs[p]; g->ntones = pa[p * 16]; g->mvol = pa[p * 16 + 1]; g->mpan = pa[p * 16 + 4];
        if (!g->ntones) continue;
        if (block >= ps) { g->ntones = 0; continue; }
        for (int k = 0; k < 16 && k < g->ntones; k++) {
            const uint8_t *t = ta + (long)block * 16 * 32 + k * 32; Tone *o = &g->t[k];
            o->prior = t[0]; o->mode = t[1]; o->vol = t[2]; o->pan = t[3]; o->center = t[4]; o->shift = t[5]; o->min = t[6]; o->max = t[7];
            o->pbmin = t[12]; o->pbmax = t[13]; o->adsr1 = le16(t + 16); o->adsr2 = le16(t + 18); o->prog = (int16_t)le16(t + 20); o->vag = (int16_t)le16(t + 22);
        }
        block++;
    }
    uint32_t off = 0;
    for (int v = 0; v < 256; v++) { vag_off[v] = off; vag_len[v] = (uint32_t)le16(vs + 2 * v) * 8; off += vag_len[v]; }
}

/* ---- the SPU ---- */
enum { ATTACK, DECAY, SUSTAIN, RELEASE, OFF };
typedef struct {
    int on, chan, key, prior; long age;
    uint32_t start, addr, loop; int16_t buf[28]; int16_t hist[4]; int32_t s1, s2; uint32_t counter; int first;
    uint32_t pitch, base_pitch; int32_t vol_l, vol_r; int reverb; const Tone *tone;
    int phase; int32_t level, env_counter; uint16_t adsr1, adsr2; uint8_t sustain_level;
} Voice;
static Voice voices[24]; static long age_counter;

static void decode_block(Voice *v)
{
    static const int f0[5] = {0, 60, 115, 98, 122}, f1[5] = {0, 0, -52, -55, -60};
    if (v->addr + 16 > (uint32_t)vb_len) { v->phase = OFF; v->on = 0; return; }
    const uint8_t *b = vb + v->addr; int shift = b[0] & 15, filter = (b[0] >> 4) & 7; if (shift > 12) shift = 9; if (filter > 4) filter = 4;
    for (int k = 0; k < 28; k++) {
        int nib = (b[2 + k / 2] >> ((k & 1) * 4)) & 15; int32_t s = (int16_t)(nib << 12) >> shift;
        s += (v->s1 * f0[filter] + v->s2 * f1[filter] + 32) >> 6;
        if (s > 32767) s = 32767; if (s < -32768) s = -32768;
        v->s2 = v->s1; v->s1 = s; v->buf[k] = (int16_t)s;
    }
}
static void block_flags_after(Voice *v)                /* after the block's 28 samples: its loop flags */
{
    const uint8_t *b = vb + v->addr; int flags = b[1];
    if (flags & 4) v->loop = v->addr;
    if (flags & 1) { v->addr = v->loop; if (!(flags & 2)) { v->phase = OFF; v->level = 0; v->on = 0; } }
    else v->addr += 16;
}
/* one envelope step, as the hardware does it (PSX-SPX "Envelope Operation") */
static void env_step(Voice *v, int exp, int decreasing, int shift, int step_val, int all_bits)
{
    int32_t step = 7 - step_val; if (decreasing) step = ~step;
    step = shift < 11 ? step << (11 - shift) : step; int32_t inc = 0x8000 >> (shift > 11 ? shift - 11 : 0);
    if (exp && !decreasing && v->level > 0x6000) {
        if (shift < 10) step >>= 2; else if (shift >= 11) inc >>= 2; else { step >>= 1; inc >>= 1; }
    } else if (exp && decreasing) step = (int32_t)((int64_t)step * v->level / 0x8000);
    if ((step_val | (shift << 2)) != all_bits) { if (inc < 1) inc = 1; } else return;
    v->env_counter += inc; if (!(v->env_counter & 0x8000)) return; v->env_counter = 0;
    v->level += step;
    if (!decreasing) { if (v->level > 0x7FFF) v->level = 0x7FFF; if (v->level < -0x8000) v->level = -0x8000; }
    else if (v->level < 0) v->level = 0;
}
static void envelope(Voice *v)
{
    uint16_t a1 = v->adsr1, a2 = v->adsr2;
    switch (v->phase) {
    case ATTACK: env_step(v, a1 >> 15, 0, (a1 >> 10) & 31, (a1 >> 8) & 3, 0x7f); if (v->level >= 0x7FFF) { v->level = 0x7FFF; v->phase = DECAY; v->env_counter = 0; } break;
    case DECAY: env_step(v, 1, 1, ((a1 >> 4) & 15), 0, 0x1f << 2); if (v->level <= ((a1 & 15) + 1) * 0x800) { v->phase = SUSTAIN; v->env_counter = 0; } break;
    case SUSTAIN: env_step(v, a2 >> 15, (a2 >> 14) & 1, (a2 >> 8) & 31, (a2 >> 6) & 3, 0x7f); break;
    case RELEASE: env_step(v, (a2 >> 5) & 1, 1, a2 & 31, 0, 0x1f << 2); if (v->level <= 0) { v->level = 0; v->phase = OFF; v->on = 0; } break;
    }
}
static void voice_sample(Voice *v, int32_t *l, int32_t *r)
{
    if (!v->on) { *l = *r = 0; return; }
    uint32_t idx = v->counter >> 12;
    while (idx >= 28) { block_flags_after(v); if (!v->on) { *l = *r = 0; return; } decode_block(v); v->counter -= 28 << 12; idx = v->counter >> 12; v->first = 0; }
    /* the 4 most recent samples: [idx-3..idx], from this block and the history */
    int16_t s[4];
    for (int k = 0; k < 4; k++) { int i = (int)idx - 3 + k; s[k] = i >= 0 ? v->buf[i] : v->hist[4 + i]; }
    int g = (v->counter >> 4) & 0xFF;
    int32_t out = ((gauss[0x0FF - g] * s[0]) >> 15) + ((gauss[0x1FF - g] * s[1]) >> 15) + ((gauss[0x100 + g] * s[2]) >> 15) + ((gauss[g] * s[3]) >> 15);
    envelope(v);
    out = (int32_t)(((int64_t)out * v->level) >> 15);
    *l = (int32_t)(((int64_t)out * v->vol_l) >> 15); *r = (int32_t)(((int64_t)out * v->vol_r) >> 15);
    uint32_t step = v->pitch > 0x3FFF ? 0x4000 : v->pitch;
    uint32_t before = v->counter >> 12; v->counter += step; uint32_t after = v->counter >> 12;
    if (after >= 28 && before < 28) { for (int k = 0; k < 4; k++) v->hist[k] = v->buf[24 + k]; }   /* the block's last 4, for the next one */
}

/* ---- reverb (22050 Hz) ---- */
static int16_t *rbuf; static int rlen, rcur; static int32_t R[32]; static int rev_on;
static int32_t evol;
static void reverb_init(int preset, int depth)
{
    rev_on = preset > 0; rlen = presets[preset].size / 2; if (rlen < 8) rlen = 8;
    rbuf = calloc((size_t)rlen, 2); rcur = 0;
    for (int k = 0; k < 32; k++) R[k] = presets[preset].r[k];
    evol = depth * 0x7FFF / 127;
}
static int16_t sat(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
static int ridx(int32_t reg_units_8bytes, int minus) { int i = rcur + reg_units_8bytes * 4 - minus; i %= rlen; if (i < 0) i += rlen; return i; }
static int32_t vs(int k) { return (int16_t)R[k]; }
static void reverb_step(int32_t lin, int32_t rin, int32_t *lout, int32_t *rout)
{
    enum { dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2,
           dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN };
    #define M(k, minus) rbuf[ridx(R[k], minus)]
    #define MUL(a, b) ((int32_t)(((int64_t)(a) * (b)) >> 15))
    int32_t Lin = MUL(vs(vLIN), lin), Rin = MUL(vs(vRIN), rin);
    int32_t iir = vs(vIIR), wall = vs(vWALL);
    M(mLSAME, 0) = sat(MUL(Lin + MUL(M(dLSAME, 0), wall) - M(mLSAME, 1), iir) + M(mLSAME, 1));
    M(mRSAME, 0) = sat(MUL(Rin + MUL(M(dRSAME, 0), wall) - M(mRSAME, 1), iir) + M(mRSAME, 1));
    M(mLDIFF, 0) = sat(MUL(Lin + MUL(M(dRDIFF, 0), wall) - M(mLDIFF, 1), iir) + M(mLDIFF, 1));
    M(mRDIFF, 0) = sat(MUL(Rin + MUL(M(dLDIFF, 0), wall) - M(mRDIFF, 1), iir) + M(mRDIFF, 1));
    int32_t L = MUL(vs(vCOMB1), M(mLCOMB1, 0)) + MUL(vs(vCOMB2), M(mLCOMB2, 0)) + MUL(vs(vCOMB3), M(mLCOMB3, 0)) + MUL(vs(vCOMB4), M(mLCOMB4, 0));
    int32_t Rr = MUL(vs(vCOMB1), M(mRCOMB1, 0)) + MUL(vs(vCOMB2), M(mRCOMB2, 0)) + MUL(vs(vCOMB3), M(mRCOMB3, 0)) + MUL(vs(vCOMB4), M(mRCOMB4, 0));
    int32_t a = vs(vAPF1), b = vs(vAPF2);
    int dA1 = R[dAPF1], dA2 = R[dAPF2];
    int32_t t;
    t = rbuf[ridx(R[mLAPF1] - dA1, 0)]; L = L - MUL(a, t); rbuf[ridx(R[mLAPF1], 0)] = sat(L); L = MUL(L, a) + t;
    t = rbuf[ridx(R[mRAPF1] - dA1, 0)]; Rr = Rr - MUL(a, t); rbuf[ridx(R[mRAPF1], 0)] = sat(Rr); Rr = MUL(Rr, a) + t;
    t = rbuf[ridx(R[mLAPF2] - dA2, 0)]; L = L - MUL(b, t); rbuf[ridx(R[mLAPF2], 0)] = sat(L); L = MUL(L, b) + t;
    t = rbuf[ridx(R[mRAPF2] - dA2, 0)]; Rr = Rr - MUL(b, t); rbuf[ridx(R[mRAPF2], 0)] = sat(Rr); Rr = MUL(Rr, b) + t;
    *lout = MUL(sat(L), evol); *rout = MUL(sat(Rr), evol);
    rcur = (rcur + 1) % rlen;
    #undef M
    #undef MUL
}

/* ---- the sequencer (libsnd) ---- */
typedef struct { int prog, vol, expr, pan, bend; } Chan;
static Chan chans[16];
static uint32_t note_pitch(const Tone *t, int key, int bend)
{
    double semis = (key - t->center) + t->shift / 128.0;
    if (bend) semis += bend > 0 ? bend / 8192.0 * t->pbmax : bend / 8192.0 * t->pbmin;
    double p = 4096.0 * pow(2.0, semis / 12.0);
    if (p > 0x3FFF) p = 0x3FFF; if (p < 1) p = 1; return (uint32_t)(p + 0.5);
}
static void voice_volume(Voice *v)
{
    const Tone *t = v->tone; const Chan *c = &chans[v->chan & 15]; const Prog *g = &progs[c->prog & 127];
    int pv = g->mvol ? g->mvol : 127;                    /* Capcom's banks leave a program's volume at 0: not applied */
    double vol = (v->prior / 127.0) * (t->vol / 127.0) * (pv / 127.0) * (c->vol / 127.0) * (c->expr / 127.0) * (vab_mvol / 127.0);
    int pan = (int)t->pan + (int)g->mpan + c->pan - 128; if (pan < 0) pan = 0; if (pan > 127) pan = 127;
    double l = pan <= 64 ? 1.0 : (127 - pan) / 63.0, r = pan >= 64 ? 1.0 : pan / 64.0;
    v->vol_l = (int32_t)(vol * l * 0x3FFF); v->vol_r = (int32_t)(vol * r * 0x3FFF);
}
static int verbose; static double tscale = 1.0;       /* -t: the tempo, scaled */ static long notes_total, notes_unmatched, prog_used[128];
static void note_on(int ch, int key, int vel)
{
    const Chan *c = &chans[ch]; const Prog *g = &progs[c->prog & 127];
    notes_total++; prog_used[c->prog & 127]++;
    { int any = 0; for (int k = 0; k < g->ntones; k++) if (key >= g->t[k].min && key <= g->t[k].max && tone_ok(&g->t[k])) any = 1;
      if (!any) { notes_unmatched++; if (verbose > 1) fprintf(stderr, "  note ch %d prog %d key %d: no tone\n", ch, c->prog, key); } }
    for (int k = 0; k < g->ntones; k++) {
        const Tone *t = &g->t[k]; if (key < t->min || key > t->max || !tone_ok(t)) continue;
        int best = -1; long oldest = 1L << 62;
        for (int i = 0; i < 24; i++) if (!voices[i].on) { best = i; break; }
        if (best < 0) for (int i = 0; i < 24; i++) if (voices[i].phase == RELEASE && voices[i].age < oldest) { oldest = voices[i].age; best = i; }
        if (best < 0) for (int i = 0; i < 24; i++) if (voices[i].age < oldest) { oldest = voices[i].age; best = i; }
        Voice *v = &voices[best]; memset(v, 0, sizeof *v);
        v->on = 1; v->chan = ch; v->key = key; v->prior = vel; v->age = ++age_counter; v->tone = t;
        v->start = v->addr = v->loop = gian ? (uint32_t)t->addr : vag_off[t->vag]; v->counter = 0; v->first = 1; v->reverb = (t->mode & 4) != 0;
        v->adsr1 = t->adsr1; v->adsr2 = t->adsr2; v->phase = ATTACK; v->level = 0;
        v->pitch = note_pitch(t, key, c->bend); voice_volume(v);
        decode_block(v);
    }
}
static void note_off(int ch, int key)
{
    for (int i = 0; i < 24; i++) if (voices[i].on && voices[i].chan == ch && voices[i].key == key && voices[i].phase != RELEASE) { voices[i].phase = RELEASE; voices[i].env_counter = 0; }
}

static const uint8_t *seq, *seq_end, *pos, *loop_pos; static int loop_count, running; static double tick_seconds; static int ppqn;
static int nrpn; static int capcom, loop_delta; static double now_s; static int loops_seen;
static void loop_mark(int end)                          /* -v: where the loop starts and ends (seconds), for the import */
{
    if (verbose && loops_seen < 4) { fprintf(stderr, "loop %s %.4f\n", end ? "end" : "start", now_s); loops_seen++; }
}
static int capcom_event(void)                           /* Capcom's: one event, then its delta in ticks; -1 at the end */
{
    for (;;) {
        if (pos + 1 >= seq_end) return -1;
        uint8_t s0 = *pos++, st = s0 | 0x80; int ch = st & 15;
        switch (st & 0xF0) {
        case 0xA0: return *pos++;                       /* a wait */
        case 0x90: { int k = *pos++, vel = *pos++; if (vel) note_on(ch, k, vel); else note_off(ch, k); break; }
        case 0x80: { int k = *pos++; pos++; note_off(ch, k); break; }
        case 0xB0: { int cc = *pos++, val = *pos++;
            if (cc == 7) chans[ch].vol = val; else if (cc == 11) chans[ch].expr = val; else if (cc == 10) chans[ch].pan = val;
            else if (cc == 99) { nrpn = val;
                if (val == 20) { loop_pos = pos; loop_delta = s0 & 0x80; loop_mark(0); }
                else if (val == 30 && loop_pos && loop_count != 0) { loop_mark(1); if (loop_count > 0) loop_count--; pos = loop_pos;
                    if (loop_delta) return *pos++; continue; } }
            else if (cc == 6 && nrpn == 20) loop_count = val == 127 || val == 0 ? -1 : val;
            if (cc == 7 || cc == 11 || cc == 10) for (int i = 0; i < 24; i++) if (voices[i].on && voices[i].chan == ch) voice_volume(&voices[i]);
            break; }
        case 0xC0: chans[ch].prog = *pos++; break;
        case 0xD0: pos++; break;
        case 0xE0: { int lo = *pos++, hi = *pos++; chans[ch].bend = ((hi << 7) | lo) - 8192; if (chans[ch].bend > 8191) chans[ch].bend = 8191;
            for (int i = 0; i < 24; i++) if (voices[i].on && voices[i].chan == ch) voices[i].pitch = note_pitch(voices[i].tone, voices[i].key, chans[ch].bend); break; }
        default:
            if (st == 0xFF) { int type = *pos++;
                if (type == 0x2F) return -1;
                if (type == 0x51) { uint32_t t = (uint32_t)pos[0] << 16 | pos[1] << 8 | pos[2]; if (t) tick_seconds = t / 1e6 / ppqn; pos += 3; }
                break; }
            return -1;
        }
        return s0 & 0x80 ? *pos++ : 0;
    }
}
static uint32_t varlen(void) { uint32_t v = 0; for (int k = 0; k < 4 && pos < seq_end; k++) { uint8_t b = *pos++; v = v << 7 | (b & 0x7F); if (!(b & 0x80)) break; } return v; }
static int seq_event(void)                              /* one event; 0 at the end */
{
    if (pos >= seq_end) return 0;
    uint8_t st = *pos; if (st & 0x80) { running = st; pos++; } else st = (uint8_t)running;
    int ch = st & 15;
    switch (st & 0xF0) {
    case 0x90: { int k = *pos++, vel = *pos++; if (vel) note_on(ch, k, vel); else note_off(ch, k); break; }
    case 0x80: { int k = *pos++; pos++; note_off(ch, k); break; }
    case 0xA0: pos += 2; break;
    case 0xB0: { int cc = *pos++, val = *pos++;
        if (cc == 7) chans[ch].vol = val; else if (cc == 11) chans[ch].expr = val; else if (cc == 10) chans[ch].pan = val;
        else if (cc == 99) { nrpn = val; if (val == 20) { loop_pos = pos; loop_mark(0); } else if (val == 30 && loop_pos) { loop_mark(1);
            if (loop_count != 0) { if (loop_count > 0) loop_count--; pos = loop_pos; } } }
        else if (cc == 6 && nrpn == 20) loop_count = val == 127 || val == 0 ? -1 : val;
        if (cc == 7 || cc == 11 || cc == 10) for (int i = 0; i < 24; i++) if (voices[i].on && voices[i].chan == ch) voice_volume(&voices[i]);
        break; }
    case 0xC0: chans[ch].prog = *pos++; break;
    case 0xD0: pos++; break;
    case 0xE0: { int lo = *pos++, hi = *pos++; chans[ch].bend = ((hi << 7) | lo) - 8192;
        for (int i = 0; i < 24; i++) if (voices[i].on && voices[i].chan == ch) voices[i].pitch = note_pitch(voices[i].tone, voices[i].key, chans[ch].bend); break; }
    case 0xF0:
        if (st == 0xFF) { int type = *pos++; uint32_t len = varlen();
            if (type == 0x51 && len == 3) { uint32_t t = (uint32_t)pos[0] << 16 | pos[1] << 8 | pos[2]; tick_seconds = t / 1e6 / ppqn; }
            if (type == 0x2F) { pos = seq_end; return 0; }
            pos += len; }
        else { uint32_t len = varlen(); pos += len; }
        break;
    }
    return 1;
}

static void put16(FILE *f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, uint32_t v) { put16(f, (int)(v & 0xFFFF)); put16(f, (int)(v >> 16)); }

static int dump_samples(int argc, char **argv)
{
    for (int k = 5; k < argc; k++) if (!strcmp(argv[k], "-b") && k + 1 < argc) bank_base = (uint32_t)strtoul(argv[++k], NULL, 16);
    long hlen; uint8_t *h = load(argv[2], &hlen); vb = load(argv[3], &vb_len); load_vab(h, hlen);
    static uint8_t done[1 << 16];
    for (int p = 0; p < 128; p++) for (int k = 0; k < progs[p].ntones; k++) {
        const Tone *t = &progs[p].t[k]; if (!tone_ok(t)) continue;
        uint32_t a = gian ? (uint32_t)t->addr : vag_off[t->vag]; if ((a >> 4) < sizeof done && done[a >> 4]) continue;
        if ((a >> 4) < sizeof done) done[a >> 4] = 1;
        char path[1024]; snprintf(path, sizeof path, "%s/%x.raw", argv[4], a);
        FILE *o = fopen(path, "wb"); if (!o) { fprintf(stderr, "psx_seq: cannot write %s\n", path); return 2; }
        Voice v; memset(&v, 0, sizeof v); v.addr = a; long n = 0; int looped = 0;
        for (int blocks = 0; v.addr + 16 <= (uint32_t)vb_len && blocks < 65536; blocks++) {
            uint8_t fl = vb[v.addr + 1]; decode_block(&v);
            for (int j = 0; j < 28; j++) { fputc(v.buf[j] & 255, o); fputc((v.buf[j] >> 8) & 255, o); }
            n += 28; v.addr += 16;
            if (fl & 1) { looped = (fl & 2) != 0; break; }
        }
        fclose(o); printf("%x %ld %d %d %d %d %d\n", a, n, looped, p, k, t->center, t->shift);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "--samples")) return dump_samples(argc, argv);
    if (argc < 5) { fprintf(stderr, "usage: psx_seq bank.vh bank.vb sequence out.wav [-n seq] [-s seconds] [-r reverb 0-9] [-d depth] [-i]\n"); return 2; }
    int which = 0, preset = 0, depth = 0, info = 0; double seconds = 0;
    for (int k = 5; k < argc; k++) {
        if (!strcmp(argv[k], "-n") && k + 1 < argc) which = atoi(argv[++k]);
        else if (!strcmp(argv[k], "-s") && k + 1 < argc) seconds = atof(argv[++k]);
        else if (!strcmp(argv[k], "-r") && k + 1 < argc) preset = atoi(argv[++k]);
        else if (!strcmp(argv[k], "-d") && k + 1 < argc) depth = atoi(argv[++k]);
        else if (!strcmp(argv[k], "-i")) info = 1;
        else if (!strcmp(argv[k], "-v")) verbose++;
        else if (!strcmp(argv[k], "-t") && k + 1 < argc) { tscale = atof(argv[++k]); if (tscale <= 0) tscale = 1; }
        else if (!strcmp(argv[k], "-b") && k + 1 < argc) bank_base = (uint32_t)strtoul(argv[++k], NULL, 16);
    }
    if (preset < 0 || preset > 9) preset = 0;
    long hlen, slen; uint8_t *h = load(argv[1], &hlen); vb = load(argv[2], &vb_len); uint8_t *s = load(argv[3], &slen);
    load_vab(h, hlen);
    if (slen >= 10 && s[0] == 0 && s[1] == 1) {          /* Capcom's (Dino Crisis) */
        capcom = 1; ppqn = s[2] | s[3] << 8; uint32_t t0 = le32(s + 4); if (!ppqn) ppqn = 48; if (!t0) t0 = 500000;
        if (info) { printf("Capcom sequence: %d ticks a beat, %.1f bpm\n", ppqn, 60e6 / t0); return 0; }
        seq = s + 8; seq_end = s + slen; pos = seq; tick_seconds = t0 / 1e6 / ppqn;
        goto play;
    }
    if (slen < 15 || memcmp(s, "pQES", 4)) { fprintf(stderr, "psx_seq: not a sequence (pQES)\n"); return 2; }
    /* SEQ (version 1): one sequence after a 15-byte header; SEP (version 0): sequences, each with its id, resolution,
     * tempo, rhythm and data size */
    uint32_t version = (uint32_t)s[4] << 24 | s[5] << 16 | s[6] << 8 | s[7];
    const uint8_t *found = NULL, *found_end = NULL; uint32_t tempo = 500000; int n = 0;
    if (version != 0) {
        ppqn = s[8] << 8 | s[9]; tempo = (uint32_t)s[10] << 16 | s[11] << 8 | s[12]; found = s + 15; found_end = s + slen;
        if (info) printf("SEQ: 1 sequence, %d ticks a beat, %.1f bpm\n", ppqn, 60e6 / tempo);
    } else {
        const uint8_t *p = s + 6;
        while (p + 13 <= s + slen) {
            int id = p[0] << 8 | p[1]; int res = p[2] << 8 | p[3]; uint32_t tm = (uint32_t)p[4] << 16 | p[5] << 8 | p[6];
            uint32_t size = (uint32_t)p[9] << 24 | p[10] << 16 | p[11] << 8 | p[12];
            if (!res || p + 13 + size > s + slen + 64) break;
            if (info) printf("SEP sequence %d: id %d, %d ticks a beat, %.1f bpm, %u bytes\n", n, id, res, 60e6 / (tm ? tm : 1), size);
            if (n == which) { ppqn = res; tempo = tm; found = p + 13; found_end = p + 13 + size; if (found_end > s + slen) found_end = s + slen; }
            p += 13 + size; n++;
        }
        if (info) printf("%d sequences\n", n);
    }
    if (info) return 0;
    if (!found || !ppqn) { fprintf(stderr, "psx_seq: no sequence %d\n", which); return 2; }
    seq = found; seq_end = found_end; pos = seq; tick_seconds = tempo / 1e6 / ppqn;
play:
    for (int c = 0; c < 16; c++) { chans[c].vol = 127; chans[c].expr = 127; chans[c].pan = 64; chans[c].prog = c; }
    reverb_init(preset, depth);
    if (seconds <= 0) seconds = 600;
    long total = (long)(seconds * 44100);
    FILE *o = fopen(argv[4], "wb"); if (!o) { fprintf(stderr, "psx_seq: cannot write %s\n", argv[4]); return 2; }
    fwrite("RIFF", 1, 4, o); put32(o, 36 + (uint32_t)total * 4); fwrite("WAVEfmt ", 1, 8, o); put32(o, 16); put16(o, 1); put16(o, 2);
    put32(o, 44100); put32(o, 44100 * 4); put16(o, 4); put16(o, 16); fwrite("data", 1, 4, o); put32(o, (uint32_t)total * 4);
    double next = 0, now = 0; int ended = 0; long silent = 0; long written = 0;
    if (!capcom) { uint32_t d = varlen(); next = d * tick_seconds / tscale; }
    int32_t rl = 0, rr = 0, rin_l = 0, rin_r = 0;
    for (long i = 0; i < total; i++) {
        now = i / 44100.0; now_s = now;
        while (!ended && now >= next) {
            if (capcom) { int d = capcom_event(); if (d < 0) { ended = 1; break; } next += d * tick_seconds / tscale; continue; }
            if (!seq_event()) { ended = 1; break; } uint32_t d = varlen(); next += d * tick_seconds / tscale; }
        int32_t L = 0, Rr = 0, el = 0, er = 0, any = 0;
        for (int k = 0; k < 24; k++) { int32_t l, r; voice_sample(&voices[k], &l, &r); L += l; Rr += r; if (voices[k].on && voices[k].reverb) { el += l; er += r; } if (voices[k].on) any = 1; }
        if (rev_on) {
            if (i & 1) { reverb_step((rin_l + el) / 2, (rin_r + er) / 2, &rl, &rr); }
            else { rin_l = el; rin_r = er; }
            L += rl; Rr += rr;
        }
        put16(o, sat(L)); put16(o, sat(Rr)); written++;
        if (ended && !any) { if (++silent > 44100 * 4) break; } else silent = 0;   /* after the end: the reverb's tail, then stop */
    }
    /* fix the sizes if it stopped early */
    fseek(o, 4, SEEK_SET); put32(o, 36 + (uint32_t)written * 4); fseek(o, 40, SEEK_SET); put32(o, (uint32_t)written * 4);
    fclose(o);
    fprintf(stderr, "psx_seq: %.1f s%s; %ld notes, %ld with no tone\n", written / 44100.0, ended ? " (the sequence ended)" : "", notes_total, notes_unmatched);
    if (verbose) {
        for (int p = 0; p < 128; p++) if (prog_used[p] || progs[p].ntones) {
            fprintf(stderr, "  prog %3d: %ld notes, %d tones:", p, prog_used[p], progs[p].ntones);
            for (int k = 0; k < progs[p].ntones; k++) fprintf(stderr, " [%d-%d c%d vag%d m%x]", progs[p].t[k].min, progs[p].t[k].max, progs[p].t[k].center, progs[p].t[k].vag, progs[p].t[k].mode);
            fprintf(stderr, "\n"); }
    }
    return 0;
}
