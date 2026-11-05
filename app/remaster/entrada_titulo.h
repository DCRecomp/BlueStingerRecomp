#pragma once
// N9.2: pontos de entrada nativos do titulo, so na edicao Remastered (decisao do mantenedor de 2026-11-04; achados e provas em
// docs/N9-2-PONTOS-DE-ENTRADA.md). O menu nativo nao conduz o menu original por entrada sintetica: ele pede uma acao e este
// modulo, na thread do jogo, leva a maquina de estados do titulo exatamente ao estado que a confirmacao original produziria.
// Dali em diante o jogo segue pelo proprio codigo:
//   - AbrirMenu: estado 5 ("PRESS START") -> 6 (menu), como o START do original (efeito 90);
//   - NovoJogo:  estado 6 -> 7, com as escritas e chamadas do NEW GAME original (efeito 90, 8C07B2A0(0), 8C079430);
//   - Carregar:  estado 6 -> a rotina de load do proprio jogo (8C01CE00, a que o painel de saves usa) com o arquivo pedido,
//                sem o painel (sem VMU, soquete nem bloco na tela), e a decisao do estado 9 (12 = partida, 11 = volta ao menu).
// Onde: a espera do VBlank (8C064A20) e envolvida so nas chamadas indiretas (o produto nao tem os slots de mods) e o modulo so
// age quando o retorno e o laco da atualizacao do titulo (8C078536). Cada acao aplicada vira um evento com o passo de jogo
// (gravacao e reproducao: o mesmo evento no mesmo passo da o mesmo jogo).
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace bs {
struct Sh4Context;
}

namespace bs::remaster {

enum class Acao : uint8_t { Nenhuma, AbrirMenu, NovoJogo, Carregar };
const char* acao_nome(Acao a);
bool        acao_de_nome(const std::string& n, Acao& out);

// Um evento aplicado: no passo `passo`, a acao (com o arquivo, 0..5, para Carregar).
struct Evento {
    uint64_t passo   = 0;
    Acao     acao    = Acao::Nenhuma;
    int      arquivo = 0;
};

// O que a janela le (atomicos, atualizados a cada quadro do titulo).
struct EstadoTitulo {
    bool     noTitulo  = false;  // o laco do titulo esta rodando (o ultimo quadro dele e recente)
    uint16_t estado    = 0;      // [8C24D7E8]
    uint64_t quadros   = 0;      // quadros do titulo vistos nesta sessao
    bool     ocupado   = false;  // uma acao em andamento (o load leva alguns quadros)
    int      resultado = 0;      // 0 nenhum; 1 a ultima acao deu certo; -1 o load falhou ou o save nao serve
};

class EntradaTitulo {
public:
    static EntradaTitulo& get();

    // Na thread do jogo, no inicio de cada sessao (os mods sao zerados a cada sessao). `eventos`: reproducao (aplicados no
    // passo gravado); vazio = jogo ao vivo. `aplicado`: chamado na thread do jogo a cada evento aplicado (gravacao, log).
    // `trocarMenu`: o menu original deixa de ser desenhado (o nativo fica no lugar); falso quando um roteiro conduz o original.
    void instalar(std::vector<Evento> eventos, std::function<void(const Evento&)> aplicado, bool trocarMenu = true);
    void desinstalar();

    // Da janela: pede uma acao (a mais nova substitui uma pendente que ainda nao comecou). Ignorado na reproducao.
    void         pedir(Acao a, int arquivo = 0);
    EstadoTitulo estado() const;

    // Usado pelo envoltorio (publico para o teste unitario da logica, com um contexto falso).
    void quadro(Sh4Context* c);

private:
    EntradaTitulo() = default;
    bool aplicar(Sh4Context* c, const Evento& e);  // true = terminou (com sucesso ou falha)

    mutable std::mutex                    m_;
    Evento                                pendente_{};
    bool                                  temPendente_ = false;
    std::vector<Evento>                   roteiro_;
    size_t                                proximo_ = 0;
    bool                                  reproducao_ = false;
    std::function<void(const Evento&)>    aplicado_;
    // em andamento (so a thread do jogo)
    Evento                                atual_{};
    bool                                  emCurso_ = false;
    int                                   etapa_   = 0;
    uint32_t                              quadrosNaEtapa_ = 0;
    // publicados
    std::atomic<bool>                     noTitulo_{false}, ocupado_{false};
    std::atomic<uint16_t>                 estado_{0};
    std::atomic<uint64_t>                 quadros_{0};
    std::atomic<int>                      resultado_{0};
};

// Enderecos e valores (docs/N9-2-PONTOS-DE-ENTRADA.md), para os testes.
namespace enderecos {
inline constexpr uint32_t kEsperaVblank   = 0x8C064A20;
inline constexpr uint32_t kRetornoLacoTit = 0x8C078536;
inline constexpr uint32_t kTitulo         = 0x8C24D2CC;  // estrutura do titulo
inline constexpr uint32_t kEstado         = kTitulo + 0x51C;
inline constexpr uint32_t kCursor         = kTitulo + 0x51E;
inline constexpr uint32_t kFade           = kTitulo + 0x520;
inline constexpr uint32_t kEfeito         = 0x8C02A6B8;  // efeito sonoro (id, 0, 0)
inline constexpr uint32_t kNovoJogoIni    = 0x8C07B2A0;  // (0)
inline constexpr uint32_t kCarregar       = 0x8C01CE00;  // (drive, arquivo, 0x22): 7 trabalhando, 8 concluido
inline constexpr uint32_t kVersaoSave     = 0x8C064700;  // () comparado com [8C24FDE8]
inline constexpr uint32_t kVersaoEsperada = 0x8C24FDE8;
// O menu original da tela de titulo, que a Remastered troca pelo nativo (o fundo, o logotipo e a personagem continuam):
// as duas rotinas da tabela 8C07A278 chamadas por ponteiro a cada quadro (medido: desligadas, so o menu some).
inline constexpr uint32_t kDesenhoPressStart = 0x8C079AAC;  // "PRESS START BUTTON" (some no 5 -> 6)
inline constexpr uint32_t kDesenhoOpcoes   = 0x8C079D20;  // os textos NEW GAME, LOAD GAME, OPTIONS
inline constexpr uint32_t kDesenhoDestaque = 0x8C07A4C0;  // a barra vermelha do item selecionado
} // namespace enderecos

} // namespace bs::remaster
