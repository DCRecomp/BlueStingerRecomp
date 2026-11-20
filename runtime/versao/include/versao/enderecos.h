// Enderecos do jogo por versao (PAL P2, docs/PAL-PLANO.md, arquitetura A aprovada em 2026-11-19).
//
// O codigo nativo cita enderecos do jogo (funcoes, pontos de espera, variaveis) sempre pelo endereco da versao
// AMERICANA (T13001N V1.000), dentro de bs::jogo(...). Os comentarios tambem usam o endereco americano.
//
//  - Americano (BS_VERSAO_JOGO == 0, padrao): bs::jogo(a) == a. Nada muda no executavel.
//  - Outras versoes: bs::jogo(a) procura a na tabela da versao (enderecos_<versao>.inc), gerada pelo
//    tools/dev/versao_traduz.py --inc a partir do mapa conferido (versao_mapa.py). Um endereco que nao esta na
//    tabela NAO COMPILA (consteval): nenhum endereco americano passa sem traducao.
//
// Numeracao (a mesma de game_version_of, tools/extractor/boot_identity.h; docs/DIRECTORS-CUT-ANALISE.md):
//    0 americano T13001N V1.000 (CDI do projeto e Redump)     3 japones HDR-0003 V1.006
//    1 PAL alemao T13001D 18 V1.000                           4 Director's Cut EN (o japones com o patch; codigo do JP)
//    2 PAL ingles T13001D-05 V1.000                           5 Director's Cut FR (idem)
//    6 PAL frances T13001D 09 V1.000
// O Director's Cut usa a tabela do japones: o patch so muda dados do executavel, nao o codigo.
//
// A versao e escolhida na compilacao: o Builder compila cada instalacao a partir do disco do usuario, e o C++ gerado
// ja e o daquela versao. Os enderecos da area do sistema (< 0x8C010000, bios_entry_state.inc, system_area.h) sao os
// mesmos nas versoes e ficam fora desta tabela.
#pragma once

#include <cstdint>

#ifndef BS_VERSAO_JOGO
#define BS_VERSAO_JOGO 0
#endif
#if BS_VERSAO_JOGO < 0 || BS_VERSAO_JOGO > 6
#error "BS_VERSAO_JOGO: 0 a 6 (ver a numeracao acima)"
#endif
// PAL: 50 Hz (cadencia de campos, cpu/time_hooks.h; estado do SPG no boot, boot/native_boot.cpp)
#define BS_VERSAO_PAL (BS_VERSAO_JOGO == 1 || BS_VERSAO_JOGO == 2 || BS_VERSAO_JOGO == 6)

namespace bs {

namespace versao_detalhe {
struct Par {
    uint32_t us;
    uint32_t outra;
};
#if BS_VERSAO_JOGO != 0
#define BS_PAR(us, outra) Par{us, outra},
inline constexpr Par kPares[] = {
#if BS_VERSAO_JOGO == 1
#include "versao/enderecos_pal_de.inc"
#elif BS_VERSAO_JOGO == 2
#include "versao/enderecos_pal_en.inc"
#elif BS_VERSAO_JOGO == 6
#include "versao/enderecos_pal_fr.inc"
#else
#include "versao/enderecos_jp.inc"
#endif
};
#undef BS_PAR
#endif
}  // namespace versao_detalhe

// Identidade do jogo compilado (produto e versao do IP.BIN), a que os pacotes de mods declaram em `jogo = "..."`
// (native/localization/mod_packages.h). Revisao Codex 2026-11-19 (P4).
// O Director's Cut tem identidade propria: os textos (e as legendas que dependem deles) sao outros.
inline constexpr const char* kJogoIdentidade = BS_VERSAO_JOGO == 1   ? "T13001D V1.000"
                                               : BS_VERSAO_JOGO == 2 ? "T13001D-05 V1.000"
                                               : BS_VERSAO_JOGO == 3 ? "HDR-0003 V1.006"
                                               : BS_VERSAO_JOGO == 4 ? "HDR-0003 V1.006 DC-EN"
                                               : BS_VERSAO_JOGO == 5 ? "HDR-0003 V1.006 DC-FR"
                                               : BS_VERSAO_JOGO == 6 ? "T13001D 09 V1.000"
                                                                     : "T13001N V1.000";
// Subpasta dos saves na pasta padrao (Documentos\DC-Recomp\Blue Stinger\saves): o arquivo e BLUESTIN.00n em todas as
// versoes, e um save de uma versao nao deve ser aberto por outra sem pedido (PAL P6). O americano fica na raiz, como
// sempre foi; a pasta portatil e a --saves-pasta ja sao de uma instalacao so.
inline constexpr const char* kVersaoPastaSaves = BS_VERSAO_JOGO == 1   ? "PAL-DE"
                                                 : BS_VERSAO_JOGO == 2 ? "PAL-EN"
                                                 : BS_VERSAO_JOGO == 3 ? "JP"
                                                 : BS_VERSAO_JOGO == 4 ? "DC-EN"
                                                 : BS_VERSAO_JOGO == 5 ? "DC-FR"
                                                 : BS_VERSAO_JOGO == 6 ? "PAL-FR"
                                                                       : "";
// Nome da versao nas mensagens
inline constexpr const char* kVersaoNome = BS_VERSAO_JOGO == 1   ? "PAL alemao"
                                           : BS_VERSAO_JOGO == 2 ? "PAL ingles"
                                           : BS_VERSAO_JOGO == 3 ? "japones"
                                           : BS_VERSAO_JOGO == 4 ? "Director's Cut EN"
                                           : BS_VERSAO_JOGO == 5 ? "Director's Cut FR"
                                           : BS_VERSAO_JOGO == 6 ? "PAL frances"
                                                                 : "americano";

// Endereco americano -> endereco da versao compilada.
consteval uint32_t jogo(uint32_t enderecoAmericano) {
#if BS_VERSAO_JOGO == 0
    return enderecoAmericano;
#else
    for (const auto& p : versao_detalhe::kPares)
        if (p.us == enderecoAmericano) return p.outra;
    throw "bs::jogo: endereco americano sem traducao na tabela desta versao (rodar tools/dev/versao_traduz.py --inc)";
#endif
}

// Em tempo de execucao (API de mods, entradas de texto de diagnostico): o mesmo contrato, endereco americano. Fora do
// americano, devolve 0 se o endereco nao esta na tabela (ela cobre os enderecos que o codigo nativo cita).
constexpr uint32_t jogo_execucao(uint32_t enderecoAmericano) noexcept {
#if BS_VERSAO_JOGO == 0
    return enderecoAmericano;
#else
    for (const auto& p : versao_detalhe::kPares)
        if (p.us == enderecoAmericano) return p.outra;
    return 0;
#endif
}

// O mesmo, como deslocamento na RAM principal (ram + deslocamento).
consteval uint32_t jogo_ram(uint32_t enderecoAmericano) { return jogo(enderecoAmericano) & 0x00FFFFFFu; }

}  // namespace bs
