// N9-L (L4): o desenho do menu interno no Linux, pelo motor de texto (native/text, FreeType + Noto Sans). Mesma geometria,
// cores, rolagem, caixa de confirmacao e areas do mouse do Windows (menu_overlay.cpp). As setas (que no Windows vem da fonte de
// simbolos que o DirectWrite busca sozinho) sao triangulos desenhados: a Noto Sans nao tem esses caracteres.
#include "menu_overlay.h"

#include "text/text_engine.h"

#include <algorithm>
#include <cmath>

namespace bs {
namespace {

namespace tx = native::text;

struct Rect {
    float l = 0, t = 0, r = 0, b = 0;
    bool  has(float x, float y) const { return x >= l && x < r && y >= t && y < b; }
    tx::Box box() const { return {l, t, r, b}; }
};

// uma chave do conteudo (refazer so quando muda)
std::string key(const MenuView& v, uint32_t w, uint32_t h, float cx, float cy, float cw, float ch) {
    std::string k = v.title + "\x1f" + v.prompt + "\x1f" + std::to_string(v.selected) + "\x1f" + std::to_string(w) + "x" +
                    std::to_string(h) + "@" + std::to_string(cx) + "," + std::to_string(cy) + "," + std::to_string(cw) + "," +
                    std::to_string(ch);
    for (const auto& i : v.items) k += "\x1e" + i.label + "\x1f" + i.value + (i.enabled ? "1" : "0") + (i.submenu ? "s" : "");
    return k + "\x1f" + v.help;
}

enum class Arrow { Left, Right, Up, Down };

// A seta como o glifo do Windows ocupa: alinhada a esquerda na caixa (Left/Right, como o texto do item) ou ao centro (Up/Down).
// Medidas da camada do Windows (2026-11-03, menu 1280x960, em = 30,7 px): 0,68 em de largura, 0,78 em de altura, 0,09 em da
// borda esquerda e o centro 0,05 em abaixo do meio da linha.
void arrow(tx::Canvas& c, Arrow a, const Rect& r, float px, const tx::Color& col) {
    const float cy = (r.t + r.b) * 0.5f + px * 0.05f;
    if (a == Arrow::Left || a == Arrow::Right) {
        const float w = px * 0.68f, h = px * 0.78f, x0 = r.l + px * 0.09f;
        if (a == Arrow::Left) c.fill_triangle(x0, cy, x0 + w, cy - h / 2, x0 + w, cy + h / 2, col);
        else c.fill_triangle(x0, cy - h / 2, x0 + w, cy, x0, cy + h / 2, col);
    } else {
        const float cx = (r.l + r.r) * 0.5f, w = px * 0.78f, h = px * 0.68f;
        if (a == Arrow::Up) c.fill_triangle(cx - w / 2, cy + h / 2, cx, cy - h / 2, cx + w / 2, cy + h / 2, col);
        else c.fill_triangle(cx - w / 2, cy - h / 2, cx + w / 2, cy - h / 2, cx, cy + h / 2, col);
    }
}

} // namespace

struct MenuOverlay::Impl {
    tx::TextEngine&       engine  = tx::TextEngine::shared();
    uint64_t              version = 0;
    std::vector<uint32_t> buf;
    std::string           last;
    struct Row {
        int  item;
        Rect row, dec, inc;
    };
    std::vector<Row> rows;
};

MenuOverlay::MenuOverlay() : impl_(std::make_unique<Impl>()) {}
MenuOverlay::~MenuOverlay() = default;
bool     MenuOverlay::ok() const { return impl_->engine.ok(); }
uint64_t MenuOverlay::version() const { return impl_->version; }

const std::vector<uint32_t>& MenuOverlay::layer(const MenuView& v, uint32_t w, uint32_t h, float cx, float cy, float cw, float ch) {
    Impl&             m = *impl_;
    const std::string k = key(v, w, h, cx, cy, cw, ch);
    if (k == m.last && m.buf.size() == size_t(w) * h) return m.buf;
    m.last = k, ++m.version;
    m.buf.assign(size_t(w) * h, 0u);
    m.rows.clear();
    if (!ok() || !w || !h) return m.buf;

    tx::TextEngine& e = m.engine;
    tx::Canvas      c(m.buf, w, h);
    const float     size = std::max(12.0f, ch * 0.032f);
    auto style = [&](float px, tx::Weight wt, tx::HAlign a, tx::VAlign va) {
        tx::TextStyle s;
        s.size = px, s.weight = wt, s.align = a, s.valign = va;
        return s;
    };
    const tx::TextStyle fTitle = style(size * 1.35f, tx::Weight::SemiBold, tx::HAlign::Leading, tx::VAlign::Center);
    const tx::TextStyle fItem  = style(size, tx::Weight::Normal, tx::HAlign::Leading, tx::VAlign::Center);
    const tx::TextStyle fSmall = style(size * 0.78f, tx::Weight::Normal, tx::HAlign::Center, tx::VAlign::Center);
    const tx::Color     cPanel{0.02f, 0.04f, 0.08f, 0.86f}, cSel{0.20f, 0.42f, 0.78f, 0.85f}, cText{1, 1, 1, 1},
        cDim{0.55f, 0.58f, 0.62f, 1}, cAccent{0.55f, 0.80f, 1.0f, 1}, cBox{0.10f, 0.12f, 0.18f, 0.97f};

    c.fill_rect({cx, cy, cx + cw, cy + ch}, {0, 0, 0, 0.45f});  // o jogo continua visivel por tras, escurecido
    const float pw = std::min(cw * 0.9f, ch * 1.15f), ph = ch * 0.88f;
    const Rect  panel{cx + (cw - pw) / 2, cy + (ch - ph) / 2, cx + (cw + pw) / 2, cy + (ch + ph) / 2};
    c.fill_round_rect(panel.box(), size * 0.4f, cPanel);
    const float pad  = size;
    auto        text = [&](const std::string& s, const tx::TextStyle& st, const Rect& r, const tx::Color& col) {
        e.draw(c, tx::from_utf8(s), st, r.box(), col);
    };
    text(v.title, fTitle, {panel.l + pad, panel.t + pad * 0.5f, panel.r - pad, panel.t + pad * 0.5f + size * 2.0f}, cText);
    const float listTop = panel.t + pad * 0.5f + size * 2.4f, footerH = size * 1.6f;
    const float listBot = panel.b - footerH - pad * 0.5f, rowH = size * 1.7f;
    const int   fit     = std::max(1, int((listBot - listTop) / rowH));
    const int   n       = int(v.items.size());
    const int   first   = std::clamp(v.selected - fit / 2, 0, std::max(0, n - fit));
    for (int i = first; i < std::min(n, first + fit); ++i) {
        const MenuItem& it = v.items[size_t(i)];
        const float     y  = listTop + float(i - first) * rowH;
        const Rect      row{panel.l + pad * 0.5f, y, panel.r - pad * 0.5f, y + rowH};
        if (i == v.selected) c.fill_round_rect({row.l, row.t + 2, row.r, row.b - 2}, size * 0.3f, cSel);
        const tx::Color& tb = it.enabled ? cText : cDim;
        text(it.label, fItem, {row.l + pad * 0.5f, row.t, row.l + (row.r - row.l) * 0.5f, row.b}, tb);
        Impl::Row hr{i, row, {}, {}};
        if (it.submenu) {  // abre outra pagina: ">" na ponta, o valor (se houver) so informativo, sem setas
            const float vr = row.r - pad * 0.5f, aw = size * 1.2f, vl = row.l + (row.r - row.l) * 0.5f;
            text("\xE2\x80\xBA", fTitle, {vr - aw, row.t, vr, row.b}, it.enabled ? cAccent : cDim);  // U+203A
            if (!it.value.empty())
                text(it.value, style(size, tx::Weight::Normal, tx::HAlign::Trailing, tx::VAlign::Center),
                     {vl + aw, row.t, vl + aw + (vr - vl - 2 * aw), row.t + rowH}, cDim);
        } else if (!it.value.empty()) {
            const float vl = row.l + (row.r - row.l) * 0.5f, vr = row.r - pad * 0.5f, aw = size * 1.2f;
            if (it.enabled) {
                hr.dec = {vl, row.t, vl + aw, row.b}, hr.inc = {vr - aw, row.t, vr, row.b};
                arrow(c, Arrow::Left, hr.dec, size, cAccent);
                arrow(c, Arrow::Right, hr.inc, size, cAccent);
            }
            text(it.value, style(size, tx::Weight::Normal, tx::HAlign::Center, tx::VAlign::Center),
                 {vl + aw, row.t, vl + aw + (vr - vl - 2 * aw), row.t + rowH}, tb);
        }
        m.rows.push_back(hr);
    }
    if (first > 0) arrow(c, Arrow::Up, {panel.l, listTop - size * 0.9f, panel.r, listTop}, fSmall.size, cDim);
    if (first + fit < n) arrow(c, Arrow::Down, {panel.l, listBot, panel.r, listBot + size * 0.9f}, fSmall.size, cDim);
    text(v.help, fSmall, {panel.l, panel.b - footerH, panel.r, panel.b - pad * 0.2f}, cDim);
    if (!v.prompt.empty()) {  // confirmacao por cima de tudo
        const float bw = pw * 0.86f, bh = size * 4.2f;
        const Rect  box{cx + (cw - bw) / 2, cy + (ch - bh) / 2, cx + (cw + bw) / 2, cy + (ch + bh) / 2};
        c.fill_round_rect(box.box(), size * 0.4f, cBox);
        c.stroke_round_rect(box.box(), size * 0.4f, 2, cAccent);
        text(v.prompt, style(size, tx::Weight::Normal, tx::HAlign::Center, tx::VAlign::Center),
             {box.l + pad, box.t, box.l + pad + (box.r - box.l - 2 * pad), box.b}, cText);
        m.rows.clear();  // com a pergunta aberta, o mouse so confirma (clique) ou volta (botao direito)
    }
    return m.buf;
}

std::optional<MenuHit> MenuOverlay::hit(float x, float y) const {
    for (const auto& r : impl_->rows) {
        if (r.dec.has(x, y)) return MenuHit{r.item, MenuHit::Part::Decrease};
        if (r.inc.has(x, y)) return MenuHit{r.item, MenuHit::Part::Increase};
        if (r.row.has(x, y)) return MenuHit{r.item, MenuHit::Part::Row};
    }
    return std::nullopt;
}

} // namespace bs
