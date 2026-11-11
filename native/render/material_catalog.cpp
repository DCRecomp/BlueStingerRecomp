#include "render/material_catalog.h"

#include <algorithm>

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
    // N9.6 fase 3: as fontes de luz, achadas pelos desenhos sob as luzes visiveis dos quadros (bs-ir-tela texturas nas IRs
    // do corredor RCG02, do quarto e do cais; tmp/sessao-20261110/n96/f3) e conferidas pela textura exportada
    {{64, 64, 0xea09b01a84c2c4aaull}, kMaterialEmissive, "corredor RCG02: o brilho azul das faixas de luz do teto (translucida One/Zero)"},
    {{64, 16, 0x10b0adb7ecfefb45ull}, kMaterialEmissive, "corredor RCG02: a placa EXIT acesa (opaca)"},
    {{64, 32, 0x14cb38a2d38b4ed9ull}, kMaterialEmissive, "corredor RCG02: a placa HEIGHT LIMIT acesa (opaca)"},
    {{64, 64, 0xe6aa04e85bbde090ull}, kMaterialEmissive, "quarto RCG02: o brilho laranja da faixa de luz do chao (translucida One/Zero)"},
    // N9.7 (do prototipo do Codex, 2026-11-08; docs/N9-7-AGUA.md): medido na IR do cais RCG00 do passo_cgate no passo 2800
    // (comando `ir cais` do assistido, `bs-ir-tela materiais`): 46 desenhos translucidos ModulateAlpha, SrcAlpha/InvSrcAlpha,
    // o mar junto ao cais e o horizonte; nenhuma outra textura daquela IR e agua
    {{32, 32, 0x7458e3539d31cd8dull}, kMaterialWater, "cais RCG00: o mar (46 desenhos translucidos ModulateAlpha)"},
};
// clang-format on
} // namespace

std::span<const MaterialEntry> material_table() { return kTable; }

uint32_t material_flags(const TextureId& id) {
    for (const MaterialEntry& e : kTable)
        if (e.id == id) return e.flags;
    return 0;
}

uint32_t emissive_color(std::span<const uint32_t> argb) {
    double r = 0, g = 0, b = 0;
    for (uint32_t p : argb) {
        const double a = double(p >> 24) / 255.0, cr = double((p >> 16) & 0xFF), cg = double((p >> 8) & 0xFF),
                     cb = double(p & 0xFF);
        const double w = a * (0.299 * cr + 0.587 * cg + 0.114 * cb);  // os texels claros e visiveis dao o tom
        r += cr * w, g += cg * w, b += cb * w;
    }
    const double m = std::max({r, g, b});
    if (!(m > 0)) return 0xFFFFFF;
    auto c = [&](double v) { return uint32_t(std::clamp(v / m * 255.0 + 0.5, 0.0, 255.0)); };
    return (c(r) << 16) | (c(g) << 8) | c(b);
}

} // namespace bs::native::render
