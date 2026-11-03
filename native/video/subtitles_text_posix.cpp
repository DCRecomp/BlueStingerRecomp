// N9-L (L4): o desenho das legendas no Linux, pelo motor de texto (native/text, FreeType + Noto Sans). Mesma geometria do
// Windows (subtitles_dwrite.cpp): tamanho, margens, alinhamento embaixo e ao centro, quebra de linha, contorno em 8 direcoes,
// cores e fundo. So a fonte muda (a Segoe UI e da Microsoft).
#include "video/subtitles.h"

#include "text/text_engine.h"

#include <algorithm>

namespace bs::native::video {

namespace tx = bs::native::text;

struct SubtitleRenderer::Impl {
    tx::TextEngine& engine = tx::TextEngine::shared();
};

std::unique_ptr<ISubtitleRasterizer> make_subtitle_rasterizer() { return std::make_unique<SubtitleRenderer>(); }

SubtitleRenderer::SubtitleRenderer() : impl_(std::make_unique<Impl>()) {}
SubtitleRenderer::~SubtitleRenderer() = default;
bool SubtitleRenderer::ok() const { return impl_ && impl_->engine.ok(); }

namespace {
// texto escuro deslocado em 8 direcoes, depois o branco por cima
void outlined(tx::TextEngine& e, tx::Canvas& c, const std::u32string& t, const tx::TextStyle& st, const tx::Box& box, float o,
              const tx::Color& dark, bool outline) {
    if (outline)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx || dy) e.draw(c, t, st, {box.l + dx * o, box.t + dy * o, box.r + dx * o, box.b + dy * o}, dark);
    e.draw(c, t, st, box, {1, 1, 1, 1});
}
} // namespace

void SubtitleRenderer::draw(std::span<uint32_t> argb, uint32_t w, uint32_t h, const std::wstring& text) {
    if (!ok() || text.empty() || argb.size() < size_t(w) * h) return;
    tx::Canvas    c(argb, w, h);
    tx::TextStyle st;
    st.size   = std::max(10.0f, float(h) * 0.065f);  // ~16 px em 240 linhas
    st.weight = tx::Weight::SemiBold, st.align = tx::HAlign::Center, st.valign = tx::VAlign::Far;
    const float   margin = float(h) * 0.05f;
    const tx::Box box{margin, margin, float(w) - margin, float(h) - margin};
    outlined(impl_->engine, c, tx::from_wide(text), st, box, std::max(1.0f, st.size / 14.0f), {0, 0, 0, 0.85f}, true);
    for (auto& p : argb.first(size_t(w) * h)) p |= 0xFF000000u;  // o quadro do filme e opaco
}

void SubtitleRenderer::draw_overlay(std::span<uint32_t> out, uint32_t w, uint32_t h, const std::wstring& text, float cx, float cy,
                                    float cw, float ch, const OverlayStyle& s) {
    if (out.size() < size_t(w) * h) return;
    std::fill(out.begin(), out.begin() + ptrdiff_t(size_t(w) * h), 0u);  // transparente
    if (!ok() || text.empty() || !w || !h || cw <= 0 || ch <= 0) return;
    tx::Canvas    c(out, w, h);
    tx::TextStyle st;
    st.size   = std::max(12.0f, ch * s.heightFraction * s.scale);
    st.weight = tx::Weight::SemiBold, st.align = tx::HAlign::Center, st.valign = tx::VAlign::Far;
    const float          margin = ch * s.safeMargin;
    const float          boxW = std::max(1.0f, cw - 2 * margin), boxH = std::max(1.0f, ch - 2 * margin);
    const tx::Box        box{cx + margin, cy + margin, cx + margin + boxW, cy + margin + boxH};
    const std::u32string t = tx::from_wide(text);
    if (s.box) {
        const tx::TextMetrics m   = impl_->engine.measure(t, st, boxW, boxH);
        const float           pad = st.size * 0.35f;
        c.fill_rect({box.l + m.left - pad, box.t + m.top - pad * 0.5f, box.l + m.left + m.width + pad, box.t + m.top + m.height + pad * 0.5f},
                    {0, 0, 0, s.boxAlpha});
    }
    outlined(impl_->engine, c, t, st, box, std::max(1.0f, st.size / 14.0f), {0, 0, 0, 0.9f}, s.outline);
}

} // namespace bs::native::video
