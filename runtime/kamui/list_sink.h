#pragma once
// N8.7 (docs/N8-7-KAMUI.md secao 5): o destino nativo das listas que o Kamui e a Ninja entregam ao TA. Recebe os blocos
// de 32 bytes da FIFO de poligonos (pelo `pref` das store queues ou pelo DMA do canal 2), junta os parametros de 64
// bytes, acompanha a lista aberta e conta os bytes (o prazo do fim de render depende deles). No STARTRENDER, decodifica o
// que chegou desde o TA_LIST_INIT direto para a IR (CommandList + a tabela das texturas usadas), com as regras do
// decodificador do legado (runtime/pvr/pvr_render.cpp Parser + transitional/render_capture Capture), sem ler nenhum
// registrador do PVR: o que vem do quadro (FrameParams, stride das texturas) chega pronto.
//
// O formato e o dos parametros do TA (PCW, ISP/TSP/TCW, vertices): e o que o codigo recompilado escreve. O que sai e a
// IR nativa; o hardware simulado (FIFO, interrupcoes, registradores) nao passa por aqui.
#include "render/render.h"
#include "render/soft/soft_raster.h"

#include <cstdint>
#include <vector>

namespace bs::kamui {

using namespace native::render;  // a IR e o rasterizador nativos (native/render), que o contrato do Kamui alimenta

// Um parametro do TA: 8 ou 16 palavras e a lista em que chegou (0 opaca, 1 modificador opaco, 2 translucida,
// 3 modificador translucido, 4 punch-through).
struct ListParam {
    uint32_t list = 0, words = 0;
    uint32_t w[16] = {};
    uint8_t  role  = 0;  // N9.5 fase 3B: o papel de HUD de quem enviou (ListSink::set_role); 0 = nenhum
};

// O que identifica os texels de uma textura (a IR so guarda o handle): TCW, tamanho do TSP e o stride linear.
struct TextureKey {
    uint32_t tcw = 0, width = 0, height = 0, stride = 0;
    bool     operator==(const TextureKey&) const = default;
};

#if BS_VALIDACAO
// N9.5 fase 2 (so validacao; docs/N9-5-AUDITORIA.md secao 13): a proveniencia de um parametro, as entradas mais internas
// da cadeia de chamadas do guest no momento da escrita (fn[0] = a funcao que escreveu; 0 = acabou a cadeia ou o
// separador de interrupcao) e se ele veio pela copia do canal 2 (DMA).
constexpr int kOriginDepth = 16;
struct ParamOrigin {
    uint32_t fn[kOriginDepth] = {};
    uint32_t site[kOriginDepth] = {};  // N9.5 fase 3B: o retorno do ponto de chamada de cada entrada (o PR nela)
    bool     dma = false;
    bool     split = false;  // um parametro de 64 bytes cujas duas metades vieram de cadeias diferentes
    // N9.5 fase 3A: com dma, fn e a cadeia de quem ESCREVEU o bloco no buffer da RAM (BS_PROVENIENCIA_DMA=1); sem isso, a
    // de quem disparou a copia
    bool     dma_writer = false;
};
// A proveniencia de um desenho da IR (ListFrame::origins, alinhado a cl.draws): o parametro de vertice que abriu o
// desenho e o global que deu o estado dele (indices em ListSink::params()).
enum class DrawKind : uint8_t { Strip, StripTriangle, Sprite };
struct DrawOrigin {
    ParamOrigin vertex, global;
    uint32_t    param = 0, global_param = 0;
    DrawKind    kind  = DrawKind::Strip;
};
#endif

// O resultado de um render: a IR, a tabela das texturas (TextureHandle.value - 1) e as contagens do decodificador.
struct ListFrame {
    CommandList             cl;
    std::vector<TextureKey> textures;
    uint32_t                texture_stride = 0;
    uint32_t modifier_params = 0, modifier_volumes = 0, modifier_unclosed = 0, skipped_params = 0, sprites = 0;
    uint32_t triangles = 0, skipped = 0, unsupported = 0, two_volume = 0;
    // volumes: fechados por inclusao/exclusao, com OU, descarte misturado no mesmo volume, instrucao invalida
    uint32_t volume_inclusion = 0, volume_exclusion = 0, volume_or = 0, volume_mixed_cull = 0, volume_bad_instruction = 0;
    uint32_t global_two_volume = 0;  // globais de poligono com "dois volumes" (o jogo nao usa; sem suporte na IR)
    // N9.5 fase 3B: o papel de HUD de cada desenho (alinhado a cl.draws), so quando a lista recebeu papeis
    // (ListSink::set_role; a Remastered com o quadro largo); vazio no resto
    std::vector<uint8_t> roles;
#if BS_VALIDACAO
    std::vector<DrawOrigin> origins;  // N9.5 fase 2: um por desenho de cl.draws, na mesma ordem
#endif
};

// Evento de um bloco: fim de uma lista (o numero dela) ou um parametro com tipo invalido.
struct BlockResult {
    int  list_end = -1;  // 0..4: a lista que acabou de fechar
    bool invalid  = false;
};

class ListSink {
public:
    // TA_LIST_INIT: listas novas (descarta o que havia); TA_LIST_CONT: continua as listas, sem lista aberta
    void list_init();
    void list_continue();
    // um bloco de 32 bytes da FIFO de poligonos
    BlockResult feed(const uint32_t block[8]);
    uint32_t    bytes() const { return bytes_; }  // bytes recebidos desde o TA_LIST_INIT
    const std::vector<ListParam>& params() const { return params_; }

    // Decodifica as listas recebidas na IR do render: `frame` (o tamanho e os parametros do quadro) e `texture_stride`
    // (TEXT_CONTROL) vem do chamador.
    void build(const FrameParams& frame, uint32_t texture_stride, ListFrame& out) const;
    // N9.5 fase 3B: o papel de HUD dos proximos parametros (vale ate a proxima chamada); liga ListFrame::roles nesta lista
    void set_role(uint8_t role) {
        role_     = role;
        roles_on_ = true;
    }

#if BS_VALIDACAO
    // N9.5 fase 2: a proveniencia dos proximos parametros (vale ate a proxima chamada) e a de cada parametro recebido
    void set_origin(const ParamOrigin& o) { origin_ = o; }
    const std::vector<ParamOrigin>& origins() const { return origins_; }
#endif

private:
#if BS_VALIDACAO
    ParamOrigin              origin_, half_origin_;  // half_origin_: a da 1a metade de um parametro de 64 bytes
    std::vector<ParamOrigin> origins_;  // alinhado a params_
#endif
    std::vector<ListParam> params_;
    uint8_t                role_ = 0, half_role_ = 0;
    bool                   roles_on_ = false;
    uint32_t               bytes_ = 0;
    int                    list_ = -1;  // lista aberta (-1: nenhuma)
    bool                   vertex64_ = false, pending_half_ = false;
    ListParam              half_;
    void push(const uint32_t* w, uint32_t words);
};

// Conversoes do estado e dos vertices decodificados para a IR (as regras ficam aqui, num lugar so; a captura do legado
// as usa tambem).
DrawState draw_state(const soft::Tri& t, uint32_t texture_handle);
Vertex    ir_vertex(const soft::Vertex& v);
// soft_raster/pvr_texture: os formatos de textura que o decodificador aceita (os outros: o poligono e omitido)
bool texture_supported(uint32_t tcw);

} // namespace bs::kamui
