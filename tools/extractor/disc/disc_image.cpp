#include "disc/disc_image.h"

#include <utility>

namespace bs::disc {

uint32_t Track::userDataOffset() const {
    if (user_offset >= 0) return uint32_t(user_offset);
    switch (sector_size) {
    case 2048: return 0;
    case 2336: return 8;                                   // subheader Mode 2
    case 2352: return mode == TrackMode::Mode1 ? 16 : 24;  // sync+header (+subheader)
    default:   throw DiscError("tamanho de setor invalido: " + std::to_string(sector_size));
    }
}

const Track* DiscImage::findTrack(uint32_t lba) const {
    for (const auto& t : tracks_)
        if (t.contains(lba)) return &t;
    return nullptr;
}

const Track& DiscImage::bootTrack() const {
    const Track* best = nullptr;
    for (const auto& t : tracks_)
        if (t.isData() && (!best || t.session >= best->session)) best = &t;
    if (!best) throw DiscError("a imagem nao tem faixa de dados");
    return *best;
}

void DiscImage::readUserData(uint32_t lba, uint8_t* out) const {
    const Track* t = findTrack(lba);
    if (!t) throw DiscError("LBA " + std::to_string(lba) + " fora de todas as faixas");
    if (!t->isData()) throw DiscError("LBA " + std::to_string(lba) + " pertence a uma faixa de audio");
    uint64_t off = t->file_offset + uint64_t(lba - t->start_lba) * t->sector_size + t->userDataOffset();
    readRaw(*t, off, out, kUserDataSize);
}

void DiscImage::readRawSector(const Track& t, uint32_t index, uint8_t* out) const {
    if (index >= t.length) throw DiscError("setor " + std::to_string(index) + " alem do fim da faixa " + std::to_string(t.number));
    // inicio dos 2 352 bytes dentro do passo guardado: CHD (user_offset) e GDI/CUE/CDI com 2352 por setor
    uint32_t rawStart = 0;
    if (t.user_offset >= 0) {
        const uint32_t header = !t.isData() ? 0 : t.mode == TrackMode::Mode1 ? 16 : 24;
        if (uint32_t(t.user_offset) != header || t.sector_size < 2352)
            throw DiscError("a faixa " + std::to_string(t.number) + " nao guarda o setor bruto (sem os 2 352 bytes)");
        rawStart = 0;
    } else if (t.sector_size != 2352) {
        throw DiscError("a faixa " + std::to_string(t.number) + " tem " + std::to_string(t.sector_size) +
                        " bytes por setor, nao o setor bruto de 2 352");
    }
    readRaw(t, t.file_offset + uint64_t(index) * t.sector_size + rawStart, out, 2352);
    if (!t.isData() && audioBigEndian())
        for (size_t i = 0; i + 1 < 2352; i += 2) std::swap(out[i], out[i + 1]);
}

InputFile::InputFile(const std::filesystem::path& path) {
#ifdef _WIN32
    f_ = _wfopen(path.c_str(), L"rb");
#else
    f_ = std::fopen(path.c_str(), "rb");
#endif
    if (!f_) throw DiscError("nao foi possivel abrir " + pathText(path));
    std::error_code ec;
    size_ = std::filesystem::file_size(path, ec);
    if (ec) throw DiscError("nao foi possivel obter o tamanho de " + pathText(path));
}

InputFile::~InputFile() {
    if (f_) std::fclose(f_);
}

void InputFile::read(uint64_t offset, void* dst, size_t n) const {
    if (offset > size_ || n > size_ - offset)
        throw DiscError("leitura alem do fim do arquivo (offset " + std::to_string(offset) + ")");
#ifdef _WIN32
    int rc = _fseeki64(f_, static_cast<long long>(offset), SEEK_SET);
#else
    int rc = fseeko(f_, static_cast<off_t>(offset), SEEK_SET);
#endif
    if (rc != 0 || std::fread(dst, 1, n, f_) != n)
        throw DiscError("falha de leitura no offset " + std::to_string(offset));
}

} // namespace bs::disc
