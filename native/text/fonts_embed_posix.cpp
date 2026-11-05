// Linux: as fontes vem do fonts_embed.S (gerado de fonts_embed.S.in, .incbin).
#include "text/fonts_embed.h"

extern "C" {
extern const unsigned char bs_font_noto_regular[], bs_font_noto_regular_end[];
extern const unsigned char bs_font_noto_semibold[], bs_font_noto_semibold_end[];
}

namespace bs::native::text {

FontBlob embedded_font(int i) {
    if (i == 0) return {bs_font_noto_regular, std::size_t(bs_font_noto_regular_end - bs_font_noto_regular)};
    if (i == 1) return {bs_font_noto_semibold, std::size_t(bs_font_noto_semibold_end - bs_font_noto_semibold)};
    return {};
}

} // namespace bs::native::text
