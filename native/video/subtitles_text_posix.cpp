// N9-L: o desenho das legendas com FreeType entra na L4. Sem backend, ok() e falso e o filme toca sem legenda.
#include "video/subtitles.h"

namespace bs::native::video {

struct SubtitleRenderer::Impl {};

SubtitleRenderer::SubtitleRenderer() = default;
SubtitleRenderer::~SubtitleRenderer() = default;
bool SubtitleRenderer::ok() const { return false; }
void SubtitleRenderer::draw(std::span<uint32_t>, uint32_t, uint32_t, const std::wstring&) {}
void SubtitleRenderer::draw_overlay(std::span<uint32_t>, uint32_t, uint32_t, const std::wstring&, float, float, float, float,
                                    const OverlayStyle&) {}
std::unique_ptr<ISubtitleRasterizer> make_subtitle_rasterizer() { return std::make_unique<SubtitleRenderer>(); }

} // namespace bs::native::video
