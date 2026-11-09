// N9.5 fase 3B (docs/N9-5-AUDITORIA.md secao 14): a marca leve de HUD do produto (cpu/hud_scope.h): o escopo de uma rotina
// de HUD, o papel por bloco das escritas na RAM e o consumo pela copia do canal 2.
#include "test.h"

#include "cpu/hud_scope.h"
#include "cpu/sh4_mem.h"

#include <vector>

using namespace bs;

BS_TEST(hud_scope_marks_only_when_enabled_and_restores) {
    hud_marks_enable(false);
    std::vector<uint8_t> ram(16u << 20, 0);
    Sh4Context c{};
    c.ram = ram.data();
    {
        HudScope s(&c, kHudRoleCornerLeft);
        BS_CHECK(c.hud_role == 0);  // marca desligada (Original, 4:3): o escopo nao faz nada
    }
    hud_marks_enable(true);
    {
        HudScope outer(&c, kHudRoleCornerLeft);
        BS_CHECK(c.hud_role == kHudRoleCornerLeft);
        sh4_write32(&c, 0x8C200040, 0x12345678);  // uma escrita na RAM com a marca: o bloco recebe o papel
        {
            HudScope inner(&c, 2);
            BS_CHECK(c.hud_role == 2);
        }
        BS_CHECK(c.hud_role == kHudRoleCornerLeft);  // o escopo de dentro devolveu o de fora
    }
    BS_CHECK(c.hud_role == 0);
    sh4_write32(&c, 0x8C200080, 1);  // fora do escopo: nada anotado
    BS_CHECK(hud_block_take(0x00200040) == kHudRoleCornerLeft);
    BS_CHECK(hud_block_take(0x00200040) == 0);  // a copia consome
    BS_CHECK(hud_block_take(0x00200080) == 0);
    BS_CHECK(hud_param_role(&c) == 0);
    g_hud_dma_role = kHudRoleCornerLeft;  // durante a copia de um bloco, o papel do bloco
    BS_CHECK(hud_param_role(&c) == kHudRoleCornerLeft);
    g_hud_dma_role = -1;
    hud_marks_enable(false);
}

// revisao Codex 20261109-094056: as marcas expiram (o bloco so vale para a copia do render em que foi escrito ou do
// seguinte), a troca da marca descarta as pendentes e a pilha corrente nao e marcada
BS_TEST(hud_block_marks_expire_and_skip_the_stack) {
    hud_marks_enable(true);
    std::vector<uint8_t> ram(16u << 20, 0);
    Sh4Context c{};
    c.ram   = ram.data();
    c.r[15] = 0x8C300000;
    {
        HudScope s(&c, kHudRoleCornerLeft);
        sh4_write32(&c, 0x8C200040, 1);   // o buffer: marcado
        sh4_write32(&c, 0x8C200060, 1);
        sh4_write32(&c, 0x8C2FFF00, 1);   // a pilha (abaixo do r15): nao
        sh4_write32(&c, 0x8C300800, 1);   // logo acima do r15 (quadros dos chamadores): nao
    }
    BS_CHECK(hud_block_take(0x002FFF00) == 0 && hud_block_take(0x00300800) == 0);
    hud_render_tick();  // o render seguinte ainda copia o bloco
    BS_CHECK(hud_block_take(0x00200040) == kHudRoleCornerLeft);
    hud_render_tick();
    hud_render_tick();  // dois renders depois: a marca de 0x...60 expirou
    BS_CHECK(hud_block_take(0x00200060) == 0);
    {
        HudScope s(&c, kHudRoleCornerLeft);
        sh4_write32(&c, 0x8C200080, 1);
    }
    hud_marks_enable(false);  // 16:9 -> 4:3: descarta
    hud_marks_enable(true);
    BS_CHECK(hud_block_take(0x00200080) == 0);
    hud_marks_enable(false);
}
