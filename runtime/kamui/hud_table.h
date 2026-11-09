#pragma once
// N9.5 fase 3 (docs/N9-5-AUDITORIA.md secao 14): a tabela dos desenhos pela proveniencia, medida contra o oraculo
// diferencial da fase 0 (bs-ir-tela comparar) com a cadeia de chamadas da validacao (fase 2) e, no caminho do DMA, a
// cadeia de quem escreveu o buffer (fase 3A). Cada linha e um par (entrada na biblioteca, rotina do jogo): a entrada e a
// ultima rotina do SDK antes da primeira do jogo; a rotina e essa primeira rotina do jogo. So os pares medidos entram,
// sem curinga; o resto fica desconhecido.
#include <cstdint>

namespace bs::kamui {

enum class DrawClass : uint8_t { Unknown, Scene, Screen, Ambiguous };

// O papel de um desenho na tela, para o reposicionamento do HUD (fase 3B). Ate la, so documenta.
enum class HudRole : uint8_t {
    None,         // cena 3D (modelos, poligonos projetados, particulas, sprites presos ao mundo): segue a projecao
    CornerLeft,   // HUD do canto inferior esquerdo (moldura do alvo, nome, barra de vida)
    Center,       // mensagens e textos centrais
    Menu,         // menus e telas 2D de tela cheia: ficam no 4:3 centralizado
    Unplaced,     // tela, sem papel conhecido (fica como esta: centralizado no 4:3)
};

struct HudEntry {
    uint32_t    entry, routine;
    DrawClass   cls;
    HudRole     role;
    const char* what;
};

// clang-format off
inline constexpr HudEntry kHudTable[] = {
    // geometria projetada do Ninja, com os chamadores medidos (revisao Codex 20261109-024548: sem curinga; um chamador
    // novo fica desconhecido)
    {0x8C15F856, 0x8C082C80, DrawClass::Scene, HudRole::None, "modelos (desenho de objetos)"},
    {0x8C15F856, 0x8C089520, DrawClass::Scene, HudRole::None, "modelos (desenho de objetos)"},
    {0x8C15F856, 0x8C08951A, DrawClass::Scene, HudRole::None, "modelos (desenho de objetos)"},
    {0x8C15F8B8, 0x8C084340, DrawClass::Scene, HudRole::None, "modelos (desenho de objetos, variante)"},
    {0x8C161738, 0x8C07B4E0, DrawClass::Scene, HudRole::None, "modelos (hierarquia)"},
    {0x8C160C12, 0x8C082C80, DrawClass::Scene, HudRole::None, "poligonos projetados"},
    {0x8C1601FE, 0x8C082C80, DrawClass::Scene, HudRole::None, "poligonos projetados"},
    // cena desenhada como sprite/quad 2D ja projetado pelo jogo
    {0x8C16938C, 0x8C057D00, DrawClass::Scene, HudRole::None, "particulas e faiscas (cais)"},
    {0x8C1692CC, 0x8C0584CC, DrawClass::Scene, HudRole::None, "sprite preso ao mundo (projetado pelo jogo)"},
    // HUD do gameplay
    {0x8C1692CC, 0x8C060278, DrawClass::Screen, HudRole::CornerLeft, "moldura do alvo e nome do personagem"},
    {0x8C16CED8, 0x8C0899D6, DrawClass::Screen, HudRole::CornerLeft, "barra de vida"},
    {0x8C1692CC, 0x8C03C0C4, DrawClass::Screen, HudRole::Center, "mensagens (texto central)"},
    {0x8C1692CC, 0x8C0626AA, DrawClass::Screen, HudRole::Unplaced, "elemento fora da tela (x ~800; papel nao identificado)"},
    {0x8C1736A0, 0x8C07B4E0, DrawClass::Screen, HudRole::Unplaced, "sprites degenerados na origem (invisiveis)"},
    // menus e telas 2D
    {0x8C1692CC, 0x8C021560, DrawClass::Screen, HudRole::Menu, "menu de pausa (texto)"},
    {0x8C16CED8, 0x8C01F0AE, DrawClass::Screen, HudRole::Menu, "menu de pausa"},
    {0x8C16CED8, 0x8C01FAEE, DrawClass::Screen, HudRole::Menu, "menu de pausa"},
    {0x8C16CED8, 0x8C01FD40, DrawClass::Screen, HudRole::Menu, "menu de pausa"},
    {0x8C16CED8, 0x8C01FC60, DrawClass::Screen, HudRole::Menu, "menu de pausa"},
    {0x8C16CED8, 0x8C020140, DrawClass::Screen, HudRole::Menu, "terminal ESER"},
    {0x8C16CED8, 0x8C03C276, DrawClass::Screen, HudRole::Menu, "terminal ESER"},
};
// clang-format on

// a linha da tabela de um par (entrada, rotina); nullptr se nao foi medido
inline const HudEntry* hud_lookup(uint32_t entry, uint32_t routine) {
    if (!entry) return nullptr;
    for (const HudEntry& e : kHudTable)
        if (e.entry == entry && e.routine == routine) return &e;
    return nullptr;
}

} // namespace bs::kamui
