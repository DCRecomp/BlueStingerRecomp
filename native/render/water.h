#pragma once
// N9.7 (docs/N9-7-PLANO.md): a agua aprimorada da Remastered. As superficies vem do catalogo de materiais
// (material_catalog.h, kMaterialWater, por TextureId da textura original): nada de lista propria, de marca no handle, de cor,
// alfa ou lista de desenho. O efeito e so do Vulkan, na variante -DWATER do fragmento: a IR, a geometria, a profundidade,
// a mistura e o estado do jogo nao mudam, e o caminho fiel (os shaders sem -DWATER) e o do N8.7.
#include "render/render.h"

#include <cstdint>
#include <functional>

namespace bs::native::render {

enum class WaterQuality : uint8_t { Original, Enhanced, High };

// A fase das ondas e o numero do render que o Renderer (transitional/render_core) conta: a contagem e do jogo, nao do
// relogio nem do dispositivo, entao sobrevive a recriacao do Vulkan e ao recuo para o software (sem salto nas ondas).
inline constexpr uint32_t kWaterPhasePeriod = 240000;
inline float              water_phase(uint64_t render) { return float(render % kWaterPhasePeriod); }

// O horizonte (o menor y) e o fundo (o maior y) dos vertices dos desenhos de agua do quadro, nos pixels da IR; desenhos
// com vertices fora da lista ficam de fora. false sem nenhum vertice de agua valido.
bool water_extent(const CommandList& cl, const std::function<bool(const Draw&)>& is_water, float& top, float& bottom);

} // namespace bs::native::render
