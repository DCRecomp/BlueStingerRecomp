#pragma once
// N9-L (L4, L5): um SFD montado nos testes, sem conteudo do jogo. Cabecalho "Sofdec Stream", packs MPEG-1 com o video de
// tests/data/video/barras_64x48.m1v (barras de cor do ffmpeg, 12 quadros de 64x48 a 30 fps) e um ADX estereo de 0,4 s a
// 22050 Hz gerado aqui, intercalados como num SFD do disco. Precisa de BS_TEST_DATA_DIR.
#include "video/sfd_media.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <span>
#include <vector>

namespace bs::test {

inline std::vector<uint8_t> sfd_read_all(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

inline void sfd_pes(std::vector<uint8_t>& o, uint8_t id, std::span<const uint8_t> payload, int64_t pts) {
    std::vector<uint8_t> h = {0xFF};
    if (pts >= 0) {
        h.push_back(uint8_t(0x21 | ((pts >> 29) & 0x0E)));
        h.push_back(uint8_t(pts >> 22)), h.push_back(uint8_t(((pts >> 14) & 0xFE) | 1));
        h.push_back(uint8_t(pts >> 7)), h.push_back(uint8_t(((pts << 1) & 0xFE) | 1));
    } else h.push_back(0x0F);
    const size_t len = h.size() + payload.size();
    o.insert(o.end(), {0, 0, 1, id, uint8_t(len >> 8), uint8_t(len)});
    o.insert(o.end(), h.begin(), h.end());
    o.insert(o.end(), payload.begin(), payload.end());
}

inline const std::filesystem::path& sfd_bars_path() {
    static const std::filesystem::path p = std::filesystem::path(BS_TEST_DATA_DIR) / "video" / "barras_64x48.m1v";
    return p;
}

// 12 quadros de 64x48 a 30 fps (0,4 s) e 0,4 s de ADX estereo a 22050 Hz (8820 amostras por canal)
inline std::vector<uint8_t> make_sfd(std::vector<int16_t>* pcmOut = nullptr) {
    namespace nv              = bs::native::video;
    const auto           es   = sfd_read_all(sfd_bars_path());
    const uint32_t       rate = 22050, frames = rate * 4 / 10;
    std::vector<int16_t> pcm(size_t(frames) * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        pcm[2 * i]     = int16_t(8000 * std::sin(2 * std::numbers::pi * 440 * i / rate));
        pcm[2 * i + 1] = int16_t(6000 * std::sin(2 * std::numbers::pi * 660 * i / rate));
    }
    const auto adx = nv::adx_encode(pcm, 2, rate);
    if (pcmOut) nv::adx_decode(adx, *pcmOut);
    std::vector<uint8_t> f(0x800, 0);
    std::memcpy(f.data(), "Sofdec Stream", 13);
    const size_t chunk = 700;
    for (size_t v = 0, a = 0, n = 0; v < es.size() || a < adx.size(); ++n) {
        f.insert(f.end(), {0, 0, 1, 0xBA, 0x21, 0, 1, 0, 1, 0x80, 0, 1});
        if (n == 0) f.insert(f.end(), {0, 0, 1, 0xBB, 0, 6, 0x80, 0, 1, 0x04, 0xE1, 0xFF});
        if (v < es.size()) {
            const size_t k = std::min(chunk, es.size() - v);
            sfd_pes(f, 0xE0, std::span(es).subspan(v, k), n == 0 ? 90000 : -1), v += k;
        }
        if (a < adx.size()) {
            const size_t k = std::min(chunk, adx.size() - a);
            sfd_pes(f, 0xC0, std::span(adx).subspan(a, k), n == 0 ? 90000 : -1), a += k;
        }
    }
    f.insert(f.end(), {0, 0, 1, 0xB9});
    f.resize((f.size() + 0x7FF) & ~size_t(0x7FF), 0);  // setores de 2048
    return f;
}

} // namespace bs::test
