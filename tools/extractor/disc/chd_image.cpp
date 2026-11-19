#include "disc/chd_image.h"

#include "disc/track_files_image.h"  // kGdHighDensityLba

#include <libchdr/chd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace bs::disc {

namespace {

// E/S da libchdr pelo nosso arquivo (_wfopen no Windows: caminhos com acentos ou outros alfabetos)
struct File {
    std::FILE* f = nullptr;
};

uint64_t cbSize(void* p) {
    auto* f = static_cast<File*>(p);
#ifdef _WIN32
    const long long cur = _ftelli64(f->f);
    _fseeki64(f->f, 0, SEEK_END);
    const long long end = _ftelli64(f->f);
    _fseeki64(f->f, cur, SEEK_SET);
#else
    const off_t cur = ftello(f->f);
    fseeko(f->f, 0, SEEK_END);
    const off_t end = ftello(f->f);
    fseeko(f->f, cur, SEEK_SET);
#endif
    return end < 0 ? 0 : uint64_t(end);
}
size_t cbRead(void* buf, size_t size, size_t count, void* p) { return std::fread(buf, size, count, static_cast<File*>(p)->f); }
int    cbClose(void* p) {
    auto* f = static_cast<File*>(p);
    const int rc = f->f ? std::fclose(f->f) : 0;
    f->f = nullptr;
    return rc;
}
int cbSeek(void* p, int64_t off, int whence) {
#ifdef _WIN32
    return _fseeki64(static_cast<File*>(p)->f, off, whence);
#else
    return fseeko(static_cast<File*>(p)->f, off_t(off), whence);
#endif
}
const core_file_callbacks kCallbacks = {cbSize, cbRead, cbClose, cbSeek};

std::string text(const char* s) { return s ? s : "?"; }

} // namespace

struct ChdImage::Impl {
    File                 file;
    chd_file*            chd = nullptr;
    uint32_t             hunkBytes = 0, unitBytes = 0, totalHunks = 0;
    mutable std::mutex   m;
    mutable uint32_t     cachedHunk = UINT32_MAX;
    mutable std::vector<uint8_t> hunk;
};

ChdTrackMeta ChdImage::parseMeta(const std::string& s, bool gd) {
    ChdTrackMeta t;
    char         type[32] = {}, sub[32] = {}, pgtype[32] = {}, pgsub[32] = {};
    int          n = 0, frames = 0, pad = 0, pregap = 0, postgap = 0;
    int          got = 0;
    if (gd)
        got = std::sscanf(s.c_str(), "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PAD:%d PREGAP:%d PGTYPE:%31s PGSUB:%31s POSTGAP:%d", &n,
                          type, sub, &frames, &pad, &pregap, pgtype, pgsub, &postgap);
    else {
        got = std::sscanf(s.c_str(), "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PREGAP:%d PGTYPE:%31s PGSUB:%31s POSTGAP:%d", &n,
                          type, sub, &frames, &pregap, pgtype, pgsub, &postgap);
        if (got < 4) got = std::sscanf(s.c_str(), "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d", &n, type, sub, &frames);
    }
    if (got < 4 || n <= 0 || frames <= 0 || pad < 0 || pregap < 0 || postgap < 0)
        throw DiscError("CHD: metadados de faixa invalidos: " + s);
    t.number = n, t.type = type, t.pgtype = pgtype;
    t.frames = uint32_t(frames), t.pregap = uint32_t(pregap), t.postgap = uint32_t(postgap);
    // cada faixa ocupa um multiplo de 4 quadros no CHD (como no MAME: extraframes). O PAD dos metadados de GD-ROM e outra
    // coisa: quadros no fim da faixa que nao sao dados (no Redump convertido pelo chdman, o vao ate a LBA 45000); ocupam
    // posicoes do disco mas saem do comprimento (Flycast: EndFAD = total - 1 - padframes; revisao Codex 2026-11-19).
    t.pad     = (4 - uint32_t(frames) % 4) % 4;
    t.metaPad = gd ? uint32_t(pad) : 0;
    return t;
}

std::vector<Track> ChdImage::layout(const std::vector<ChdTrackMeta>& meta, bool gd, uint32_t unitBytes) {
    std::vector<Track> out;
    uint64_t           chdFrame = 0;  // quadro guardado no CHD onde a faixa comeca
    uint32_t           lba      = 0;  // posicao no disco
    for (size_t i = 0; i < meta.size(); ++i) {
        const ChdTrackMeta& m = meta[i];
        if (m.number != int(i + 1)) throw DiscError("CHD: faixas fora de ordem (faixa " + std::to_string(m.number) + ")");
        Track t;
        t.number      = m.number;
        t.sector_size = unitBytes;  // passo entre quadros guardados
        if (m.type == "MODE1_RAW") t.mode = TrackMode::Mode1, t.user_offset = 16;
        else if (m.type == "MODE1") t.mode = TrackMode::Mode1, t.user_offset = 0;
        else if (m.type == "MODE2_RAW") t.mode = TrackMode::Mode2, t.user_offset = 24;
        else if (m.type == "MODE2_FORM1") t.mode = TrackMode::Mode2, t.user_offset = 0;
        else if (m.type == "MODE2") t.mode = TrackMode::Mode2, t.user_offset = 8;
        else if (m.type == "AUDIO") t.mode = TrackMode::Audio, t.user_offset = 0;
        else throw DiscError("CHD: tipo de faixa nao suportado: " + m.type);
        // GD-ROM: a 3a faixa abre a area de alta densidade
        if (gd && m.number == 3) {
            if (lba > kGdHighDensityLba) throw DiscError("CHD: a area de baixa densidade passa da LBA 45000");
            lba = kGdHighDensityLba;
        }
        // pregap guardado ("V..." no PGTYPE) faz parte dos quadros da faixa; senao, so desloca a posicao
        const bool     stored = !m.pgtype.empty() && m.pgtype[0] == 'V';
        const uint32_t skip   = stored ? m.pregap : 0;
        if (uint64_t(skip) + m.metaPad >= m.frames) throw DiscError("CHD: pregap e PAD maiores que a faixa " + std::to_string(m.number));
        t.pregap      = m.pregap;
        t.start_lba   = lba + m.pregap;
        t.length      = m.frames - skip - m.metaPad;
        t.session     = gd && m.number >= 3 ? 2 : 1;
        t.file_offset = (chdFrame + skip) * uint64_t(unitBytes);
        if (!t.length) throw DiscError("CHD: faixa " + std::to_string(m.number) + " vazia");
        out.push_back(t);
        lba      = t.start_lba + t.length + m.metaPad + m.postgap;
        chdFrame += uint64_t(m.frames) + m.pad;
    }
    if (out.empty()) throw DiscError("CHD sem faixas (nao e um CD/GD-ROM?)");
    return out;
}

ChdImage::ChdImage(const std::filesystem::path& path) : impl_(std::make_unique<Impl>()) {
#ifdef _WIN32
    impl_->file.f = _wfopen(path.c_str(), L"rb");
#else
    impl_->file.f = std::fopen(path.c_str(), "rb");
#endif
    if (!impl_->file.f) throw DiscError("nao foi possivel abrir " + pathText(path));
    const chd_error e = chd_open_core_file_callbacks(&kCallbacks, &impl_->file, CHD_OPEN_READ, nullptr, &impl_->chd);
    if (e != CHDERR_NONE) {
        cbClose(&impl_->file);
        throw DiscError("CHD invalido (" + text(chd_error_string(e)) + "): " + pathText(path));
    }
    const chd_header* h = chd_get_header(impl_->chd);
    impl_->hunkBytes = h->hunkbytes, impl_->unitBytes = h->unitbytes, impl_->totalHunks = h->totalhunks;
    if (impl_->unitBytes < 2352 || !impl_->hunkBytes || impl_->hunkBytes % impl_->unitBytes)
        throw DiscError("CHD sem quadros de CD (unidade de " + std::to_string(impl_->unitBytes) + " bytes): " + pathText(path));

    std::vector<ChdTrackMeta> meta;
    bool                      gd = false;
    for (uint32_t tag : {uint32_t(GDROM_TRACK_METADATA_TAG), uint32_t(CDROM_TRACK_METADATA2_TAG), uint32_t(CDROM_TRACK_METADATA_TAG)}) {
        for (uint32_t i = 0;; ++i) {
            char     buf[512] = {};
            uint32_t len = 0, rtag = 0;
            uint8_t  flags = 0;
            if (chd_get_metadata(impl_->chd, tag, i, buf, sizeof buf - 1, &len, &rtag, &flags) != CHDERR_NONE) break;
            meta.push_back(parseMeta(std::string(buf, std::min<size_t>(len, sizeof buf - 1)).c_str(), tag == GDROM_TRACK_METADATA_TAG));
        }
        if (!meta.empty()) {
            gd = tag == GDROM_TRACK_METADATA_TAG;
            break;
        }
    }
    tracks_ = layout(meta, gd, impl_->unitBytes);
    impl_->hunk.resize(impl_->hunkBytes);
}

ChdImage::~ChdImage() {
    if (impl_ && impl_->chd) chd_close(impl_->chd);  // fecha o arquivo pelo cbClose
}

void ChdImage::readRaw(const Track&, uint64_t offset, void* dst, size_t n) const {
    std::lock_guard<std::mutex> lock(impl_->m);
    const uint64_t perHunk = impl_->hunkBytes;
    auto*          out     = static_cast<uint8_t*>(dst);
    while (n) {
        const uint64_t h = offset / perHunk;
        if (h >= impl_->totalHunks) throw DiscError("CHD: leitura alem do fim (hunk " + std::to_string(h) + ")");
        if (impl_->cachedHunk != h) {
            const chd_error e = chd_read(impl_->chd, uint32_t(h), impl_->hunk.data());
            if (e != CHDERR_NONE) {
                impl_->cachedHunk = UINT32_MAX;
                throw DiscError("CHD: falha ao ler o hunk " + std::to_string(h) + " (" + text(chd_error_string(e)) + ")");
            }
            impl_->cachedHunk = uint32_t(h);
        }
        const size_t at = size_t(offset % perHunk), k = std::min<size_t>(n, size_t(perHunk) - at);
        std::memcpy(out, impl_->hunk.data() + at, k);
        out += k, offset += k, n -= k;
    }
}

} // namespace bs::disc
