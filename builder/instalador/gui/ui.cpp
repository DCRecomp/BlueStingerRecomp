#include "instalador/gui/ui.h"

#include "instalador/gui/tema.h"

#include <algorithm>
#include <cmath>

namespace bs::builder::instalador::gui {

namespace {
enum : uint8_t { kBotao = 1, kCampo = 2 };
}

Id id(std::string_view nome, int indice) {
    uint32_t h = 2166136261u;
    for (unsigned char c : nome) h = (h ^ c) * 16777619u;
    h = (h ^ uint32_t(indice)) * 16777619u;
    return h ? h : 1;
}

void Ui::comecar(Pintor& p, const Entrada& e, double agora, bool menosAnimacao) {
    p_ = &p;
    entrada_ = e;
    ultimo_ = agora_ == 0 ? agora : agora_;
    agora_ = agora;
    menosAnim_ = menosAnimacao;
    animando_ = false;
    ordemAnterior_.swap(ordem_);
    ordem_.clear();
    tab_ = shiftTab_ = enter_ = espaco_ = esc_ = f1_ = false;
    seta_ = 0;
    enterFoco_ = enterPadrao_ = false;
    focoNovo_ = 0;
    // o que o componente com foco era no quadro anterior
    uint8_t tipoFoco = 0;
    int     grupoFoco = 0;
    size_t  idx = SIZE_MAX;
    for (size_t k = 0; k < ordemAnterior_.size(); ++k)
        if (ordemAnterior_[k].id == foco_) idx = k, grupoFoco = ordemAnterior_[k].grupo;
    if (idx != SIZE_MAX) tipoFoco = uint8_t(uint32_t(ordemAnterior_[idx].grupo) >> 24);
    grupoFoco &= 0xFFFFFF;
    auto mover = [&](int d) {
        if (ordemAnterior_.empty()) return;
        const size_t n = ordemAnterior_.size();
        size_t       k = idx == SIZE_MAX ? (d > 0 ? n - 1 : 0) : idx;
        k = (k + n + size_t(d)) % n;
        foco_ = ordemAnterior_[k].id;
        idx = k;
        grupoFoco = ordemAnterior_[k].grupo & 0xFFFFFF;
        tipoFoco = uint8_t(uint32_t(ordemAnterior_[k].grupo) >> 24);
    };
    auto moverGrupo = [&](int d) {
        std::vector<size_t> g;
        for (size_t k = 0; k < ordemAnterior_.size(); ++k)
            if ((ordemAnterior_[k].grupo & 0xFFFFFF) == grupoFoco) g.push_back(k);
        const auto it = std::find(g.begin(), g.end(), idx);
        if (g.empty() || it == g.end()) return;
        const size_t pos = size_t(it - g.begin()), n = g.size();
        idx = g[(pos + n + size_t(d)) % n];
        foco_ = focoNovo_ = ordemAnterior_[idx].id;
    };
    if (e.apertou) teclado_ = false;  // anel de foco so na navegacao pelo teclado
    for (Tecla t : e.teclas) {
        if (t != Tecla::Esc && t != Tecla::F1) teclado_ = true;
        switch (t) {
        case Tecla::Tab: mover(+1); break;
        case Tecla::ShiftTab: mover(-1); break;
        case Tecla::Enter: enter_ = true; break;
        case Tecla::Espaco: espaco_ = tipoFoco != kCampo; break;
        case Tecla::Esc: esc_ = true; break;
        case Tecla::F1: f1_ = true; break;
        case Tecla::Cima:
        case Tecla::Baixo:
        case Tecla::Esquerda:
        case Tecla::Direita: {
            const int  d = (t == Tecla::Cima || t == Tecla::Esquerda) ? -1 : +1;
            const bool horizontal = t == Tecla::Esquerda || t == Tecla::Direita;
            if (tipoFoco == kCampo && horizontal) break;  // o campo usa as setas laterais
            if (grupoFoco != 0) moverGrupo(d);
            else if (!horizontal) mover(d);
            break;
        }
        default: break;
        }
    }
    // Enter com foco num botao e dele; senao, do botao padrao da tela
    enterFoco_   = enter_ && tipoFoco == kBotao;
    enterPadrao_ = enter_ && tipoFoco != kBotao;
    if (e.apertou) pressionado_ = 0;
}

void Ui::terminar() {
    if (std::none_of(ordem_.begin(), ordem_.end(), [&](const Focavel& f) { return f.id == foco_; })) {
        if (!focoPedido_) foco_ = 0;
    }
    focoPedido_ = false;
    if (entrada_.soltou) pressionado_ = 0;
}

float Ui::suave(Id i, float alvo, double duracao) {
    auto [it, novo] = suaves_.try_emplace(i, alvo);
    if (novo || menosAnim_ || duracao <= 0) return it->second = alvo;
    const float passo = float(std::max(0.0, agora_ - ultimo_) / duracao);
    float&      v = it->second;
    if (v < alvo) v = std::min(alvo, v + passo);
    else if (v > alvo) v = std::max(alvo, v - passo);
    if (v != alvo) animando_ = true;
    return v;
}

Medida Ui::rotulo(std::wstring_view t, const Estilo& e, const Ret& r, const Cor& c) {
    const Medida m = p_->medir(t, e, e.quebra ? r.w : 0);
    if (m.h > r.h + 0.75f) cortes.push_back({contexto, std::wstring(t), m.h, r.h});
    else if (m.w > r.w + 0.75f) cortes.push_back({contexto, std::wstring(t), m.w, r.w});
    p_->texto(t, e, r, c);
    return m;
}

float Ui::altura_texto(std::wstring_view t, const Estilo& e, float largura) { return p_->medir(t, e, largura).h; }

std::wstring Ui::elidir(std::wstring_view t, const Estilo& e, float largura) {
    Estilo um = e;
    um.quebra = false;
    if (p_->medir(t, um, 0).w <= largura) return std::wstring(t);
    // busca binaria no tanto que sai do meio
    size_t lo = 1, hi = t.size(), melhor = t.size();
    auto   montar = [&](size_t corte) {
        const size_t fica = t.size() - std::min(corte, t.size()), ini = (fica + 1) / 2, fim = fica / 2;
        return std::wstring(t.substr(0, ini)) + L"…" + std::wstring(t.substr(t.size() - fim));
    };
    while (lo <= hi) {
        const size_t m = (lo + hi) / 2;
        if (p_->medir(montar(m), um, 0).w <= largura) melhor = m, hi = m - 1;
        else lo = m + 1;
    }
    return montar(melhor);
}

void Ui::anel(const Ret& r, float raio) {
    if (!teclado_) return;
    p_->contorno({r.x - 3, r.y - 3, r.w + 6, r.h + 6}, raio + 3, tema::kFocoEspessura, tema::kFoco);
}

bool Ui::interagir(Id i, int grupo, const Ret& r, bool habilitado, bool aceitaEspaco) {
    if (!habilitado) return false;
    ordem_.push_back({i, grupo});
    bool ativo = false;
    if (entrada_.apertou && hover(r)) pressionado_ = i, foco_ = i;
    if (entrada_.soltou && pressionado_ == i && hover(r)) ativo = true;
    if (foco_ == i) {
        if (aceitaEspaco && espaco_) ativo = true, espaco_ = false;
        if ((uint32_t(grupo) >> 24) == kBotao && enterFoco_) ativo = true, enterFoco_ = false;
    }
    if (focoNovo_ == i) ativo = true;
    return ativo;
}

bool Ui::botao(Id i, std::wstring_view t, const Ret& r, TipoBotao tipo, bool habilitado, bool padrao) {
    bool ativo = interagir(i, int(uint32_t(kBotao) << 24), r, habilitado);
    if (habilitado && padrao && enterPadrao_) ativo = true, enterPadrao_ = false;
    const float h = suave(i, habilitado && hover(r) ? 1.f : 0.f, tema::kHover);
    Cor fundo, texto;
    switch (tipo) {
    case TipoBotao::Primario: fundo = misturar(tema::kAmbar, rgb(0xFFC165), h), texto = tema::kTextoSobreAmbar; break;
    case TipoBotao::Perigo: fundo = misturar(tema::kCoral, rgb(0xFF8A7C), h), texto = rgb(0x1E0806); break;
    case TipoBotao::Secundario: fundo = misturar(tema::kCartao, tema::kCartaoHover, h), texto = tema::kTexto; break;
    case TipoBotao::Texto: fundo = tema::kCartaoHover.com_alfa(h * 0.8f), texto = tema::kAmbar; break;
    }
    if (!habilitado) fundo = tema::kCartao.com_alfa(0.6f), texto = tema::kTextoApagado;
    if (pressionado_ == i && hover(r)) fundo = misturar(fundo, rgb(0x000000), 0.15f);
    p_->arredondado(r, tema::kRaio, fundo);
    if (tipo == TipoBotao::Secundario) p_->contorno(r, tema::kRaio, 1, tema::kBorda);
    Estilo e;
    e.tamanho = tema::kCorpo;
    e.peso    = tipo == TipoBotao::Texto ? Peso::Normal : Peso::SemiNegrito;
    e.h = e.v = Alinha::Centro;
    e.quebra  = false;
    rotulo(t, e, r.recuo(10, 0), texto);
    if (focado(i)) anel(r, tema::kRaio);
    return ativo;
}

bool Ui::caixa(Id i, std::wstring_view t, std::wstring_view dica, const Ret& r, bool& valor, bool habilitado) {
    const bool ativo = interagir(i, 0, r, habilitado);
    if (ativo) valor = !valor;
    const float h = suave(i, habilitado && hover(r) ? 1.f : 0.f, tema::kHover);
    const Ret   q{r.x, r.y + 2, 18, 18};
    p_->arredondado(q, 4, valor ? tema::kAmbar : misturar(tema::kCartao, tema::kCartaoHover, h));
    p_->contorno(q, 4, 1.25f, valor ? tema::kAmbar : tema::kBorda);
    if (valor) {
        // visto desenhado com dois tracos
        const float s = q.w;
        auto        tr = [&](float x0, float y0, float x1, float y1) {
            const float dx = x1 - x0, dy = y1 - y0, n = std::sqrt(dx * dx + dy * dy), e = s * 0.13f;
            const float nx = -dy / n * e, ny = dx / n * e;
            p_->triangulo(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, tema::kTextoSobreAmbar);
            p_->triangulo(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, tema::kTextoSobreAmbar);
        };
        tr(q.x + s * 0.2f, q.y + s * 0.52f, q.x + s * 0.42f, q.y + s * 0.74f);
        tr(q.x + s * 0.4f, q.y + s * 0.74f, q.x + s * 0.8f, q.y + s * 0.28f);
    }
    Estilo e;
    e.tamanho = tema::kCorpo;
    const Cor  ct = habilitado ? tema::kTexto : tema::kTextoApagado;
    const float tx = r.x + 30, tw = r.w - 30;
    const float th = altura_texto(t, e, tw);
    rotulo(t, e, {tx, r.y, tw, th}, ct);
    if (!dica.empty()) {
        Estilo d;
        d.tamanho = tema::kPequeno;
        rotulo(dica, d, {tx, r.y + th + 2, tw, r.h - th - 2}, tema::kTextoSuave);
    }
    if (focado(i)) anel({r.x - 2, r.y, r.w + 4, r.h}, 4);
    return ativo;
}

bool Ui::opcao(Id i, int grupo, std::wstring_view t, std::wstring_view dica, const Ret& r, bool marcado, bool habilitado) {
    const bool  ativo = interagir(i, grupo, r, habilitado);
    const float h = suave(i, habilitado && hover(r) ? 1.f : 0.f, tema::kHover);
    const float cx = r.x + 9, cy = r.y + 11;
    p_->arredondado({cx - 9, cy - 9, 18, 18}, 9, misturar(tema::kCartao, tema::kCartaoHover, h));
    p_->contorno({cx - 9, cy - 9, 18, 18}, 9, 1.25f, marcado ? tema::kAmbar : tema::kBorda);
    if (marcado) p_->arredondado({cx - 5, cy - 5, 10, 10}, 5, tema::kAmbar);
    Estilo e;
    e.tamanho = tema::kCorpo;
    const float tx = r.x + 30, tw = r.w - 30, th = altura_texto(t, e, tw);
    rotulo(t, e, {tx, r.y + 1, tw, th}, habilitado ? tema::kTexto : tema::kTextoApagado);
    if (!dica.empty()) {
        Estilo d;
        d.tamanho = tema::kPequeno;
        rotulo(dica, d, {tx, r.y + 1 + th + 2, tw, r.h - th - 3}, tema::kTextoSuave);
    }
    if (focado(i)) anel({r.x - 2, r.y, r.w + 4, r.h}, 4);
    return ativo;
}

bool Ui::cartao(Id i, int grupo, const Ret& r, bool marcado, const Cor& destaque, const std::function<void(const Ret&)>& conteudo) {
    const bool  ativo = interagir(i, grupo, r, true);
    const float h = suave(i, hover(r) ? 1.f : 0.f, tema::kHover);
    const float m = suave(i ^ 0x5A5A5A5Au, marcado ? 1.f : 0.f, tema::kTransicao);
    p_->arredondado(r, 12, misturar(tema::kCartao, tema::kCartaoHover, std::max(h, m * 0.6f)));
    p_->contorno(r, 12, 1 + m, misturar(tema::kBorda, destaque, m));
    if (conteudo) conteudo(r);
    if (focado(i)) anel(r, 12);
    return ativo;
}

bool Ui::campo(Id i, std::wstring& valor, const Ret& r, std::wstring_view vazio) {
    const bool clicou = interagir(i, int(uint32_t(kCampo) << 24), r, true, false);
    Cursor&    c = cursores_[i];
    c.pos = std::min(c.pos, valor.size());
    bool mudou = false;
    if (clicou) c.pos = valor.size(), c.tudo = false;
    if (focado(i)) {
        auto apagarTudo = [&] {
            if (c.tudo) valor.clear(), c.pos = 0, c.tudo = false, mudou = true;
        };
        for (Tecla t : entrada_.teclas) {
            switch (t) {
            case Tecla::Esquerda: c.tudo = false; if (c.pos) --c.pos; break;
            case Tecla::Direita: c.tudo = false; if (c.pos < valor.size()) ++c.pos; break;
            case Tecla::Inicio: c.tudo = false, c.pos = 0; break;
            case Tecla::Fim: c.tudo = false, c.pos = valor.size(); break;
            case Tecla::SelecionarTudo: c.tudo = !valor.empty(); break;
            case Tecla::Espaco:  // a janela do Windows manda o espaco como tecla
                apagarTudo();
                valor.insert(c.pos++, 1, L' '), mudou = true;
                break;
            case Tecla::Backspace:
                if (c.tudo) apagarTudo();
                else if (c.pos) valor.erase(--c.pos, 1), mudou = true;
                break;
            case Tecla::Delete:
                if (c.tudo) apagarTudo();
                else if (c.pos < valor.size()) valor.erase(c.pos, 1), mudou = true;
                break;
            case Tecla::Colar: {
                apagarTudo();
                std::wstring s;
                for (wchar_t ch : entrada_.colado)
                    if (ch != L'\r' && ch != L'\n' && ch != L'\t') s += ch;
                // um caminho colado do Explorer vem entre aspas
                if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') s = s.substr(1, s.size() - 2);
                valor.insert(c.pos, s), c.pos += s.size(), mudou = mudou || !s.empty();
                break;
            }
            default: break;
            }
        }
        for (wchar_t ch : entrada_.texto) {
            if (ch < 0x20 || ch == 0x7F) continue;
            apagarTudo();
            valor.insert(c.pos++, 1, ch), mudou = true;
        }
    }
    p_->arredondado(r, 6, rgb(0x0A101A));
    p_->contorno(r, 6, focado(i) ? 1.5f : 1.f, focado(i) ? tema::kAmbar : tema::kBorda);
    Estilo e;
    e.tamanho = tema::kCorpo;
    e.v = Alinha::Centro;
    e.quebra = false;
    const Ret dentro = r.recuo(10, 0);
    p_->recortar(dentro);
    if (valor.empty() && !vazio.empty()) {
        p_->texto(vazio, e, dentro, tema::kTextoApagado);
    } else {
        // rolagem: o cursor sempre visivel (o campo rola, nao corta: fora da prova de cortes)
        const float antes = p_->medir(std::wstring_view(valor).substr(0, c.pos), e, 0).w;
        const float total = p_->medir(valor, e, 0).w;
        float       off = 0;
        if (total > dentro.w) off = std::clamp(antes - dentro.w + 8, 0.f, total - dentro.w + 2);
        if (c.tudo && focado(i)) p_->retangulo({dentro.x - off, dentro.y + 8, std::min(total, dentro.w + off), dentro.h - 16},
                                               tema::kAmbar.com_alfa(0.35f));
        p_->texto(valor, e, {dentro.x - off, dentro.y, total + 4, dentro.h}, tema::kTexto);
        if (focado(i)) p_->retangulo({dentro.x - off + antes, dentro.y + 9, 1.5f, dentro.h - 18}, tema::kAmbar);
    }
    p_->soltar_recorte();
    return mudou;
}

void Ui::barra(const Ret& r, double fracao, const Cor& c, bool ocupada) {
    p_->arredondado(r, r.h / 2, tema::kCartao);
    const float alvo = float(std::clamp(fracao, 0.0, 1.0));
    const float f = suave(id("barra", int(r.y)), alvo, 0.35);
    if (f > 0) p_->arredondado({r.x, r.y, std::max(r.h, r.w * f), r.h}, r.h / 2, c);
    if (ocupada && !menosAnim_ && f > 0) {  // brilho que corre na parte feita: a etapa esta viva
        const float w = std::max(r.h, r.w * f), x = float(std::fmod(agora_ * 0.6, 1.0)) * (w + 80) - 80;
        p_->recortar({r.x, r.y, w, r.h});
        p_->retangulo({r.x + x, r.y, 80, r.h}, rgb(0xFFFFFF, 0.18f));
        p_->soltar_recorte();
        animando_ = true;
    }
}

} // namespace bs::builder::instalador::gui
