/* The game's own Direct3D 9 shaders (vs/ps 1.1-3.0 bytecode, as Fallout NV draws everything): each vertex/pixel
 * shader pair becomes one renderer program, translated once by MojoShader (third_party/mojoshader) to GLSL (the
 * OpenGL renderer: Linux) or to Metal (macOS: DSR_MSL_CREATE, see msl_create below).
 *
 * DSR_GLSL_CREATE carries the program's vertex attributes (name -> attribute slot, the slots d3d9.c's programmable
 * draws use) and both sources. MojoShader packs the constant registers a shader reads into one uniform array per type
 * (vec4, ivec4, bool), in the order of its uniform list; DSR_SHADER_CONST sends exactly those, before each draw. The
 * pixel shader is wrapped so that D3D9's alpha test, which runs after it, still applies. */
#include "d3d9_priv.h"
#include "mojoshader.h"
#include <pthread.h>

#define MAXPROG 4096
typedef struct Pack { int16_t index, count; } Pack;      /* registers [index, index + count) */
typedef struct Prog {
    Shader9 *vs, *ps; uint32_t id; int bad;
    Pack f[2][64], i[2][16], b[2][16]; int nf[2], ni[2], nb[2], tf[2], ti[2], tb[2];   /* per stage (0 vs, 1 ps): runs, totals */
} Prog;
static Prog progs[MAXPROG]; static int nprogs; static pthread_mutex_t plock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t next_id = 0x10000;                      /* above d3d9.c's effect program ids */

static int shlog(void) { static int on = -1; if (on < 0) on = getenv("D3D9_SHADERLOG") ? atoi(getenv("D3D9_SHADERLOG")) > 1 ? 2 : 1 : 0; return on; }   /* 2: the GLSL too */
#ifdef __APPLE__
#define SHADER_PROFILE MOJOSHADER_PROFILE_METAL      /* entry points d3dvs / d3dps: Metal does not allow "main" */
#else
#define SHADER_PROFILE MOJOSHADER_PROFILE_GLSL120
#endif
static const MOJOSHADER_parseData *parse(Shader9 *s)
{
#ifdef __APPLE__
    const char *entry = s->ps ? "d3dps" : "d3dvs";
#else
    const char *entry = "main";
#endif
    const MOJOSHADER_parseData *pd = MOJOSHADER_parse(SHADER_PROFILE, entry, G_MEM + s->mem, s->len, 0, 0, 0, 0, 0, 0, 0);
    if (pd->error_count) {
        static int n; if (n++ < 20) fprintf(stderr, "d3d9: %s shader %08x not translated: %s\n", s->ps ? "pixel" : "vertex", s->g, pd->errors[0].error);
        MOJOSHADER_freeParseData(pd); return 0;
    }
    return pd;
}
static void packs(Prog *p, int st, const MOJOSHADER_parseData *pd)
{
    for (int k = 0; k < pd->uniform_count; k++) {
        const MOJOSHADER_uniform *u = &pd->uniforms[k]; if (u->constant) continue;
        int n = u->array_count ? u->array_count : 1;
        Pack *runs = u->type == MOJOSHADER_UNIFORM_FLOAT ? p->f[st] : u->type == MOJOSHADER_UNIFORM_INT ? p->i[st] : p->b[st];
        int *nr = u->type == MOJOSHADER_UNIFORM_FLOAT ? &p->nf[st] : u->type == MOJOSHADER_UNIFORM_INT ? &p->ni[st] : &p->nb[st];
        int *tot = u->type == MOJOSHADER_UNIFORM_FLOAT ? &p->tf[st] : u->type == MOJOSHADER_UNIFORM_INT ? &p->ti[st] : &p->tb[st];
        int cap = u->type == MOJOSHADER_UNIFORM_FLOAT ? 64 : 16;
        if (*nr < cap) { runs[*nr].index = (int16_t)u->index; runs[*nr].count = (int16_t)n; (*nr)++; *tot += n; }
    }
}
static char *append(char *b, size_t *len, size_t *cap, const char *s, size_t n)
{
    if (*len + n + 1 > *cap) { *cap = (*len + n + 1) * 2; b = realloc(b, *cap); }
    memcpy(b + *len, s, n); *len += n; b[*len] = 0; return b;
}
/* the pixel shader with D3D9's alpha test after it (d3d_alphatest: reference, D3DCMP function, enabled) */
static char *wrap_ps(const char *src, size_t n, size_t *outlen)
{
    size_t len = 0, cap = n + 1024; char *o = malloc(cap); o[0] = 0;
    const char *m = strstr(src, "void main()"); if (!m) { o = append(o, &len, &cap, src, n); *outlen = len; return o; }
    o = append(o, &len, &cap, src, (size_t)(m - src));
    o = append(o, &len, &cap, "void d3d_main()", 15);
    const char *rest = m + 11; o = append(o, &len, &cap, rest, n - (size_t)(rest - src));
    const char *out = strstr(src, "gl_FragData[0]") ? "gl_FragData[0]" : "gl_FragColor";
    char tail[1024], nanpaint[256] = "";   /* D3D9_NANPAINT=nan|inf: such outputs painted magenta (debugging) */
    { const char *np = getenv("D3D9_NANPAINT");
      if (np) snprintf(nanpaint, sizeof nanpaint, strcmp(np, "inf") ? "\tif (any(notEqual(%s, %s))) %s = vec4(1.0, 0.0, 1.0, 1.0);\n"   /* GLSL 1.20: no isnan/isinf */
                                                                     : "\tif (any(greaterThan(abs(%s), vec4(3.0e38))) && %s == %s) %s = vec4(1.0, 0.0, 1.0, 1.0);\n", out, out, out, out); }
    int k = snprintf(tail, sizeof tail,
        "\nuniform vec4 d3d_alphatest;\n"
        "void main()\n{\n\td3d_main();\n"
        "\tif (d3d_alphatest.z != 0.0) {\n\t\tfloat a = floor(clamp(%s.a, 0.0, 1.0) * 255.0 + 0.5), r = d3d_alphatest.x; int f = int(d3d_alphatest.y); bool pass = true;\n"
        "\t\tif (f == 1) pass = false; else if (f == 2) pass = a < r; else if (f == 3) pass = a == r; else if (f == 4) pass = a <= r;\n"
        "\t\telse if (f == 5) pass = a > r; else if (f == 6) pass = a != r; else if (f == 7) pass = a >= r;\n"
        "\t\tif (!pass) discard;\n\t}\n%s}\n", out, nanpaint);
    o = append(o, &len, &cap, tail, (size_t)k); *outlen = len; return o;
}
/* the vertex shader with D3D9's pixel centres: D3D9 puts them on integer coordinates, GL half a pixel further, so
 * positions move by half a pixel (d3d_pixfix = 1/viewport width, -1/viewport height, before the render-target flip).
 * Games correct for D3D9 themselves (a 1x1 target's quad is shifted by half the target: without this, no pixel centre
 * is inside it at all, as FNV's eye-adaptation pass found) */
static char *wrap_vs(const char *src, size_t n, size_t *outlen)
{
    size_t len = 0, cap = n + 256; char *o = malloc(cap); o[0] = 0;
    const char *ver = strstr(src, "\n"), *flip = strstr(src, "\tgl_Position.y = gl_Position.y * vpFlip;");
    if (!ver || !flip) { o = append(o, &len, &cap, src, n); *outlen = len; return o; }
    o = append(o, &len, &cap, src, (size_t)(ver + 1 - src));
    /* invariant: the same position math gives the same bits in every program, as D3D drivers guarantee. FNV lights
     * characters in several passes that redraw them with depth test EQUAL; without it NVIDIA's compiler optimises each
     * program differently, a pass misses on scattered pixels, and they flicker (bright specks on Doc Mitchell, the player) */
    if (!getenv("D3D9_NO_INVARIANT")) o = append(o, &len, &cap, "invariant gl_Position;\n", 23);
    o = append(o, &len, &cap, "uniform vec4 d3d_pixfix;\n", 25);
    o = append(o, &len, &cap, ver + 1, (size_t)(flip - ver - 1));
    o = append(o, &len, &cap, "\tgl_Position.xy += d3d_pixfix.xy * gl_Position.w;\n", 50);
    o = append(o, &len, &cap, flip, n - (size_t)(flip - src));
    *outlen = len; return o;
}
static const char *FF_PS = "#version 120\nvoid main()\n{\n\tgl_FragColor = gl_Color;\n}\n";   /* a vertex shader without a pixel shader */

static int slot_of(int usage, int index)               /* d3d9.c sem_slot(): the attribute slots of programmable draws */
{
    switch (usage) {
    case MOJOSHADER_USAGE_POSITION: case MOJOSHADER_USAGE_POSITIONT: return index ? -1 : 0;
    case MOJOSHADER_USAGE_NORMAL: return index ? -1 : 1;
    case MOJOSHADER_USAGE_COLOR: return index < 2 ? 2 + index : -1;
    case MOJOSHADER_USAGE_TEXCOORD: return index < 8 ? 4 + index : -1;
    case MOJOSHADER_USAGE_TANGENT: return index ? -1 : 12;
    case MOJOSHADER_USAGE_BINORMAL: return index ? -1 : 13;
    case MOJOSHADER_USAGE_BLENDWEIGHT: return index ? -1 : 14;
    case MOJOSHADER_USAGE_BLENDINDICES: return index ? -1 : 15;
    default: return -1;
    }
}

#ifdef __APPLE__
/* ---- Metal (macOS) ----
 * MojoShader's Metal function takes its constants as one struct at buffer(0) (float4s, then int4s, then bools, packed
 * as packs() lists them) and its inputs as [[attribute(input register)]]. Each stage gets one more buffer(1): the
 * vertex stage D3D9's pixel centres and the fog (D3DExtra), the pixel stage the alpha test. A vertex shader without a
 * pixel shader returns the renderer's own VOut (shaders.metal), so the fixed-function texture stages (fs_main) finish
 * it, as Direct3D does. */
static const char *MSL_EXTRA =
    "struct D3DExtra { float4 pixfix; float4 fog; };\n"
    "struct VOut { float4 pos [[position]]; float4 diffuse; float2 uv; float2 uv1; float3 tcproj; float3 spec; float fogf; };\n";
static char *msl_insert(char *src, size_t *len, size_t at, const char *what)       /* a new string: what inserted at offset at */
{
    size_t n = strlen(what); char *o = malloc(*len + n + 1);
    memcpy(o, src, at); memcpy(o + at, what, n); memcpy(o + at + n, src + at, *len - at + 1);
    free(src); *len += n; return o;
}
/* the output member with this attribute (e.g. "[[position]]", "[[user(texcoord0)]]"): its name, or "" */
static void msl_member(const char *src, const char *attr, char *name, size_t cap)
{
    name[0] = 0;
    const char *st = strstr(src, "_Output\n{"); if (st) src = st;       /* MojoShader's output struct (not VOut, inserted above it) */
    const char *a = strstr(src, attr); if (!a) return;
    const char *e = a; while (e > src && e[-1] == ' ') e--;
    const char *b = e; while (b > src && (b[-1] == '_' || (b[-1] >= '0' && b[-1] <= '9') || ((b[-1] | 32) >= 'a' && (b[-1] | 32) <= 'z'))) b--;
    snprintf(name, cap, "%.*s", (int)(e - b), b);
}
/* one more parameter for the stage's entry point (after MojoShader's own, which may be none) */
static char *msl_param(char *src, size_t *len, const char *kind, const char *param)
{
    char *f = strstr(src, kind); if (!f) return src;
    char *open = strchr(f, '('), *close = open ? strstr(open, ") {") : 0; if (!close) return src;
    int any = 0; for (char *q = open + 1; q < close; q++) if (*q > ' ') any = 1;
    char buf[160]; snprintf(buf, sizeof buf, "%s    %s\n", any ? "," : "", param);
    size_t at = (size_t)(close - src); while (at > 0 && src[at - 1] == '\n') at--;
    return msl_insert(src, len, at, buf);
}
static char *msl_before_return(char *src, size_t *len, const char *code)
{
    char *r = strstr(src, "return output;"); if (!r) return src;
    while (r > src && r[-1] != '\n') r--;
    return msl_insert(src, len, (size_t)(r - src), code);
}
static char *msl_vs(const char *in, size_t n, int ff, size_t *outlen)
{
    size_t len = n; char *s = malloc(n + 1); memcpy(s, in, n); s[n] = 0;
    char *ns = strstr(s, "using namespace metal;\n");
    if (ns) s = msl_insert(s, &len, (size_t)(ns - s) + 23, MSL_EXTRA);
    s = msl_param(s, &len, "vertex ", "constant D3DExtra &d3d_x [[buffer(1)]]");
    char pos[64], c0[64], c1[64], t0[64], t1[64], fog[64];
    msl_member(s, "[[position]]", pos, sizeof pos); msl_member(s, "[[user(color0)]]", c0, sizeof c0); msl_member(s, "[[user(color1)]]", c1, sizeof c1);
    msl_member(s, "[[user(texcoord0)]]", t0, sizeof t0); msl_member(s, "[[user(texcoord1)]]", t1, sizeof t1); msl_member(s, "[[user(fog)]]", fog, sizeof fog);
    char code[2048]; int k = 0;
    if (pos[0]) k += snprintf(code + k, sizeof code - (size_t)k, "        output.%s.xy += output.%s.w * d3d_x.pixfix.xy;\n", pos, pos);
    if (!ff) { s = msl_before_return(s, &len, code); *outlen = len; return s; }
    /* the fixed-function stages finish it: VOut from the shader's outputs (colours clamped as D3D does), the fog factor
     * the shader wrote, else by depth (w) and the fog mode (d3d_x.fog: start, end, mode 1 exp 2 exp2 3 linear, density) */
    k += snprintf(code + k, sizeof code - (size_t)k,
        "        VOut vo_; vo_.pos = %s%s; vo_.diffuse = %s%s%s; vo_.uv = %s%s; vo_.uv1 = %s%s; vo_.tcproj = float3(0, 0, 1);\n"
        "        vo_.spec = %s%s%s;\n",
        pos[0] ? "output." : "float4(0)", pos, c0[0] ? "saturate(output." : "float4(1)", c0, c0[0] ? ")" : "",
        t0[0] ? "output." : "float2(0)", t0[0] ? strcat(t0, ".xy") : "", t1[0] ? "output." : "float2(0)", t1[0] ? strcat(t1, ".xy") : "",
        c1[0] ? "saturate(output." : "float3(0)", c1[0] ? strcat(c1, ".rgb") : "", c1[0] ? ")" : "");
    /* fog (d3d_x.pixfix.z on): table fog (pixfix.w) by depth and the table mode, as Direct3D ignores oFog then; else the
     * shader's oFog (none written: no fog) */
    k += snprintf(code + k, sizeof code - (size_t)k,
        "        { float fd_ = vo_.pos.w, fm_ = d3d_x.fog.z; float tf_ = fm_ == 3 ? clamp((d3d_x.fog.y - fd_) / (d3d_x.fog.y - d3d_x.fog.x), 0.0, 1.0)"
        " : fm_ == 1 ? exp(-fd_ * d3d_x.fog.w) : fm_ == 2 ? exp(-(fd_ * d3d_x.fog.w) * (fd_ * d3d_x.fog.w)) : 1.0;\n"
        "          vo_.fogf = d3d_x.pixfix.z == 0 ? 1.0 : d3d_x.pixfix.w != 0 ? tf_ : %s%s%s; }\n",
        fog[0] ? "saturate(output." : "1.0", fog, fog[0] ? ".x)" : "");
    k += snprintf(code + k, sizeof code - (size_t)k, "        return vo_;\n");
    s = msl_before_return(s, &len, code);
    { char *ro = strstr(s, "return output;"); if (ro) memset(ro, ' ', 14); }   /* ours returns VOut: MojoShader's own goes */
    char *v = strstr(s, "vertex d3dvs_Output d3dvs");
    if (v) { size_t at = (size_t)(v - s) + 7; char *t = malloc(len + 8); memcpy(t, s, at); memcpy(t + at, "VOut", 4);
             memcpy(t + at + 4, s + at + 12, len - at - 12 + 1); len = len - 12 + 4; free(s); s = t; }
    *outlen = len; return s;
}
static char *msl_ps(const char *in, size_t n, size_t *outlen)
{
    size_t len = n; char *s = malloc(n + 1); memcpy(s, in, n); s[n] = 0;
    s = msl_param(s, &len, "fragment ", "constant float4 &d3d_alphatest [[buffer(1)]]");
    char col[64]; msl_member(s, "[[color(0)]]", col, sizeof col);
    if (col[0]) {
        char code[1024];
        snprintf(code, sizeof code,
            "        if (d3d_alphatest.z != 0) { float a_ = floor(saturate(output.%s.a) * 255.0 + 0.5), r_ = d3d_alphatest.x; int f_ = int(d3d_alphatest.y); bool p_ = true;\n"
            "            if (f_ == 1) p_ = false; else if (f_ == 2) p_ = a_ < r_; else if (f_ == 3) p_ = a_ == r_; else if (f_ == 4) p_ = a_ <= r_;\n"
            "            else if (f_ == 5) p_ = a_ > r_; else if (f_ == 6) p_ = a_ != r_; else if (f_ == 7) p_ = a_ >= r_;\n"
            "            if (!p_) discard_fragment(); }\n", col);
        s = msl_before_return(s, &len, code);
    }
    *outlen = len; return s;
}
static void msl_create(Prog *p, const MOJOSHADER_parseData *v, const MOJOSHADER_parseData *f)
{
    size_t vl, pl = 0; char *vs = msl_vs(v->output, (size_t)v->output_len, !f, &vl), *ps = f ? msl_ps(f->output, (size_t)f->output_len, &pl) : 0;
    uint32_t na = 0, at[16][2], kinds = 0;
    for (int k = 0; k < v->attribute_count && na < 16; k++) {
        int sl = slot_of(v->attributes[k].usage, v->attributes[k].index); const char *nm = v->attributes[k].name;
        const char *d = nm; while (d && *d && !(*d >= '0' && *d <= '9')) d++;
        if (sl < 0 || !d || !*d) { if (shlog()) fprintf(stderr, "d3d9: vertex shader %08x: input usage %d/%d has no slot\n", p->vs->g, v->attributes[k].usage, v->attributes[k].index); continue; }
        at[na][0] = (uint32_t)atoi(d); at[na][1] = (uint32_t)sl; na++;
    }
    if (f) for (int k = 0; k < f->sampler_count; k++) {
        uint32_t idx = (uint32_t)f->samplers[k].index; if (idx >= 16) continue;
        uint32_t kind = f->samplers[k].type == MOJOSHADER_SAMPLER_CUBE ? 1 : f->samplers[k].type == MOJOSHADER_SAMPLER_VOLUME ? 2 : 0;
        kinds |= kind << (2 * idx);
    }
    uint32_t size = 20 + 8 * na + (((uint32_t)vl + 4) & ~3u) + (ps ? (((uint32_t)pl + 4) & ~3u) : 0);
    p->id = next_id++;
    uint32_t *c = cmd_begin(DSR_MSL_CREATE, size);
    c[0] = p->id; c[1] = na; c[2] = (uint32_t)vl; c[3] = ps ? (uint32_t)pl : 0; c[4] = kinds;
    memcpy(c + 5, at, 8 * (size_t)na);
    char *o = (char *)(c + 5 + 2 * na); memcpy(o, vs, vl); o[vl] = 0;
    if (ps) { o += (vl + 4) & ~3u; memcpy(o, ps, pl); o[pl] = 0; }
    cmd_end();
    if (shlog()) fprintf(stderr, "d3d9: program %u (Metal): vs %08x + %s %08x, %u inputs, constants vs %d/%d/%d ps %d/%d/%d\n", p->id, p->vs->g,
                         ps ? "ps" : "fixed-function stages", p->ps ? p->ps->g : 0, na, p->tf[0], p->ti[0], p->tb[0], p->tf[1], p->ti[1], p->tb[1]);
    if (shlog() > 1) fprintf(stderr, "d3d9: program %u vertex MSL:\n%s\nd3d9: program %u pixel MSL:\n%s\n", p->id, vs, p->id, ps ? ps : "(none)");
    free(vs); free(ps);
}
#endif
static void create(Prog *p)
{
    const MOJOSHADER_parseData *v = parse(p->vs), *f = p->ps ? parse(p->ps) : 0;
    if (!v || (p->ps && !f)) { p->bad = 1; if (v) MOJOSHADER_freeParseData(v); if (f) MOJOSHADER_freeParseData(f); return; }
    packs(p, 0, v); if (f) packs(p, 1, f);
#ifdef __APPLE__
    msl_create(p, v, f); MOJOSHADER_freeParseData(v); if (f) MOJOSHADER_freeParseData(f); return;
#endif
    size_t pslen; char *ps = f ? wrap_ps(f->output, (size_t)f->output_len, &pslen) : wrap_ps(FF_PS, strlen(FF_PS), &pslen);
    size_t vslen; char *vs = wrap_vs(v->output, (size_t)v->output_len, &vslen);
    int na = 0; struct { uint32_t slot; char name[28]; } at[16];
    for (int k = 0; k < v->attribute_count && na < 16; k++) {
        int sl = slot_of(v->attributes[k].usage, v->attributes[k].index);
        if (sl < 0 || !v->attributes[k].name) { if (shlog()) fprintf(stderr, "d3d9: vertex shader %08x: input usage %d/%d has no slot\n", p->vs->g, v->attributes[k].usage, v->attributes[k].index); continue; }
        at[na].slot = (uint32_t)sl; memset(at[na].name, 0, 28); snprintf(at[na].name, 28, "%s", v->attributes[k].name); na++;
        if (shlog()) fprintf(stderr, "d3d9: program %u input %s = usage %d/%d -> slot %d\n", next_id, v->attributes[k].name, v->attributes[k].usage, v->attributes[k].index, sl);
    }
    uint32_t vl = (uint32_t)vslen, pl = (uint32_t)pslen, size = 16 + 32 * (uint32_t)na + ((vl + 4) & ~3u) + ((pl + 4) & ~3u);
    p->id = next_id++;
    uint32_t *c = cmd_begin(DSR_GLSL_CREATE, size);
    c[0] = p->id; c[1] = (uint32_t)na; c[2] = vl; c[3] = pl;
    memcpy(c + 4, at, 32 * (size_t)na);
    char *s = (char *)(c + 4 + 8 * na); memcpy(s, vs, vl); s[vl] = 0;
    s += (vl + 4) & ~3u; memcpy(s, ps, pl); s[pl] = 0;
    cmd_end();
    if (shlog()) fprintf(stderr, "d3d9: program %u: vs %08x + ps %08x, %d attributes, constants vs %d/%d/%d ps %d/%d/%d\n", p->id, p->vs->g,
                         p->ps ? p->ps->g : 0, na, p->tf[0], p->ti[0], p->tb[0], p->tf[1], p->ti[1], p->tb[1]);
    if (shlog() > 1) fprintf(stderr, "d3d9: program %u vertex GLSL:\n%s\nd3d9: program %u pixel GLSL:\n%s\n", p->id, vs, p->id, ps);
    free(ps); free(vs); MOJOSHADER_freeParseData(v); if (f) MOJOSHADER_freeParseData(f);
}
/* the bound shaders' program, created on first use; 0 if they could not be translated */
uint32_t d9_shader_program(Dev9 *d)
{
    pthread_mutex_lock(&plock);
    Prog *p = 0;
    for (int k = 0; k < nprogs; k++) if (progs[k].vs == d->vs && progs[k].ps == d->ps) { p = &progs[k]; break; }
    if (!p && nprogs < MAXPROG) { p = &progs[nprogs++]; memset(p, 0, sizeof *p); p->vs = d->vs; p->ps = d->ps; create(p); }
    uint32_t id = p && !p->bad ? p->id : 0;
    pthread_mutex_unlock(&plock);
    return id;
}
/* a shader is released: forget its programs (the renderer keeps them; ids are not reused) */
void d9_shader_forget(Shader9 *s)
{
    pthread_mutex_lock(&plock);
    for (int k = 0; k < nprogs; ) if (progs[k].vs == s || progs[k].ps == s) progs[k] = progs[--nprogs]; else k++;
    pthread_mutex_unlock(&plock);
}
/* DSR_SHADER_CONST: u32 vs float4 count, ps float4 count, vs int4, ps int4, vs bool, ps bool; float4 vs[], ps[];
 * int4 vs[], ps[]; u32 vs[], ps[]; then float4 alpha test (reference 0-255, function, enabled, 0) */
void d9_shader_consts(Dev9 *d, uint32_t id)
{
    Prog *p = 0;
    pthread_mutex_lock(&plock);
    for (int k = 0; k < nprogs; k++) if (progs[k].id == id) { p = &progs[k]; break; }
    if (!p) { pthread_mutex_unlock(&plock); return; }
    Prog q = *p; pthread_mutex_unlock(&plock);
    uint32_t words = 6 + 4u * (uint32_t)(q.tf[0] + q.tf[1] + q.ti[0] + q.ti[1]) + (uint32_t)(q.tb[0] + q.tb[1]) + 4;
    uint32_t *c = cmd_begin(DSR_SHADER_CONST, 4 * words), *w = c + 6;
    c[0] = (uint32_t)q.tf[0]; c[1] = (uint32_t)q.tf[1]; c[2] = (uint32_t)q.ti[0]; c[3] = (uint32_t)q.ti[1]; c[4] = (uint32_t)q.tb[0]; c[5] = (uint32_t)q.tb[1];
    for (int st = 0; st < 2; st++) for (int r = 0; r < q.nf[st]; r++) {
        int max = st ? 224 : 256;
        for (int k = 0; k < q.f[st][r].count; k++) { int reg = q.f[st][r].index + k; if (reg < max) memcpy(w, st ? d->psc[reg] : d->vsc[reg], 16); else memset(w, 0, 16); w += 4; }
    }
    for (int st = 0; st < 2; st++) for (int r = 0; r < q.ni[st]; r++)
        for (int k = 0; k < q.i[st][r].count; k++) { int reg = q.i[st][r].index + k; if (reg < 16) memcpy(w, st ? d->psi[reg] : d->vsi[reg], 16); else memset(w, 0, 16); w += 4; }
    for (int st = 0; st < 2; st++) for (int r = 0; r < q.nb[st]; r++)
        for (int k = 0; k < q.b[st][r].count; k++) { int reg = q.b[st][r].index + k; *w++ = reg < 16 ? (st ? d->psb[reg] : d->vsb[reg]) : 0; }
    float at[4] = {(float)(d->rs[24] & 255), (float)d->rs[25], d->rs[15] ? 1.0f : 0.0f, 0}; memcpy(w, at, 16);
    cmd_end();
}
