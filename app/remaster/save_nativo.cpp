#include "remaster/save_nativo.h"

#include "bu_adapter.h"
#include "cpu/sh4_context.h"
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "mods/mod_api.h"
#include "time_adapter.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace bs::remaster {

namespace e = enderecos_save;

namespace {

constexpr uint32_t kLimiteGravacao = 900;  // quadros (o painel original grava em uns 60 com o cartao nativo)

Sh4Func g_painelOriginal = nullptr;

// Chamada nativa a uma rotina do jogo de dentro do painel substituido, como o BS_CALL do codigo gerado: argumentos em r4-r6,
// retorno esperado coerente (o do painel) e os registradores restaurados depois. Igual a de entrada_titulo.cpp, com o retorno
// de quem chamou o painel.
uint32_t chamar(Sh4Context* c, uint32_t retorno, uint32_t addr, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0) {
    const Sh4Func f = sh4_lookup(addr);
    if (!f) throw std::runtime_error("remaster: rotina do jogo ausente (save)");
    uint32_t r[16], rbank[8], fr[16], xf[16];
    std::memcpy(r, c->r, sizeof r);
    std::memcpy(rbank, c->rbank, sizeof rbank);
    std::memcpy(fr, c->fr, sizeof fr);
    std::memcpy(xf, c->xf, sizeof xf);
    const uint32_t sr = c->sr, t = c->t, gbr = c->gbr, pr = c->pr, mach = c->mach, macl = c->macl, fpscr = c->fpscr,
                   fpul = c->fpul, retE = c->ret_expect, retS = c->ret_sp, fn = c->cur_fn, pc = c->pc;
    c->r[4] = a0, c->r[5] = a1, c->r[6] = a2;
    c->pr = c->ret_expect = retorno;
    c->ret_sp             = c->r[15];
    f(c);
    const uint32_t r0 = c->r[0];
    if (c->r[15] != r[15] || c->redirect) throw std::runtime_error("remaster: pilha ou longjmp inesperado no save nativo");
    std::memcpy(c->r, r, sizeof r);
    std::memcpy(c->rbank, rbank, sizeof rbank);
    std::memcpy(c->fr, fr, sizeof fr);
    std::memcpy(c->xf, xf, sizeof xf);
    c->sr = sr, c->t = t, c->gbr = gbr, c->pr = pr, c->mach = mach, c->macl = macl, c->fpscr = fpscr, c->fpul = fpul;
    c->ret_expect = retE, c->ret_sp = retS, c->cur_fn = fn, c->pc = pc;
    mods::poll(c, retorno, 0);
    if (c->redirect) throw std::runtime_error("remaster: longjmp pendente depois do save nativo");
    return r0;
}

// O painel: so o save do gameplay (modo 0, chamado em 8C052694) e nativo; o boot (2) e o LOAD do titulo (1) seguem o original
void envoltorio(Sh4Context* c) {
    if (c->r[4] == 0 && (c->pr & 0x1FFFFFFFu) == (e::kRetornoSave & 0x1FFFFFFFu)) {
        SaveNativo::get().quadro(c, c->pr);
        c->r[0] = 0;
        return;
    }
    g_painelOriginal(c);
}

} // namespace

SaveNativo& SaveNativo::get() {
    static SaveNativo s;
    return s;
}

void SaveNativo::instalar(std::vector<Evento> eventos, std::function<void(const Evento&)> aplicado) {
    {
        std::lock_guard l(m_);
        roteiro_     = std::move(eventos);
        proximo_     = 0;
        reproducao_  = !roteiro_.empty();
        aplicado_    = std::move(aplicado);
        temPendente_ = false;
    }
    ativo_ = false, etapa_ = 0;
    aguardando_.store(false), ocupado_.store(false), resultado_.store(0), arquivo_.store(-1);
    g_painelOriginal = sh4_lookup_generated(e::kPainel);
    // sem o slot (um codigo gerado de antes, ou uma versao do jogo sem ele), o painel original continua: o jogo funciona igual
    if (!g_painelOriginal || !mods::has_slot(e::kPainel) || !mods::replace(e::kPainel, &envoltorio)) {
        std::fprintf(stderr, "[remaster] save: o painel 8C01D200 nao tem slot nativo; os pontos de save usam o painel original\n");
        return;
    }
}

void SaveNativo::desinstalar() {
    mods::replace(e::kPainel, nullptr);
    std::lock_guard l(m_);
    temPendente_ = false;
    ativo_       = false;
    aguardando_.store(false), ocupado_.store(false);
}

void SaveNativo::pedir(Acao a, int arquivo) {
    std::lock_guard l(m_);
    if (reproducao_) return;
    pendente_    = {0, a, arquivo};
    temPendente_ = true;
}

void SaveNativo::escolha_automatica(Acao a, int arquivo) {
    std::lock_guard l(m_);
    auto_    = {0, a, arquivo};
    autoTem_ = true;
}

EstadoSave SaveNativo::estado() const {
    EstadoSave s;
    s.aguardando = aguardando_.load();
    s.ocupado    = ocupado_.load();
    s.vez        = vez_.load();
    s.resultado  = resultado_.load();
    s.arquivo    = arquivo_.load();
    return s;
}

void SaveNativo::quadro(Sh4Context* c, uint32_t retorno) {
    if (!ativo_) {  // o primeiro quadro do estado 8: o jogo pediu o save (no lugar do painel, a lista nativa)
        ativo_ = true, etapa_ = 0, quadros_ = 0;
        {
            std::lock_guard l(m_);
            temPendente_ = false;  // uma escolha velha (de outro ponto de save) nunca vale
        }
        resultado_.store(0), aguardando_.store(true), vez_.fetch_add(1);
        sh4_write16(c, e::kSituacao, 1);
        std::fprintf(stderr, "[remaster] save: ponto de save no passo %llu (lista nativa)\n",
                     static_cast<unsigned long long>(transitional::time::game_steps()));
        return;
    }
    if (etapa_ == 0) {
        bool tem = false;
        {
            std::lock_guard l(m_);
            const uint64_t  passo = transitional::time::game_steps();
            if (reproducao_) {
                if (proximo_ < roteiro_.size() && passo >= roteiro_[proximo_].passo) atual_ = roteiro_[proximo_++], tem = true;
            } else if (temPendente_) {
                atual_ = pendente_, atual_.passo = passo, temPendente_ = false, tem = true;
            } else if (autoTem_) {
                atual_ = auto_, atual_.passo = passo, autoTem_ = false, tem = true;
            }
        }
        if (!tem) {
            sh4_write16(c, e::kSituacao, 1);  // a lista continua aberta: o laco continua no estado 8
            return;
        }
        if (aplicado_) aplicado_(atual_);
        aguardando_.store(false);
        if (atual_.acao != Acao::Salvar) {  // cancelar: o laco sai do fluxo sem gravar
            sh4_write16(c, e::kSituacao, 0);
            ativo_ = false;
            resultado_.store(2);
            std::fprintf(stderr, "[remaster] save: cancelado no passo %llu\n", static_cast<unsigned long long>(atual_.passo));
            return;
        }
        std::fprintf(stderr, "[remaster] save: salvar %d no passo %llu\n", atual_.arquivo, static_cast<unsigned long long>(atual_.passo));
        sh4_write8(c, e::kSoquete, 1);  // o que o painel deixaria escrito (soquete 1 = unidade 0, o bloco escolhido)
        sh4_write8(c, e::kBloco, uint8_t(atual_.arquivo));
        okAntes_ = transitional::bu::installed() ? transitional::bu::stats().saveOk : 0;
        arquivo_.store(atual_.arquivo), ocupado_.store(true);
        etapa_ = 1, quadros_ = 0;
    }
    // gravando: a rotina do jogo a cada quadro ate terminar (6 trabalhando, 8 gravou; medido na prova de 2026-11-25: o
    // painel do Dreamcast trata 8 como sucesso e 9 como erro)
    const uint32_t r = chamar(c, retorno, e::kGravar, 0, uint32_t(atual_.arquivo), 0);
    ++quadros_;
    if (r == 6 && quadros_ < kLimiteGravacao) {
        sh4_write16(c, e::kSituacao, 1);
        return;
    }
    const bool gravou = r == 8 && (!transitional::bu::installed() || transitional::bu::stats().saveOk > okAntes_);
    if (!gravou) {  // a maquina do cartao volta ao inicio (como no load nativo)
        sh4_write32(c, e::kMaquina, 0);
        sh4_write8(c, e::kBandeira, 0);
        std::fprintf(stderr, "[remaster] save: salvar %d falhou (retorno %u em %u quadros)\n", atual_.arquivo, r, quadros_);
    } else {
        std::fprintf(stderr, "[remaster] save: salvo no arquivo %d em %u quadros\n", atual_.arquivo, quadros_);
    }
    sh4_write16(c, e::kSituacao, 0);
    ativo_ = false;
    ocupado_.store(false);
    resultado_.store(gravou ? 1 : -1);
}

} // namespace bs::remaster
