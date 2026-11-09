// N8.7 (docs/N8-7-KAMUI.md): o ListSink nativo (runtime/kamui), sem o jogo e sem o hardware simulado: os blocos
// de 32 bytes da FIFO de poligonos, a juncao dos parametros de 64 bytes, o fim de lista e a IR decodificada.
#include "test.h"

#include "kamui/list_sink.h"
#if BS_VALIDACAO
#include "kamui/dma_origin.h"
#include "kamui/draw_class.h"
#endif

#include <cstring>

using namespace bs::native::render;
using bs::kamui::ListFrame;
using bs::kamui::ListSink;

namespace {

uint32_t fbits(float f) {
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return w;
}

// PCW: tipo (31-29), fim de strip (28), lista (26-24), controle do objeto (7-0)
uint32_t pcw(uint32_t type, uint32_t list, uint32_t obj, bool end = false) {
    return (type << 29) | (end ? 1u << 28 : 0) | (list << 24) | obj;
}

void feed(ListSink& s, std::initializer_list<uint32_t> w) {
    uint32_t b[8] = {};
    size_t   i    = 0;
    for (uint32_t x : w) b[i++] = x;
    s.feed(b);
}

// vertice empacotado (tipo 0): x, y, 1/w e a cor nas palavras 1..3 e 6
void vertex(ListSink& s, float x, float y, uint32_t argb, bool end, uint32_t list = 0) {
    feed(s, {pcw(7, list, 0, end), fbits(x), fbits(y), fbits(1.f), 0, 0, argb, 0});
}

FrameParams frame() {
    FrameParams f;
    f.width = 640, f.height = 480;
    return f;
}

} // namespace

BS_TEST(kamui_list_sink_strip_is_one_draw_and_list_end_reports_the_list) {
    ListSink s;
    s.list_init();
    feed(s, {pcw(4, 0, 0x02), 0x80000000u, 0x20000000u, 0, 0, 0, 0, 0});  // global opaco, gouraud
    vertex(s, 0, 0, 0xFFFF0000, false);
    vertex(s, 10, 0, 0xFF00FF00, false);
    vertex(s, 0, 10, 0xFF0000FF, false);
    vertex(s, 10, 10, 0xFFFFFFFF, true);
    uint32_t end[8] = {pcw(0, 0, 0)};
    const bs::kamui::BlockResult r = s.feed(end);
    BS_CHECK(r.list_end == 0 && !r.invalid);
    BS_CHECK(s.bytes() == 6 * 32 && s.params().size() == 6);
    ListFrame f;
    s.build(frame(), 0, f);
    BS_CHECK(f.cl.draws.size() == 1 && f.cl.draws[0].vertex_count == 4 && f.cl.vertices.size() == 4);
    BS_CHECK(f.triangles == 2 && f.cl.draws[0].state.list == ListType::Opaque && f.cl.draws[0].state.gouraud);
    BS_CHECK(f.cl.vertices[1].x == 10.f && f.cl.vertices[1].color == 0xFF00FF00u);
}

BS_TEST(kamui_list_sink_joins_64_byte_parameters_and_rejects_invalid_type) {
    ListSink s;
    s.list_init();
    // global de intensidade com textura e offset (64 bytes): a segunda metade nao e um parametro
    feed(s, {pcw(4, 2, 0x2C), 0, 0, 0, 0, 0, 0, 0});
    BS_CHECK(s.params().empty());
    feed(s, {fbits(1.f), fbits(1.f), fbits(1.f), fbits(1.f), fbits(0.f), 0, 0, 0});
    BS_CHECK(s.params().size() == 1 && s.params()[0].words == 16 && s.params()[0].list == 2);
    uint32_t bad[8] = {3u << 29};  // tipo 3 nao existe
    BS_CHECK(s.feed(bad).invalid);
    s.list_init();
    BS_CHECK(s.params().empty() && s.bytes() == 0);
}

BS_TEST(kamui_list_sink_sprite_is_two_triangles_and_texture_table_dedups) {
    ListSink s;
    s.list_init();
    const uint32_t tcw = 0x08000100;  // 565 twiddled
    const uint32_t tsp = (1u << 3) | 1u;  // 16 x 16
    feed(s, {pcw(5, 2, 0x08), 0, tsp, tcw, 0xFFFFFFFF, 0, 0, 0});  // sprite com textura
    for (int k = 0; k < 2; ++k) {
        // sprite: 64 bytes; A (0,0), B (8,0), C (8,8) com z nas palavras 1..9, D (0,8) sem z nas 10..11, UV de 16 bits
        // nas 13..15
        feed(s, {pcw(7, 2, 0, true), fbits(0), fbits(0), fbits(1), fbits(8), fbits(0), fbits(1), fbits(8)});
        feed(s, {fbits(8), fbits(1), fbits(0), fbits(8), 0, 0, 0, 0});
    }
    ListFrame f;
    s.build(frame(), 64, f);
    BS_CHECK(f.sprites == 2 && f.cl.draws.size() == 4 && f.textures.size() == 1);
    BS_CHECK(f.textures[0].tcw == tcw && f.textures[0].width == 16 && f.textures[0].height == 16 && f.textures[0].stride == 64);
    BS_CHECK(f.cl.draws[0].state.texture.value == 1 && f.cl.draws[3].state.texture.value == 1);
}

BS_TEST(kamui_list_sink_modifier_volume_closes_by_the_global_instruction) {
    ListSink s;
    s.list_init();
    auto tri = [&](bool end) {
        feed(s, {pcw(7, 1, 0, end), fbits(0), fbits(0), fbits(2), fbits(9), fbits(0), fbits(2), fbits(0)});
        feed(s, {fbits(9), fbits(2), 0, 0, 0, 0, 0, 0});
    };
    feed(s, {pcw(4, 1, 0x40), 0u << 29, 0, 0, 0, 0, 0, 0});  // volume: instrucao 0 (poligono do volume)
    tri(false);
    feed(s, {pcw(4, 1, 0x40), 1u << 29, 0, 0, 0, 0, 0, 0});  // instrucao 1: ultimo poligono, inclusao
    tri(true);
    ListFrame f;
    s.build(frame(), 0, f);
    BS_CHECK(f.cl.modifiers.size() == 1 && f.cl.modifiers[0].triangle_count == 2 && f.cl.modifiers[0].closed);
    BS_CHECK(f.cl.modifiers[0].inclusion && f.cl.modifiers[0].or_from == ~0u && f.volume_inclusion == 1);
    BS_CHECK(f.modifier_params == 2 && f.modifier_unclosed == 0 && f.cl.draws.empty());
}

BS_TEST(kamui_list_sink_unsupported_texture_is_counted_not_drawn) {
    ListSink s;
    s.list_init();
    feed(s, {pcw(4, 0, 0x08), 0, 0, (4u << 27), 0, 0, 0, 0});  // textura bump (formato 4): sem suporte
    vertex(s, 0, 0, 0, false);
    vertex(s, 1, 0, 0, false);
    vertex(s, 0, 1, 0, true);
    ListFrame f;
    s.build(frame(), 0, f);
    BS_CHECK(f.cl.draws.empty() && f.unsupported == 3 && f.skipped_params == 3);
}

#if BS_VALIDACAO
// N9.5 fase 2 (so validacao): a proveniencia de cada parametro chega a cada desenho da IR, alinhada a cl.draws: a do
// vertice que abriu o desenho (os vertices seguintes de um strip nao mudam a do desenho aberto) e a do global dele
BS_TEST(kamui_list_sink_origin_follows_each_draw) {
    using bs::kamui::DrawKind;
    using bs::kamui::ParamOrigin;
    auto org = [](uint32_t a, uint32_t b, bool dma = false) {
        ParamOrigin o;
        o.fn[0] = a, o.fn[1] = b, o.dma = dma;
        return o;
    };
    ListSink s;
    s.list_init();
    s.set_origin(org(0x8C170000, 0x8C050000));
    feed(s, {pcw(4, 0, 0x02), 0x80000000u, 0x20000000u, 0, 0, 0, 0, 0});  // global 0
    s.set_origin(org(0x8C170010, 0x8C050000));
    vertex(s, 0, 0, 0xFFFF0000, false);  // 1: abre o strip
    s.set_origin(org(0x8C170020, 0x8C050000));
    vertex(s, 10, 0, 0xFF00FF00, false);
    vertex(s, 0, 10, 0xFF0000FF, false);
    vertex(s, 10, 10, 0xFFFFFFFF, true);  // o strip continua no mesmo desenho
    // um sprite pelo DMA: global 5 e um parametro de 64 bytes (6), dois desenhos
    s.set_origin(org(0x8C180000, 0x8C060000, true));
    feed(s, {pcw(5, 2, 0x00), 0, 0, 0, 0xFFFFFFFF, 0, 0, 0});
    feed(s, {pcw(7, 2, 0, true), fbits(0), fbits(0), fbits(1), fbits(8), fbits(0), fbits(1), fbits(8)});
    feed(s, {fbits(8), fbits(1), fbits(0), fbits(8), 0, 0, 0, 0});
    BS_CHECK(s.origins().size() == s.params().size() && s.params().size() == 7);
    ListFrame f;
    s.build(frame(), 0, f);
    BS_CHECK(f.cl.draws.size() == 3 && f.origins.size() == 3);
    const auto& a = f.origins[0];
    // o desenho do strip abre no 3o vertice (o primeiro triangulo): a origem e a desse parametro (3)
    BS_CHECK(a.kind == DrawKind::Strip && a.param == 3 && a.global_param == 0);
    BS_CHECK(a.vertex.fn[0] == 0x8C170020 && a.global.fn[0] == 0x8C170000 && a.vertex.fn[1] == 0x8C050000 && !a.vertex.dma);
    for (int k = 1; k < 3; ++k) {
        const auto& b = f.origins[size_t(k)];
        BS_CHECK(b.kind == DrawKind::Sprite && b.param == 6 && b.global_param == 5);
        BS_CHECK(b.vertex.fn[0] == 0x8C180000 && b.vertex.fn[1] == 0x8C060000 && b.vertex.dma && b.global.dma);
    }
    // revisao Codex 20261108-201953: um parametro de 64 bytes e da 1a metade (o PCW); metades de cadeias diferentes
    // ficam marcadas
    s.set_origin(org(0x8C180100, 0x8C060100, true));
    feed(s, {pcw(7, 2, 0, true), fbits(0), fbits(0), fbits(1), fbits(8), fbits(0), fbits(1), fbits(8)});
    s.set_origin(org(0x8C180200, 0x8C060100, true));
    feed(s, {fbits(8), fbits(1), fbits(0), fbits(8), 0, 0, 0, 0});
    BS_CHECK(s.origins().size() == 8 && s.origins()[7].fn[0] == 0x8C180100 && s.origins()[7].split);
    BS_CHECK(!s.origins()[6].split && !s.origins()[3].split);
    // TA_LIST_INIT descarta as origens junto com os parametros
    s.list_init();
    BS_CHECK(s.origins().empty() && s.params().empty());
}
#endif

#if BS_VALIDACAO
// N9.5 fase 2: a marca cena/HUD com as cadeias medidas nas seis cenas (docs/N9-5-AUDITORIA.md secao 13)
BS_TEST(kamui_draw_class_by_library_entry_and_dma_inv_w) {
    using namespace bs::kamui;
    auto org = [](std::initializer_list<uint32_t> fns, bool dma) {
        DrawOrigin o;
        int        i = 0;
        for (uint32_t f : fns) o.vertex.fn[i++] = f;
        o.vertex.dma = dma;
        return o;
    };
    CommandList cl;
    for (float w : {0.5f, 0.25f, 0.125f, 0.2f}) {  // um quad com perspectiva (0..3) e um com 1/w constante (4..7)
        Vertex v;
        v.inv_w = w;
        cl.vertices.push_back(v);
    }
    for (int k = 0; k < 4; ++k) {
        Vertex v;
        v.inv_w = 0.149254f;
        cl.vertices.push_back(v);
    }
    const Draw persp{{}, 0, 4}, flat{{}, 4, 4};
    // modelos 3D: o desenho do Ninja entra pela 8C15F856, chamada pelo jogo
    const DrawOrigin model = org({0x8C17EA5C, 0x8C15F078, 0x8C15F856, 0x8C089520, 0x8C084340}, false);
    BS_CHECK(library_entry(model.vertex) == 0x8C15F856 && classify(model, flat, cl) == DrawClass::Scene);
    // sprites 2D do Ninja (o recorte de 8C1736A0)
    const DrawOrigin sprite = org({0x8C190B32, 0x8C1736A0, 0x8C07B4E0, 0x8C07862E}, false);
    BS_CHECK(game_routine(sprite.vertex) == 0x8C07B4E0 && classify(sprite, persp, cl) == DrawClass::Screen);
    // o mesmo recorte chamado por outra rotina do jogo (pode ser um sprite projetado da cena): desconhecida
    BS_CHECK(classify(org({0x8C190B32, 0x8C1736A0, 0x8C064A52}, false), persp, cl) == DrawClass::Unknown);
    BS_CHECK(classify(org({0x8C16C610, 0x8C16CD2C, 0x8C16CED8, 0x8C0899D6}, false), flat, cl) == DrawClass::Screen);
    BS_CHECK(classify(org({0x8C16C610, 0x8C16CD2C, 0x8C16CED8, 0x8C060380}, false), flat, cl) == DrawClass::Unknown);
    // metades de cadeias diferentes: nada e afirmado
    DrawOrigin split = model;
    split.vertex.split = true;
    BS_CHECK(classify(split, persp, cl) == DrawClass::Unknown);
    // a copia do canal 2: 1/w variavel = cena; constante = ambiguo; sem o DMA = desconhecida
    const DrawOrigin dma = org({0x8C1C1E16, 0x8C18BB84, 0x8C18BC3C, 0x8C173044, 0x8C064A52, 0x8C064A20}, true);
    BS_CHECK(library_entry(dma.vertex) == 0x8C173044);
    BS_CHECK(classify(dma, persp, cl) == DrawClass::Scene && classify(dma, flat, cl) == DrawClass::Ambiguous);
    BS_CHECK(classify(org({0x8C1C1E16, 0x8C18BB84, 0x8C18BC3C, 0x8C173044, 0x8C064A52}, false), persp, cl) ==
             DrawClass::Unknown);
    // cadeia que nao chega ao jogo, entrada fora da tabela e o escritor ja no jogo: desconhecida (nunca chutada)
    BS_CHECK(classify(org({0x8C19BF78, 0x8C160CD4, 0x8C16145E}, false), flat, cl) == DrawClass::Unknown);
    BS_CHECK(classify(org({0x8C19BF78, 0x8C160CD4, 0x8C07B4E0}, false), flat, cl) == DrawClass::Unknown);
    BS_CHECK(library_entry(org({0x8C07B4E0}, false).vertex) == 0 && classify(org({0x8C07B4E0}, false), flat, cl) == DrawClass::Unknown);
    // o desenho fora dos vertices da IR nao e lido
    BS_CHECK(classify(dma, Draw{{}, 6, 4}, cl) == DrawClass::Unknown);
    // N9.5 fase 3A: pelo DMA com o escritor do buffer conhecido, vale a tabela do par (entrada, rotina do jogo)
    auto writer = [&](std::initializer_list<uint32_t> fns) {
        DrawOrigin o = org(fns, true);
        o.vertex.dma_writer = true;
        return o;
    };
    const DrawOrigin corner = writer({0x8C169C30, 0x8C1692CC, 0x8C060278, 0x8C060380, 0x8C07B4E0});
    BS_CHECK(classify(corner, flat, cl) == DrawClass::Screen);
    BS_CHECK(hud_lookup(library_entry(corner.vertex), game_routine(corner.vertex))->role == HudRole::CornerLeft);
    BS_CHECK(hud_lookup(0x8C16CED8, 0x8C0899D6)->role == HudRole::CornerLeft);
    BS_CHECK(hud_lookup(0x8C1692CC, 0x8C03C0C4)->role == HudRole::Center);
    BS_CHECK(hud_lookup(0x8C16CED8, 0x8C01F0AE)->role == HudRole::Menu);
    // a mesma entrada 8C1692CC desenha um sprite preso ao mundo quando o jogo o projetou (8C0584CC): cena
    BS_CHECK(classify(writer({0x8C169C30, 0x8C1692CC, 0x8C0584CC, 0x8C058286}), flat, cl) == DrawClass::Scene);
    // as particulas e as faiscas do cais nunca sao HUD
    BS_CHECK(classify(writer({0x8C169450, 0x8C16938C, 0x8C057D00, 0x8C052240}), flat, cl) == DrawClass::Scene);
    // o escritor conhecido com um par fora da tabela: desconhecida, mesmo com 1/w constante
    BS_CHECK(classify(writer({0x8C169C30, 0x8C1692CC, 0x8C064A52}), flat, cl) == DrawClass::Unknown);
    BS_CHECK(hud_lookup(0x8C1692CC, 0x8C064A52) == nullptr && hud_lookup(0, 0x8C060278) == nullptr);
    // revisao Codex 20261109-024548: sem curinga; o desenho de modelos com um chamador nao medido e desconhecido
    BS_CHECK(classify(org({0x8C17EA5C, 0x8C15F078, 0x8C15F856, 0x8C064A52}, false), persp, cl) == DrawClass::Unknown);
    for (uint32_t e : {0x8C15F856u, 0x8C15F8B8u, 0x8C161738u, 0x8C160C12u, 0x8C1601FEu}) BS_CHECK(hud_lookup(e, 0) == nullptr);
}
#endif

#if BS_VALIDACAO
// N9.5 fase 3A (revisao Codex 20261109-024548): a sombra dos buffers do DMA so afirma o escritor de um bloco escrito
// inteiro pela mesma cadeia desde a copia anterior
BS_TEST(kamui_dma_origin_shadow_needs_the_whole_block_from_one_writer) {
    using bs::kamui::DmaOriginShadow;
    using bs::kamui::ParamOrigin;
    ParamOrigin hud, scene, out;
    hud.fn[0] = 0x8C169C30, hud.fn[1] = 0x8C1692CC, hud.fn[2] = 0x8C060278;
    scene.fn[0] = 0x8C169450, scene.fn[1] = 0x8C16938C, scene.fn[2] = 0x8C057D00;
    DmaOriginShadow s;
    s.enable();
    const uint32_t blk = 0x00200040;  // um bloco de 32 bytes do buffer
    BS_CHECK(!s.marked(blk));
    s.mark(blk);
    BS_CHECK(s.marked(blk) && !s.marked(blk + 32) && (s.bitmap()[blk >> 8] >> ((blk >> 5) & 7) & 1));
    // 8 escritas de 32 bits da mesma cadeia: o escritor vale, e a copia consome a anotacao
    for (uint32_t k = 0; k < 32; k += 4) s.note(blk + k, 4, hud);
    BS_CHECK(s.take(blk, out) && out.fn[2] == 0x8C060278);
    BS_CHECK(!s.take(blk, out));  // a proxima copia precisa de escrita nova
    // escrita parcial (o resto do bloco ficou do conteudo anterior): sem escritor
    for (uint32_t k = 0; k < 28; k += 4) s.note(blk + k, 4, hud);
    s.note(blk + 28, 2, hud);
    BS_CHECK(!s.take(blk, out));
    // os 32 bytes por bytes e meias palavras da mesma cadeia: vale
    for (uint32_t k = 0; k < 16; ++k) s.note(blk + k, 1, scene);
    for (uint32_t k = 16; k < 32; k += 2) s.note(blk + k, 2, scene);
    BS_CHECK(s.take(blk, out) && out.fn[2] == 0x8C057D00);
    // dois escritores no mesmo bloco: sem escritor
    for (uint32_t k = 0; k < 16; k += 4) s.note(blk + k, 4, hud);
    for (uint32_t k = 16; k < 32; k += 4) s.note(blk + k, 4, scene);
    BS_CHECK(!s.take(blk, out));
    // reset (sessao nova): nada marcado, nada anotado
    for (uint32_t k = 0; k < 32; k += 4) s.note(blk + k, 4, hud);
    s.reset();
    BS_CHECK(!s.marked(blk) && !s.take(blk, out));
    // espelhos P1/P2 da RAM caem no mesmo bloco (o offset e mascarado)
    s.mark(blk | 0x0C000000);
    BS_CHECK(s.marked(blk));
}
#endif
