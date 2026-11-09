// N9.5 fase 2 (so validacao): a marca cena 3D x HUD 2D (draw_class.h).
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

DrawClass classify(const DrawOrigin& o, const Draw& d, const CommandList& cl) {
    if (o.vertex.split) return DrawClass::Unknown;  // as duas metades do parametro vieram de cadeias diferentes
    switch (library_entry(o.vertex)) {
    case 0x8C15F856:
    case 0x8C161738:
    case 0x8C160C12:
    case 0x8C1601FE: return DrawClass::Scene;
    // revisao Codex 20261108-201953: 8C1736A0 e o recorte geral dos sprites 2D do Ninja; ele recebe tambem sprites ja
    // projetados pelo jogo (as faiscas do cais vao por ele ate o DMA). So o chamador medido (8C07B4E0) e tela; o resto
    // fica desconhecido. O mesmo para 8C16CED8 (chamador medido 8C0899D6).
    case 0x8C1736A0: return game_routine(o.vertex) == 0x8C07B4E0 ? DrawClass::Screen : DrawClass::Unknown;
    case 0x8C16CED8: return game_routine(o.vertex) == 0x8C0899D6 ? DrawClass::Screen : DrawClass::Unknown;
    case 0x8C173044: {
        if (!o.vertex.dma) return DrawClass::Unknown;  // a entrada do DMA sem a copia do canal 2: fora do medido
        if (d.vertex_count == 0 || d.first_vertex + d.vertex_count > cl.vertices.size()) return DrawClass::Unknown;
        const float w0 = cl.vertices[d.first_vertex].inv_w;
        for (uint32_t k = 1; k < d.vertex_count; ++k)
            if (cl.vertices[d.first_vertex + k].inv_w != w0) return DrawClass::Scene;  // projetado (perspectiva)
        return DrawClass::Ambiguous;
    }
    default: return DrawClass::Unknown;
    }
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
