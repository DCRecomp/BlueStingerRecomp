#pragma once
// N9.5 fase 3B (docs/N9-5-AUDITORIA.md secao 14; arquitetura A1 escolhida pelo mantenedor em 2026-11-09): a marca leve de
// HUD no produto. O gerador emite BS_HUD_SCOPE(papel) so nas rotinas do HUD medidas na Parte A ([remaster] no
// config/bluestinger_us.toml; hoje as do canto inferior esquerdo, 8C060278 e 8C0899D6). Com a marca ligada:
//   - uma escrita direta no TA (store queue) leva o papel corrente no parametro;
//   - uma escrita da CPU na RAM anota o papel no bloco de 32 bytes (sh4_mem.h); a copia do canal 2 le e consome o papel
//     do bloco que copia (o conteudo do buffer de vertices do DMA).
// A marca so liga com hud_marks_enable (a Remastered com o quadro largo). Desligada (a Original, o 4:3, o software), o
// escopo nao muda nada, nenhuma escrita anota e nenhum papel chega a IR: o jogo e o desenho sao os de antes.
#include "cpu/sh4_context.h"

#include <cstdint>

namespace bs {

// os papeis (o mesmo numero de kamui::HudRole)
constexpr uint8_t kHudRoleCornerLeft = 1;

extern bool      g_hud_marks;       // a marca esta ligada
// o papel por bloco de 32 bytes da RAM e o render em que foi anotado: (render << 8) | papel (nulo antes da 1a vez ligada).
// Revisao Codex 20261109-094056: o bloco so vale para a copia do render em que foi escrito ou do seguinte; uma marca mais
// velha (um bloco reaproveitado depois, uma escrita que nunca foi copiada) expira sozinha.
extern uint32_t* g_hud_block_role;
extern uint32_t  g_hud_render;     // renders pedidos ate aqui (hud_render_tick)
extern int      g_hud_dma_role;    // durante a copia de um bloco pelo canal 2: o papel dele; -1 fora da copia

// o papel de um parametro que chega a entrada da area 4: o do bloco copiado (DMA) ou o da rotina em execucao
inline uint8_t hud_param_role(const Sh4Context* c) { return g_hud_dma_role >= 0 ? uint8_t(g_hud_dma_role) : c->hud_role; }

void hud_marks_enable(bool on);

// um render pedido (STARTRENDER): a idade das marcas
inline void hud_render_tick() { ++g_hud_render; }

// uma escrita da CPU na RAM com a marca ligada (off = offset na RAM). A pilha corrente (r15 - 64 KiB .. r15 + 4 KiB) fica de
// fora: as variaveis locais das rotinas de HUD nao sao o buffer de vertices.
inline void hud_role_write(const Sh4Context* c, uint32_t off) {
    off &= 0x00FFFFFFu;
    const uint32_t sp = c->r[15] & 0x00FFFFFFu;
    if (off + 0x10000u >= sp && off < sp + 0x1000u) return;
    g_hud_block_role[off >> 5] = (g_hud_render << 8) | c->hud_role;
}
// a copia do canal 2 le um bloco: o papel dele, consumido (a proxima copia precisa de escrita nova com a marca)
uint8_t hud_block_take(uint32_t off);

// o escopo de uma rotina de HUD: liga o papel na entrada e devolve o anterior em qualquer saida (retorno, redirecionamento
// de longjmp que sobe pelos quadros, excecao)
struct HudScope {
    Sh4Context* c;
    uint8_t     prev;
    HudScope(Sh4Context* ctx, uint8_t role) : c(ctx), prev(ctx->hud_role) {
        if (g_hud_marks) c->hud_role = role;
    }
    ~HudScope() { c->hud_role = prev; }
    HudScope(const HudScope&)            = delete;
    HudScope& operator=(const HudScope&) = delete;
};

} // namespace bs
