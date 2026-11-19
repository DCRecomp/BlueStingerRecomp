#pragma once
// Imagens CHD (MAME, chdman createcd) pela libchdr (BSD-3). Issue #2; docs/IMAGENS-DISCO-PLANO.md (I6).
// So nas ferramentas (BS_BUILD_TOOLS): o jogo le os dados preparados, nunca a imagem.
//
// Layout: os metadados de faixa (GD-ROM "CHGD"; CD "CHT2"/"CHTR") dao o tipo, os quadros guardados, o pregap e o
// preenchimento de cada faixa. Cada quadro do CHD tem 2448 bytes (2352 do setor + 96 de subcodigo). No GD-ROM, a 3a faixa
// abre a area de alta densidade na LBA 45000 (sessao 2, como no GDI e no CUE).
#include "disc/disc_image.h"

#include <memory>
#include <string>
#include <vector>

namespace bs::disc {

// Uma faixa como os metadados a descrevem (exposto para testes)
struct ChdTrackMeta {
    int         number = 0;
    std::string type, pgtype;
    uint32_t    frames = 0, pad = 0, pregap = 0, postgap = 0;  // pad: quadros de preenchimento ate multiplo de 4
    uint32_t    metaPad = 0;  // GD-ROM (PAD): quadros finais que nao sao dados
};

class ChdImage final : public DiscImage {
public:
    explicit ChdImage(const std::filesystem::path& path);
    ~ChdImage() override;

    std::string formatName() const override { return "CHD"; }

    // Exposto para testes: layout das faixas a partir dos metadados. `gd`: metadados CHGD (GD-ROM). `unitBytes`: bytes
    // por quadro no CHD (2448). Lanca DiscError para tipo desconhecido ou faixas fora de ordem.
    static std::vector<Track> layout(const std::vector<ChdTrackMeta>& meta, bool gd, uint32_t unitBytes);
    // Interpreta uma linha de metadados (CHGD, CHT2 ou CHTR)
    static ChdTrackMeta parseMeta(const std::string& text, bool gd);

protected:
    void readRaw(const Track& t, uint64_t offset, void* dst, size_t n) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bs::disc
