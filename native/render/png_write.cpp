#include "render/png_write.h"

#include <algorithm>
#include <array>

namespace bs::native::render {

namespace {

uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t v = i;
            for (int k = 0; k < 8; ++k) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            t[i] = v;
        }
        return t;
    }();
    c = ~c;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

void be32(std::string& o, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) o.push_back(char((v >> s) & 0xFF));
}

void chunk(std::string& o, const char* type, const std::string& data) {
    be32(o, uint32_t(data.size()));
    const size_t at = o.size();
    o.append(type, 4);
    o += data;
    be32(o, crc32(reinterpret_cast<const uint8_t*>(o.data() + at), o.size() - at));
}

} // namespace

std::string encode_png(uint32_t width, uint32_t height, std::span<const uint32_t> argb, bool alpha) {
    if (!width || !height || argb.size() != size_t(width) * height) return {};
    const size_t ch = alpha ? 4 : 3;
    std::string  raw;  // as linhas com o filtro 0 (nenhum)
    raw.reserve(size_t(height) * (1 + width * ch));
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back('\0');
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t t = argb[size_t(y) * width + x];
            raw.push_back(char((t >> 16) & 0xFF)), raw.push_back(char((t >> 8) & 0xFF)), raw.push_back(char(t & 0xFF));
            if (alpha) raw.push_back(char(t >> 24));
        }
    }
    std::string z = {char(0x78), char(0x01)};  // zlib: deflate, janela de 32 KiB, sem dicionario
    for (size_t at = 0;;) {
        const size_t n    = std::min<size_t>(raw.size() - at, 65535);
        const bool   last = at + n == raw.size();
        z.push_back(char(last ? 1 : 0));  // bloco "stored"
        z.push_back(char(n & 0xFF)), z.push_back(char(n >> 8));
        z.push_back(char(~n & 0xFF)), z.push_back(char((~n >> 8) & 0xFF));
        z.append(raw, at, n);
        at += n;
        if (last) break;
    }
    uint32_t a = 1, b = 0;  // adler-32
    for (unsigned char c : raw) a = (a + c) % 65521, b = (b + a) % 65521;
    be32(z, (b << 16) | a);

    std::string o = "\x89PNG\r\n\x1a\n";
    std::string ihdr;
    be32(ihdr, width), be32(ihdr, height);
    ihdr += char(8), ihdr += char(alpha ? 6 : 2), ihdr += '\0', ihdr += '\0', ihdr += '\0';
    chunk(o, "IHDR", ihdr);
    chunk(o, "IDAT", z);
    chunk(o, "IEND", {});
    return o;
}

} // namespace bs::native::render
