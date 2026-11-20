#include "disc/gdi_export.h"

#include <cstdio>
#include <fstream>
#include <vector>

namespace bs::disc {

namespace fs = std::filesystem;

fs::path export_gdi(const DiscImage& img, const fs::path& dir, const std::string& nome,
                    const std::function<void(double)>& progress, const std::atomic<bool>* cancel) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) throw DiscError("nao foi possivel criar " + pathText(dir));
    const auto& tracks = img.tracks();
    if (tracks.empty()) throw DiscError("imagem sem faixas");
    uint64_t total = 0, done = 0;
    for (const auto& t : tracks) total += t.length;
    std::string           sheet = std::to_string(tracks.size()) + "\r\n";
    constexpr uint32_t    kChunk = 512;  // setores por escrita
    std::vector<uint8_t>  buf(size_t(kChunk) * 2352);
    for (const auto& t : tracks) {
        char name[32];
        std::snprintf(name, sizeof name, "track%02d.%s", t.number, t.isData() ? "bin" : "raw");
        std::ofstream o(dir / name, std::ios::binary | std::ios::trunc);
        if (!o) throw DiscError("nao foi possivel criar " + pathText(dir / name));
        for (uint32_t i = 0; i < t.length;) {
            if (cancel && cancel->load()) throw DiscError("cancelado");
            const uint32_t n = std::min(kChunk, t.length - i);
            for (uint32_t k = 0; k < n; ++k) img.readRawSector(t, i + k, buf.data() + size_t(k) * 2352);
            o.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(size_t(n) * 2352));
            if (!o) throw DiscError("falha ao gravar " + pathText(dir / name) + " (espaco em disco?)");
            i += n, done += n;
            if (progress && total) progress(double(done) / double(total));
        }
        o.close();
        if (!o) throw DiscError("falha ao gravar " + pathText(dir / name));
        // "<numero> <lba> <tipo: 4 dados, 0 audio> <bytes por setor> <arquivo> 0"
        sheet += std::to_string(t.number) + " " + std::to_string(t.start_lba) + " " + (t.isData() ? "4" : "0") + " 2352 " + name + " 0\r\n";
    }
    const fs::path gdi = dir / (nome + ".gdi");
    std::ofstream  g(gdi, std::ios::binary | std::ios::trunc);
    g << sheet;
    g.close();
    if (!g) throw DiscError("nao foi possivel gravar " + pathText(gdi));
    if (progress) progress(1.0);
    return gdi;
}

} // namespace bs::disc
