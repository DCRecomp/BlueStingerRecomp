#version 450
// N7c: nitidez interna adaptativa ao contraste (cadeia de pos-processamento, render/post_shader.h). Um filtro de realce
// em cruz (centro e os quatro vizinhos) cujo peso negativo cai onde o contraste local ja e alto, para nao criar halos
// nem estourar bordas; formula conhecida de "contrast adaptive sharpening", escrita aqui de forma independente.
// p[0].x intensidade (0 a 1; 0 = a mesma imagem).
// N9.8 fase 8.4: p[3].w = 1, o modo da mascara do HUD (hudmask.comp): o alfa da entrada e a mascara (1 = a cena 3D fora
// do HUD); a nitidez vale na proporcao dela e o alfa passa adiante. p[3].w = 0: como antes (o alfa sai 1).
layout(set = 0, binding = 0) uniform sampler2D prev;
layout(push_constant) uniform Post {
    vec4 size;  // largura, altura, 1/largura, 1/altura
    vec4 p[4];
} pc;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
    const vec2 d = pc.size.zw;
    const vec4 c4 = texture(prev, vUv);
    const vec3 c  = c4.rgb;
    const vec3 n = texture(prev, vUv + vec2(0.0, -d.y)).rgb;
    const vec3 s = texture(prev, vUv + vec2(0.0, d.y)).rgb;
    const vec3 w = texture(prev, vUv + vec2(-d.x, 0.0)).rgb;
    const vec3 e = texture(prev, vUv + vec2(d.x, 0.0)).rgb;
    const vec3 mn = min(c, min(min(n, s), min(w, e)));
    const vec3 mx = max(c, max(max(n, s), max(w, e)));
    // quanto de realce a vizinhanca aguenta: perto de 0 onde ja ha muito contraste (ou perto do preto/branco)
    const vec3 room = clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-4)), 0.0, 1.0);
    const vec3 amt  = sqrt(room) * mix(0.0, -0.2, clamp(pc.p[0].x, 0.0, 1.0));  // peso de cada vizinho (negativo)
    const vec3 o    = (c + (n + s + w + e) * amt) / (1.0 + 4.0 * amt);
    const vec3 oc   = clamp(o, 0.0, 1.0);
    const bool mask = pc.p[3].w > 0.5;
    // as pontas exatas: na cena (1) a mesma conta de sem a mascara, no HUD (0) a entrada; entre elas, na borda suave, a mistura
    const vec3 r    = c4.a >= 1.0 ? oc : c4.a <= 0.0 ? c : mix(c, oc, c4.a);
    outColor        = mask ? vec4(r, c4.a) : vec4(oc, 1.0);
}
