#include "extract.h"

#include "boot_identity.h"
#include "disc/disc_open.h"
#include "disc/ip_bin.h"
#include "disc/iso9660.h"
#include "util/hash.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <ostream>
#include <sstream>

namespace bs {
namespace fs = std::filesystem;
using disc::DiscError;

namespace {

constexpr const char* kToolVersion = "bs-extract 0.1";

std::unique_ptr<disc::DiscImage> openImage(const fs::path& p, const fs::path& confine) { return disc::disc_open(p, confine); }

std::string json(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += char(c); }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += char(c);
    }
    return o + "\"";
}

const char* modeName(disc::TrackMode m) {
    switch (m) {
    case disc::TrackMode::Audio: return "audio";
    case disc::TrackMode::Mode1: return "mode1";
    default: return "mode2";
    }
}


void writeFile(const fs::path& p, const uint8_t* d, size_t n) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(d), static_cast<std::streamsize>(n));
    if (!f) throw DiscError("falha ao gravar " + p.string());
}

} // namespace

int runExtract(const ExtractOptions& opt, std::ostream& log) {
    auto        img  = openImage(opt.image, opt.confine);
    const auto& boot = img->bootTrack();

    log << "Imagem: " << opt.image.string() << " (" << img->formatName() << ")\n";
    for (const auto& t : img->tracks())
        log << "  faixa " << t.number << " sessao " << t.session << " " << modeName(t.mode) << " LBA " << t.start_lba
            << " +" << t.length << " setores de " << t.sector_size << "\n";

    // IP.BIN: primeiros 16 setores da faixa de boot.
    std::vector<uint8_t> ipbin(disc::kIpBinSize);
    for (uint32_t i = 0; i < disc::kIpBinSize / disc::kUserDataSize; ++i)
        img->readUserData(boot.start_lba + i, ipbin.data() + i * disc::kUserDataSize);
    const disc::IpBin ip = disc::parseIpBin(ipbin.data(), ipbin.size());
    log << "IP.BIN: " << ip.product << " " << ip.version << " " << ip.release_date << " area " << ip.area
        << " boot " << ip.boot_file << " \"" << ip.title << "\"\n";
    if (ip.usesWindowsCE()) throw DiscError("o jogo usa Windows CE; este projeto suporta apenas Katana");

    disc::Iso9660 iso(*img, boot.start_lba + 16);
    const auto*   bootEntry = iso.find(ip.boot_file);
    if (!bootEntry || bootEntry->is_dir) throw DiscError("executavel de boot " + ip.boot_file + " nao encontrado no ISO9660");

    BootIdentity bc = identify_boot(iso.readAll(*bootEntry), boot.start_lba);
    log << ip.boot_file << ": " << bc.data.size() << " bytes, SHA-256 " << bc.sha256
        << (bc.scrambled ? " (desembaralhado)" : "") << "\n";
    if (bc.version)
        log << "Versao reconhecida: " << bc.version->id
            << (bc.variant ? " (FAD " + std::to_string(bc.fad.lowerBound) + "/" + std::to_string(bc.fad.pvdFad) + ")" : "")
            << "\n";
    else log << "AVISO: " << boot_rejection_text(ip.product, ip.version, bc) << "\n";

    if (opt.info_only) return 0;
    if (!bc.version && !opt.force)
        throw DiscError(boot_rejection_text(ip.product, ip.version, bc) +
                        " (use --force para extrair mesmo assim; o resultado nao sera validado)");

    fs::create_directories(opt.out);
    writeFile(opt.out / "IP.BIN", ipbin.data(), ipbin.size());
    writeFile(opt.out / ip.boot_file, bc.data.data(), bc.data.size());

    std::ostringstream files;
    size_t             count = 0;
    uint64_t           bytes = 0;
    for (const auto& e : iso.entries()) {
        if (e.is_dir) continue;
        const bool  isBoot = &e == bootEntry;
        util::Sha256 sha;
        util::Crc32  crc;
        if (isBoot) {
            sha.update(bc.data.data(), bc.data.size());
            crc.update(bc.data.data(), bc.data.size());
        } else {
            const fs::path dst = opt.out / fs::path(e.path);
            fs::create_directories(dst.parent_path());
            std::ofstream f(dst, std::ios::binary | std::ios::trunc);
            iso.read(e, [&](const uint8_t* p, size_t n) {
                f.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(n));
                sha.update(p, n);
                crc.update(p, n);
            });
            if (!f) throw DiscError("falha ao gravar " + dst.string());
        }
        files << (count ? ",\n" : "") << "    {\"path\": " << json(e.path) << ", \"lba\": " << e.lba
              << ", \"size\": " << e.size << ", \"sha256\": \"" << sha.hexdigest() << "\", \"crc32\": \""
              << util::hex32(crc.value()) << "\"}";
        ++count;
        bytes += e.size;
        if (count % 250 == 0) log << "  " << count << " arquivos...\n";
    }

    std::ofstream m(opt.out / "manifest.json", std::ios::trunc);
    m << "{\n"
      << "  \"tool\": " << json(kToolVersion) << ",\n"
      << "  \"source\": {\"file\": " << json(opt.image.filename().string()) << ", \"format\": "
      << json(img->formatName()) << "},\n"
      << "  \"tracks\": [";
    for (size_t i = 0; i < img->tracks().size(); ++i) {
        const auto& t = img->tracks()[i];
        m << (i ? ", " : "") << "{\"session\": " << t.session << ", \"mode\": \"" << modeName(t.mode)
          << "\", \"start_lba\": " << t.start_lba << ", \"length\": " << t.length << ", \"sector_size\": "
          << t.sector_size << "}";
    }
    m << "],\n"
      << "  \"ip\": {\"product\": " << json(ip.product) << ", \"version\": " << json(ip.version)
      << ", \"date\": " << json(ip.release_date) << ", \"area\": " << json(ip.area)
      << ", \"peripherals\": " << json(ip.peripherals) << ", \"boot\": " << json(ip.boot_file)
      << ", \"title\": " << json(ip.title) << "},\n"
      << "  \"volume\": " << json(iso.volumeId()) << ",\n"
      << "  \"boot\": {\"sha256\": \"" << bc.sha256 << "\", \"scrambled_on_disc\": " << (bc.scrambled ? "true" : "false")
      << ", \"verified\": " << (bc.version ? "true" : "false") << ", \"version_id\": "
      << (bc.version ? json(bc.version->id) : "null") << "},\n"
      << "  \"files\": [\n" << files.str() << "\n  ]\n}\n";
    if (!m) throw DiscError("falha ao gravar manifest.json");

    log << "Extraidos " << count << " arquivos (" << bytes / (1024 * 1024) << " MB) para " << opt.out.string() << "\n";
    return 0;
}

} // namespace bs
