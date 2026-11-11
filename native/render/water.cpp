#include "render/water.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bs::native::render {

bool is_water_surface(const TextureId& id) {
    // RCG00, mar ao redor do cais. Identificado na IR do passo 2800: textura 32x32, 46 desenhos translucidos
    // ModulateAlpha (SrcAlpha/InvSrcAlpha), inclusive a superficie visivel no canto inferior esquerdo. O hash e dos
    // texels ARGB8888 decodificados e independe do endereco/formato da VRAM.
    return id.width == 32 && id.height == 32 && id.content == 0x7458e3539d31cd8dull;
}

WaterQuality marked_water_quality(TextureHandle texture) {
    if (texture.value & kWaterHigh) return WaterQuality::High;
    if (texture.value & kWaterEnhanced) return WaterQuality::Enhanced;
    return WaterQuality::Original;
}

TextureHandle unmark_water_texture(TextureHandle texture) {
    texture.value &= kWaterHandleMask;
    return texture;
}

TextureHandle mark_water_texture(TextureHandle texture, WaterQuality quality) {
    texture.value &= kWaterHandleMask;
    if (quality == WaterQuality::High) texture.value |= kWaterHigh;
    else if (quality == WaterQuality::Enhanced) texture.value |= kWaterEnhanced;
    return texture;
}

uint32_t animate_water(CommandList& cl, std::span<const uint8_t> water_textures, uint64_t frame, WaterQuality quality) {
    if (quality == WaterQuality::Original) return 0;
    float horizon = std::numeric_limits<float>::max(), bottom = std::numeric_limits<float>::lowest();
    for (const Draw& d : cl.draws) {
        const uint32_t ti = d.state.texture.value;
        if (!ti || ti > water_textures.size() || !water_textures[ti - 1] || d.first_vertex > cl.vertices.size() ||
            d.vertex_count > cl.vertices.size() - d.first_vertex)
            continue;
        for (uint32_t k = 0; k < d.vertex_count; ++k) {
            const float y = cl.vertices[d.first_vertex + k].y;
            if (std::isfinite(y)) horizon = std::min(horizon, y), bottom = std::max(bottom, y);
        }
    }
    if (!(horizon <= bottom)) return 0;
    // O movimento grosso continua nas UVs para que a silhueta da textura se desloque. O detalhe, as normais, o reflexo
    // e a refracao sao por fragmento no Vulkan (pvr.frag), sem deformar a geometria nem o estado logico do jogo.
    const float strength = quality == WaterQuality::High ? 0.015f : 0.009f;
    const float phase = float(frame % 240000) * (quality == WaterQuality::High ? 0.037f : 0.029f);
    uint32_t changed = 0;
    for (Draw& d : cl.draws) {
        const uint32_t ti = d.state.texture.value;
        if (!ti || ti > water_textures.size() || !water_textures[ti - 1] || d.first_vertex > cl.vertices.size() ||
            d.vertex_count > cl.vertices.size() - d.first_vertex)
            continue;
        for (uint32_t k = 0; k < d.vertex_count; ++k) {
            Vertex& v = cl.vertices[d.first_vertex + k];
            // Duas ondas com frequencias/direcoes diferentes evitam uma simples rolagem da textura. A entrada e a UV
            // original (coordenada da propria malha), portanto vertices compartilhados recebem o mesmo deslocamento.
            const float u = v.u, vv = v.v;
            v.u += std::sin(vv * 0.73f + phase) * strength + std::sin((u + vv) * 0.19f - phase * 0.61f) * strength * 0.45f +
                   std::sin(u * 0.31f - vv * 0.47f + phase * 0.23f) * strength * 0.25f;
            v.v += std::sin(u * 0.57f - phase * 0.83f) * strength * 0.72f +
                   std::sin((u - vv) * 0.23f + phase * 0.37f) * strength * 0.32f +
                   std::sin(u * 0.17f + vv * 0.41f + phase * 0.19f) * strength * 0.22f;
        }
        d.state.texture = mark_water_texture(d.state.texture, quality);
        ++changed;
    }
    return changed;
}

} // namespace bs::native::render
