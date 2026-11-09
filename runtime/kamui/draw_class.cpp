// N9.5 fases 2 e 3A (so validacao): a marca cena 3D x HUD 2D (draw_class.h), pela tabela de hud_table.h.
#include "kamui/draw_class.h"

namespace bs::kamui {

uint32_t library_entry(const ParamOrigin& o) {
    uint32_t prev = 0;
    for (uint32_t f : o.fn) {
        if (!f) return 0;  // a cadeia acabou (ou o separador de interrupcao) antes de chegar ao jogo
        if (f < kGameCodeEnd) return prev;
        prev = f;
    }
    return 0;  // a janela de kOriginDepth entradas nao chegou ao jogo
}

uint32_t game_routine(const ParamOrigin& o) {
    for (uint32_t f : o.fn) {
        if (!f) return 0;
        if (f < kGameCodeEnd) return f;
    }
    return 0;
}

uint32_t game_site(const ParamOrigin& o) {
    for (int i = 0; i < kOriginDepth; ++i) {
        if (!o.fn[i]) return 0;
        if (o.fn[i] < kGameCodeEnd) return o.site[i];
    }
    return 0;
}

DrawClass classify(const DrawOrigin& o, const Draw& d, const CommandList& cl) {
    if (o.vertex.split) return DrawClass::Unknown;  // as duas metades do parametro vieram de cadeias diferentes
    // N9.5 fase 3A: a copia do canal 2 sem o escritor do buffer conhecido (a primeira copia de um bloco, ou sem
    // BS_PROVENIENCIA_DMA): a cadeia e a de quem disparou a copia (8C173044); como na fase 2, 1/w variavel = cena
    if (o.vertex.dma && !o.vertex.dma_writer) {
        if (library_entry(o.vertex) != 0x8C173044) return DrawClass::Unknown;
        if (d.vertex_count == 0 || d.first_vertex + d.vertex_count > cl.vertices.size()) return DrawClass::Unknown;
        const float w0 = cl.vertices[d.first_vertex].inv_w;
        for (uint32_t k = 1; k < d.vertex_count; ++k)
            if (cl.vertices[d.first_vertex + k].inv_w != w0) return DrawClass::Scene;  // projetado (perspectiva)
        return DrawClass::Ambiguous;
    }
    // a tabela medida (hud_table.h): o par (entrada na biblioteca, rotina do jogo); o resto e desconhecido
    const HudEntry* e = hud_lookup(library_entry(o.vertex), game_routine(o.vertex), game_site(o.vertex));
    return e ? e->cls : DrawClass::Unknown;
}

const char* draw_class_name(DrawClass c) {
    switch (c) {
    case DrawClass::Scene: return "cena";
    case DrawClass::Screen: return "tela";
    case DrawClass::Ambiguous: return "ambiguo";
    default: return "desconhecida";
    }
}

} // namespace bs::kamui
