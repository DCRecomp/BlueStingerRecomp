#pragma once
// Imagens com um arquivo por faixa: GDI (TOSEC e afins) e CUE/BIN (Redump). Issues #1 e #3, plano em
// docs/IMAGENS-DISCO-PLANO.md (I2).
//
// GD-ROM: a area de alta densidade comeca na LBA 45000 (onde ficam IP.BIN e o ISO9660). As faixas dela ganham a
// sessao 2, para o bootTrack() (faixa de dados da ultima sessao) apontar para ela como no CDI.
#include "disc/disc_image.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bs::disc {

constexpr uint32_t kGdHighDensityLba = 45000;

class TrackFilesImage final : public DiscImage {
public:
    // `confine` (nao vazio): as faixas citadas precisam ficar dentro dessa pasta (imagem extraida de um pacote: uma folha
    // nao pode apontar para fora; revisao Codex 2026-11-19)
    static std::unique_ptr<TrackFilesImage> openGdi(const std::filesystem::path& gdi, const std::filesystem::path& confine = {});
    static std::unique_ptr<TrackFilesImage> openCue(const std::filesystem::path& cue, const std::filesystem::path& confine = {});

    // Expostos para testes: interpretam o texto. `fileSize` da o tamanho de cada arquivo citado (como no texto);
    // `files` recebe os nomes na ordem de file_index. Lancam DiscError com a linha do problema.
    using SizeFn = std::function<uint64_t(const std::string&)>;
    static std::vector<Track> parseGdi(const std::string& text, const SizeFn& fileSize, std::vector<std::string>& files);
    static std::vector<Track> parseCue(const std::string& text, const SizeFn& fileSize, std::vector<std::string>& files);

    std::string formatName() const override { return format_; }

protected:
    void readRaw(const Track& t, uint64_t offset, void* dst, size_t n) const override;

private:
    TrackFilesImage() = default;
    static std::unique_ptr<TrackFilesImage> open(const std::filesystem::path& p, bool cue, const std::filesystem::path& confine);

    std::vector<std::unique_ptr<InputFile>> files_;
    std::string                             format_;
};

} // namespace bs::disc
