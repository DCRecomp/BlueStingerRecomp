#pragma once
// Save nativo da Remastered (pedido do mantenedor, 2026-11-25: "o save dentro do jogo precisa ser atualizado na
// remasterizacao, ainda esta com o menu de escolha de slots"). Nos pontos de save, o jogo chama o painel do Dreamcast
// 8C01D200(0, 8) a cada quadro do estado 8 do fluxo de save (laco 8C052240, chamada em 8C052694) e sai do fluxo quando a
// situacao u16 [8C1F3B40] volta a 0. Na Remastered, essa chamada (e so ela: o modo 0 com o retorno 8C052698) nao desenha o
// painel: a interface nativa mostra os 6 perfis e este modulo, na thread do jogo, grava pela rotina do proprio jogo
// 8C01CCE0(unidade 0, arquivo n), a mesma que o painel usa (mesmo nome BLUESTIN.00n, mesma imagem VMS a partir de
// 8C24F93C). O sistema de saves do jogo nao muda: o arquivo e o mesmo do painel e carrega nas duas edicoes.
// Cada decisao (gravar n ou cancelar) vira um evento com o passo de jogo, como as do titulo (gravacao e reproducao).
#include "remaster/entrada_titulo.h"  // Evento, Acao
#include "versao/enderecos.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace bs {
struct Sh4Context;
}

namespace bs::remaster {

// O que a janela le
struct EstadoSave {
    bool     aguardando = false;  // o jogo esta num ponto de save esperando a escolha (a lista deve estar aberta)
    bool     ocupado    = false;  // gravando
    uint64_t vez        = 0;      // conta os pedidos do jogo (cada ponto de save acionado)
    int      resultado  = 0;      // 0 nada; 1 gravou; -1 falhou; 2 cancelado
    int      arquivo    = -1;     // o ultimo arquivo gravado (ou tentado)
};

class SaveNativo {
public:
    static SaveNativo& get();

    // Na thread do jogo, no inicio da sessao: troca o painel (slot nativo 8C01D200). `eventos`: so os de save (Salvar e
    // CancelarSave) de uma reproducao; vazio = jogo ao vivo.
    void instalar(std::vector<Evento> eventos, std::function<void(const Evento&)> aplicado);
    void desinstalar();

    // Da janela: a escolha do jogador (Acao::Salvar com o arquivo, ou Acao::CancelarSave). Ignorado na reproducao.
    void       pedir(Acao a, int arquivo = 0);
    // Testes sem janela (BS_REMASTER_SALVAR): a escolha feita no primeiro ponto de save, como se o jogador a pedisse.
    void       escolha_automatica(Acao a, int arquivo);
    EstadoSave estado() const;

    // Um quadro do fluxo de save (publico para o teste com contexto falso). `retorno` = o PR da chamada do painel.
    void quadro(Sh4Context* c, uint32_t retorno);

private:
    SaveNativo() = default;

    mutable std::mutex                 m_;
    Evento                             pendente_{};
    bool                               temPendente_ = false;
    std::vector<Evento>                roteiro_;
    size_t                             proximo_    = 0;
    bool                               reproducao_ = false;
    bool                               autoTem_    = false;
    Evento                             auto_{};
    std::function<void(const Evento&)> aplicado_;
    // em andamento (so a thread do jogo)
    bool     ativo_  = false;
    int      etapa_  = 0;  // 0 esperando a escolha; 1 gravando
    Evento   atual_{};
    uint32_t quadros_ = 0;
    uint64_t okAntes_ = 0;
    // publicados
    std::atomic<bool>     aguardando_{false}, ocupado_{false};
    std::atomic<uint64_t> vez_{0};
    std::atomic<int>      resultado_{0}, arquivo_{-1};
};

namespace enderecos_save {
inline constexpr uint32_t kPainel        = bs::jogo(0x8C01D200);  // (modo, 8): 0 = save, 1 = load, 2 = conferencia do boot
inline constexpr uint32_t kRetornoSave   = bs::jogo(0x8C052698);  // o retorno da chamada em 8C052694 (estado 8 do fluxo)
inline constexpr uint32_t kSituacao      = bs::jogo(0x8C1F3B40);  // u16: 0 = o painel terminou (o laco vai ao estado 9)
inline constexpr uint32_t kGravar        = bs::jogo(0x8C01CCE0);  // (unidade, arquivo): 6 trabalhando, 8 gravou, 9 erro
inline constexpr uint32_t kMaquina       = bs::jogo(0x8C1F3858);  // u32: o estado da maquina de save/load do cartao
inline constexpr uint32_t kBandeira      = bs::jogo(0x8C1F3A60);  // u8
inline constexpr uint32_t kSoquete       = bs::jogo(0x8C1F3BAA);  // u8: o soquete que o painel escolheu (1 = unidade 0)
inline constexpr uint32_t kBloco         = bs::jogo(0x8C1F3BAB);  // u8: o arquivo que o painel escolheu
} // namespace enderecos_save

} // namespace bs::remaster
