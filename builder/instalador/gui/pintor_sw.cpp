#include "instalador/gui/pintor_sw.h"

#include "text/text_engine.h"
#include "util/hash.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace bs::builder::instalador::gui {

namespace txt = bs::native::text;

namespace {

txt::TextStyle estilo_txt(const Estilo& e, float k) {
    txt::TextStyle s;
    s.size   = e.tamanho * k;
    s.weight = e.peso == Peso::SemiNegrito ? txt::Weight::SemiBold : txt::Weight::Normal;
    s.align  = e.h == Alinha::Centro ? txt::HAlign::Center : e.h == Alinha::Fim ? txt::HAlign::Trailing : txt::HAlign::Leading;
    s.valign = e.v == Alinha::Centro ? txt::VAlign::Center : e.v == Alinha::Fim ? txt::VAlign::Far : txt::VAlign::Near;
    s.wrap   = e.quebra;
    return s;
}

float sdf_arredondado(float px, float py, float cx, float cy, float hw, float hh, float r) {
    const float qx = std::fabs(px - cx) - (hw - r), qy = std::fabs(py - cy) - (hh - r);
    const float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - r;
}

} // namespace

PintorSW::PintorSW(uint32_t w, uint32_t h, float k) { redimensionar(w, h, k); }

void PintorSW::redimensionar(uint32_t w, uint32_t h, float k) {
    w_ = w, h_ = h, k_ = k;
    px_.assign(size_t(w) * h, 0);
    clips_.clear();
}

void PintorSW::misturar(int x, int y, float cov, const Cor& c) {
    const Clip cl = clip();
    if (x < cl.x0 || y < cl.y0 || x >= cl.x1 || y >= cl.y1 || cov <= 0) return;
    const float a = std::clamp(c.a * cov, 0.f, 1.f);
    if (a <= 0) return;
    uint32_t&   d = px_[size_t(y) * w_ + size_t(x)];
    const float da = float(d >> 24) / 255.f, dr = float((d >> 16) & 255) / 255.f, dg = float((d >> 8) & 255) / 255.f,
                db = float(d & 255) / 255.f;
    const float ra = a + da * (1 - a), rr = c.r * a + dr * (1 - a), rg = c.g * a + dg * (1 - a), rb = c.b * a + db * (1 - a);
    auto        b = [](float v) { return uint32_t(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    d = b(ra) << 24 | b(rr) << 16 | b(rg) << 8 | b(rb);
}

void PintorSW::limpar(const Cor& c) {
    auto       b = [](float v) { return uint32_t(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    const uint32_t v = b(c.a) << 24 | b(c.r * c.a) << 16 | b(c.g * c.a) << 8 | b(c.b * c.a);
    std::fill(px_.begin(), px_.end(), v);
}

void PintorSW::retangulo(const Ret& r, const Cor& c) {
    const float x0 = r.x * k_, y0 = r.y * k_, x1 = (r.x + r.w) * k_, y1 = (r.y + r.h) * k_;
    const Clip  cl = clip();
    const int   ix0 = std::max(cl.x0, int(std::floor(x0))), iy0 = std::max(cl.y0, int(std::floor(y0)));
    const int   ix1 = std::min(cl.x1, int(std::ceil(x1))), iy1 = std::min(cl.y1, int(std::ceil(y1)));
    for (int y = iy0; y < iy1; ++y) {
        const float cy = std::min(float(y + 1), y1) - std::max(float(y), y0);
        for (int x = ix0; x < ix1; ++x) {
            const float cx = std::min(float(x + 1), x1) - std::max(float(x), x0);
            misturar(x, y, cx * cy, c);
        }
    }
}

template <class F> void PintorSW::por_distancia(const Ret& r, float margem, F cobertura, const Cor& c) {
    const float x0 = r.x * k_ - margem, y0 = r.y * k_ - margem, x1 = (r.x + r.w) * k_ + margem, y1 = (r.y + r.h) * k_ + margem;
    const Clip  cl = clip();
    const int   ix0 = std::max(cl.x0, int(std::floor(x0))), iy0 = std::max(cl.y0, int(std::floor(y0)));
    const int   ix1 = std::min(cl.x1, int(std::ceil(x1))), iy1 = std::min(cl.y1, int(std::ceil(y1)));
    const float cx = (r.x + r.w / 2) * k_, cy = (r.y + r.h / 2) * k_, hw = r.w / 2 * k_, hh = r.h / 2 * k_;
    for (int y = iy0; y < iy1; ++y)
        for (int x = ix0; x < ix1; ++x) misturar(x, y, cobertura(float(x) + 0.5f, float(y) + 0.5f, cx, cy, hw, hh), c);
}

void PintorSW::arredondado(const Ret& r, float raio, const Cor& c) {
    const float rp = std::min({raio, r.w / 2, r.h / 2}) * k_;
    por_distancia(r, 1, [rp](float px, float py, float cx, float cy, float hw, float hh) {
        return std::clamp(0.5f - sdf_arredondado(px, py, cx, cy, hw, hh, rp), 0.f, 1.f);
    }, c);
}

void PintorSW::contorno(const Ret& r, float raio, float e, const Cor& c) {
    const float rp = std::min({raio, r.w / 2, r.h / 2}) * k_, ep = std::max(1.f, e * k_);
    por_distancia(r, 1, [rp, ep](float px, float py, float cx, float cy, float hw, float hh) {
        const float d = sdf_arredondado(px, py, cx, cy, hw, hh, rp);  // traco por dentro: -ep <= d <= 0
        return std::clamp(0.5f - (std::fabs(d + ep / 2) - ep / 2), 0.f, 1.f);
    }, c);
}

void PintorSW::triangulo(float x0, float y0, float x1, float y1, float x2, float y2, const Cor& c) {
    x0 *= k_, y0 *= k_, x1 *= k_, y1 *= k_, x2 *= k_, y2 *= k_;
    const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::fabs(area) < 1e-6f) return;
    const float s = area > 0 ? 1.f : -1.f;
    const Clip  cl = clip();
    const int   ix0 = std::max(cl.x0, int(std::floor(std::min({x0, x1, x2})))), iy0 = std::max(cl.y0, int(std::floor(std::min({y0, y1, y2}))));
    const int   ix1 = std::min(cl.x1, int(std::ceil(std::max({x0, x1, x2})))), iy1 = std::min(cl.y1, int(std::ceil(std::max({y0, y1, y2}))));
    auto        dentro = [&](float px, float py) {
        const float e0 = ((x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)) * s, e1 = ((x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)) * s,
                    e2 = ((x0 - x2) * (py - y2) - (y0 - y2) * (px - x2)) * s;
        return e0 >= 0 && e1 >= 0 && e2 >= 0;
    };
    for (int y = iy0; y < iy1; ++y)
        for (int x = ix0; x < ix1; ++x) {
            int n = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) n += dentro(float(x) + (float(sx) + 0.5f) / 4, float(y) + (float(sy) + 0.5f) / 4);
            if (n) misturar(x, y, float(n) / 16.f, c);
        }
}

void PintorSW::degrade(const Ret& r, const Cor& topo, const Cor& base) {
    const float y0 = r.y * k_, y1 = (r.y + r.h) * k_;
    const Clip  cl = clip();
    const int   iy0 = std::max(cl.y0, int(std::floor(y0))), iy1 = std::min(cl.y1, int(std::ceil(y1)));
    for (int y = iy0; y < iy1; ++y) {
        const float t = std::clamp((float(y) + 0.5f - y0) / std::max(1.f, y1 - y0), 0.f, 1.f);
        retangulo({r.x, float(y) / k_, r.w, 1.f / k_}, gui::misturar(topo, base, t));
    }
}

Medida PintorSW::medir(std::wstring_view t, const Estilo& e, float largura) {
    auto&          eng = txt::TextEngine::shared();
    txt::TextStyle st = estilo_txt(e, k_);
    st.align = txt::HAlign::Leading, st.valign = txt::VAlign::Near;
    if (largura <= 0) st.wrap = false;
    const auto m = eng.measure(txt::from_wide(t), st, largura > 0 ? largura * k_ : 1e6f, 1e6f);
    return {m.width / k_, m.height / k_, m.lines};
}

void PintorSW::texto(std::wstring_view t, const Estilo& e, const Ret& caixa, const Cor& c) {
    auto&           eng = txt::TextEngine::shared();
    const txt::Color cor{c.r, c.g, c.b, c.a};
    const Clip      cl = clip();
    // o motor desenha no buffer inteiro; com recorte, numa camada do tamanho do recorte, composta depois
    const bool cheio = cl.x0 == 0 && cl.y0 == 0 && cl.x1 == int(w_) && cl.y1 == int(h_);
    if (cheio) {
        txt::Canvas cv{std::span<uint32_t>(px_), w_, h_};
        eng.draw(cv, txt::from_wide(t), estilo_txt(e, k_), {caixa.x * k_, caixa.y * k_, caixa.dir() * k_, caixa.base() * k_}, cor);
        return;
    }
    const int cw = cl.x1 - cl.x0, ch = cl.y1 - cl.y0;
    if (cw <= 0 || ch <= 0) return;
    std::vector<uint32_t> camada(size_t(cw) * size_t(ch), 0);
    txt::Canvas           cv{std::span<uint32_t>(camada), uint32_t(cw), uint32_t(ch)};
    const float           ox = float(cl.x0), oy = float(cl.y0);
    eng.draw(cv, txt::from_wide(t), estilo_txt(e, k_),
             {caixa.x * k_ - ox, caixa.y * k_ - oy, caixa.dir() * k_ - ox, caixa.base() * k_ - oy}, cor);
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) {
            const uint32_t s = camada[size_t(y) * size_t(cw) + size_t(x)];
            const uint32_t sa = s >> 24;
            if (!sa) continue;
            uint32_t&  d = px_[size_t(y + cl.y0) * w_ + size_t(x + cl.x0)];
            const uint32_t inv = 255 - sa;
            auto ch8 = [&](int sh) { return std::min<uint32_t>(255, ((s >> sh) & 255) + (((d >> sh) & 255) * inv + 127) / 255); };
            d = ch8(24) << 24 | ch8(16) << 16 | ch8(8) << 8 | ch8(0);
        }
}

void PintorSW::recortar(const Ret& r) {
    const Clip a = clip();
    Clip       c{int(std::floor(r.x * k_)), int(std::floor(r.y * k_)), int(std::ceil(r.dir() * k_)), int(std::ceil(r.base() * k_))};
    c.x0 = std::max(c.x0, a.x0), c.y0 = std::max(c.y0, a.y0), c.x1 = std::min(c.x1, a.x1), c.y1 = std::min(c.y1, a.y1);
    if (c.x1 < c.x0) c.x1 = c.x0;
    if (c.y1 < c.y0) c.y1 = c.y0;
    clips_.push_back(c);
}

void PintorSW::soltar_recorte() {
    if (!clips_.empty()) clips_.pop_back();
}

// PNG sem compressao (blocos "stored" do deflate): so para as capturas de teste e o icone do .desktop
bool PintorSW::salvar_png(const std::filesystem::path& f) const {
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h_) * (size_t(w_) * 4 + 1));
    for (uint32_t y = 0; y < h_; ++y) {
        raw.push_back(0);
        for (uint32_t x = 0; x < w_; ++x) {
            const uint32_t p = px_[size_t(y) * w_ + x], a = p >> 24;
            auto           un = [&](uint32_t v) { return uint8_t(a ? std::min<uint32_t>(255, (v * 255 + a / 2) / a) : 0); };
            raw.push_back(un((p >> 16) & 255)), raw.push_back(un((p >> 8) & 255)), raw.push_back(un(p & 255)), raw.push_back(uint8_t(a));
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t             s1 = 1, s2 = 0;
    for (uint8_t b : raw) s1 = (s1 + b) % 65521, s2 = (s2 + s1) % 65521;
    for (size_t i = 0; i < raw.size() || i == 0; i += 65535) {
        const size_t n = std::min<size_t>(65535, raw.size() - i);
        z.push_back(i + n >= raw.size() ? 1 : 0);
        z.push_back(uint8_t(n)), z.push_back(uint8_t(n >> 8)), z.push_back(uint8_t(~n)), z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + std::ptrdiff_t(i), raw.begin() + std::ptrdiff_t(i + n));
        if (raw.empty()) break;
    }
    const uint32_t ad = s2 << 16 | s1;
    z.push_back(uint8_t(ad >> 24)), z.push_back(uint8_t(ad >> 16)), z.push_back(uint8_t(ad >> 8)), z.push_back(uint8_t(ad));
    std::ofstream o(f, std::ios::binary | std::ios::trunc);
    auto          be32 = [&](uint32_t v) {
        const char b[4] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v)};
        o.write(b, 4);
    };
    auto chunk = [&](const char* tipo, const std::vector<uint8_t>& d) {
        be32(uint32_t(d.size()));
        util::Crc32 crc;
        crc.update(tipo, 4);
        crc.update(d.data(), d.size());
        o.write(tipo, 4);
        o.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
        be32(crc.value());
    };
    o.write("\x89PNG\r\n\x1a\n", 8);
    std::vector<uint8_t> ihdr = {uint8_t(w_ >> 24), uint8_t(w_ >> 16), uint8_t(w_ >> 8), uint8_t(w_),
                                 uint8_t(h_ >> 24), uint8_t(h_ >> 16), uint8_t(h_ >> 8), uint8_t(h_), 8, 6, 0, 0, 0};
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    return bool(o);
}

} // namespace bs::builder::instalador::gui
