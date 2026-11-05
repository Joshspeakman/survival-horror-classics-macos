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
    int4 tg;             // local viewer, specular on (D3DRS_SPECULARENABLE), unused x2
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
fragment float4 fs_main(VOut in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                        texture2d<float> t0 [[texture(0)]], sampler s0 [[sampler(0)]],
                        texture2d<float> t1 [[texture(1)]], sampler s1 [[sampler(1)]])
{
    float4 diffuse = in.diffuse, current = diffuse;
    float2 uv0 = in.uv;
    if ((u.a0.z & 0xffff0000) == 0x20000) uv0 = (u.a0.w & 0x100) ? in.tcproj.xy / in.tcproj.z : in.tcproj.xy;   // TCI_CAMERASPACEPOSITION
    float4 texel = u.misc.z ? t0.sample(s0, uv0) : float4(1);
    if (u.xrgb.x) texel.a = 1;
    if (u.c0.x != 1) {
        float4 c = op(u.c0.x, arg(u.c0.y, diffuse, current, texel, u.tfactor), arg(u.c0.z, diffuse, current, texel, u.tfactor), diffuse, texel, u.tfactor, current);
        float4 a = u.c0.w == 1 ? current : op(u.c0.w, arg(u.a0.x, diffuse, current, texel, u.tfactor), arg(u.a0.y, diffuse, current, texel, u.tfactor), diffuse, texel, u.tfactor, current);
        current = float4(c.rgb, a.a);
        if (u.c1.x != 1) {
            float4 texel1 = u.misc.w ? t1.sample(s1, in.uv1) : float4(1);
            if (u.xrgb.y) texel1.a = 1;
            float4 c1 = op(u.c1.x, arg(u.c1.y, diffuse, current, texel1, u.tfactor), arg(u.c1.z, diffuse, current, texel1, u.tfactor), diffuse, texel1, u.tfactor, current);
            float4 a1 = u.c1.w == 1 ? current : op(u.c1.w, arg(u.a1.x, diffuse, current, texel1, u.tfactor), arg(u.a1.y, diffuse, current, texel1, u.tfactor), diffuse, texel1, u.tfactor, current);
            current = float4(c1.rgb, a1.a);
        }
    }
    if (kAlphaTest && u.at.z != 0) {                     // alpha test (D3DCMP_*)
        float a = current.a, r = u.at.x; int f = int(u.at.y); bool pass;
        switch (f) { case 1: pass = false; break; case 2: pass = a < r; break; case 3: pass = a == r; break; case 4: pass = a <= r; break;
                     case 5: pass = a > r; break; case 6: pass = a != r; break; case 7: pass = a >= r; break; default: pass = true; }
        if (!pass) discard_fragment();
    }
    if (u.tg.y) current.rgb = saturate(current.rgb + in.spec);
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
fragment float4 fs_present(POut in [[stage_in]], texture2d<float> src [[texture(0)]], texture1d<float> lut [[texture(1)]], sampler s [[sampler(0)]])
{
    float3 c = src.sample(s, in.uv).rgb;
    uint3 i = uint3(saturate(c) * 255.0 + 0.5);
    return float4(lut.read(i.r).r, lut.read(i.g).g, lut.read(i.b).b, 1);
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
