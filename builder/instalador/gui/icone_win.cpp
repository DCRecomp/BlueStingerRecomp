// N9.1, Windows: gera o icone do instalador (.ico com PNG de 16 a 256 px) a partir do emblema vetorial (arte.cpp), na
// compilacao. Uso: bs-instalador-icone <saida.ico>
#include "instalador/gui/arte.h"
#include "instalador/gui/pintor_d2d.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

using namespace bs::builder::instalador::gui;

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const int                         tamanhos[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};
    std::vector<std::vector<uint8_t>> pngs;
    const std::filesystem::path       tmp = std::filesystem::path(argv[1]).concat(L".tmp.png");
    for (int t : tamanhos) {
        Quadro q(float(t), float(t), 1.f);
        if (!q.ok()) return 1;
        q.rt()->BeginDraw();
        {
            PintorD2D p(q.rt(), 1.f);
            p.limpar({0, 0, 0, 0});
            // nos tamanhos pequenos o emblema ocupa o quadro todo, sem a moldura
            arte::emblema(p, 0, 0, float(t), t >= 32);
        }
        q.rt()->EndDraw();
        if (!q.salvar(tmp)) return 1;
        std::ifstream in(tmp, std::ios::binary);
        pngs.emplace_back(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::filesystem::remove(tmp);
    std::ofstream o(argv[1], std::ios::binary | std::ios::trunc);
    auto          u16 = [&](uint16_t v) { o.put(char(v & 255)).put(char(v >> 8)); };
    auto          u32 = [&](uint32_t v) { u16(uint16_t(v & 0xFFFF)), u16(uint16_t(v >> 16)); };
    const size_t  n = pngs.size();
    u16(0), u16(1), u16(uint16_t(n));
    uint32_t off = uint32_t(6 + 16 * n);
    for (size_t i = 0; i < n; ++i) {
        const int t = tamanhos[i];
        o.put(char(t >= 256 ? 0 : t)).put(char(t >= 256 ? 0 : t)).put(0).put(0);
        u16(1), u16(32), u32(uint32_t(pngs[i].size())), u32(off);
        off += uint32_t(pngs[i].size());
    }
    for (const auto& p : pngs) o.write(reinterpret_cast<const char*>(p.data()), std::streamsize(p.size()));
    return o ? 0 : 1;
}
