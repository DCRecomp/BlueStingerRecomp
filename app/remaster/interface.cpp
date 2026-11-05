#include "remaster/interface.h"

#include <algorithm>
#include <ctime>

namespace bs::remaster {

std::optional<Perfil> Interface::mais_recente(const std::vector<Perfil>& perfis) {
    std::optional<Perfil> r;
    for (const auto& p : perfis)
        if (p.valido && (!r || p.gravado > r->gravado || (p.gravado == r->gravado && p.arquivo < r->arquivo))) r = p;
    return r;
}

std::string Interface::data_texto(int64_t unix, bool ingles) {
    if (unix <= 0) return ingles ? "date unknown" : "data desconhecida";
    const std::time_t t = std::time_t(unix);
    std::tm           tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    // pt-BR: dd/mm/aaaa hh:mm; en-US: aaaa-mm-dd hh:mm (sem ambiguidade de dia e mes)
    if (ingles) std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", &tm);
    else std::strftime(b, sizeof b, "%d/%m/%Y %H:%M", &tm);
    return b;
}

std::string Interface::rotulo_perfil(const Perfil& p) const {
    return std::string(L("Perfil ", "Profile ")) + std::to_string(p.arquivo + 1);
}

bool Interface::tem_perfil() const { return mais_recente(perfis_).has_value(); }

void Interface::ir(Tela t) {
    pilha_.push_back({tela_, sel_});
    tela_ = t;
    sel_  = 0;
    const auto es = entradas();  // a primeira opcao ativa
    for (size_t i = 0; i < es.size(); ++i)
        if (es[i].ativo) {
            sel_ = int(i);
            break;
        }
}

void Interface::abrir_inicial(std::vector<Perfil> perfis) {
    perfis_ = std::move(perfis);
    pilha_.clear();
    origem_ = tela_ = Tela::Inicial;
    saida_.reset();
    // Continuar fica selecionado quando existe um perfil (o caso comum de quem volta ao jogo)
    sel_ = tem_perfil() ? 1 : 0;
}

void Interface::abrir_pausa(std::vector<Perfil> perfis) {
    perfis_ = std::move(perfis);
    pilha_.clear();
    origem_ = tela_ = Tela::Pausa;
    saida_.reset();
    sel_ = 0;
}

void Interface::aguardar(const std::string& texto) {
    aviso_ = texto;
    tela_  = Tela::Aguarde;
    sel_   = 0;
}

void Interface::erro(const std::string& texto) {
    aviso_ = texto;
    tela_  = Tela::Erro;
    sel_   = 0;
}

void Interface::fechar() {
    tela_ = origem_ = Tela::Fechada;
    pilha_.clear();
}

std::vector<Interface::Entrada> Interface::entradas() const {
    std::vector<Entrada> r;
    const auto           recente = mais_recente(perfis_);
    switch (tela_) {
    case Tela::Inicial:
        r.push_back({Item::NovoJogo, L("Novo jogo", "New game"), ""});
        r.push_back({Item::Continuar, L("Continuar", "Continue"),
                     recente ? rotulo_perfil(*recente) + "  \xC2\xB7  " + data_texto(recente->gravado, ingles_)
                             : L("Nenhum perfil gravado", "No saved profile"),
                     bool(recente)});
        r.push_back({Item::Carregar, L("Carregar", "Load"), "", tem_perfil()});
        r.push_back({Item::Configuracoes, L("Configura\xC3\xA7\xC3\xB5" "es", "Settings"), ""});
        r.push_back({Item::Extras, L("Extras", "Extras"), ""});
        r.push_back({Item::Sair, L("Sair", "Quit"), ""});
        break;
    case Tela::Pausa:
        r.push_back({Item::Retomar, L("Continuar", "Resume"), ""});
        r.push_back({Item::Salvar, L("Salvar", "Save"), L("Nos pontos de grava\xC3\xA7\xC3\xA3o do jogo", "At the game's save points"), false});
        r.push_back({Item::Carregar, L("Carregar", "Load"), "", tem_perfil()});
        r.push_back({Item::Configuracoes, L("Configura\xC3\xA7\xC3\xB5" "es", "Settings"), ""});
        r.push_back({Item::MenuPrincipal, L("Menu principal", "Main menu"), "", true, true});
        r.push_back({Item::Sair, L("Sair do jogo", "Quit game"), "", true, true});
        break;
    case Tela::Perfis:
        for (const auto& p : perfis_) {
            Entrada e{Item::Perfil, rotulo_perfil(p),
                      p.valido ? data_texto(p.gravado, ingles_) : L("Arquivo danificado", "Damaged file"), p.valido};
            e.arquivo = p.arquivo;
            r.push_back(e);
        }
        r.push_back({Item::Voltar, L("Voltar", "Back"), ""});
        break;
    case Tela::Confirmar:
        r.push_back({Item::Sim, L("Sim, continuar", "Yes, continue"), "", true, true});
        r.push_back({Item::Nao, L("Cancelar", "Cancel"), ""});
        break;
    case Tela::Extras:
    case Tela::Erro: r.push_back({Item::Voltar, L("Voltar", "Back"), ""}); break;
    default: break;
    }
    return r;
}

void Interface::confirmar_entrada(const Entrada& e) {
    if (!e.ativo) return;
    const bool pausa = origem_ == Tela::Pausa;
    auto       destrutivo = [&](Saida s, const char* pt, const char* en) {
        if (!pausa) return void(saida_ = s);  // no menu inicial nao ha partida a perder
        aConfirmar_ = s;
        pergunta_   = L(pt, en);
        ir(Tela::Confirmar);
    };
    switch (e.item) {
    case Item::NovoJogo: saida_ = Saida{Pedido::NovoJogo}; break;
    case Item::Continuar:
        if (const auto p = mais_recente(perfis_)) saida_ = Saida{Pedido::Carregar, p->arquivo};
        break;
    case Item::Carregar: ir(Tela::Perfis); break;
    case Item::Perfil:
        destrutivo(Saida{Pedido::Carregar, e.arquivo},
                   "Carregar este perfil? O progresso desde o \xC3\xBAltimo save ser\xC3\xA1 perdido.",
                   "Load this profile? Progress since the last save will be lost.");
        break;
    case Item::Configuracoes: saida_ = Saida{Pedido::Configuracoes}; break;
    case Item::Extras: ir(Tela::Extras); break;
    case Item::Retomar: saida_ = Saida{Pedido::Retomar}; break;
    case Item::MenuPrincipal:
        destrutivo(Saida{Pedido::MenuPrincipal}, "Voltar ao menu principal? O progresso desde o \xC3\xBAltimo save ser\xC3\xA1 perdido.",
                   "Return to the main menu? Progress since the last save will be lost.");
        break;
    case Item::Sair:
        destrutivo(Saida{Pedido::Sair}, "Sair do jogo? O progresso desde o \xC3\xBAltimo save ser\xC3\xA1 perdido.",
                   "Quit the game? Progress since the last save will be lost.");
        break;
    case Item::Sim: saida_ = aConfirmar_; break;
    case Item::Nao:
    case Item::Voltar: input(MenuAction::Back); break;
    case Item::Salvar: break;
    }
}

void Interface::input(MenuAction a) {
    if (tela_ == Tela::Fechada || tela_ == Tela::Aguarde) return;
    const auto es = entradas();
    const int  n  = int(es.size());
    switch (a) {
    case MenuAction::Up:
    case MenuAction::Down: {
        if (!n) return;
        const int d = a == MenuAction::Up ? -1 : 1;
        for (int k = 1; k <= n; ++k) {  // pula as desativadas; da volta no fim
            const int i = ((sel_ + d * k) % n + n) % n;
            if (es[size_t(i)].ativo) {
                sel_ = i;
                break;
            }
        }
        break;
    }
    case MenuAction::Confirm:
        if (sel_ >= 0 && sel_ < n) confirmar_entrada(es[size_t(sel_)]);
        break;
    case MenuAction::Back:
        if (tela_ == Tela::Erro) {  // o erro volta a tela de origem (Inicial ou Pausa)
            pilha_.clear();
            tela_ = origem_, sel_ = 0;
            return;
        }
        if (pilha_.empty()) {
            if (tela_ == Tela::Pausa) saida_ = Saida{Pedido::Retomar};  // o menu inicial nao fecha
            return;
        }
        tela_ = pilha_.back().first, sel_ = pilha_.back().second;
        pilha_.pop_back();
        break;
    default: break;  // Left/Right: nada nestas telas
    }
}

void Interface::selecionar(int i) {
    if (tela_ == Tela::Aguarde) return;
    const auto es = entradas();
    if (i >= 0 && i < int(es.size()) && es[size_t(i)].ativo) sel_ = i;
}

std::optional<Saida> Interface::tomar() {
    auto s = saida_;
    saida_.reset();
    return s;
}

Vista Interface::vista() const {
    Vista v;
    v.tela      = tela_;
    v.sobreTitulo = origem_ == Tela::Inicial;
    for (const auto& e : entradas()) v.itens.push_back({e.rotulo, e.detalhe, e.ativo, e.destrutivo});
    v.selecionado = sel_;
    const char* nav = L("Enter ou A: escolher   \xC2\xB7   Esc ou B: voltar", "Enter or A: select   \xC2\xB7   Esc or B: back");
    v.dica          = nav;
    switch (tela_) {
    case Tela::Inicial:
        v.titulo    = "BLUE STINGER";
        v.subtitulo = "Remastered";
        v.dica      = L("Enter ou A: escolher", "Enter or A: select");
        break;
    case Tela::Pausa:
        v.titulo = L("Pausa", "Paused");
        v.dica   = L("Enter ou A: escolher   \xC2\xB7   Esc ou B: continuar", "Enter or A: select   \xC2\xB7   Esc or B: resume");
        break;
    case Tela::Perfis:
        v.titulo    = L("Carregar", "Load");
        v.subtitulo = L("Escolha o perfil", "Choose a profile");
        break;
    case Tela::Confirmar:
        v.titulo = L("Confirmar", "Confirm");
        v.texto  = pergunta_;
        break;
    case Tela::Extras:
        v.titulo = L("Extras", "Extras");
        v.texto  = std::string(L("Blue Stinger Recompiled: projeto comunit\xC3\xA1rio e de c\xC3\xB3" "digo aberto (MIT). O jogo "
                                 "vem do disco do pr\xC3\xB3prio jogador; nada dele \xC3\xA9 distribu\xC3\xAD" "do pelo projeto. "
                                 "Blue Stinger \xC3\xA9 marca do seu titular.",
                                 "Blue Stinger Recompiled: an open-source community project (MIT). The game comes from the "
                                 "player's own disc; nothing from it is distributed by the project. Blue Stinger is a trademark "
                                 "of its owner.")) +
                   (versao_.empty() ? "" : std::string("\n\n") + L("Vers\xC3\xA3o ", "Version ") + versao_);
        break;
    case Tela::Aguarde:
        v.titulo = L("Aguarde", "Please wait");
        v.texto  = aviso_;
        v.dica.clear();
        break;
    case Tela::Erro:
        v.titulo = L("N\xC3\xA3o foi poss\xC3\xADvel", "Something went wrong");
        v.texto  = aviso_;
        break;
    default: break;
    }
    return v;
}

} // namespace bs::remaster
