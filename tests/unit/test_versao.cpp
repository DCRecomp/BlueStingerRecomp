// PAL P2: enderecos do jogo por versao (runtime/versao/include/versao/enderecos.h). O mesmo arquivo e compilado duas
// vezes, em executaveis separados (a definicao de bs::jogo muda com a versao): bs_versao_us_tests (padrao) e
// bs_versao_pal_tests (BS_VERSAO_JOGO=1). Revisao Codex 2026-11-19 (P2): traducao conhecida, endereco ausente, limites
// de faixa e o contrato de enderecos americanos em tempo de execucao.
#include "test.h"

#include "cpu/time_hooks.h"
#include "versao/enderecos.h"

#include <string>

namespace {

#if BS_VERSAO_JOGO != 0
// a tabela: chaves americanas unicas e em ordem crescente (o gerador escreve assim; uma edicao a mao que quebre isso
// para aqui na compilacao)
constexpr bool tabela_ordenada() {
    const auto& t = bs::versao_detalhe::kPares;
    for (size_t i = 1; i < sizeof t / sizeof t[0]; ++i)
        if (!(t[i - 1].us < t[i].us)) return false;
    return true;
}
static_assert(tabela_ordenada(), "enderecos_pal_de.inc: chaves repetidas ou fora de ordem");
#endif

}  // namespace

BS_TEST(versao_traducoes_conhecidas) {
#if BS_VERSAO_JOGO == 0
    // americano: identidade (o executavel nao muda; docs/PAL-PLANO.md secao 7)
    static_assert(bs::jogo(0x8C08B544) == 0x8C08B544);
    static_assert(bs::jogo_ram(0x8C24D668) == 0x0024D668);
    BS_CHECK(bs::jogo_execucao(0x8C08B544) == 0x8C08B544);
    BS_CHECK(bs::jogo_execucao(0x8C123456) == 0x8C123456);  // qualquer endereco: identidade
#elif BS_VERSAO_JOGO == 1
    // PAL alemao: pares conferidos pelo mapa do P1 (docs/PAL-PLANO.md secao 6)
    static_assert(bs::jogo(0x8C08B544) == 0x8C08B6C4);  // funcao de video (+384)
    static_assert(bs::jogo(0x8C064A2C) == 0x8C064A4C);  // espera do VBlank do jogo (+32)
    static_assert(bs::jogo(0x8C15B6B4) == 0x8C15C654);  // biblioteca do SDK (+4000)
    static_assert(bs::jogo(0x8C0792CC) == 0x8C07934C);  // casamento tolerante (excecoes.tsv)
    static_assert(bs::jogo(0x8C000068) == 0x8C000068);  // area do sistema: igual
    static_assert(bs::jogo_ram(0x8C24D668) == 0x0024E608);  // posicao do Eliot (BSS +4000)
    BS_CHECK(bs::jogo_execucao(0x8C08B544) == 0x8C08B6C4);
#endif
#if BS_VERSAO_JOGO == 2
    // PAL ingles: pares do mapa (docs/DIRECTORS-CUT-ANALISE.md, secao 6); a BSS anda -32
    static_assert(bs::jogo(0x8C08B544) == 0x8C08B604);  // funcao de video (+192)
    static_assert(bs::jogo(0x8C064A2C) == 0x8C064A8C);  // espera do VBlank do jogo (+96)
    static_assert(bs::jogo(0x8C15B6B4) == 0x8C15B694);  // biblioteca do SDK (-32)
    static_assert(bs::jogo(0x8C000068) == 0x8C000068);  // area do sistema: igual
    static_assert(bs::jogo_ram(0x8C24D668) == 0x0024D648);  // posicao do Eliot (BSS -32)
    BS_CHECK(bs::jogo_execucao(0x8C08B544) == 0x8C08B604);
#endif
#if BS_VERSAO_JOGO == 6
    // PAL frances: pares do mapa (tmp/discos/mapa-pal-fr); a BSS anda +3808
    static_assert(bs::jogo(0x8C08B544) == 0x8C08B6E4);  // funcao de video
    static_assert(bs::jogo(0x8C064A2C) == 0x8C064A4C);  // espera do VBlank do jogo (+32)
    static_assert(bs::jogo(0x8C15B6B4) == 0x8C15C594);  // biblioteca do SDK
    static_assert(bs::jogo(0x8C07A4C0) == 0x8C07A540);  // excecao conferida a mao (o mesmo do PAL alemao)
    static_assert(bs::jogo_ram(0x8C24D668) == 0x0024E548);  // posicao do Eliot (BSS +3808)
    BS_CHECK(bs::jogo_execucao(0x8C08B544) == 0x8C08B6E4);
#endif
#if BS_VERSAO_JOGO == 3 || BS_VERSAO_JOGO == 4 || BS_VERSAO_JOGO == 5
    // japones v1.006: outra compilacao (docs/DIRECTORS-CUT-ANALISE.md); excecoes conferidas a mao (excecoes.tsv)
    static_assert(bs::jogo(0x8C08B544) == 0x8C08A164);  // funcao de video
    static_assert(bs::jogo(0x8C064A2C) == 0x8C0639EC);  // espera do VBlank (o mesmo laco)
    static_assert(bs::jogo(0x8C15B6B4) == 0x8C158834);  // biblioteca do SDK
    static_assert(bs::jogo(0x8C000068) == 0x8C000068);  // area do sistema: igual
    static_assert(bs::jogo_ram(0x8C24D668) == 0x0024A764);  // posicao do Eliot (BSS do JP sem deslocamento uniforme)
    BS_CHECK(bs::jogo_execucao(0x8C08B544) == 0x8C08A164);
#endif
}

BS_TEST(versao_limites_de_faixa) {
    // faixas semiabertas traduzidas ponto a ponto continuam do mesmo tamanho (a BSS anda junta)
    constexpr uint32_t ini = bs::jogo(0x8C240000), fim = bs::jogo(0x8C260000);
    static_assert(fim - ini == 0x20000);
    // o fim do codigo do jogo (draw_class.h) fica no mesmo ponto da biblioteca do SDK: 0x94 depois do inicio dela
    static_assert(bs::jogo(0x8C158000) - bs::jogo(0x8C157F6C) == 0x94);
    BS_CHECK(ini < fim);
}

BS_TEST(versao_endereco_ausente_em_execucao) {
#if BS_VERSAO_JOGO == 0
    BS_CHECK(bs::jogo_execucao(0x8C0100A0) == 0x8C0100A0);
#else
    // fora da tabela: 0 (a API de mods transforma em erro; bs::jogo nem compila: consteval)
    BS_CHECK(bs::jogo_execucao(0x8C0100A0) == 0);
    BS_CHECK(bs::jogo_execucao(0) == 0);
#endif
}

// P4 (revisao Codex 2026-11-19): a cadencia de campos por versao (cpu/time_hooks.h). NTSC: 60000/1001, como sempre foi;
// PAL: 50 Hz exatos. O relogio de amostras do mixer (44,1 kHz) anda um segundo em um segundo de campos.
BS_TEST(versao_cadencia_de_campos) {
#if !BS_VERSAO_PAL
    // NTSC: o americano e o japones (e o Director's Cut, que roda o executavel japones)
    static_assert(bs::kFieldRateNum == 60000 && bs::kFieldRateDen == 1001);
    static_assert(bs::kCompatTicksPerField == 3336666);  // 200 MHz * 1001/60000, truncado
    constexpr uint64_t campos = 60000;                    // 1001 s de campos NTSC
    BS_CHECK(campos * 44100ull * bs::kFieldRateDen / bs::kFieldRateNum == 44100ull * 1001);
#else
    static_assert(bs::kFieldRateNum == 50 && bs::kFieldRateDen == 1);
    static_assert(bs::kCompatTicksPerField == 4000000);  // 200 MHz / 50
    BS_CHECK(50ull * 44100ull * bs::kFieldRateDen / bs::kFieldRateNum == 44100);  // 50 campos = 1 s de amostras
    BS_CHECK(1ull * 44100ull * bs::kFieldRateDen / bs::kFieldRateNum == 882);     // 882 amostras por campo
#endif
    // identidade (pacotes de mods) e subpasta dos saves (P6; o americano continua na raiz)
    constexpr const char* kIdent[] = {"T13001N V1.000", "T13001D V1.000", "T13001D-05 V1.000", "HDR-0003 V1.006",
                                      "HDR-0003 V1.006 DC-EN", "HDR-0003 V1.006 DC-FR", "T13001D 09 V1.000"};
    constexpr const char* kPasta[] = {"", "PAL-DE", "PAL-EN", "JP", "DC-EN", "DC-FR", "PAL-FR"};
    BS_CHECK(std::string(bs::kJogoIdentidade) == kIdent[BS_VERSAO_JOGO]);
    BS_CHECK(std::string(bs::kVersaoPastaSaves) == kPasta[BS_VERSAO_JOGO]);
    static_assert(BS_VERSAO_PAL == (BS_VERSAO_JOGO == 1 || BS_VERSAO_JOGO == 2 || BS_VERSAO_JOGO == 6));
}
