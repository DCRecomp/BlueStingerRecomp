#include "render/material_catalog.h"

namespace bs::native::render {

namespace {
// clang-format off
// N9.6 fase 2b: medido na IR do quarto do passo_load_slot1 (render 2318, o quadro da L no VBlank 4640), com
// `bs-ir-tela texturas --cobertura` e o 1/w por pixel do opaco por baixo (tmp/sessao-20261110/n96/f2_coplanar.py); nas 22
// cenas da fase 3B do N9.5 a textura so aparece nessa area (o load e o C-GATE), sempre translucida, SrcAlpha/InvSrcAlpha e
// escrevendo a profundidade. O piso de grade do mesmo quarto (128x128-563c7fcc146c1f60) ficou de fora: o opaco por baixo
// dele e o fundo de um fosso (36 % de diferenca de 1/w), e o AO desse fundo nao e o da grade.
constexpr MaterialEntry kTable[] = {
    {{128, 128, 0x743d83eda77f1bb8ull}, kMaterialAoSurface,
     "quarto RCG02: os racks da parede direita (45 637 px finais mudam sem eles), 0,66 % de 1/w a frente da parede "
     "opaca nos 43 583 pixels medidos com opaco por baixo"},
};
// clang-format on
} // namespace

std::span<const MaterialEntry> material_table() { return kTable; }

uint32_t material_flags(const TextureId& id) {
    for (const MaterialEntry& e : kTable)
        if (e.id == id) return e.flags;
    return 0;
}

} // namespace bs::native::render
