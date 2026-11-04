#pragma once
// N9.1: a identidade visual do projeto (docs/N9-1-ARTE.md). Cores, tipografia, medidas e tempos. E o que o N9.2 reaproveita
// na interface do jogo (decisao 12.3: a identidade, nao o motor). Tudo em DIP.
#include "instalador/gui/pintor.h"

namespace bs::builder::instalador::gui::tema {

// Fundo azul-noite, ambar do ferrao (acao principal, edicao Original), verde-agua (edicao Remastered, sucesso), coral (erro).
inline constexpr Cor kFundo        = rgb(0x0B111C);
inline constexpr Cor kFundoAlto    = rgb(0x111A2A);
inline constexpr Cor kLateral      = rgb(0x0E1626);
inline constexpr Cor kCartao       = rgb(0x162133);
inline constexpr Cor kCartaoHover  = rgb(0x1C2A40);
inline constexpr Cor kBorda        = rgb(0x2A3A55);
inline constexpr Cor kTexto        = rgb(0xEAF0F7);
inline constexpr Cor kTextoSuave   = rgb(0xA4B1C4);
inline constexpr Cor kTextoApagado = rgb(0x6B7A90);
inline constexpr Cor kAmbar        = rgb(0xF4A637);
inline constexpr Cor kAmbarEscuro  = rgb(0xC97F17);
inline constexpr Cor kAgua         = rgb(0x3CC6B8);
inline constexpr Cor kCoral        = rgb(0xF06A5B);
inline constexpr Cor kTextoSobreAmbar = rgb(0x1A1206);
inline constexpr Cor kFoco         = rgb(0xFFD27A);

// Tipografia: Segoe UI (Windows) e Noto Sans (Linux), pelos pintores
inline constexpr float kTitulo     = 26;
inline constexpr float kSubtitulo  = 17;
inline constexpr float kCorpo      = 14;
inline constexpr float kPequeno    = 12.5f;

// Janela e grade
inline constexpr float kLarguraJanela = 960, kAlturaJanela = 620;  // tamanho inicial
inline constexpr float kLarguraMin = 880, kAlturaMin = 580;
inline constexpr float kLateralLargura = 236;
inline constexpr float kMargem = 32;
inline constexpr float kRaio = 8;
inline constexpr float kBotaoAltura = 38;
inline constexpr float kFocoEspessura = 2;

// Animacoes discretas (segundos); desligadas quando o sistema pede menos animacao
inline constexpr double kTransicao = 0.18;
inline constexpr double kHover     = 0.12;

} // namespace bs::builder::instalador::gui::tema
