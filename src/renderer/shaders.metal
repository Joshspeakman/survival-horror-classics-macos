// DSR fixed-function emulation: Direct3D 7-9 fixed function (vertex lighting, texture-coordinate generation, 2 texture
// stages, fog).
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 wvp;        // world*view*proj (D3D row-vector order; uploaded as-is => m * v)
    float4x4 wv;         // world*view
    float4x4 tex0;       // texture-0 transform
    float4 vp;           // viewport x, y, w, h (pixels)
    float4 fog;          // start, end, enabled, unused
    float4 fogcolor;
    float4 tfactor;
    int4 c0;             // stage 0: colorop, colorarg1, colorarg2, alphaop
    int4 a0;             // stage 0: alphaarg1, alphaarg2, texcoordindex, texturetransformflags
    int4 c1;             // stage 1: colorop, colorarg1, colorarg2, alphaop
    int4 a1;             // stage 1: alphaarg1, alphaarg2, unused, unused
    float4 at;           // alpha test: ref (0..1), func, enabled, unused
    int4 misc;           // pretransformed, has diffuse, has texture bound (stage0), has texture bound (stage1)
    int4 xrgb;           // stage 0/1 texture has no alpha channel
    float4x4 tex1;       // texture-1 transform
    int4 s1;             // stage 1: texcoordindex, texturetransformflags, unused, unused
    float4x4 nrm;        // normals to camera space: the inverse transpose of world*view
    float4 amb;          // D3DRS_AMBIENT
    float4 md, ma, ms, me;   // the material: diffuse, ambient, specular, emissive (after D3DRS_*MATERIALSOURCE)
    int4 lit;            // lighting on, lights, material sources (diffuse | ambient << 4 | emissive << 8: 0 material, 1 colour 1, 2 colour 2), normalize normals
    int4 tg;             // local viewer, specular on (D3DRS_SPECULARENABLE), PS2 lighting (the GTA games), unused
    float4 lod;          // stage 0/1 D3DTSS_MIPMAPLODBIAS (0 unless the game sets one), unused x2
    float4 lt[8 * 7];    // each light (camera space): diffuse, ambient, position (w type), direction, attenuation 0-2 + range, spot (cos theta/2, cos phi/2, falloff), specular
};

struct VIn {
    float4 pos [[attribute(0)]];     // xyz (w=1) or xyz+rhw
    float4 diffuse [[attribute(1)]]; // BGRA8 normalized (bound to a constant white buffer when absent)
    float2 uv [[attribute(2)]];
    float3 normal [[attribute(3)]];  // (0, 0, 0) when absent
    float4 specular [[attribute(4)]];// colour 2 (BGRA8)
    float2 uv1 [[attribute(5)]];     // texture coordinate set 1 ((0, 0) when absent)
};
struct VOut {
    float4 pos [[position]];
    float4 diffuse;
    float2 uv;                       // stage 0 coordinates (after its texture transform)
    float2 uv1;                      // stage 1 coordinates
    float3 tcproj;                   // projected texgen (camera-space position * tex0)
    float3 spec;                     // specular colour, added after the texture stages (D3DRS_SPECULARENABLE)
    float fogf;
};

/* A stage's texture coordinates: D3DTSS_TEXCOORDINDEX picks a set (low bits) or generates them (D3DTSS_TCI_* in the
 * upper bits: 1 camera-space normal, 2 camera-space position, 3 camera-space reflection vector), then
 * D3DTSS_TEXTURETRANSFORMFLAGS runs them through the stage's texture matrix. Passed-through 2-D coordinates enter it as
 * (u, v, 1, 0), so translation lives in the third row (how Dungeon Siege scrolls its river layers); generated ones as
 * (x, y, z, 1) (RenderWare's car reflections: the normal's x and y scaled by 0.5 and moved by 0.5). Projected
 * camera-space positions are divided per pixel instead (tcproj). */
static float2 texcoord(float2 uv0, float2 uv1, float4x4 m, int tci, int flags, float3 cam, float3 n, bool local)
{
    int count = flags & 0xff, gen = (tci >> 16) & 0xffff;
    float4 t;
    if (gen == 0) {
        float2 uv = (tci & 7) == 1 ? uv1 : uv0;
        if (count == 0) return uv;
        t = m * float4(uv, 1.0, 0.0);
    } else {
        float3 g = gen == 1 ? n : gen == 2 ? cam : reflect(local ? normalize(cam) : float3(0, 0, 1), n);
        if (count == 0) return g.xy;
        t = m * float4(g, 1.0);
    }
    if (flags & 0x100) { float q = count == 2 ? t.y : count == 3 ? t.z : t.w; return q != 0 ? t.xy / q : t.xy; }
    return t.xy;
}
/* Direct3D's vertex lighting (D3DRS_LIGHTING): emissive + ambient * (D3DRS_AMBIENT + each light's ambient) + diffuse *
 * each light's diffuse * N.L, attenuated by distance (point and spot lights) and the spot cone; alpha is the diffuse
 * colour's. Specular: each light's specular * (N.H)^power (H halfway to the viewer). Colours come from the material or
 * the vertex (D3DRS_COLORVERTEX + *MATERIALSOURCE). */
static float4 light_vertex(constant Uniforms &u, float3 p, float3 n, float4 c1, float4 c2, thread float3 &spec)
{
    int src = u.lit.z;
    float4 cs = ((src >> 12) & 15) == 1 ? c1 : ((src >> 12) & 15) == 2 ? c2 : u.ms;
    float3 view = u.tg.x ? (dot(p, p) > 0 ? -normalize(p) : float3(0, 0, -1)) : float3(0, 0, -1), sp = float3(0);
    float4 cd = (src & 15) == 1 ? c1 : (src & 15) == 2 ? c2 : u.md;
    float4 ca = ((src >> 4) & 15) == 1 ? c1 : ((src >> 4) & 15) == 2 ? c2 : u.ma;
    float4 ce = ((src >> 8) & 15) == 1 ? c1 : ((src >> 8) & 15) == 2 ? c2 : u.me;
    float3 amb = u.amb.rgb, dif = float3(0);
    for (int i = 0; i < u.lit.y; i++) {
        float4 ld = u.lt[i * 7], la = u.lt[i * 7 + 1], lp = u.lt[i * 7 + 2], dir = u.lt[i * 7 + 3], att = u.lt[i * 7 + 4], spot = u.lt[i * 7 + 5];
        int type = int(lp.w); float3 l; float a = 1.0;
        if (type == 3) l = -normalize(dir.xyz);                     // directional
        else {                                                      // point (1), spot (2)
            float3 v = lp.xyz - p; float d = length(v);
            if (d > att.w) continue;
            l = d > 0 ? v / d : float3(0, 0, 1);
            float k = att.x + att.y * d + att.z * d * d; a = k > 0 ? 1.0 / k : 1.0;
            if (type == 2) {
                float rho = dot(normalize(dir.xyz), -l);
                a *= rho > spot.x ? 1.0 : rho <= spot.y ? 0.0 : pow(saturate((rho - spot.y) / max(spot.x - spot.y, 1e-6)), spot.z);
            }
        }
        amb += a * la.rgb;
        float nl = dot(n, l);
        dif += a * max(nl, 0.0) * ld.rgb;
        if (u.tg.y && nl > 0 && u.ms.a > 0) sp += a * pow(max(dot(n, normalize(l + view)), 0.0), u.ms.a) * u.lt[i * 7 + 6].rgb;
    }
    spec = saturate(cs.rgb * sp);
    if (u.tg.z) {   /* the PS2's RenderWare (aap's SkyGfx describes it): the light is clamped before it takes the
                     * material's colour, so a lit car or ped never goes past its own colour; the ambient keeps its
                     * share of the material (surface ambient over diffuse) */
        float3 ka = select(float3(1), ca.rgb / max(cd.rgb, float3(1e-4)), cd.rgb > 1e-4);
        return float4(saturate(ce.rgb + saturate(amb * ka + dif) * cd.rgb), cd.a);
    }
    return float4(saturate(ce.rgb + ca.rgb * amb + cd.rgb * dif), cd.a);
}
vertex VOut vs_main(VIn in [[stage_in]], constant Uniforms &u [[buffer(1)]])
{
    VOut o;
    if (u.misc.x) {          // XYZRHW: already in screen space
        float w = in.pos.w != 0 ? 1.0 / in.pos.w : 1.0;
        float x = (in.pos.x + 0.5 - u.vp.x) / u.vp.z * 2.0 - 1.0;   // +0.5: D3D samples pixel centres at integers
        float y = 1.0 - (in.pos.y + 0.5 - u.vp.y) / u.vp.w * 2.0;
        o.pos = float4(x * w, y * w, in.pos.z * w, w);
        o.fogf = 1.0;
        o.tcproj = float3(0, 0, 1);
    } else {
        float4 p = float4(in.pos.xyz, 1.0);
        o.pos = u.wvp * p;
        o.pos.x += o.pos.w / u.vp.z;      // half-pixel: same rasterisation as D3D7
        o.pos.y -= o.pos.w / u.vp.w;
        float4 cam = u.wv * p;
        // fog (D3DRS_FOGTABLEMODE, else D3DRS_FOGVERTEXMODE): 1 exp, 2 exp2, 3 linear, by camera-space depth
        float fd = cam.z, fm = u.fog.z;
        o.fogf = fm == 3 ? clamp((u.fog.y - fd) / (u.fog.y - u.fog.x), 0.0, 1.0) : fm == 1 ? exp(-fd * u.fog.w) : fm == 2 ? exp(-(fd * u.fog.w) * (fd * u.fog.w)) : 1.0;
        o.tcproj = (u.tex0 * cam).xyz;
    }
    float3 cam3 = u.misc.x ? float3(0) : (u.wv * float4(in.pos.xyz, 1.0)).xyz;
    float3 n = u.misc.x ? float3(0) : (u.nrm * float4(in.normal, 0.0)).xyz;
    if (u.lit.w && dot(n, n) > 0) n = normalize(n);
    o.spec = u.tg.y ? in.specular.rgb : float3(0);   // unlit: the vertex's own specular colour (black when it has none)
    o.diffuse = u.lit.x ? light_vertex(u, cam3, n, in.diffuse, in.specular, o.spec) : in.diffuse;
    o.uv = texcoord(in.uv, in.uv1, u.tex0, u.a0.z, u.a0.w, cam3, n, u.tg.x != 0);
    o.uv1 = texcoord(in.uv, in.uv1, u.tex1, u.s1.x, u.s1.y, cam3, n, u.tg.x != 0);
    return o;
}

static float4 arg(int a, float4 diffuse, float4 current, float4 texel, float4 tfactor)
{
    float4 v;
    switch (a & 0xf) { case 0: v = diffuse; break; case 1: v = current; break; case 2: v = texel; break; case 3: v = tfactor; break; default: v = diffuse; }
    if (a & 0x10) v = 1.0 - v;            // D3DTA_COMPLEMENT
    if (a & 0x20) v = float4(v.a);        // D3DTA_ALPHAREPLICATE
    return v;
}
static float4 op(int o, float4 x, float4 y, float4 diffuse, float4 texel, float4 tf, float4 current)
{
    switch (o) {
    case 2: return x;                                   // SELECTARG1
    case 3: return y;                                   // SELECTARG2
    case 4: return x * y;                               // MODULATE
    case 5: return saturate(x * y * 2);                 // MODULATE2X
    case 6: return saturate(x * y * 4);                 // MODULATE4X
    case 7: return saturate(x + y);                     // ADD
    case 8: return saturate(x + y - 0.5);               // ADDSIGNED
    case 9: return saturate((x + y - 0.5) * 2);         // ADDSIGNED2X
    case 10: return saturate(x - y);                    // SUBTRACT
    case 11: return saturate(x + y - x * y);            // ADDSMOOTH
    case 12: return mix(y, x, diffuse.a);               // BLENDDIFFUSEALPHA
    case 13: return mix(y, x, texel.a);                 // BLENDTEXTUREALPHA
    case 14: return mix(y, x, tf.a);                    // BLENDFACTORALPHA
    case 15: return saturate(x + y * (1 - texel.a));    // BLENDTEXTUREALPHAPM
    case 16: return mix(y, x, current.a);               // BLENDCURRENTALPHA
    default: return x;
    }
}

/* Pipelines for draws without alpha testing are built with kAlphaTest false: a shader that can discard keeps the GPU
 * from removing hidden surfaces before shading (Apple GPUs' HSR) for every draw that uses it. */
constant bool kAlphaTest [[function_constant(0)]];
/* The texture stages' state as constants (renderer.m fs_key): each pipeline's shader keeps only what its stages do.
 * kC0/kA0/kC1/kA1: stage 0/1 colour and alpha: op | arg1 << 8 | arg2 << 16. kFl: 1 texture 0, 2 texture 1, 4/8 those
 * are XRGB (alpha 1), 0x10 specular added, 0x20 stage 0's coordinates the camera-space position, 0x40 projected,
 * 0x1000 alpha test with function (kFl >> 8) & 15. */
constant int kC0 [[function_constant(1)]];
constant int kA0 [[function_constant(2)]];
constant int kC1 [[function_constant(3)]];
constant int kA1 [[function_constant(4)]];
constant int kFl [[function_constant(5)]];
fragment float4 fs_main(VOut in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                        texture2d<float> t0 [[texture(0)]], sampler s0 [[sampler(0)]],
                        texture2d<float> t1 [[texture(1)]], sampler s1 [[sampler(1)]])
{
    float4 diffuse = in.diffuse, current = diffuse, tf = u.tfactor;
    float2 uv0 = in.uv;
    if (kFl & 0x20) uv0 = (kFl & 0x40) ? in.tcproj.xy / in.tcproj.z : in.tcproj.xy;   // TCI_CAMERASPACEPOSITION
    float4 texel = (kFl & 1) ? t0.sample(s0, uv0, bias(u.lod.x)) : float4(1);
    if (kFl & 4) texel.a = 1;
    const int c0 = kC0 & 0xff, c0a = (kC0 >> 8) & 0xff, c0b = (kC0 >> 16) & 0xff, a0 = kA0 & 0xff, a0a = (kA0 >> 8) & 0xff, a0b = (kA0 >> 16) & 0xff;
    const int c1 = kC1 & 0xff, c1a = (kC1 >> 8) & 0xff, c1b = (kC1 >> 16) & 0xff, a1 = kA1 & 0xff, a1a = (kA1 >> 8) & 0xff, a1b = (kA1 >> 16) & 0xff;
    if (c0 != 1) {
        float4 c = op(c0, arg(c0a, diffuse, current, texel, tf), arg(c0b, diffuse, current, texel, tf), diffuse, texel, tf, current);
        float4 a = a0 == 1 ? current : op(a0, arg(a0a, diffuse, current, texel, tf), arg(a0b, diffuse, current, texel, tf), diffuse, texel, tf, current);
        current = float4(c.rgb, a.a);
        if (c1 != 1) {
            float4 texel1 = (kFl & 2) ? t1.sample(s1, in.uv1, bias(u.lod.y)) : float4(1);
            if (kFl & 8) texel1.a = 1;
            float4 cc = op(c1, arg(c1a, diffuse, current, texel1, tf), arg(c1b, diffuse, current, texel1, tf), diffuse, texel1, tf, current);
            float4 aa = a1 == 1 ? current : op(a1, arg(a1a, diffuse, current, texel1, tf), arg(a1b, diffuse, current, texel1, tf), diffuse, texel1, tf, current);
            current = float4(cc.rgb, aa.a);
        }
    }
    if (kAlphaTest && (kFl & 0x1000)) {                  // alpha test (D3DCMP_*)
        float a = current.a, r = u.at.x; const int f = (kFl >> 8) & 15; bool pass;
        switch (f) { case 1: pass = false; break; case 2: pass = a < r; break; case 3: pass = a == r; break; case 4: pass = a <= r; break;
                     case 5: pass = a > r; break; case 6: pass = a != r; break; case 7: pass = a >= r; break; default: pass = true; }
        if (!pass) discard_fragment();
    }
    if (kFl & 0x10) current.rgb = saturate(current.rgb + in.spec);
    current.rgb = mix(u.fogcolor.rgb, current.rgb, in.fogf);
    return current;
}

// Blit / fill helper: textured or solid quad into a destination rectangle.
struct QOut { float4 pos [[position]]; float2 uv; };
struct QUni { float4 dst; float4 src; float4 color; int4 mode; };   // dst/src in normalized [0,1] rects; mode.x: 0 tex, 1 fill
vertex QOut vs_quad(uint vid [[vertex_id]], constant QUni &q [[buffer(0)]])
{
    float2 c = float2((vid & 1) ? 1.0 : 0.0, (vid & 2) ? 1.0 : 0.0);
    float2 d = mix(q.dst.xy, q.dst.zw, c);
    QOut o; o.pos = float4(d.x * 2 - 1, 1 - d.y * 2, 0, 1); o.uv = mix(q.src.xy, q.src.zw, c); return o;
}
fragment float4 fs_quad(QOut in [[stage_in]], constant QUni &q [[buffer(0)]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]])
{
    return q.mode.x ? q.color : t.sample(s, in.uv);
}
// Depth clear helper: writes a constant depth.
struct ZOut { float4 pos [[position]]; };
vertex ZOut vs_zclear(uint vid [[vertex_id]], constant float4 &z [[buffer(0)]])
{
    float2 c = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? -1.0 : 1.0); ZOut o; o.pos = float4(c, z.x, 1); return o;
}

// Present: back buffer -> drawable, through the game's gamma ramp (256-entry LUT per channel).
struct POut { float4 pos [[position]]; float2 uv; };
vertex POut vs_present(uint vid [[vertex_id]])
{
    float2 c = float2((vid << 1) & 2, vid & 2);   // fullscreen triangle
    POut o; o.pos = float4(c * 2 - 1, 0, 1); o.uv = float2(c.x, 1 - c.y); return o;
}
// Depth upload (DSR_DEPTH_UPLOAD): a depth buffer the game wrote with the CPU, from a float texture, pixel for pixel.
struct ZUOut { float depth [[depth(any)]]; };
fragment ZUOut fs_zupload(POut in [[stage_in]], texture2d<float> z [[texture(0)]])
{
    ZUOut o; o.depth = z.read(uint2(in.pos.xy)).r; return o;
}
constant float bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
fragment float4 fs_present(POut in [[stage_in]], texture2d<float> src [[texture(0)]], texture1d<float> lut [[texture(1)]], sampler s [[sampler(0)]],
                           constant uint4 &pm [[buffer(0)]])
{
    float3 c = src.sample(s, in.uv).rgb;
    if (pm.x) {   // a 16-bit screen (R5G6B5): each colour to 5, 6, 5 bits with a 4x4 ordered dither in the game's pixels
        uint2 t = uint2(in.uv * float2(pm.yz));
        float d = (bayer4[(t.y & 3) * 4 + (t.x & 3)] + 0.5) / 16.0 - 0.5;
        const float3 q = float3(31, 63, 31);
        c = clamp(floor(saturate(c) * q + 0.5 + d), 0.0, q) / q;
    }
    uint3 i = uint3(saturate(c) * 255.0 + 0.5);
    return float4(lut.read(i.r).r, lut.read(i.g).g, lut.read(i.b).b, 1);
}

// The PS2's radiosity (DSR_GLOW): g = (limit, strength, source texel w, h)
fragment float4 fs_glow_extract(POut in [[stage_in]], texture2d<float> src [[texture(0)]], sampler s [[sampler(0)]], constant float4 &g [[buffer(0)]])
{
    float2 d = g.zw;   // four bilinear taps: the average of the 4x4 block this quarter-size pixel covers
    float3 c = src.sample(s, in.uv + float2(-d.x, -d.y)).rgb + src.sample(s, in.uv + float2(d.x, -d.y)).rgb
             + src.sample(s, in.uv + float2(-d.x, d.y)).rgb + src.sample(s, in.uv + float2(d.x, d.y)).rgb;
    return float4(max(c * 0.25 - g.x, 0.0), 1);
}
constant float glow_w[5] = {0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216};
fragment float4 fs_glow_blur(POut in [[stage_in]], texture2d<float> src [[texture(0)]], sampler s [[sampler(0)]], constant float4 &g [[buffer(0)]])
{
    float3 c = src.sample(s, in.uv).rgb * glow_w[0];   // g.xy: one texel along the blur's direction
    for (int i = 1; i < 5; i++) c += (src.sample(s, in.uv + g.xy * i).rgb + src.sample(s, in.uv - g.xy * i).rgb) * glow_w[i];
    return float4(c, 1);
}
fragment float4 fs_glow_add(POut in [[stage_in]], texture2d<float> src [[texture(0)]], sampler s [[sampler(0)]], constant float4 &g [[buffer(0)]])
{
    return float4(src.sample(s, in.uv).rgb * g.y, 1);   // added (one, one) over the frame
}

// Frame-time graph: 240 columns (oldest left), y = 0..40 ms. g[0..239] game frame interval, g[240..479] time on screen.
fragment float4 fs_graph(POut in [[stage_in]], constant float *g [[buffer(0)]])
{
    float x = in.uv.x, y = (1.0 - in.uv.y) * 40.0;           // ms at this pixel row
    int i = clamp(int(x * 240.0), 0, 239);
    float px = 40.0 / 200.0;                                  // ~1 px line thickness in ms
    float4 c = float4(0, 0, 0, 0.55);
    if (abs(y - 8.333) < px * 0.6 || abs(y - 12.5) < px * 0.6 || abs(y - 16.667) < px * 0.6) c = float4(0.6, 0.6, 0.2, 0.8);
    float a = g[i], b = g[240 + i];
    if (b > 0 && abs(y - b) < px * 1.5) c = float4(0.2, 1.0, 0.3, 1);
    if (a > 0 && abs(y - a) < px * 1.2) c = float4(1, 1, 1, 1);
    if (a > 40.0 && y > 38.5) c = float4(1, 0.2, 0.2, 1);   // off-scale spike marker
    return c;
}
