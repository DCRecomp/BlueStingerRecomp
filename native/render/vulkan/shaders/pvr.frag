#version 450
#extension GL_GOOGLE_include_directive : require
// N7b: o fragmento do PVR2 com as regras do rasterizador de referencia (runtime/pvr/pvr_render.cpp):
// amostragem manual (texelFetch) com wrap/clamp/flip, ponto ou bilinear e meio texel, instrucoes de sombreamento,
// cor de deslocamento, use alpha, ignore texture alpha, color clamp, punch-through e recorte "fora".
// Modos (flags bits 18-17): 0 opaco (escreve a cor quantizada), 1 punch-through (descarta abaixo do PT_ALPHA_REF),
// 2 translucido na ordem de submissao (mistura do hardware), 3 autosort (so com AUTOSORT: o fragmento vai para a
// lista do pixel; a mistura em ordem de 1/w e feita por resolve.comp).
//
// MODO FIEL (F_EXACT, a resolucao original): cobertura, profundidade e atributos com as CONTAS do software, a partir
// dos dados de cada triangulo (frame_prep: tri_data) e nao da interpolacao da GPU: funcoes de aresta no ponto de
// amostra com a regra de cima/esquerda, pesos l = w * |1/area| (1/area calculado na CPU, como o software), 1/w,
// pesos na amostra dos atributos (HALF_OFFSET bit 1), correcao de perspectiva com divisao corretamente arredondada
// e as mesmas somas na mesma ordem (exact.glsl: divisao exata, c / 255 exato, o ambiente FP do software quando o
// dispositivo o oferece). `precise` impede fusao e reassociacao. Os candidatos sao o retangulo envolvente do
// triangulo (frame_prep, o mesmo laco do software): a cobertura e decidida aqui, nao pelo raster da GPU. Sem
// F_EXACT (as resolucoes maiores do N7c), os atributos vem da interpolacao da GPU.
#ifdef AUTOSORT
// segundo subpasso: a profundidade das opacas e punch-through chega como anexo de entrada; o >= forcado do autosort e
// feito aqui com o 1/w do software (no modo fiel), antes de o fragmento entrar na lista
layout(input_attachment_index = 0, set = 1, binding = 3) uniform subpassInput depthIn;
#endif
layout(location = 0) noperspective in float vZ;
layout(location = 1) noperspective in vec2  vUvZ;
layout(location = 2) noperspective in vec4  vColZ;
layout(location = 3) noperspective in vec3  vOffZ;
layout(location = 4) flat in vec4 fCol;
layout(location = 5) flat in vec3 fOff;
layout(location = 6) flat in uint fSeq;
layout(location = 7) flat in uint fTri;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform Batch {
    vec2  size;
    float depthScale;
    uint  flags;
    ivec4 clip;
    uvec2 texSize;
    uint  ptRef;
    uint  blend;
    uint  clampMin, clampMax;
    float depthBias;   // subtraido de 1/w antes da escala (quadros com 1/w negativo)
    float invScale;    // 1 / escala da resolucao interna: pixel do alvo -> coordenadas do jogo (1 no modo fiel)
#ifdef WATER
    // N9.7 (so a variante -DWATER, desenhos de agua do catalogo): qualidade (1 aprimorada, 2 alta), a fase (o numero do
    // render do Renderer, modulo 240000), o horizonte e o fundo da superficie em pixels do alvo
    vec4  water;
#endif
} pc;

const uint F_TEXTURED = 1u, F_GOURAUD = 2u, F_OFFSET = 4u, F_USE_ALPHA = 8u, F_IGNORE_TEX_ALPHA = 16u;
const uint F_CLAMP = 32u, F_CLIP_OUTSIDE = 64u, F_CLAMP_U = 4096u, F_CLAMP_V = 8192u, F_FLIP_U = 16384u;
const uint F_FLIP_V = 32768u, F_TEXEL_HALF = 65536u, F_ATTR_PLUS = 524288u, F_ATTR_MINUS = 1048576u;
const uint F_EXACT = 2097152u, F_SAMPLE_CORNER = 4194304u, F_ATTR_CORNER = 8388608u, F_RAW_DEPTH = 16777216u;
const uint F_FOG = 33554432u;  // N8.7: fog por tabela (TSP bits 23-22 = 0)
const uint F_SHADOW = 67108864u;  // N8.7: PCW bit 7 (o passe das opacas grava o bit de sombra do pixel)
const uint F_SCENE  = 134217728u; // N9.6 fase 1: desenho da cena 3D (bit 1 do anexo de sombra; o AO so age nele)
const uint F_AO_SURF = 268435456u; // N9.6 fase 2b: translucida do catalogo (material_catalog.h): o AO do opaco no plano dela

#include "exact.glsl"

// dados de cada triangulo (frame_prep: kTriWords palavras): ax ay bx by cx cy | az bz cz | 1/area |
// ua va ub vb uc vc | cor a b c | deslocamento a b c | 2 de folga
layout(std430, set = 1, binding = 2) readonly buffer Tris { uint triW[]; };
const uint kTriWords = 24u;

// N8.7: os parametros do quadro (FOG_TABLE: 2 entradas de 16 bits por palavra; FOG_COL_RAM; FOG_DENSITY)
layout(std430, set = 1, binding = 4) readonly buffer FrameData { uint fogTable[64]; uint fogColor; uint fogDensity; } fd;
// N9.6 fase 2b: o fator do AO e o 1/w do opaco por pixel (ao.comp -DSURF); sem o AO no quadro, uma textura neutra
// (a amostra so acontece com F_AO_SURF). O binding 5 fica para a agua do N9.7.
layout(set = 1, binding = 6) uniform sampler2D aoSurfTex;
#ifdef WATER
// N9.7: a copia da cena opaca (depois do AO, da luz e dos volumes), feita entre os passes A e B nos quadros com agua
layout(set = 1, binding = 5) uniform sampler2D sceneColor;
#endif

#ifdef AUTOSORT
struct Frag {
    vec4  c;
    float z;
    uint  seq, blend, next;
};
layout(std430, set = 1, binding = 0) buffer Frags { Frag frags[]; };
layout(std430, set = 1, binding = 1) buffer Heads { uint count, capacity, overflow, width, maxList, pad0, pad1, pad2; uint head[]; };
#else
layout(location = 0) out vec4 outColor;
// N8.7: o bit de sombra do poligono visivel (o anexo R8_UINT do passe das opacas; sem anexo nas translucidas)
layout(location = 1) out uint outShadow;
#endif

vec4 argb(uint c) {  // c / 255 do decodificador do TA (RGBA aqui)
    return vec4(k255[(c >> 16) & 0xFFu], k255[(c >> 8) & 0xFFu], k255[c & 0xFFu], k255[c >> 24]);
}

int floormod(int v, int n) {  // sem o % de negativos (indefinido no GLSL)
    const int q = v >= 0 ? v / n : -((n - 1 - v) / n);
    return v - q * n;
}
// n e 8 << k no original; uma substituicao HD pode ter fator inteiro qualquer (texture_replacement.h)
int wrapc(int v, int n, bool cl, bool fl) {
    if (cl) return clamp(v, 0, n - 1);
    if (fl) {
        const int m = floormod(v, 2 * n);
        return m < n ? m : 2 * n - 1 - m;
    }
    return floormod(v, n);
}
vec4 texel(int x, int y) {  // RGBA, com o c / 255 do software
    const ivec2 n = ivec2(pc.texSize);
    const ivec2 c = ivec2(wrapc(x, n.x, (pc.flags & F_CLAMP_U) != 0u, (pc.flags & F_FLIP_U) != 0u),
                          wrapc(y, n.y, (pc.flags & F_CLAMP_V) != 0u, (pc.flags & F_FLIP_V) != 0u));
    const uvec4 t = min(uvec4(floor(texelFetch(tex, c, 0) * 255.0 + 0.5)), uvec4(255u));  // o byte guardado
    return vec4(k255[t.r], k255[t.g], k255[t.b], k255[t.a]);
}

float fw(uint i) { return uintBitsToFloat(triW[i]); }

bool topLeft(vec2 p, vec2 q, float area) {
    precise float dx = q.x - p.x, dy = q.y - p.y;
    if (area > 0.0) dx = -dx, dy = -dy;
    return (dy == 0.0 && dx < 0.0) || dy > 0.0;
}

// Raster::draw para um pixel: cobertura, z (1/w) e os atributos; false = o software nao cobre o pixel
bool exact(ivec2 pix, out float z, out vec4 col, out vec3 off, out vec2 uv) {
    const uint t = fTri * kTriWords;
    const vec2 a = vec2(fw(t), fw(t + 1u)), b = vec2(fw(t + 2u), fw(t + 3u)), c = vec2(fw(t + 4u), fw(t + 5u));
    const float az = fw(t + 6u), bz = fw(t + 7u), cz = fw(t + 8u), inv = fw(t + 9u);
    precise float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    const float fpuOff = (pc.flags & F_SAMPLE_CORNER) != 0u ? 0.0 : 0.5;
    const float tspOff = (pc.flags & F_ATTR_CORNER) != 0u ? 0.0 : 0.5;
    // o ponto de amostra nas coordenadas do jogo: com a resolucao interna maior, cada pixel do alvo amostra as
    // mesmas contas num ponto entre os pixels do jogo (x 1.0 exato na resolucao original)
    precise float px = (float(pix.x) + fpuOff) * pc.invScale, py = (float(pix.y) + fpuOff) * pc.invScale;
    precise float w0 = (b.x - px) * (c.y - py) - (c.x - px) * (b.y - py);
    precise float w1 = (c.x - px) * (a.y - py) - (a.x - px) * (c.y - py);
    precise float w2 = (a.x - px) * (b.y - py) - (b.x - px) * (a.y - py);
    if (area < 0.0) w0 = -w0, w1 = -w1, w2 = -w2;
    if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) return false;
    if ((w0 == 0.0 && !topLeft(b, c, area)) || (w1 == 0.0 && !topLeft(c, a, area)) || (w2 == 0.0 && !topLeft(a, b, area)))
        return false;
    precise float ai = abs(inv);
    precise float l0 = w0 * ai, l1 = w1 * ai, l2 = w2 * ai;
    precise float zz = l0 * az + l1 * bz + l2 * cz;
    precise float m0 = l0, m1 = l1, m2 = l2;
    if (tspOff != fpuOff) {
        precise float qx = (float(pix.x) + tspOff) * pc.invScale, qy = (float(pix.y) + tspOff) * pc.invScale;
        m0 = ((b.x - qx) * (c.y - qy) - (c.x - qx) * (b.y - qy)) * inv;
        m1 = ((c.x - qx) * (a.y - qy) - (a.x - qx) * (c.y - qy)) * inv;
        m2 = 1.0 - m0 - m1;
    }
    precise float zt = m0 * az + m1 * bz + m2 * cz;
    precise float p0 = crdiv(m0 * az, zt), p1 = crdiv(m1 * bz, zt), p2 = crdiv(m2 * cz, zt);
    const vec4 ca = argb(triW[t + 16u]), cb = argb(triW[t + 17u]), cc = argb(triW[t + 18u]);
    const vec4 oa = argb(triW[t + 19u]), ob = argb(triW[t + 20u]), oc = argb(triW[t + 21u]);
    if ((pc.flags & F_GOURAUD) != 0u) {
        precise vec4 g = p0 * ca + p1 * cb + p2 * cc;
        precise vec3 h = p0 * oa.rgb + p1 * ob.rgb + p2 * oc.rgb;
        col = g, off = h;
    } else {
        col = cc, off = oc.rgb;  // sombreamento plano: o ultimo vertice
    }
    precise float u = p0 * fw(t + 10u) + p1 * fw(t + 12u) + p2 * fw(t + 14u);
    precise float v = p0 * fw(t + 11u) + p1 * fw(t + 13u) + p2 * fw(t + 15u);
    uv = vec2(u, v);
    z  = zz;
    return true;
}

// N8.7: o alfa da fog por tabela para um 1/w, com as contas do rasterizador em software (soft_raster.cpp, fogAlpha):
// z = densidade x 1/w em [1, 256); 16 entradas por oitava; os dois valores da entrada interpolados pela fracao
float fogAlpha(float invW) {
    const int     ex  = int(fd.fogDensity << 24u) >> 24;
    precise float den = float((fd.fogDensity >> 8u) & 0xFFu) / 128.0 * ldexp(1.0, ex);
    precise float zz  = min(max(den * invW, 1.0), 255.9999);
    int           e;
    frexp(zz, e);
    e -= 1;
    precise float m   = ldexp(zz * 16.0, -e) - 16.0;
    const float   fl  = floor(m);
    const int     idx = clamp(int(fl) + e * 16, 0, 127);
    precise float f0  = m - fl;
    const uint    ent = (fd.fogTable[uint(idx) >> 1u] >> ((uint(idx) & 1u) * 16u)) & 0xFFFFu;
    const float   hi = k255[ent >> 8u], lo = k255[ent & 0xFFu];
    precise float r  = hi + (lo - hi) * f0;
    return r;
}

#ifdef WATER
// N9.7: o movimento da textura do mar (o mesmo do prototipo do Codex, que o fazia nas UVs dos vertices na CPU): tres
// ondas de direcoes, frequencias e velocidades diferentes sobre a UV ORIGINAL, aqui por pixel (no modo fiel a UV vem dos
// dados do triangulo, entao o deslocamento so pode ser aplicado depois deles). A fase e o numero do render.
vec2 waterUv(vec2 uv) {
    const float high = step(1.5, pc.water.x);
    const float strength = mix(0.009, 0.015, high);
    const float phase = pc.water.y * mix(0.029, 0.037, high);
    const float u = uv.x, v = uv.y;
    return uv + vec2(sin(v * 0.73 + phase) * strength + sin((u + v) * 0.19 - phase * 0.61) * strength * 0.45 +
                         sin(u * 0.31 - v * 0.47 + phase * 0.23) * strength * 0.25,
                     sin(u * 0.57 - phase * 0.83) * strength * 0.72 + sin((u - v) * 0.23 + phase * 0.37) * strength * 0.32 +
                         sin(u * 0.17 + v * 0.41 + phase * 0.19) * strength * 0.22);
}

// N9.7: o material da agua (do prototipo do Codex). Tres trens de onda dao a normal por pixel; a cena opaca e amostrada
// na posicao atual (refracao) e espelhada em torno do horizonte da malha (reflexo em espaco de tela); fora da imagem, um
// tom de ceu e maresia. O tom original continua como base.
vec3 shadeWater(vec3 base, ivec2 pix) {
    const float high = step(1.5, pc.water.x);
    const vec2  suv = (vec2(pix) + 0.5) / pc.size;
    const float horizon = clamp(pc.water.z / pc.size.y, 0.0, 1.0);
    const float bottom = max(pc.water.w / pc.size.y, horizon + 1.0 / pc.size.y);
    const float depth = clamp((suv.y - horizon) / (bottom - horizon), 0.0, 1.0);
    const float t = pc.water.y / 60.0;
    const vec2 q = vec2(suv.x * 31.0, depth * 13.0);

    const float a = q.x + q.y * 0.72 + t * 1.15;
    const float b = q.x * -0.63 + q.y * 1.38 - t * 0.81;
    const float c = q.x * 1.71 - q.y * 0.44 + t * 0.47;
    const float wave = sin(a) * 0.48 + sin(b) * 0.32 + sin(c) * 0.20;
    const float dx = cos(a) * 0.48 - cos(b) * 0.20 + cos(c) * 0.34;
    const float dy = cos(a) * 0.35 + cos(b) * 0.44 - cos(c) * 0.09;
    const vec3 normal = normalize(vec3(-dx * mix(0.22, 0.34, high), -dy * mix(0.18, 0.29, high), 1.0));
    const vec2 distort = normal.xy * mix(2.2, 4.0, high) / pc.size;

    const vec2 refractUv = clamp(suv + distort * (0.55 + depth * 0.45), vec2(0.001), vec2(0.999));
    const float reflectedY = horizon - (suv.y - horizon) * mix(0.52, 0.62, high);
    const vec2 rawReflectUv = vec2(suv.x - distort.x * 1.8, reflectedY + distort.y * 2.2);
    const vec2 reflectUv = clamp(rawReflectUv, vec2(0.001), vec2(0.999));
    const vec3 refracted = texture(sceneColor, refractUv).rgb;
    const vec3 reflectedOnScreen = texture(sceneColor, reflectUv).rgb;
    const float onScreen = step(0.0, rawReflectUv.x) * step(rawReflectUv.x, 1.0) *
                           step(0.0, rawReflectUv.y) * step(rawReflectUv.y, 1.0);
    const vec3 reflectedFallback = mix(vec3(0.16, 0.31, 0.43), vec3(0.43, 0.64, 0.78), 1.0 - depth);
    const vec3 reflected = mix(reflectedFallback, reflectedOnScreen, onScreen);

    // perto do horizonte a vista e rasante e o reflexo domina; perto da camera, a refracao e o tom original
    const float fresnel = mix(0.14, 0.34, high) * (1.0 - smoothstep(0.0, 0.88, depth)) + mix(0.05, 0.08, high);
    vec3 color = mix(base, refracted, mix(0.08, 0.14, high));
    color = mix(color, reflected, clamp(fresnel, 0.0, 0.48));
    color *= vec3(0.94, 1.01, 1.09);  // absorcao leve: preserva o azul e reduz um pouco o vermelho
    color += vec3(0.0, 0.012, 0.032) * (0.35 + 0.65 * depth);

    const vec3 lightDir = normalize(vec3(-0.38, -0.48, 0.80));
    const float specular = pow(max(dot(normal, lightDir), 0.0), mix(34.0, 58.0, high));
    const float crest = smoothstep(0.72, 1.0, wave) * (0.035 + 0.055 * high) * (0.35 + 0.65 * (1.0 - depth));
    color += vec3(0.78, 0.91, 1.0) * (specular * mix(0.32, 0.55, high) + crest);
    return clamp(color, 0.0, 1.0);
}
#endif

void main() {
    const ivec2 p = ivec2(gl_FragCoord.xy);
    float z;
    vec4  col;
    vec3  off;
    vec2  uv;
    if ((pc.flags & F_EXACT) != 0u) {
        if (!exact(p, z, col, off, uv)) discard;
    } else {
        // HALF_OFFSET bit 1 diferente do bit 0: as entradas sem perspectiva sao lineares na tela, a derivada fina as
        // leva meio pixel (antes de qualquer descarte)
        float zv = vZ;
        vec2  uvz = vUvZ;
        vec4  colz = vColZ;
        vec3  offz = vOffZ;
        if ((pc.flags & (F_ATTR_PLUS | F_ATTR_MINUS)) != 0u) {
            const float d = (pc.flags & F_ATTR_PLUS) != 0u ? 0.5 : -0.5;
            zv += (dFdxFine(vZ) + dFdyFine(vZ)) * d;
            uvz += (dFdxFine(vUvZ) + dFdyFine(vUvZ)) * d;
            colz += (dFdxFine(vColZ) + dFdyFine(vColZ)) * d;
            offz += (dFdxFine(vOffZ) + dFdyFine(vOffZ)) * d;
        }
        z   = vZ;
        col = (pc.flags & F_GOURAUD) != 0u ? colz / zv : fCol;
        off = (pc.flags & F_GOURAUD) != 0u ? offz / zv : fOff;
        uv  = uvz / zv;
    }
#ifdef WATER
    if (pc.water.x < 4.5) uv = waterUv(uv);  // 5 e 6: as mascaras do diagnostico sem o movimento (a pegada original)
#endif
    // recorte "fora": o pixel do jogo que contem o centro do pixel do alvo (o proprio pixel na resolucao original)
    const ivec2 g = pc.invScale == 1.0 ? p : ivec2(floor((vec2(p) + 0.5) * pc.invScale));
    if ((pc.flags & F_CLIP_OUTSIDE) != 0u && g.x >= pc.clip.x && g.x <= pc.clip.z && g.y >= pc.clip.y && g.y <= pc.clip.w)
        discard;
    if ((pc.flags & F_USE_ALPHA) == 0u) col.a = 1.0;
    precise vec4 o = col;
    if ((pc.flags & F_TEXTURED) != 0u) {
        const float   half_ = (pc.flags & F_TEXEL_HALF) != 0u ? 0.5 : 0.0;
        precise float fu = uv.x * float(pc.texSize.x) - half_, fv = uv.y * float(pc.texSize.y) - half_;
        precise vec4  t;
        if (((pc.flags >> 10) & 3u) == 0u) {
            t = texel(int(floor(fu + 0.5)), int(floor(fv + 0.5)));
        } else {
            const int     ix = int(floor(fu)), iy = int(floor(fv));
            precise float ax = fu - float(ix), ay = fv - float(iy);
            const vec4    c00 = texel(ix, iy), c10 = texel(ix + 1, iy), c01 = texel(ix, iy + 1), c11 = texel(ix + 1, iy + 1);
            t = (c00 * (1.0 - ax) + c10 * ax) * (1.0 - ay) + (c01 * (1.0 - ax) + c11 * ax) * ay;
        }
        if ((pc.flags & F_IGNORE_TEX_ALPHA) != 0u) t.a = 1.0;
        const uint env = (pc.flags >> 8) & 3u;
        if (env == 0u) o = t;                                                      // decal
        else if (env == 1u) o = vec4(t.rgb * col.rgb, t.a);                        // modulate
        else if (env == 2u) o = vec4(t.rgb * t.a + col.rgb * (1.0 - t.a), col.a);  // decal alpha
        else o = vec4(t.rgb * col.rgb, t.a * col.a);                               // modulate alpha
        if ((pc.flags & F_OFFSET) != 0u) o.rgb += off;
    }
#ifdef WATER
    o.rgb = shadeWater(o.rgb, p);
#endif
    if ((pc.flags & F_CLAMP) != 0u) o = min(max(o, argb(pc.clampMin)), argb(pc.clampMax));
    if ((pc.flags & F_FOG) != 0u) {  // N8.7: fog por tabela, sobre a cor final (o alfa nao muda)
        precise float f  = fogAlpha(z);
        const vec4    fc = argb(fd.fogColor | 0xFF000000u);
        precise vec3  g  = o.rgb + (fc.rgb - o.rgb) * f;
        o.rgb            = g;
    }
#ifdef WATER
    // so o build de validacao (BS_AGUA_DIAGNOSTICO, vk_device): 3 e 5 = magenta, 4 e 6 = verde (5 e 6 sem o movimento da
    // textura), DEPOIS da fog (com a fog cheia no horizonte as duas cores sairiam iguais); o pixel diferente entre magenta e
    // verde e o dominio dos desenhos de agua (a mistura e monotona na cor de cima)
    if (pc.water.x > 2.5) o.rgb = (int(pc.water.x + 0.5) & 1) == 0 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 1.0);
#endif
    // N9.6 fase 2b: so a ate 1 % de 1/w do opaco por baixo (um decalque: os racks medidos ficam a 0,66 %); mais longe (uma
    // grade sobre um fosso, uma placa solta), a cor do jogo
    if ((pc.flags & F_AO_SURF) != 0u) {
        const vec4 s = texelFetch(aoSurfTex, p, 0);
        const uint u = (uint(s.g * 255.0 + 0.5) << 16) | (uint(s.b * 255.0 + 0.5) << 8) | uint(s.a * 255.0 + 0.5);
        if (u != 0u) {
            const float iw = exp2(float(u) / 16777215.0 * 32.0 - 32.0);
            if (abs(z - iw) <= 0.01 * iw) o.rgb *= s.r;
        }
    }
    const uint mode = (pc.flags >> 17) & 3u;
    if (mode == 1u && q8(o.a) < pc.ptRef) discard;
#ifdef AUTOSORT
    const float zd = (pc.flags & F_RAW_DEPTH) != 0u ? z
                     : ((pc.flags & F_EXACT) != 0u ? clamp((z - pc.depthBias) * pc.depthScale, 0.0, 1.0) : gl_FragCoord.z);
    if (!(zd >= subpassLoad(depthIn).r)) return;
    const uint i = atomicAdd(count, 1u);
    if (i >= capacity) {
        atomicAdd(overflow, 1u);
        return;
    }
    frags[i].c     = o;
    frags[i].z     = z;
    frags[i].seq   = fSeq;
    frags[i].blend = pc.blend;
    frags[i].next  = atomicExchange(head[uint(p.y) * width + uint(p.x)], i);
#else
    // modo fiel: a profundidade do software (1/w, deslocado e escalado como os vertices: a escala e potencia de 2)
    // com VK_EXT_depth_range_unrestricted, o proprio 1/w (as comparacoes do software, inclusive com 1/w negativo)
    gl_FragDepth = (pc.flags & F_RAW_DEPTH) != 0u ? z
                 : ((pc.flags & F_EXACT) != 0u ? clamp((z - pc.depthBias) * pc.depthScale, 0.0, 1.0) : gl_FragCoord.z);
    outColor     = mode == 2u ? o : quant(o);
    outShadow    = ((pc.flags & F_SHADOW) != 0u ? 1u : 0u) | ((pc.flags & F_SCENE) != 0u ? 2u : 0u);
#endif
}
