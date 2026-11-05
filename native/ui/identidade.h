#pragma once
// N9.2: a identidade visual do projeto em dados, sem desenho (decisao 12.3: o instalador do N9.1 e a interface do jogo
// compartilham a identidade, nao o motor). Origem e direitos: docs/N9-1-ARTE.md (geometria do projeto, MIT). O instalador
// (builder/instalador/gui/tema.h e arte.cpp) e a interface da Remastered (app/remaster) desenham a partir daqui, cada um com o
// seu pintor. Cores em 0xRRGGBB; medidas do emblema normalizadas no quadrado 0..1.
#include <cstdint>

namespace bs::native::ui::identidade {

// Paleta
inline constexpr uint32_t kFundo        = 0x0B111C;  // azul-noite
inline constexpr uint32_t kFundoAlto    = 0x111A2A;
inline constexpr uint32_t kLateral      = 0x0E1626;
inline constexpr uint32_t kCartao       = 0x162133;
inline constexpr uint32_t kCartaoHover  = 0x1C2A40;
inline constexpr uint32_t kBorda        = 0x2A3A55;
inline constexpr uint32_t kTexto        = 0xEAF0F7;
inline constexpr uint32_t kTextoSuave   = 0xA4B1C4;
inline constexpr uint32_t kTextoApagado = 0x6B7A90;
inline constexpr uint32_t kAmbar        = 0xF4A637;  // acao principal, foco, edicao Original
inline constexpr uint32_t kAmbarEscuro  = 0xC97F17;
inline constexpr uint32_t kAgua         = 0x3CC6B8;  // edicao Remastered, sucesso
inline constexpr uint32_t kCoral        = 0xF06A5B;  // erro, acoes destrutivas
inline constexpr uint32_t kTextoSobreAmbar = 0x1A1206;
inline constexpr uint32_t kFoco         = 0xFFD27A;

// Animacoes discretas (segundos)
inline constexpr double kTransicao = 0.18;
inline constexpr double kHover     = 0.12;

// Emblema (o ferrao estilizado): triangulos no quadrado 0..1, desenhados nesta ordem. `cor` e uma das cores acima; `alfa`
// multiplica a dela. As duas faixas do corpo sao quadrilateros (quatro pontos) em kAmbarEscuro.
struct Triangulo {
    float    x0, y0, x1, y1, x2, y2;
    uint32_t cor;
    float    alfa;
};
inline constexpr Triangulo kEmblema[] = {
    // asas: duas de cada lado, em leque
    {0.47f, 0.36f, 0.12f, 0.22f, 0.20f, 0.46f, kAgua, 0.9f},
    {0.53f, 0.36f, 0.88f, 0.22f, 0.80f, 0.46f, kAgua, 0.9f},
    {0.47f, 0.44f, 0.16f, 0.52f, 0.30f, 0.64f, kAgua, 0.55f},
    {0.53f, 0.44f, 0.84f, 0.52f, 0.70f, 0.64f, kAgua, 0.55f},
    // cabeca e corpo (o ferrao para baixo)
    {0.41f, 0.12f, 0.59f, 0.12f, 0.50f, 0.24f, kAmbar, 1.f},
    {0.37f, 0.27f, 0.63f, 0.27f, 0.50f, 0.90f, kAmbar, 1.f},
};
// Faixas do corpo: comecam nestas alturas, com esta espessura; a largura acompanha as bordas do corpo.
inline constexpr float kFaixas[]        = {0.42f, 0.57f};
inline constexpr float kFaixaEspessura  = 0.045f;
inline constexpr float faixa_meia(float v) { return 0.13f * (0.90f - v) / (0.90f - 0.27f); }
// Fundo opcional do emblema: quadrado arredondado em kFundoAlto, contorno em kBorda
inline constexpr float kEmblemaRaio      = 0.22f;
inline constexpr float kEmblemaContorno  = 0.02f;

} // namespace bs::native::ui::identidade
