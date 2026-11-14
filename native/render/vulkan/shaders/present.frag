#version 450
// N7c: apresentacao direta. flags bit 0: imagem do jogo (opaca, o alfa do alvo nao conta); senao, camada
// pre-multiplicada (menu e legendas), misturada pelo pipeline (One, OneMinusSrcAlpha).
// N9.8 fase 8.3 (A1): flags bit 1, a reducao por area na imagem do jogo. Ate 2:1 o bilinear de sempre ja e a media de 2x2
// (a mesma imagem de antes); acima, a area que o pixel cobre na imagem e a media de N x N amostras bilineares (cada uma a
// media de 2x2 texels), sem pular pixels.
layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform Quad {
    vec4 dst;
    vec4 src;
    uint flags;
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

const uint kMaxTaps = 8u;  // por eixo: ate 16:1

void main() {
    // texels da imagem por pixel da janela (constante no quadrilatero; fora do ramo para as derivadas valerem)
    const vec2 ts = vec2(textureSize(tex, 0));
    const vec2 fp = fwidth(vUv) * ts;
    vec4 c;
    if ((pc.flags & 2u) != 0u && max(fp.x, fp.y) > 2.0) {
        const uvec2 n    = uvec2(clamp(ceil(fp * 0.5), vec2(1.0), vec2(float(kMaxTaps))));
        const vec2  step = fp / vec2(n);           // em texels: cada amostra no meio de um bloco da area
        const vec2  o    = vUv * ts - fp * 0.5;    // o canto da area do pixel
        c                = vec4(0.0);
        for (uint y = 0u; y < n.y; ++y)
            for (uint x = 0u; x < n.x; ++x) c += textureLod(tex, (o + (vec2(x, y) + 0.5) * step) / ts, 0.0);
        c /= float(n.x * n.y);
    } else {
        c = texture(tex, vUv);
    }
    outColor = (pc.flags & 1u) != 0u ? vec4(c.rgb, 1.0) : c;
}
