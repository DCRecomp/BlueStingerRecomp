#include "remaster/entrada_titulo.h"

#include "cpu/sh4_context.h"
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "mods/mod_api.h"
#include "time_adapter.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace bs::remaster {

namespace e = enderecos;

namespace {

// Quantos quadros o load pode levar antes de desistir (o painel original leva uns 50 com o cartao nativo)
constexpr uint32_t kLimiteLoad = 900;

// Chamada nativa a uma rotina do jogo dentro do quadro do titulo, como o BS_CALL do codigo gerado: retorno esperado
// coerente, argumentos em r4-r6 e os registradores do chamador (a espera do VBlank, que volta ao laco do titulo)
// restaurados depois. O tempo (ciclos, eventos, polls) segue: nao e restaurado.
uint32_t chamar(Sh4Context* c, uint32_t addr, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0) {
    const Sh4Func f = sh4_lookup(addr);
    if (!f) throw std::runtime_error("remaster: rotina do jogo ausente");
    uint32_t r[16], rbank[8], fr[16], xf[16];
    std::memcpy(r, c->r, sizeof r);
    std::memcpy(rbank, c->rbank, sizeof rbank);
    std::memcpy(fr, c->fr, sizeof fr);
    std::memcpy(xf, c->xf, sizeof xf);
    const uint32_t sr = c->sr, t = c->t, gbr = c->gbr, pr = c->pr, mach = c->mach, macl = c->macl, fpscr = c->fpscr,
                   fpul = c->fpul, retE = c->ret_expect, retS = c->ret_sp, fn = c->cur_fn, pc = c->pc;
    c->r[4] = a0, c->r[5] = a1, c->r[6] = a2;
    c->pr = c->ret_expect = e::kRetornoLacoTit;
    c->ret_sp             = c->r[15];
    f(c);
    const uint32_t r0 = c->r[0];
    if (c->r[15] != r[15] || c->redirect) throw std::runtime_error("remaster: pilha ou longjmp inesperado numa chamada nativa");
    std::memcpy(c->r, r, sizeof r);
    std::memcpy(c->rbank, rbank, sizeof rbank);
    std::memcpy(c->fr, fr, sizeof fr);
    std::memcpy(c->xf, xf, sizeof xf);
    c->sr = sr, c->t = t, c->gbr = gbr, c->pr = pr, c->mach = mach, c->macl = macl, c->fpscr = fpscr, c->fpul = fpul;
    c->ret_expect = retE, c->ret_sp = retS, c->cur_fn = fn, c->pc = pc;
    // o ponto de poll que o BS_CALL tem no retorno (interrupcoes e eventos vencidos), no endereco de continuacao do laco do
    // titulo e com os registradores do chamador ja restaurados (revisao Codex 20261104-222855)
    mods::poll(c, e::kRetornoLacoTit, 0);
    if (c->redirect) throw std::runtime_error("remaster: longjmp pendente depois de uma chamada nativa");
    return r0;
}

Sh4Func g_esperaOriginal = nullptr;  // a funcao gerada da espera (no produto nao ha slots: mods::original nao a conhece)

void envoltorio(Sh4Context* c) {
    const uint32_t retorno = c->pr;  // quem chamou a espera (o laco do titulo volta a 8C078536)
    g_esperaOriginal(c);
    if (retorno == e::kRetornoLacoTit) EntradaTitulo::get().quadro(c);
}

// Estado 5 -> 6: o START do "PRESS START" (8C079148): efeito 90 e o menu
void abrir_menu(Sh4Context* c) {
    chamar(c, e::kEfeito, 90, 0, 0);
    sh4_write16(c, e::kEstado, 6);
}

} // namespace

const char* acao_nome(Acao a) {
    switch (a) {
    case Acao::AbrirMenu: return "menu";
    case Acao::NovoJogo: return "novojogo";
    case Acao::Carregar: return "carregar";
    default: return "nenhuma";
    }
}

bool acao_de_nome(const std::string& n, Acao& out) {
    for (Acao a : {Acao::AbrirMenu, Acao::NovoJogo, Acao::Carregar})
        if (n == acao_nome(a)) return out = a, true;
    return false;
}

EntradaTitulo& EntradaTitulo::get() {
    static EntradaTitulo t;
    return t;
}

void EntradaTitulo::instalar(std::vector<Evento> eventos, std::function<void(const Evento&)> aplicado, bool trocarMenu) {
    {
        std::lock_guard l(m_);
        roteiro_     = std::move(eventos);
        proximo_     = 0;
        reproducao_  = !roteiro_.empty();
        aplicado_    = std::move(aplicado);
        temPendente_ = false;
        emCurso_     = false;
    }
    noTitulo_.store(false), ocupado_.store(false), estado_.store(0), quadros_.store(0), resultado_.store(0);
    g_esperaOriginal = sh4_lookup_generated(e::kEsperaVblank);
    if (!g_esperaOriginal) throw std::runtime_error("remaster: a espera do VBlank nao foi gerada");
    if (!mods::replace(e::kEsperaVblank, &envoltorio)) throw std::runtime_error("remaster: a espera do VBlank nao e substituivel");
    // o menu original nao e desenhado: o nativo fica no lugar dele (nada por cima, nada do antigo por tras)
    for (uint32_t a : {e::kDesenhoPressStart, e::kDesenhoOpcoes, e::kDesenhoDestaque})
        if (trocarMenu && !mods::replace(a, [](Sh4Context*) {})) throw std::runtime_error("remaster: o desenho do menu original nao e substituivel");
}

void EntradaTitulo::desinstalar() {
    for (uint32_t a : {e::kEsperaVblank, e::kDesenhoPressStart, e::kDesenhoOpcoes, e::kDesenhoDestaque}) mods::replace(a, nullptr);
    std::lock_guard l(m_);
    temPendente_ = emCurso_ = false;
}

void EntradaTitulo::pedir(Acao a, int arquivo) {
    std::lock_guard l(m_);
    if (reproducao_) return;
    pendente_    = {0, a, arquivo};
    temPendente_ = true;
    resultado_.store(0);
}

EstadoTitulo EntradaTitulo::estado() const {
    EstadoTitulo s;
    s.noTitulo  = noTitulo_.load();
    s.estado    = estado_.load();
    s.quadros   = quadros_.load();
    s.ocupado   = ocupado_.load();
    s.resultado = resultado_.load();
    return s;
}

void EntradaTitulo::quadro(Sh4Context* c) {
    const uint16_t estado = sh4_read16(c, e::kEstado);
    estado_.store(estado), noTitulo_.store(true), quadros_.fetch_add(1);
    if (!emCurso_) {
        std::lock_guard l(m_);
        const uint64_t passo = transitional::time::game_steps();
        if (reproducao_) {
            if (proximo_ < roteiro_.size() && passo >= roteiro_[proximo_].passo) {
                atual_ = roteiro_[proximo_++], emCurso_ = true;
            }
        } else if (temPendente_) {
            // so comeca onde o original aceitaria: no "PRESS START" ou no menu
            if (estado == 5 || estado == 6) {
                atual_ = pendente_, atual_.passo = passo, temPendente_ = false, emCurso_ = true;
            }
        }
        if (emCurso_) {
            etapa_ = 0, quadrosNaEtapa_ = 0;
            ocupado_.store(true);
            if (aplicado_) aplicado_(atual_);
            std::fprintf(stderr, "[remaster] %s%s no passo %llu (titulo no estado %u)\n", acao_nome(atual_.acao),
                         atual_.acao == Acao::Carregar ? (" " + std::to_string(atual_.arquivo)).c_str() : "",
                         static_cast<unsigned long long>(atual_.passo), unsigned(estado));
        }
    }
    const bool terminou = emCurso_ && aplicar(c, atual_);
    estado_.store(sh4_read16(c, e::kEstado));  // o estado depois da acao deste quadro, antes de soltar o "ocupado"
    if (terminou) emCurso_ = false, ocupado_.store(false);
}

// Uma etapa por quadro do titulo; true = terminou.
bool EntradaTitulo::aplicar(Sh4Context* c, const Evento& ev) {
    const uint16_t estado = sh4_read16(c, e::kEstado);
    ++quadrosNaEtapa_;
    if (etapa_ == 0) {  // o menu original tem de estar aberto (estado 6); do "PRESS START" ele abre como o START faz
        if (estado == 5) {
            abrir_menu(c);
            if (ev.acao == Acao::AbrirMenu) return resultado_.store(1), true;
            return false;  // a confirmacao vem no quadro seguinte, como no original
        }
        if (estado != 6) {
            if (quadrosNaEtapa_ > 600) return resultado_.store(-1), true;  // o titulo nao chegou ao menu
            return false;
        }
        if (ev.acao == Acao::AbrirMenu) return resultado_.store(1), true;
        etapa_ = 1, quadrosNaEtapa_ = 0;
    }
    if (ev.acao == Acao::NovoJogo) {
        // estado 6, confirmacao com o cursor em NEW GAME (8C079166 e 8C079430)
        chamar(c, e::kEfeito, 90, 0, 0);
        sh4_write16(c, e::kCursor, 0);
        chamar(c, e::kNovoJogoIni, 0);
        sh4_write16(c, e::kEstado, 7);
        sh4_write8(c, 0x8C24D2AA, 1);
        sh4_write16(c, 0x8C24D29E, 8);
        sh4_write16(c, 0x8C24D2A0, 0x00FF);
        sh4_write32(c, 0x8C24D298, 0xBF800000u);  // -1.0f
        resultado_.store(1);
        return true;
    }
    // Carregar: a rotina de load do jogo com o arquivo pedido, a cada quadro ate concluir (7 trabalhando, 8 concluido)
    if (etapa_ == 1) {
        chamar(c, e::kEfeito, 90, 0, 0);
        sh4_write16(c, e::kCursor, 1);
        etapa_ = 2, quadrosNaEtapa_ = 0;
    }
    const uint32_t r = chamar(c, e::kCarregar, 0, uint32_t(ev.arquivo), 0x22);
    if (r == 7 && quadrosNaEtapa_ < kLimiteLoad) return false;
    if (r != 8) {  // falhou ou nao terminou: a maquina do load volta ao inicio, o titulo continua no menu
        sh4_write32(c, 0x8C1F3858, 0);
        sh4_write8(c, 0x8C1F3A60, 0);
        std::fprintf(stderr, "[remaster] carregar %d falhou (retorno %u em %u quadros)\n", ev.arquivo, r, quadrosNaEtapa_);
        resultado_.store(-1);
        return true;
    }
    // o fim do painel no estado 9 (8C07930A): o save serve (12, a partida carregada) ou nao (11, volta ao menu)
    const bool serve = chamar(c, e::kVersaoSave) == sh4_read32(c, e::kVersaoEsperada);
    sh4_write16(c, e::kFade, 0xC0);
    sh4_write16(c, e::kEstado, serve ? 12 : 11);
    resultado_.store(serve ? 1 : -1);
    if (!serve) std::fprintf(stderr, "[remaster] carregar %d: o save nao serve para este jogo\n", ev.arquivo);
    return true;
}

} // namespace bs::remaster
