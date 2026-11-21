#include "disc/archive.h"

#include "disc/disc_image.h"  // DiscError, pathText

#include <miniz.h>

extern "C" {
#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"
#include "7zFile.h"
}

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <memory>

namespace bs::disc {
namespace fs = std::filesystem;

namespace {

std::string lowerExt(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    std::string  e   = dot == std::string::npos ? "" : name.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e;
}

bool safeName(const std::string& n) {
    if (n.empty() || n.size() > 1024 || n[0] == '/' || n[0] == '\\' || n.find(':') != std::string::npos) return false;
    size_t i = 0;
    while (i <= n.size()) {
        const size_t j = n.find_first_of("/\\", i);
        const std::string part = n.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (part == "..") return false;
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return std::none_of(n.begin(), n.end(), [](unsigned char c) { return c < 0x20; });
}

fs::path u8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string utf16to8(const UInt16* w) {
    std::string o;
    for (size_t i = 0; w[i]; ++i) {
        uint32_t c = w[i];
        if (c >= 0xD800 && c < 0xDC00 && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000) c = 0x10000 + ((c - 0xD800) << 10) + (w[++i] - 0xDC00);
        if (c < 0x80) o += char(c);
        else if (c < 0x800) o += char(0xC0 | (c >> 6)), o += char(0x80 | (c & 0x3F));
        else if (c < 0x10000) o += char(0xE0 | (c >> 12)), o += char(0x80 | ((c >> 6) & 0x3F)), o += char(0x80 | (c & 0x3F));
        else o += char(0xF0 | (c >> 18)), o += char(0x80 | ((c >> 12) & 0x3F)), o += char(0x80 | ((c >> 6) & 0x3F)), o += char(0x80 | (c & 0x3F));
    }
    return o;
}

void checkCancel(const std::atomic<bool>* cancel) {
    if (cancel && cancel->load()) throw DiscError("cancelado");
}

// ---- .zip (miniz, com o arquivo pelo nosso FILE*: caminhos Unicode e mais de 4 GB) ----
struct Zip {
    std::FILE*     f = nullptr;
    mz_zip_archive z{};
    bool           open = false;
    explicit Zip(const fs::path& p) {
#ifdef _WIN32
        f = _wfopen(p.c_str(), L"rb");
#else
        f = std::fopen(p.c_str(), "rb");
#endif
        if (!f) throw DiscError("nao foi possivel abrir " + pathText(p));
        std::error_code ec;
        const uint64_t  size = fs::file_size(p, ec);
        z.m_pIO_opaque       = this;
        z.m_pRead            = [](void* op, mz_uint64 ofs, void* buf, size_t n) -> size_t {
            auto* self = static_cast<Zip*>(op);
#ifdef _WIN32
            if (_fseeki64(self->f, static_cast<long long>(ofs), SEEK_SET)) return 0;
#else
            if (fseeko(self->f, off_t(ofs), SEEK_SET)) return 0;
#endif
            return std::fread(buf, 1, n, self->f);
        };
        if (ec || !mz_zip_reader_init(&z, size, 0))
            throw DiscError("ZIP invalido (" + std::string(mz_zip_get_error_string(mz_zip_get_last_error(&z))) + "): " + pathText(p));
        open = true;
    }
    ~Zip() {
        if (open) mz_zip_reader_end(&z);
        if (f) std::fclose(f);
    }
    std::vector<ArchiveEntry> list() {
        std::vector<ArchiveEntry> out;
        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z); ++i) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&z, i, &st)) throw DiscError("ZIP: entrada ilegivel");
            out.push_back({st.m_filename, st.m_uncomp_size, bool(st.m_is_directory)});
        }
        return out;
    }
};

// ---- .7z (LZMA SDK, dominio publico) ----
struct SevenZip {
    CFileInStream archiveStream{};
    CLookToRead2  look{};
    CSzArEx       db{};
    ISzAlloc      alloc{SzAlloc, SzFree};
    bool          fileOpen = false, dbOpen = false;
    explicit SevenZip(const fs::path& p) {
#ifdef _WIN32
        if (InFile_OpenW(&archiveStream.file, p.c_str()) != 0) throw DiscError("nao foi possivel abrir " + pathText(p));
#else
        if (InFile_Open(&archiveStream.file, p.c_str()) != 0) throw DiscError("nao foi possivel abrir " + pathText(p));
#endif
        fileOpen = true;
        FileInStream_CreateVTable(&archiveStream);
        archiveStream.wres = 0;
        LookToRead2_CreateVTable(&look, False);
        look.buf = static_cast<Byte*>(ISzAlloc_Alloc(&alloc, size_t(1) << 18));
        if (!look.buf) throw DiscError("7Z: sem memoria");
        look.bufSize     = size_t(1) << 18;
        look.realStream  = &archiveStream.vt;
        LookToRead2_INIT(&look)
        static const bool crc = (CrcGenerateTable(), true);
        (void)crc;
        SzArEx_Init(&db);
        if (SzArEx_Open(&db, &look.vt, &alloc, &alloc) != SZ_OK) throw DiscError("7Z invalido: " + pathText(p));
        dbOpen = true;
    }
    ~SevenZip() {
        if (dbOpen) SzArEx_Free(&db, &alloc);
        if (look.buf) ISzAlloc_Free(&alloc, look.buf);
        if (fileOpen) File_Close(&archiveStream.file);
    }
    std::string name(UInt32 i) {
        const size_t         len = SzArEx_GetFileNameUtf16(&db, i, nullptr);
        std::vector<UInt16> w(len + 1, 0);
        SzArEx_GetFileNameUtf16(&db, i, w.data());
        return utf16to8(w.data());
    }
    std::vector<ArchiveEntry> list() {
        std::vector<ArchiveEntry> out;
        for (UInt32 i = 0; i < db.NumFiles; ++i) out.push_back({name(i), uint64_t(SzArEx_GetFileSize(&db, i)), SzArEx_IsDir(&db, i) != 0});
        return out;
    }
};

void writeOut(const fs::path& dst, const uint8_t* d, size_t n) {
    fs::create_directories(dst.parent_path());
    std::ofstream o(dst, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(d), std::streamsize(n));
    if (!o) throw DiscError("falha ao gravar " + pathText(dst) + " (espaco em disco?)");
}

} // namespace

bool archive_supported(const fs::path& p) {
    const auto  u = p.extension().u8string();
    std::string e(u.begin(), u.end());
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e == ".zip" || e == ".7z";
}

std::vector<ArchiveEntry> archive_list(const fs::path& archive) {
    const auto  u = archive.extension().u8string();
    std::string e(u.begin(), u.end());
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (e == ".zip") return Zip(archive).list();
    if (e == ".7z") return SevenZip(archive).list();
    throw DiscError("pacote nao suportado: " + pathText(archive.filename()) + " (use .zip ou .7z)");
}

// Limites de um pacote de imagem (revisao Codex 2026-11-19): um disco tem poucas faixas; um GD-ROM cabe em ~1,3 GB
constexpr size_t   kMaxEntries = 256;
constexpr uint64_t kMaxBytes   = 8ull << 30;  // 8 GB extraidos no total (o 7z decodifica um bloco solido inteiro na memoria)

// nomes seguros e limites de quantidade e tamanho (todo pacote extraido)
static void check_entries(const std::vector<ArchiveEntry>& entries) {
    if (entries.size() > kMaxEntries) throw DiscError("pacote com arquivos demais (" + std::to_string(entries.size()) + ")");
    uint64_t total = 0;
    for (const auto& en : entries) {
        if (!safeName(en.name)) throw DiscError("pacote com nome de arquivo inseguro: " + en.name);
        if (en.size > kMaxBytes || total + en.size > kMaxBytes) throw DiscError("pacote grande demais (mais de 8 GB extraidos)");
        total += en.size;
    }
}

std::string archive_image_name(const std::vector<ArchiveEntry>& entries) {
    if (entries.size() > kMaxEntries) throw DiscError("pacote com arquivos demais para uma imagem de disco (" + std::to_string(entries.size()) + ")");
    uint64_t total = 0;
    for (const auto& en : entries) {
        if (!safeName(en.name)) throw DiscError("pacote com nome de arquivo inseguro: " + en.name);
        if (en.size > kMaxBytes || total + en.size > kMaxBytes)
            throw DiscError("pacote grande demais para uma imagem de disco (mais de 8 GB extraidos)");
        total += en.size;
    }
    for (const char* want : {".gdi", ".cue", ".chd", ".cdi"})
        for (const auto& en : entries)
            if (!en.dir && lowerExt(en.name) == want) return en.name;
    throw DiscError("o pacote nao tem imagem de disco (.gdi, .cue, .chd ou .cdi)");
}

namespace {
fs::path extract_impl(const fs::path& archive, const fs::path& dir, const std::function<void(double)>& progress,
                      const std::atomic<bool>* cancel, bool needImage);
}

fs::path archive_extract_image(const fs::path& archive, const fs::path& dir, const std::function<void(double)>& progress,
                               const std::atomic<bool>* cancel) {
    return extract_impl(archive, dir, progress, cancel, true);
}

void archive_extract_all(const fs::path& archive, const fs::path& dir, const std::function<void(double)>& progress,
                         const std::atomic<bool>* cancel) {
    extract_impl(archive, dir, progress, cancel, false);
}

void archive_extract_selected(const fs::path& archive, const fs::path& dir, const ArchiveSelection& sel,
                              const std::function<void(double)>& progress, const std::atomic<bool>* cancel) {
    if (lowerExt(pathText(archive.filename())) != ".zip") throw DiscError("so .zip: " + pathText(archive.filename()));
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) throw DiscError("nao foi possivel criar " + pathText(dir));
    Zip                 z(archive);
    const auto          all = z.list();
    std::vector<mz_uint> pick;
    uint64_t            total = 0;
    for (mz_uint i = 0; i < mz_uint(all.size()); ++i) {
        const auto& en = all[i];
        if (!safeName(en.name)) throw DiscError("pacote com nome de arquivo inseguro: " + en.name);
        if (en.dir || (sel.keep && !sel.keep(en.name))) continue;
        pick.push_back(i);
        if (pick.size() > sel.maxEntries) throw DiscError("pacote com arquivos demais (" + std::to_string(pick.size()) + "+)");
        if (en.size > sel.maxBytes || total + en.size > sel.maxBytes) throw DiscError("pacote grande demais");
        total += en.size;
    }
    uint64_t done = 0;
    struct Out {
        std::ofstream*                      o;
        uint64_t*                           done;
        uint64_t                            total;
        const std::function<void(double)>*  say;
        const std::atomic<bool>*            cancel;
    };
    for (const mz_uint i : pick) {
        checkCancel(cancel);
        const fs::path dst = dir / u8(all[i].name);
        fs::create_directories(dst.parent_path(), ec);
        std::ofstream o(dst, std::ios::binary | std::ios::trunc);
        if (!o) throw DiscError("nao foi possivel criar " + pathText(dst));
        Out        ctx{&o, &done, total, &progress, cancel};
        const bool ok = mz_zip_reader_extract_to_callback(
            &z.z, i,
            [](void* op, mz_uint64, const void* buf, size_t n) -> size_t {
                auto* c = static_cast<Out*>(op);
                if (c->cancel && c->cancel->load()) return 0;
                c->o->write(static_cast<const char*>(buf), std::streamsize(n));
                if (!*c->o) return 0;
                *c->done += n;
                if (c->total && *c->say) (*c->say)(std::clamp(double(*c->done) / double(c->total), 0.0, 1.0));
                return n;
            },
            &ctx, 0);
        o.close();
        checkCancel(cancel);
        if (!ok || !o)
            throw DiscError("ZIP: falha ao extrair " + all[i].name + " (" + mz_zip_get_error_string(mz_zip_get_last_error(&z.z)) +
                            "; espaco em disco?)");
    }
    if (progress) progress(1.0);
}

namespace {
fs::path extract_impl(const fs::path& archive, const fs::path& dir, const std::function<void(double)>& progress,
                      const std::atomic<bool>* cancel, bool needImage) {
    const auto  u = archive.extension().u8string();
    std::string e(u.begin(), u.end());
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) throw DiscError("nao foi possivel criar " + pathText(dir));
    auto say = [&](double f) {
        if (progress) progress(std::clamp(f, 0.0, 1.0));
    };

    if (e == ".zip") {
        Zip                       z(archive);
        const auto                entries = z.list();
        check_entries(entries);
        const std::string         image   = needImage ? archive_image_name(entries) : std::string();
        uint64_t                  total = 0, done = 0;
        for (const auto& en : entries) total += en.dir ? 0 : en.size;
        struct Out {
            std::ofstream*           o;
            uint64_t*                done;
            uint64_t                 total;
            const std::function<void(double)>* say;
            const std::atomic<bool>* cancel;
        };
        for (mz_uint i = 0; i < mz_uint(entries.size()); ++i) {
            if (entries[i].dir) continue;
            checkCancel(cancel);
            const fs::path dst = dir / u8(entries[i].name);
            fs::create_directories(dst.parent_path());
            std::ofstream o(dst, std::ios::binary | std::ios::trunc);
            if (!o) throw DiscError("nao foi possivel criar " + pathText(dst));
            std::function<void(double)> s = say;
            Out ctx{&o, &done, total, &s, cancel};
            const bool ok = mz_zip_reader_extract_to_callback(
                &z.z, i,
                [](void* op, mz_uint64, const void* buf, size_t n) -> size_t {
                    auto* c = static_cast<Out*>(op);
                    if (c->cancel && c->cancel->load()) return 0;
                    c->o->write(static_cast<const char*>(buf), std::streamsize(n));
                    if (!*c->o) return 0;
                    *c->done += n;
                    if (c->total) (*c->say)(double(*c->done) / double(c->total));
                    return n;
                },
                &ctx, 0);
            o.close();
            checkCancel(cancel);
            if (!ok || !o)
                throw DiscError("ZIP: falha ao extrair " + entries[i].name + " (" +
                                mz_zip_get_error_string(mz_zip_get_last_error(&z.z)) + "; espaco em disco?)");
        }
        say(1.0);
        return dir / u8(image);
    }
    if (e == ".7z") {
        SevenZip          z(archive);
        const auto        entries = z.list();
        check_entries(entries);
        const std::string image   = needImage ? archive_image_name(entries) : std::string();
        uint64_t          total = 0, done = 0;
        for (const auto& en : entries) total += en.dir ? 0 : en.size;
        UInt32 blockIndex = 0xFFFFFFFF;
        Byte*  outBuffer  = nullptr;
        size_t outSize    = 0;
        struct Free {
            ISzAlloc* a;
            Byte**    b;
            ~Free() { ISzAlloc_Free(a, *b); }
        } freeBuf{&z.alloc, &outBuffer};
        for (UInt32 i = 0; i < z.db.NumFiles; ++i) {
            if (entries[i].dir) continue;
            checkCancel(cancel);
            size_t offset = 0, processed = 0;
            // um bloco (pasta solida) inteiro e decodificado na memoria de uma vez: os seguintes do mesmo bloco saem dele
            const SRes r = SzArEx_Extract(&z.db, &z.look.vt, i, &blockIndex, &outBuffer, &outSize, &offset, &processed, &z.alloc, &z.alloc);
            if (r != SZ_OK)
                throw DiscError("7Z: falha ao extrair " + entries[i].name + (r == SZ_ERROR_MEM ? " (memoria insuficiente)" : "") +
                                " (codigo " + std::to_string(r) + ")");
            writeOut(dir / u8(entries[i].name), outBuffer + offset, processed);
            done += processed;
            say(total ? double(done) / double(total) : 1.0);
        }
        say(1.0);
        return dir / u8(image);
    }
    throw DiscError("pacote nao suportado: " + pathText(archive.filename()) + " (use .zip ou .7z)");
}
} // namespace

} // namespace bs::disc
