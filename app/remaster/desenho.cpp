#include "remaster/desenho.h"

#include "text/text_engine.h"
#include "ui/identidade.h"

#include <algorithm>
#include <cmath>

namespace bs::remaster {

namespace {

namespace tx = native::text;
namespace id = native::ui::identidade;

tx::Color cor(uint32_t rgb, float a = 1) {
    return {float((rgb >> 16) & 0xFF) / 255.f, float((rgb >> 8) & 0xFF) / 255.f, float(rgb & 0xFF) / 255.f, a};
}

// Contexto de um desenho: o canvas, o motor de texto e o registro de cortes.
struct Ctx {
    tx::Canvas&         c;
    tx::TextEngine&     te;
    std::vector<Corte>* cortes;

    // Texto numa caixa. `uma` = uma linha so: se nao cabe na largura, registra o corte. Glifo ausente tambem e corte.
    float texto(const std::string& s, float x, float y, float w, float h, float tam, tx::Weight peso, const tx::Color& col,
                tx::HAlign al = tx::HAlign::Leading, tx::VAlign va = tx::VAlign::Near, bool uma = true) {
        if (s.empty()) return 0;
        tx::TextStyle st;
        st.size = tam, st.weight = peso, st.align = al, st.valign = va, st.wrap = !uma;
        std::u32string t = tx::from_utf8(s);
        // a Noto Sans nao tem as setas que o modelo usa na ajuda (o mesmo caso do menu interno do Linux)
        for (char32_t& ch : t)
            if (ch == U'◀' || ch == U'←') ch = U'<';
            else if (ch == U'▶' || ch == U'→') ch = U'>';
        const tx::TextMetrics m = te.measure(t, st, uma ? 1e6f : w, h);
        if (cortes) {
            if (m.missing) cortes->push_back({"(glifo ausente) " + s, float(m.missing), 0});
            if (uma && m.width > w + 0.5f) cortes->push_back({s, m.width, w});
            if (!uma && m.height > h + 0.5f) cortes->push_back({s, m.height, h});
        }
        te.draw(c, t, st, {x, y, x + w, y + h}, col);
        return m.height;
    }
    float altura_texto(const std::string& s, float w, float tam, tx::Weight peso) {
        tx::TextStyle st;
        st.size = tam, st.weight = peso, st.wrap = true;
        return te.measure(tx::from_utf8(s), st, w, 1e6f).height;
    }
};

void emblema(tx::Canvas& c, float x, float y, float s) {
    for (const auto& t : id::kEmblema)
        c.fill_triangle(x + t.x0 * s, y + t.y0 * s, x + t.x1 * s, y + t.y1 * s, x + t.x2 * s, y + t.y2 * s, cor(t.cor, t.alfa));
    for (float v : id::kFaixas) {
        const float     e = id::kFaixaEspessura, a = id::faixa_meia(v), b = id::faixa_meia(v + e);
        const tx::Color k = cor(id::kAmbarEscuro);
        c.fill_triangle(x + (0.5f - a) * s, y + v * s, x + (0.5f + a) * s, y + v * s, x + (0.5f + b) * s, y + (v + e) * s, k);
        c.fill_triangle(x + (0.5f - a) * s, y + v * s, x + (0.5f + b) * s, y + (v + e) * s, x + (0.5f - b) * s, y + (v + e) * s, k);
    }
}

// A regiao do menu na tela de titulo: onde o original desenhava NEW GAME, LOAD GAME e OPTIONS (0,57 a 0,72 da altura),
// entre o logotipo e o copyright (que comeca em ~0,78), em fracoes da altura da area do jogo.
constexpr float kRegiaoTopo = 0.535f, kRegiaoBase = 0.772f;

std::string chave(const Vista& v, uint32_t w, uint32_t h, const Area& a, float alfa) {
    std::string k = std::to_string(int(v.tela)) + "|" + std::to_string(w) + "x" + std::to_string(h) + "@" + std::to_string(int(a.x)) +
                    "," + std::to_string(int(a.y)) + "," + std::to_string(int(a.w)) + "|" + v.titulo + "|" + v.subtitulo + "|" +
                    v.texto + "|" + v.dica + "|" + std::to_string(v.selecionado) + (v.sobreTitulo ? "T" : "t") + "|" +
                    std::to_string(int(alfa * 64));
    for (const auto& i : v.itens) k += "\x1e" + i.rotulo + "\x1f" + i.detalhe + (i.ativo ? "1" : "0") + (i.destrutivo ? "d" : "");
    return k;
}

} // namespace

Area area_4x3(uint32_t w, uint32_t h) {
    if (!w || !h) return {0, 0, 640, 480};
    const float sw = float(w), sh = float(h), aw = std::min(sw, sh * 4.f / 3.f), ah = aw * 3.f / 4.f;
    return {std::floor((sw - aw) / 2), std::floor((sh - ah) / 2), std::floor(aw), std::floor(ah)};
}

bool Desenho::texto_ok() const { return tx::TextEngine::shared().ok(); }

// O alfa do fade sobre a camada pronta (pre-multiplicada: todos os canais escalam juntos)
void Desenho::finalizar(float alfa) {
    if (alfa >= 0.999f) return;
    const uint32_t k = uint32_t(std::clamp(alfa, 0.f, 1.f) * 256.f);
    for (uint32_t& p : buf_) {
        if (!p) continue;
        const uint32_t ag = ((p >> 8) & 0x00FF00FFu) * k, rb = (p & 0x00FF00FFu) * k;
        p = (ag & 0xFF00FF00u) | ((rb >> 8) & 0x00FF00FFu);
    }
}

const std::vector<uint32_t>& Desenho::camada(const Vista& v, uint32_t w, uint32_t h, const Area& a, float alfa) {
    const std::string k = chave(v, w, h, a, alfa);
    if (k == chave_ && buf_.size() == size_t(w) * h) return buf_;
    chave_ = k, ++versao_;
    buf_.assign(size_t(w) * h, 0);
    linhas_.clear(), cortes_.clear();
    if (!w || !h) return buf_;
    tx::Canvas c(buf_, w, h);
    Ctx        x{c, tx::TextEngine::shared(), &cortes_};
    const float W = float(w), H = float(h);

    if (v.sobreTitulo && v.tela != Tela::Confirmar) {
        // No lugar do menu original (que a Remastered nao desenha mais): entre o logotipo e o copyright da tela de titulo, sobre
        // o fundo animado do proprio jogo, sem faixa nem cartao por cima.
        const float s = a.h / 480.f;  // a unidade da tela de titulo original (640 x 480)
        const float topo = a.y + a.h * kRegiaoTopo, base = a.y + a.h * kRegiaoBase;
        const float larg = std::min(a.w * 0.60f, 384 * s), lx = a.x + (a.w - larg) / 2;
        auto sombra = [&](const std::string& t, float xx, float yy, float ww, float hh, float tam, tx::Weight p, uint32_t k,
                          tx::HAlign al, bool uma = true) {
            x.cortes = nullptr;  // a sombra mede igual: o corte sai do texto de cima
            x.texto(t, xx + 1.2f * s, yy + 1.2f * s, ww, hh, tam, p, cor(0x000000, 0.75f), al, tx::VAlign::Center, uma);
            x.cortes = &cortes_;
            x.texto(t, xx, yy, ww, hh, tam, p, cor(k), al, tx::VAlign::Center, uma);
        };
        float y = topo;
        if (v.tela == Tela::Inicial) {
            // a marca da edicao, discreta, logo abaixo do logotipo original
            emblema(c, a.x + a.w * 0.5f - 46 * s, a.y + a.h * 0.462f, 11 * s);
            sombra("Remastered", a.x + a.w * 0.5f - 31 * s, a.y + a.h * 0.455f, 120 * s, 18 * s, 11 * s, tx::Weight::SemiBold, id::kAgua,
                   tx::HAlign::Leading);
        } else {
            std::string cab = v.titulo + (v.subtitulo.empty() ? std::string() : std::string("  \xC2\xB7  ") + v.subtitulo);
            sombra(cab, lx, y, larg, 17 * s, 12.5f * s, tx::Weight::SemiBold, id::kTexto, tx::HAlign::Center);
            y += 19 * s;
            if (!v.texto.empty()) {
                const float th = std::min(base - y - (v.itens.empty() ? 0 : 18 * s), x.altura_texto(v.texto, larg, 10.5f * s, tx::Weight::Normal) + 2);
                x.cortes = nullptr;
                x.texto(v.texto, lx + 1.2f * s, y + 1.2f * s, larg, th, 10.5f * s, tx::Weight::Normal, cor(0x000000, 0.75f), tx::HAlign::Center,
                        tx::VAlign::Near, false);
                x.cortes = &cortes_;
                x.texto(v.texto, lx, y, larg, th, 10.5f * s, tx::Weight::Normal, cor(id::kTextoSuave), tx::HAlign::Center, tx::VAlign::Near, false);
                y += th + 4 * s;
            }
        }
        const float n = float(std::max<size_t>(1, v.itens.size()));
        const float alt = std::min(17.5f * s, (base - y) / n);
        const float tam = std::min(13.5f * s, alt * 0.74f);
        for (size_t i = 0; i < v.itens.size(); ++i) {
            const auto& it  = v.itens[i];
            const bool  sel = int(i) == v.selecionado;
            const float t = y + float(i) * alt, b = t + alt - 1.5f * s;
            if (sel) {
                c.fill_round_rect({lx, t, lx + larg, b}, 4 * s, cor(id::kCartaoHover, 0.78f));
                c.fill_round_rect({lx, t + 3 * s, lx + 2.5f * s, b - 3 * s}, 1.2f * s, cor(id::kAmbar));
            }
            const uint32_t kc = !it.ativo ? id::kTextoApagado : sel ? id::kAmbar : id::kTexto;
            // todos os rotulos no centro, como as opcoes originais; o detalhe (o perfil de Continuar) pequeno, a direita
            sombra(it.rotulo, lx + larg * 0.25f, t, larg * 0.5f, b - t, tam, sel ? tx::Weight::SemiBold : tx::Weight::Normal, kc,
                   tx::HAlign::Center);
            if (!it.detalhe.empty() && it.ativo)
                sombra(it.detalhe, lx + larg * 0.62f, t, larg * 0.38f - 6 * s, b - t, tam * 0.66f, tx::Weight::Normal, id::kTextoSuave,
                       tx::HAlign::Trailing);
            linhas_.push_back({int(i), lx, t, lx + larg, b});
        }
        if (!v.dica.empty())
            sombra(v.dica, a.x, a.y + a.h - 17 * s, a.w, 14 * s, 9.5f * s, tx::Weight::Normal, id::kTextoSuave, tx::HAlign::Center);
        finalizar(alfa);
        return buf_;
    }
    if (v.sobreTitulo) {
        c.fill_rect({0, 0, W, H}, cor(id::kFundo, 0.35f));  // confirmacao a partir do menu inicial (nao ha hoje)
    } else {
        c.fill_rect({0, 0, W, H}, cor(id::kFundo, 0.74f));  // pausa: o jogo escurecido
    }

    const float u = std::min(W / 1280.f, H / 720.f);
    const float cw = std::min(600 * u, W - 48 * u), pad = 28 * u, alt = 48 * u;
    float       conteudo = 56 * u;  // titulo
    if (!v.subtitulo.empty()) conteudo += 30 * u;
    const float textoH = v.texto.empty() ? 0 : x.altura_texto(v.texto, cw - 2 * pad, 17 * u, tx::Weight::Normal) + 18 * u;
    conteudo += textoH + float(v.itens.size()) * (alt + 4 * u) + (v.dica.empty() ? 0 : 40 * u);
    const float ch = std::min(H - 32 * u, conteudo + 2 * pad), cx = (W - cw) / 2, cy = (H - ch) / 2;
    c.fill_round_rect({cx, cy, cx + cw, cy + ch}, 14 * u, cor(id::kCartao, 0.97f));
    c.stroke_round_rect({cx, cy, cx + cw, cy + ch}, 14 * u, std::max(1.f, 1.5f * u), cor(id::kBorda));
    c.fill_round_rect({cx + pad, cy + 14 * u, cx + pad + 44 * u, cy + 18 * u}, 2 * u, cor(id::kAmbar));
    float y = cy + pad;
    x.texto(v.titulo, cx + pad, y, cw - 2 * pad, 40 * u, 28 * u, tx::Weight::SemiBold, cor(id::kTexto));
    y += 48 * u;
    if (!v.subtitulo.empty()) x.texto(v.subtitulo, cx + pad, y, cw - 2 * pad, 26 * u, 16 * u, tx::Weight::Normal, cor(id::kTextoSuave)), y += 30 * u;
    if (textoH > 0) {
        x.texto(v.texto, cx + pad, y, cw - 2 * pad, textoH, 17 * u, tx::Weight::Normal, cor(id::kTextoSuave), tx::HAlign::Leading,
                tx::VAlign::Near, false);
        y += textoH;
    }
    for (size_t i = 0; i < v.itens.size(); ++i) {
        const auto& it  = v.itens[i];
        const bool  sel = int(i) == v.selecionado;
        const float t = y + float(i) * (alt + 4 * u), b = t + alt, l = cx + pad - 10 * u, r = cx + cw - pad + 10 * u;
        if (sel) {
            c.fill_round_rect({l, t, r, b}, 8 * u, cor(id::kCartaoHover));
            c.stroke_round_rect({l, t, r, b}, 8 * u, std::max(1.f, 2 * u), cor(it.destrutivo ? id::kCoral : id::kAmbar));
        }
        const uint32_t kc  = !it.ativo ? id::kTextoApagado : sel ? (it.destrutivo ? id::kCoral : id::kAmbar) : id::kTexto;
        const float    rot = it.detalhe.empty() ? (r - l) - 20 * u : (r - l) * 0.40f;
        x.texto(it.rotulo, l + 10 * u, t, rot, alt, 21 * u, sel ? tx::Weight::SemiBold : tx::Weight::Normal, cor(kc),
                tx::HAlign::Leading, tx::VAlign::Center);
        if (!it.detalhe.empty())
            x.texto(it.detalhe, l + 10 * u + rot, t, (r - l) - rot - 30 * u, alt, 14 * u, tx::Weight::Normal,
                    cor(it.ativo ? id::kTextoSuave : id::kTextoApagado), tx::HAlign::Trailing, tx::VAlign::Center);
        linhas_.push_back({int(i), l, t, r, b});
    }
    if (!v.dica.empty())
        x.texto(v.dica, cx + pad, cy + ch - pad - 22 * u, cw - 2 * pad, 24 * u, 14 * u, tx::Weight::Normal, cor(id::kTextoSuave));
    finalizar(alfa);
    return buf_;
}

const std::vector<uint32_t>& Desenho::camada_config(const MenuView& v, uint32_t w, uint32_t h, bool sobreTitulo, const Area& a,
                                                    float alfa) {
    std::string k = std::string(sobreTitulo ? "cfgT|" : "cfg|") + std::to_string(w) + "x" + std::to_string(h) + "@" +
                    std::to_string(int(a.y)) + "|" + v.title + "|" + v.prompt + "|" + v.help + "|" + std::to_string(v.selected) + "|" +
                    std::to_string(int(alfa * 64));
    for (const auto& i : v.items) k += "\x1e" + i.label + "\x1f" + i.value + (i.enabled ? "1" : "0") + (i.submenu ? "s" : "");
    if (k == chave_ && buf_.size() == size_t(w) * h) return buf_;
    chave_ = k, ++versao_;
    buf_.assign(size_t(w) * h, 0);
    linhas_.clear(), cortes_.clear();
    if (!w || !h) return buf_;
    tx::Canvas c(buf_, w, h);
    Ctx        x{c, tx::TextEngine::shared(), &cortes_};
    const float W = float(w), H = float(h), u = std::min(W / 1280.f, H / 720.f);
    if (sobreTitulo) c.fill_rect({0, 0, W, H}, cor(id::kFundo, 0.35f));  // o titulo animado continua a vista, sem o menu antigo
    else c.fill_rect({0, 0, W, H}, cor(id::kFundo, 0.80f));
    const float cw = std::min(820 * u, W - 48 * u), pad = 28 * u, alt = 40 * u;
    const float ch = H - 64 * u, cx = (W - cw) / 2, cy = 32 * u;
    c.fill_round_rect({cx, cy, cx + cw, cy + ch}, 14 * u, cor(id::kCartao, 0.97f));
    c.stroke_round_rect({cx, cy, cx + cw, cy + ch}, 14 * u, std::max(1.f, 1.5f * u), cor(id::kBorda));
    c.fill_round_rect({cx + pad, cy + 14 * u, cx + pad + 44 * u, cy + 18 * u}, 2 * u, cor(id::kAmbar));
    x.texto(v.title, cx + pad, cy + pad, cw - 2 * pad, 40 * u, 26 * u, tx::Weight::SemiBold, cor(id::kTexto));
    // rolagem: a selecao sempre visivel
    const float topo = cy + pad + 52 * u, base = cy + ch - pad - 36 * u;
    const int   cabe = std::max(1, int((base - topo) / (alt + 2 * u)));
    const int   n    = int(v.items.size());
    const int   ini  = std::clamp(v.selected - cabe / 2, 0, std::max(0, n - cabe));
    for (int i = ini; i < std::min(n, ini + cabe); ++i) {
        const auto& it  = v.items[size_t(i)];
        const bool  sel = i == v.selected;
        const float t = topo + float(i - ini) * (alt + 2 * u), b = t + alt, l = cx + pad - 10 * u, r = cx + cw - pad + 10 * u;
        if (sel) {
            c.fill_round_rect({l, t, r, b}, 7 * u, cor(id::kCartaoHover));
            c.stroke_round_rect({l, t, r, b}, 7 * u, std::max(1.f, 2 * u), cor(id::kAmbar));
        }
        const uint32_t kc  = !it.enabled ? id::kTextoApagado : sel ? id::kAmbar : id::kTexto;
        const float    rot = it.value.empty() ? (r - l) - 40 * u : (r - l) * 0.48f;
        x.texto(it.label, l + 10 * u, t, rot, alt, 18 * u, sel ? tx::Weight::SemiBold : tx::Weight::Normal, cor(kc),
                tx::HAlign::Leading, tx::VAlign::Center);
        Linha ln{i, l, t, r, b};
        if (it.submenu) {
            const float ax = r - 26 * u, ay = (t + b) / 2;
            c.fill_triangle(ax, ay - 6 * u, ax + 8 * u, ay, ax, ay + 6 * u, cor(kc));
        } else if (!it.value.empty()) {
            // "< valor >": as setas sao areas do mouse (diminuir e aumentar)
            const float     vr = r - 14 * u, vl = l + 10 * u + rot, aw = 22 * u;
            const tx::Color ka = cor(it.enabled ? (sel ? id::kAmbar : id::kTextoSuave) : id::kTextoApagado);
            if (it.enabled) {
                const float ay = (t + b) / 2;
                c.fill_triangle(vl + aw * 0.7f, ay - 6 * u, vl + aw * 0.7f, ay + 6 * u, vl + aw * 0.2f, ay, ka);
                c.fill_triangle(vr - aw * 0.7f, ay - 6 * u, vr - aw * 0.2f, ay, vr - aw * 0.7f, ay + 6 * u, ka);
                ln.decL = vl, ln.decR = vl + aw, ln.incL = vr - aw, ln.incR = vr;
            }
            x.texto(it.value, vl + aw, t, (vr - aw) - (vl + aw), alt, 17 * u, tx::Weight::Normal,
                    cor(it.enabled ? id::kTexto : id::kTextoApagado), tx::HAlign::Center, tx::VAlign::Center);
        }
        linhas_.push_back(ln);
    }
    if (!v.help.empty()) x.texto(v.help, cx + pad, cy + ch - pad - 24 * u, cw - 2 * pad, 26 * u, 14 * u, tx::Weight::Normal, cor(id::kTextoSuave));
    if (!v.prompt.empty()) {  // a confirmacao do modelo (tela, restaurar padroes) por cima
        const float pw = std::min(560 * u, cw - 40 * u), ph = 150 * u, px = (W - pw) / 2, py = (H - ph) / 2;
        c.fill_round_rect({px, py, px + pw, py + ph}, 12 * u, cor(id::kFundoAlto));
        c.stroke_round_rect({px, py, px + pw, py + ph}, 12 * u, std::max(1.f, 2 * u), cor(id::kAmbar));
        x.texto(v.prompt, px + 24 * u, py + 20 * u, pw - 48 * u, ph - 40 * u, 18 * u, tx::Weight::Normal, cor(id::kTexto),
                tx::HAlign::Center, tx::VAlign::Center, false);
    }
    finalizar(alfa);
    return buf_;
}

std::optional<int> Desenho::item_em(float px, float py) const {
    for (const auto& l : linhas_)
        if (px >= l.l && px < l.r && py >= l.t && py < l.b) return l.item;
    return std::nullopt;
}

std::optional<MenuHit> Desenho::config_em(float px, float py) const {
    for (const auto& l : linhas_)
        if (px >= l.l && px < l.r && py >= l.t && py < l.b) {
            MenuHit h;
            h.item = l.item;
            if (l.decR > l.decL && px >= l.decL && px < l.decR) h.part = MenuHit::Part::Decrease;
            else if (l.incR > l.incL && px >= l.incL && px < l.incR) h.part = MenuHit::Part::Increase;
            return h;
        }
    return std::nullopt;
}

} // namespace bs::remaster
