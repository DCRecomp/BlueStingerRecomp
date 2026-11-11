#pragma once
// N9.7: agua modernizada da Remastered. A selecao e fechada por TextureId comprovado numa captura da area; nunca por
// cor, transparencia ou lista de desenho. O efeito altera apenas UVs da copia da IR enviada ao renderizador: geometria,
// profundidade, mistura, estado do jogo e a IR original permanecem intactos.
#include "render/render.h"
#include "render/texture_replacement.h"

#include <cstdint>
#include <span>

namespace bs::native::render {

enum class WaterQuality : uint8_t { Original, Enhanced, High };

// A marca existe somente na copia da IR enviada ao Vulkan. Os handles reais usam os 30 bits baixos; ela nao muda o
// formato binario de DrawState/IR e e removida antes de procurar a textura no dispositivo.
inline constexpr uint32_t kWaterEnhanced = 0x40000000u, kWaterHigh = 0x80000000u, kWaterHandleMask = 0x3FFFFFFFu;
WaterQuality              marked_water_quality(TextureHandle texture);
TextureHandle             unmark_water_texture(TextureHandle texture);
TextureHandle             mark_water_texture(TextureHandle texture, WaterQuality quality);

bool is_water_surface(const TextureId& id);

// `water_textures` acompanha os indices 1..N usados pelos TextureHandle da IR antes de eles serem trocados pelos handles
// do dispositivo. Marca os desenhos para o material Vulkan (ondas, reflexo e refracao) e anima suas UVs. Devolve quantos
// desenhos foram alterados. Original e listas malformadas nao mudam nada.
uint32_t animate_water(CommandList& cl, std::span<const uint8_t> water_textures, uint64_t frame, WaterQuality quality);

} // namespace bs::native::render
