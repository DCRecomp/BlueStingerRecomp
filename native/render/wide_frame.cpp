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

namespace {
// o ponto (x, y) dentro do triangulo a, b, c (qualquer orientacao; a borda conta)
bool inside(const Vertex& a, const Vertex& b, const Vertex& c, float x, float y) {
    const float d1 = (x - b.x) * (a.y - b.y) - (a.x - b.x) * (y - b.y);
    const float d2 = (x - c.x) * (b.y - c.y) - (b.x - c.x) * (y - c.y);
    const float d3 = (x - a.x) * (c.y - a.y) - (c.x - a.x) * (y - a.y);
    const bool  neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}
bool counts(const CommandList& cl, size_t i, const std::vector<uint8_t>& anchors) {
    if (anchors.size() == cl.draws.size() && anchors[i] == kAnchorLeft) return false;
    return cl.draws[i].vertex_count >= 3 && !full_width_fill(cl, cl.draws[i]);
}
} // namespace

BandCoverage band_uncovered(const CommandList& cl, uint32_t off, const std::vector<uint8_t>& anchors) {
    BandCoverage r;
    const float W = float(cl.frame.width), H = float(cl.frame.height);
    if (!off || W <= 2 * float(off)) return r;
    constexpr int kCols = 8, kRows = 24;
    bool          covered[2][kRows][kCols] = {};
    for (size_t i = 0; i < cl.draws.size(); ++i) {
        if (!counts(cl, i, anchors)) continue;
        const Draw&      d = cl.draws[i];
        const DrawState& s = d.state;
        if (s.clip == Clip::Inside && (s.clip_x0 > s.clip_x1 || s.clip_y0 > s.clip_y1)) continue;
        for (uint32_t k = 0; k + 2 < d.vertex_count; ++k) {  // strip: os triangulos k, k+1, k+2
            const Vertex* t[3] = {&cl.vertices[d.first_vertex + k], &cl.vertices[d.first_vertex + k + 1],
                                  &cl.vertices[d.first_vertex + k + 2]};
            if (k & 1) std::swap(t[0], t[1]);  // a orientacao do strip, como no frame_prep
            const Vertex &a = *t[0], &b = *t[1], &c = *t[2];
            const float   area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
            if (area == 0 || !std::isfinite(area)) continue;
            if ((s.cull == Cull::Small && std::fabs(area) < 2 * cl.frame.small_cull) || (s.cull == Cull::CounterClockwise && area < 0) ||
                (s.cull == Cull::Clockwise && area > 0))
                continue;
            const float x0 = std::min({a.x, b.x, c.x}), x1 = std::max({a.x, b.x, c.x});
            const float y0 = std::min({a.y, b.y, c.y}), y1 = std::max({a.y, b.y, c.y});
            for (int side = 0; side < 2; ++side) {
                const float bx = side ? W - float(off) : 0.f;
                if (x1 < bx || x0 > bx + float(off)) continue;
                for (int r_ = 0; r_ < kRows; ++r_) {
                    const float y = (float(r_) + 0.5f) * H / kRows;
                    if (y < y0 || y > y1) continue;
                    for (int q = 0; q < kCols; ++q) {
                        if (covered[side][r_][q]) continue;
                        const float x = bx + (float(q) + 0.5f) * float(off) / kCols;
                        if (x < x0 || x > x1 || !inside(a, b, c, x, y)) continue;
                        const bool in_rect = x >= float(s.clip_x0) && x <= float(s.clip_x1) + 1 && y >= float(s.clip_y0) &&
                                             y <= float(s.clip_y1) + 1;
                        if ((s.clip == Clip::Inside && !in_rect) || (s.clip == Clip::Outside && in_rect)) continue;
                        covered[side][r_][q] = true;
                    }
                }
            }
        }
    }
    int n[2] = {};
    for (int side = 0; side < 2; ++side)
        for (int r_ = 0; r_ < kRows; ++r_)
            for (int q = 0; q < kCols; ++q) n[side] += !covered[side][r_][q];
    r.left = float(n[0]) / float(kRows * kCols), r.right = float(n[1]) / float(kRows * kCols);
    return r;
}

uint32_t pillar_bands(CommandList& cl, uint32_t off, bool left, bool right, const std::vector<uint8_t>& anchors) {
    if (!left && !right) return 0;
    const int32_t W = int32_t(cl.frame.width), H = int32_t(cl.frame.height);
    const int32_t lo = left ? int32_t(off) : 0, hi = right ? W - int32_t(off) - 1 : W - 1;
    uint32_t      n = 0;
    for (size_t i = 0; i < cl.draws.size(); ++i) {
        if (!counts(cl, i, anchors)) continue;
        DrawState& s = cl.draws[i].state;
        if (s.clip == Clip::Outside) continue;
        if (s.clip == Clip::None) s.clip = Clip::Inside, s.clip_x0 = lo, s.clip_x1 = hi, s.clip_y0 = 0, s.clip_y1 = H - 1;
        else s.clip_x0 = std::max(s.clip_x0, lo), s.clip_x1 = std::min(s.clip_x1, hi);
        ++n;
    }
    return n;
}

bool PillarState::step(Side& s, float uncovered) {
    const bool want = s.on ? uncovered > kPillarOff : uncovered >= kPillarOn;  // continuar / ligar
    if (want == s.on) {
        s.run = 0;
        return false;
    }
    if (++s.run < kPillarFrames) return false;
    s.on = !s.on, s.run = 0;
    return true;
}

bool PillarState::update(const BandCoverage& c) {
    const bool a = step(left_, c.left), b = step(right_, c.right);
    return a || b;
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
