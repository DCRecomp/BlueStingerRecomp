#pragma once
// As fontes do texto embutidas no executavel (Noto Sans Regular = 0, SemiBold = 1; OFL 1.1, third_party/fonts/noto-sans).
// Linux: fonts_embed.S.in (.incbin) e fonts_embed_posix.cpp; Windows (N9.2): vetores gerados por fonts_embed.cmake.
#include <cstddef>

namespace bs::native::text {

struct FontBlob {
    const unsigned char* data = nullptr;
    std::size_t          size = 0;
};

FontBlob embedded_font(int index);

} // namespace bs::native::text
