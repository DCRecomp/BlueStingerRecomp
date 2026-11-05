#pragma once
// N9.1: a identidade visual do projeto (docs/N9-1-ARTE.md). Cores, tipografia, medidas e tempos. E o que o N9.2 reaproveita
// na interface do jogo (decisao 12.3: a identidade, nao o motor). Tudo em DIP.
#include "instalador/gui/pintor.h"
#include "ui/identidade.h"  // N9.2: a paleta comum com a interface do jogo (native/ui)

namespace bs::builder::instalador::gui::tema {

// Fundo azul-noite, ambar do ferrao (acao principal, edicao Original), verde-agua (edicao Remastered, sucesso), coral (erro).
inline constexpr Cor kFundo        = rgb(native::ui::identidade::kFundo);
inline constexpr Cor kFundoAlto    = rgb(native::ui::identidade::kFundoAlto);
inline constexpr Cor kLateral      = rgb(native::ui::identidade::kLateral);
inline constexpr Cor kCartao       = rgb(native::ui::identidade::kCartao);
inline constexpr Cor kCartaoHover  = rgb(native::ui::identidade::kCartaoHover);
inline constexpr Cor kBorda        = rgb(native::ui::identidade::kBorda);
inline constexpr Cor kTexto        = rgb(native::ui::identidade::kTexto);
inline constexpr Cor kTextoSuave   = rgb(native::ui::identidade::kTextoSuave);
inline constexpr Cor kTextoApagado = rgb(native::ui::identidade::kTextoApagado);
inline constexpr Cor kAmbar        = rgb(native::ui::identidade::kAmbar);
inline constexpr Cor kAmbarEscuro  = rgb(native::ui::identidade::kAmbarEscuro);
inline constexpr Cor kAgua         = rgb(native::ui::identidade::kAgua);
inline constexpr Cor kCoral        = rgb(native::ui::identidade::kCoral);
inline constexpr Cor kTextoSobreAmbar = rgb(native::ui::identidade::kTextoSobreAmbar);
inline constexpr Cor kFoco         = rgb(native::ui::identidade::kFoco);

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
inline constexpr double kTransicao = native::ui::identidade::kTransicao;
inline constexpr double kHover     = native::ui::identidade::kHover;

} // namespace bs::builder::instalador::gui::tema
