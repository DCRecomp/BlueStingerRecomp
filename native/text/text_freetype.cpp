// N9-L (L4): o motor de texto do Linux (text_engine.h), nos dois sistemas desde o N9.2: FreeType sobre a Noto Sans embutida
// (fonts_embed.h). Sem hinting,
// com posicao de glifo em quartos de pixel e cobertura em tons de cinza, como o texto do Direct2D em modo software.
#include "text/text_engine.h"
#include "text/fonts_embed.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_OUTLINE_H

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>


namespace bs::native::text {

// ---------------------------------------------------------------------------------------------------------------- texto
std::u32string from_utf8(std::string_view s) {
    static constexpr char32_t kMin[5] = {0, 0, 0x80, 0x800, 0x10000};
    std::u32string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        char32_t   cp;
        size_t     n;
        if (c < 0x80) cp = c, n = 1;
        else if ((c >> 5) == 6) cp = c & 0x1F, n = 2;
        else if ((c >> 4) == 14) cp = c & 0x0F, n = 3;
        else if ((c >> 3) == 30) cp = c & 0x07, n = 4;
        else {
            out += U'�', ++i;
            continue;
        }
        bool good = i + n <= s.size();
        for (size_t k = 1; good && k < n; ++k) {
            const auto d = static_cast<unsigned char>(s[i + k]);
            good         = (d >> 6) == 2;
            cp           = cp << 6 | (d & 0x3F);
        }
        if (!good || cp < kMin[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            out += U'�', ++i;  // a sequencia invalida consome so o primeiro byte
            continue;
        }
        out += cp, i += n;
    }
    return out;
}

std::u32string from_wide(std::wstring_view s) {
    std::u32string out;
    out.reserve(s.size());
    if constexpr (sizeof(wchar_t) == 4) {
        for (wchar_t ch : s) out += char32_t(ch);
    } else {  // UTF-16 (nao usado no Linux; mantido para o modulo nao depender da plataforma)
        for (size_t i = 0; i < s.size(); ++i) {
            const auto u = char32_t(uint16_t(s[i]));
            if (u >= 0xD800 && u <= 0xDBFF && i + 1 < s.size()) {
                const auto v = char32_t(uint16_t(s[i + 1]));
                if (v >= 0xDC00 && v <= 0xDFFF) {
                    out += 0x10000 + ((u - 0xD800) << 10) + (v - 0xDC00), ++i;
                    continue;
                }
            }
            out += (u >= 0xD800 && u <= 0xDFFF) ? U'�' : u;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------- camada
namespace {

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// distancia com sinal ao retangulo arredondado (negativa dentro)
float sdRoundRect(float px, float py, const Box& r, float radius) {
    const float cx = (r.l + r.r) * 0.5f, cy = (r.t + r.b) * 0.5f;
    const float hx = (r.r - r.l) * 0.5f, hy = (r.b - r.t) * 0.5f;
    radius        = std::min({radius, hx, hy});
    const float qx = std::fabs(px - cx) - (hx - radius), qy = std::fabs(py - cy) - (hy - radius);
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
}

// distancia com sinal ao triangulo (negativa dentro), qualquer sentido dos vertices
float sdTriangle(float px, float py, float x0, float y0, float x1, float y1, float x2, float y2) {
    const float e0x = x1 - x0, e0y = y1 - y0, e1x = x2 - x1, e1y = y2 - y1, e2x = x0 - x2, e2y = y0 - y2;
    const float v0x = px - x0, v0y = py - y0, v1x = px - x1, v1y = py - y1, v2x = px - x2, v2y = py - y2;
    auto seg = [](float vx, float vy, float ex, float ey) {
        const float t = std::clamp((vx * ex + vy * ey) / (ex * ex + ey * ey), 0.0f, 1.0f);
        const float dx = vx - ex * t, dy = vy - ey * t;
        return dx * dx + dy * dy;
    };
    const float s  = (e0x * e2y - e0y * e2x) > 0 ? 1.0f : -1.0f;
    const float d0 = seg(v0x, v0y, e0x, e0y), d1 = seg(v1x, v1y, e1x, e1y), d2 = seg(v2x, v2y, e2x, e2y);
    const float c0 = s * (v0x * e0y - v0y * e0x), c1 = s * (v1x * e1y - v1y * e1x), c2 = s * (v2x * e2y - v2y * e2x);
    const float d  = std::min({d0, d1, d2});
    const float c  = std::min({c0, c1, c2});
    return -std::sqrt(d) * (c > 0 ? 1.0f : (c < 0 ? -1.0f : 0.0f));
}

} // namespace

void Canvas::clear(uint32_t v) { std::fill(px_.begin(), px_.begin() + ptrdiff_t(std::min(px_.size(), size_t(w_) * h_)), v); }

void Canvas::blend(int x, int y, float coverage, const Color& c) {
    if (x < 0 || y < 0 || uint32_t(x) >= w_ || uint32_t(y) >= h_) return;
    const float a = clamp01(coverage) * clamp01(c.a);
    if (a <= 0) return;
    const uint32_t sa = uint32_t(std::lround(a * 255.0f));
    const uint32_t sr = uint32_t(std::lround(clamp01(c.r) * a * 255.0f));
    const uint32_t sg = uint32_t(std::lround(clamp01(c.g) * a * 255.0f));
    const uint32_t sb = uint32_t(std::lround(clamp01(c.b) * a * 255.0f));
    uint32_t&      d  = px_[size_t(y) * w_ + uint32_t(x)];
    const uint32_t k  = 255 - sa;
    auto mix = [k](uint32_t s, uint32_t dv) { return std::min<uint32_t>(255, s + (dv * k + 127) / 255); };
    d = mix(sa, d >> 24) << 24 | mix(sr, (d >> 16) & 0xFF) << 16 | mix(sg, (d >> 8) & 0xFF) << 8 | mix(sb, d & 0xFF);
}

void Canvas::fill_rect(const Box& r, const Color& c) {
    const int x0 = std::max(0, int(std::floor(r.l))), x1 = std::min(int(w_), int(std::ceil(r.r)));
    const int y0 = std::max(0, int(std::floor(r.t))), y1 = std::min(int(h_), int(std::ceil(r.b)));
    for (int y = y0; y < y1; ++y) {
        const float cy = std::min(r.b, float(y + 1)) - std::max(r.t, float(y));
        for (int x = x0; x < x1; ++x) blend(x, y, cy * (std::min(r.r, float(x + 1)) - std::max(r.l, float(x))), c);
    }
}

void Canvas::fill_round_rect(const Box& r, float radius, const Color& c) {
    if (radius <= 0) return fill_rect(r, c);
    const int x0 = std::max(0, int(std::floor(r.l))), x1 = std::min(int(w_), int(std::ceil(r.r)));
    const int y0 = std::max(0, int(std::floor(r.t))), y1 = std::min(int(h_), int(std::ceil(r.b)));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) blend(x, y, clamp01(0.5f - sdRoundRect(float(x) + 0.5f, float(y) + 0.5f, r, radius)), c);
}

void Canvas::stroke_round_rect(const Box& r, float radius, float stroke, const Color& c) {
    const float e  = stroke * 0.5f + 1.0f;
    const int   x0 = std::max(0, int(std::floor(r.l - e))), x1 = std::min(int(w_), int(std::ceil(r.r + e)));
    const int   y0 = std::max(0, int(std::floor(r.t - e))), y1 = std::min(int(h_), int(std::ceil(r.b + e)));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const float d = std::fabs(sdRoundRect(float(x) + 0.5f, float(y) + 0.5f, r, radius)) - stroke * 0.5f;
            blend(x, y, clamp01(0.5f - d), c);
        }
}

void Canvas::fill_triangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color& c) {
    const int bx0 = std::max(0, int(std::floor(std::min({x0, x1, x2}))) - 1), bx1 = std::min(int(w_), int(std::ceil(std::max({x0, x1, x2}))) + 1);
    const int by0 = std::max(0, int(std::floor(std::min({y0, y1, y2}))) - 1), by1 = std::min(int(h_), int(std::ceil(std::max({y0, y1, y2}))) + 1);
    for (int y = by0; y < by1; ++y)
        for (int x = bx0; x < bx1; ++x)
            blend(x, y, clamp01(0.5f - sdTriangle(float(x) + 0.5f, float(y) + 0.5f, x0, y0, x1, y1, x2, y2)), c);
}

// ---------------------------------------------------------------------------------------------------------------- motor
namespace {

bool isSpace(char32_t c) { return c == U' ' || c == U'\t' || c == U'　'; }

class FreeTypeEngine final : public TextEngine {
public:
    FreeTypeEngine() {
        if (FT_Init_FreeType(&lib_)) {
            lib_ = nullptr;
            return;
        }
        for (int i = 0; i < 2; ++i) {
            const FontBlob f = embedded_font(i);
            if (f.data) load(i, f.data, f.data + f.size);
        }
    }
    ~FreeTypeEngine() override {
        for (FT_Face& f : faces_)
            if (f) FT_Done_Face(f);
        if (lib_) FT_Done_FreeType(lib_);
    }

    bool ok() const override { return lib_ && faces_[0] && faces_[1]; }

    std::string describe() const override {
        FT_Int a = 0, b = 0, c = 0;
        if (lib_) FT_Library_Version(lib_, &a, &b, &c);
        return "FreeType " + std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c) + ", Noto Sans 2.015";
    }

    bool has_glyph(char32_t ch, Weight w) override {
        const std::lock_guard<std::mutex> g(m_);
        return ok() && FT_Get_Char_Index(faces_[idx(w)], FT_ULong(ch)) != 0;
    }

    TextMetrics measure(std::u32string_view text, const TextStyle& st, float boxW, float boxH) override {
        const std::lock_guard<std::mutex> g(m_);
        if (!ok()) return {};
        const Layout l = layout(text, st, boxW);
        return metrics(l, st, boxW, boxH);
    }

    TextMetrics draw(Canvas& c, std::u32string_view text, const TextStyle& st, const Box& box, const Color& color) override {
        const std::lock_guard<std::mutex> g(m_);
        if (!ok()) return {};
        const float       boxW = box.r - box.l, boxH = box.b - box.t;
        const Layout      l    = layout(text, st, boxW);
        const TextMetrics m    = metrics(l, st, boxW, boxH);
        FT_Face           f    = faces_[idx(st.weight)];
        for (size_t li = 0; li < l.lines.size(); ++li) {
            const Line& ln = l.lines[li];
            double      x  = box.l + alignX(ln.width, st.align, boxW);
            const double y = box.t + m.top + l.asc + double(li) * l.lineH;  // linha de base
            for (size_t i = ln.begin; i < ln.end; ++i) {
                if (!isSpace(text[i])) glyph(c, f, st, l.gi[i], x, y, color);
                x += l.adv[i];
            }
        }
        return m;
    }

private:
    struct Line {
        size_t begin, end;  // [begin, end) no texto, sem a quebra
        double width;       // sem os espacos do fim
    };
    struct Layout {
        std::vector<Line>     lines;
        std::vector<double>   adv;
        std::vector<FT_UInt>  gi;
        double                asc = 0, lineH = 0;
        uint32_t              missing = 0;
    };
    struct Bitmap {
        int                  left = 0, top = 0, w = 0, h = 0;
        std::vector<uint8_t> cov;
    };

    static int idx(Weight w) { return w == Weight::SemiBold ? 1 : 0; }

    void load(int i, const unsigned char* b, const unsigned char* e) {
        FT_Face f = nullptr;
        if (!FT_New_Memory_Face(lib_, b, FT_Long(e - b), 0, &f)) faces_[i] = f;
    }

    void setSize(FT_Face f, float px) {
        const FT_F26Dot6 s = FT_F26Dot6(std::lround(double(px) * 64.0));
        if (f->size && f->size->metrics.x_ppem && sizeOf_[f == faces_[1]] == s) return;
        FT_Set_Char_Size(f, 0, s, 72, 72);  // 1 ponto = 1 pixel: o em tem `px` pixels
        sizeOf_[f == faces_[1]] = s;
    }

    Layout layout(std::u32string_view text, const TextStyle& st, float boxW) {
        Layout  l;
        FT_Face f = faces_[idx(st.weight)];
        setSize(f, st.size);
        const double em = double(st.size) / f->units_per_EM;
        l.asc           = f->ascender * em;
        l.lineH         = f->height * em;  // ascendente - descendente + espaco entre linhas (hhea)
        l.adv.resize(text.size());
        l.gi.resize(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == U'\n' || text[i] == U'\r') continue;
            FT_UInt gi = FT_Get_Char_Index(f, FT_ULong(text[i]));
            if (!gi && !isSpace(text[i])) ++l.missing;
            FT_Fixed a = 0;
            FT_Get_Advance(f, gi, FT_LOAD_NO_HINTING, &a);
            l.gi[i] = gi, l.adv[i] = double(a) / 65536.0;
        }
        auto trimmed = [&](size_t b, size_t e) {
            while (e > b && isSpace(text[e - 1])) --e;
            double w = 0;
            for (size_t k = b; k < e; ++k) w += l.adv[k];
            return w;
        };
        const bool wrap = st.wrap && boxW > 0;
        size_t     p    = 0;
        while (p <= text.size()) {
            size_t pe = p;
            while (pe < text.size() && text[pe] != U'\n') ++pe;
            size_t lineEnd = pe;
            if (lineEnd > p && text[lineEnd - 1] == U'\r') --lineEnd;
            size_t ls = p, brk = std::u32string_view::npos;
            double w  = 0;
            for (size_t i = p; i < lineEnd;) {
                const char32_t ch = text[i];
                if (wrap && !isSpace(ch) && i > ls && w + l.adv[i] > double(boxW) + 1e-3) {
                    const size_t cut = (brk != std::u32string_view::npos && brk > ls) ? brk : i;  // na palavra, ou dentro dela
                    l.lines.push_back({ls, cut, trimmed(ls, cut)});
                    ls = cut, brk = std::u32string_view::npos, w = 0;
                    for (size_t k = ls; k < i; ++k) w += l.adv[k];
                    continue;
                }
                w += l.adv[i];
                if ((isSpace(ch) || ch == U'-') && i + 1 < lineEnd && !isSpace(text[i + 1])) brk = i + 1;
                ++i;
            }
            l.lines.push_back({ls, lineEnd, trimmed(ls, lineEnd)});
            if (pe >= text.size()) break;
            p = pe + 1;
        }
        if (text.empty()) l.lines.clear();
        return l;
    }

    static double alignX(double lineW, HAlign a, float boxW) {
        switch (a) {
        case HAlign::Center: return (double(boxW) - lineW) * 0.5;
        case HAlign::Trailing: return double(boxW) - lineW;
        default: return 0;
        }
    }

    static TextMetrics metrics(const Layout& l, const TextStyle& st, float boxW, float boxH) {
        TextMetrics m;
        m.lines   = int(l.lines.size());
        m.missing = l.missing;
        m.height  = float(l.lineH * double(l.lines.size()));
        double maxW = 0, minX = 1e30;
        for (const Line& ln : l.lines) maxW = std::max(maxW, ln.width), minX = std::min(minX, alignX(ln.width, st.align, boxW));
        m.width = float(maxW);
        m.left  = l.lines.empty() ? 0.0f : float(minX);
        switch (st.valign) {
        case VAlign::Center: m.top = (boxH - m.height) * 0.5f; break;
        case VAlign::Far: m.top = boxH - m.height; break;
        default: m.top = 0;
        }
        return m;
    }

    void glyph(Canvas& c, FT_Face f, const TextStyle& st, FT_UInt gi, double x, double y, const Color& color) {
        int ix = int(std::floor(x)), iy = int(std::floor(y));
        int sx = int(std::lround((x - ix) * 4)), sy = int(std::lround((y - iy) * 4));
        if (sx == 4) sx = 0, ++ix;
        if (sy == 4) sy = 0, ++iy;
        const auto key = std::make_tuple(idx(st.weight), sizeOf_[f == faces_[1]], gi, sx, sy);
        auto       it  = cache_.find(key);
        if (it == cache_.end()) {
            if (cache_.size() > 8192) cache_.clear();
            Bitmap b;
            if (!FT_Load_Glyph(f, gi, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) && f->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
                FT_Outline_Translate(&f->glyph->outline, sx * 16, -sy * 16);  // y do FreeType sobe; o da tela desce
                if (!FT_Render_Glyph(f->glyph, FT_RENDER_MODE_NORMAL)) {
                    const FT_Bitmap& bm = f->glyph->bitmap;
                    b.left = f->glyph->bitmap_left, b.top = f->glyph->bitmap_top, b.w = int(bm.width), b.h = int(bm.rows);
                    b.cov.resize(size_t(b.w) * size_t(b.h));
                    for (int r = 0; r < b.h; ++r)
                        std::copy_n(bm.buffer + ptrdiff_t(r) * bm.pitch, b.w, b.cov.begin() + ptrdiff_t(r) * b.w);
                }
            }
            it = cache_.emplace(key, std::move(b)).first;
        }
        const Bitmap& b = it->second;
        for (int r = 0; r < b.h; ++r)
            for (int k = 0; k < b.w; ++k)
                if (const uint8_t v = b.cov[size_t(r) * size_t(b.w) + size_t(k)]) c.blend(ix + b.left + k, iy - b.top + r, float(v) / 255.0f, color);
    }

    std::mutex                                                   m_;
    FT_Library                                                   lib_ = nullptr;
    FT_Face                                                      faces_[2] = {};
    FT_F26Dot6                                                   sizeOf_[2] = {};
    std::map<std::tuple<int, FT_F26Dot6, FT_UInt, int, int>, Bitmap> cache_;
};

} // namespace

TextEngine& TextEngine::shared() {
    static FreeTypeEngine engine;
    return engine;
}

} // namespace bs::native::text
