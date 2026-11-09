#include "render/wide_frame.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace bs::native::render {

namespace {
// a faixa de largura inteira (ver widen): sem textura, todo vertice numa das bordas laterais (x <= 0 ou x >= largura, as
// duas presentes), cor e 1/w uniformes. Cobre os fades de tela cheia e as barras (de cinema) de largura inteira.
bool full_width_fill(const CommandList& cl, const Draw& d) {
    if (d.state.texture || d.vertex_count < 3) return false;
    const float   W = float(cl.frame.width);
    const Vertex& f = cl.vertices[d.first_vertex];
    bool          left = false, right = false;
    for (uint32_t k = 0; k < d.vertex_count; ++k) {
        const Vertex& v = cl.vertices[d.first_vertex + k];
        if (!(v.x <= 0 || v.x >= W)) return false;
        if (v.color != f.color || v.offset != f.offset || v.inv_w != f.inv_w) return false;
        left |= v.x <= 0, right |= v.x >= W;
    }
    return left && right;
}
} // namespace

uint32_t widen(CommandList& cl, uint32_t wide_width, const std::vector<uint8_t>& anchors) {
    const uint32_t off = wide_offset(cl.frame.width, wide_width);
    if (!off) return 0;
    const float W = float(cl.frame.width), dx = float(off);
    // os fades antes do deslocamento (os vertices de um desenho nao sao compartilhados com outro)
    std::vector<uint8_t> fill(cl.vertices.size(), 0);
    uint32_t             filled = 0;
    for (const Draw& d : cl.draws)
        if (full_width_fill(cl, d)) {
            ++filled;
            for (uint32_t k = 0; k < d.vertex_count; ++k) fill[d.first_vertex + k] = 1;
        }
    // N9.5 fase 3B: os desenhos ancorados a esquerda nao andam (2 = nao desloca; uma faixa de largura inteira continua
    // faixa)
    const bool anchored = anchors.size() == cl.draws.size();
    if (anchored)
        for (size_t i = 0; i < cl.draws.size(); ++i)
            if (anchors[i] == kAnchorLeft)
                for (uint32_t k = 0; k < cl.draws[i].vertex_count; ++k)
                    if (!fill[cl.draws[i].first_vertex + k]) fill[cl.draws[i].first_vertex + k] = 2;
    for (size_t i = 0; i < cl.vertices.size(); ++i) {  // os volumes modificadores usam o mesmo vetor
        Vertex& v = cl.vertices[i];
        v.x += fill[i] == 1 ? (v.x >= W ? 2 * dx : 0.f) : (fill[i] == 2 ? 0.f : dx);
    }
    for (size_t i = 0; i < cl.draws.size(); ++i) {
        Draw& d = cl.draws[i];
        if (d.state.clip != Clip::None && !(anchored && anchors[i] == kAnchorLeft))
            d.state.clip_x0 += int32_t(off), d.state.clip_x1 += int32_t(off);
    }
    cl.frame.width = wide_width;
    return filled;
}

void crop_center(const uint32_t* src, uint32_t src_width, uint32_t height, uint32_t* dst, uint32_t width) {
    const uint32_t off = wide_offset(width, src_width);
    for (uint32_t y = 0; y < height; ++y)
        std::memcpy(dst + size_t(y) * width, src + size_t(y) * src_width + off, size_t(width) * sizeof(uint32_t));
}

AreaRect fit_area(int w, int h, uint32_t fw, uint32_t fh) {
    if (w <= 0 || h <= 0 || !fw || !fh) return {0, 0, float(fw), float(fh)};
    // em inteiros (a razao em float perde um pixel numa janela exatamente na proporcao)
    const uint64_t W = uint64_t(w), H = uint64_t(h);
    uint64_t       aw = W, ah = W * fh / fw;
    if (ah > H) ah = H, aw = H * fw / fh;
    return {float((W - aw) / 2), float((H - ah) / 2), float(aw), float(ah)};
}

AreaRect center_part(const AreaRect& outer, uint32_t inner_w, uint32_t outer_w) {
    if (!outer_w || inner_w >= outer_w) return outer;
    const float iw = std::floor(outer.w * float(inner_w) / float(outer_w));
    return {outer.x + std::floor((outer.w - iw) / 2), outer.y, iw, outer.h};
}

} // namespace bs::native::render
