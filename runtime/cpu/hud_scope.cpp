// N9.5 fase 3B: a marca leve de HUD no produto (hud_scope.h).
#include "cpu/hud_scope.h"

#include <algorithm>
#include <vector>

namespace bs {

bool      g_hud_marks      = false;
uint32_t* g_hud_block_role = nullptr;
uint32_t  g_hud_render     = 0;
int      g_hud_dma_role   = -1;

namespace {
std::vector<uint32_t> g_roles;  // (16 MB / 32) entradas, alocado so com a marca ligada
}

void hud_marks_enable(bool on) {
    if (on && g_roles.empty()) g_roles.assign((16u << 20) / 32, 0);
    // revisao Codex 20261109-094056: a troca 16:9 <-> 4:3 descarta as marcas pendentes
    if (on != g_hud_marks && !g_roles.empty()) std::fill(g_roles.begin(), g_roles.end(), 0u);
    g_hud_marks = on;
    // uma vez alocado, o mapa fica (um escopo aberto quando a marca desliga ainda pode escrever nele)
    if (!g_roles.empty()) g_hud_block_role = g_roles.data();
}

uint8_t hud_block_take(uint32_t off) {
    if (!g_hud_block_role) return 0;
    uint32_t&      r = g_hud_block_role[(off & 0x00FFFFFFu) >> 5];
    const uint32_t v = r;
    r                = 0;
    if (!v) return 0;
    // so a marca anotada no render corrente ou no anterior (o buffer de um quadro e montado e copiado entre dois renders)
    const uint32_t age = (g_hud_render << 8 >> 8) - (v >> 8);
    return (age & 0x00FFFFFFu) <= 1 ? uint8_t(v) : 0;
}

} // namespace bs
