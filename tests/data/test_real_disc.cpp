// Teste com a imagem real do usuario. So roda se BS_DISC_IMAGE apontar para o
// .cdi, .gdi ou .cue; caso contrario retorna 77 (SKIP no CTest). Nao grava nada em disco.
#include "../unit/test.h"

#include "boot_identity.h"
#include "disc/disc_open.h"
#include "disc/track_files_image.h"
#include "disc/ip_bin.h"
#include "disc/iso9660.h"
#include "known_versions.h"
#include "util/hash.h"

#include <cstdlib>
#include <cstring>
#include <exception>

using namespace bs;

static int run() {
    const char* path = std::getenv("BS_DISC_IMAGE");
    if (!path || !*path) {
        std::printf("BS_DISC_IMAGE nao definido: teste ignorado\n");
        return 77;
    }
    // CDI do projeto (2 sessoes) ou o disco original (GDI ou CUE/BIN do Redump, area de alta densidade na LBA 45000)
    const auto  imgp = disc::disc_open(path);
    const auto& img  = *imgp;
    const auto& boot = img.bootTrack();
    const bool  gd   = boot.start_lba == disc::kGdHighDensityLba;
    std::printf("imagem %s: %s\n", img.formatName().c_str(), gd ? "disco original (GD-ROM)" : "CDI");
    BS_CHECK(img.tracks().size() == (gd ? 3u : 2u));
    BS_CHECK(boot.start_lba == (gd ? 45000u : 239355u));
    BS_CHECK(boot.sector_size == (img.formatName() == "CHD" ? 2448u : gd ? 2352u : 2336u));  // CHD: quadro com subcodigo

    std::vector<uint8_t> ipbin(disc::kIpBinSize);
    for (uint32_t i = 0; i < 16; ++i) img.readUserData(boot.start_lba + i, &ipbin[i * 2048]);
    auto ip = disc::parseIpBin(ipbin.data(), ipbin.size());
    BS_CHECK(ip.product == "T13001N");
    BS_CHECK(ip.version == "V1.000");
    BS_CHECK(!ip.usesWindowsCE());

    disc::Iso9660 iso(img, boot.start_lba + 16);
    BS_CHECK(iso.volumeId() == (gd ? "BLUESTINGER" : "BLUE_DCRES"));
    const auto* exe = iso.find("1ST_READ.BIN");
    BS_CHECK(exe && exe->size == kKnownVersions[0].boot_size);
    if (exe) {
        const BootIdentity id = identify_boot(iso.readAll(*exe), boot.start_lba);
        BS_CHECK(id.version && !id.variant && !id.scrambled);
        BS_CHECK(id.version && std::string(id.version->id) == (gd ? "bluestinger_us_v1000_gd" : "bluestinger_us_v1000"));
        BS_CHECK(id.sha256 == (gd ? kExtraVersions[0].boot_sha256 : kKnownVersions[0].boot_sha256));
        // o executavel exato num layout de outra imagem: recusado pelos FADs (revisao Codex 2026-11-19)
        BS_CHECK(!identify_boot(id.data, gd ? 239355u : 45000u).version);
        BS_CHECK(identify_boot(id.data, UINT32_MAX).version != nullptr);  // sem faixa conhecida: so o hash
        // D2 (I7): variante sintetica, os literais de FAD de uma faixa de boot hipotetica na LBA 100000
        auto put = [](std::vector<uint8_t>& v, uint32_t o, uint32_t x) {
            for (int i = 0; i < 4; ++i) v[o + i] = uint8_t(x >> (8 * i));
        };
        std::vector<uint8_t> v = id.data;
        put(v, 0x14AEE4, 150), put(v, 0x14B1C8, 150), put(v, 0x1CAC38, 100000 + 16 + 150);
        const BootIdentity a = identify_boot(v, 100000);
        BS_CHECK(a.version && a.variant && a.fad.lowerBound == 150 && a.fad.pvdFad == 100166);
        BS_CHECK(!identify_boot(v, 45000).version);   // literais incoerentes com a faixa da imagem
        const std::string why = boot_rejection_text("T13001N", "V1.000", identify_boot(v, 45000));
        BS_CHECK(why.find("100166") != std::string::npos);
        put(v, 0x14B1C8, 151);
        BS_CHECK(!identify_boot(v, 100000).version);  // os dois limites precisam ser iguais
        put(v, 0x14B1C8, 150);
        v[0x1000] ^= 1;
        BS_CHECK(!identify_boot(v, 100000).version);  // qualquer outro byte diferente: recusado
    }

    // FMV: cabecalho "Sofdec Stream" e primeiro pack MPEG em +0x2000 (no CDI, na primeira sessao; no GD, na area alta)
    const auto* fmv = iso.find("BSDATA/ENDING.SFD");
    BS_CHECK(fmv && img.findTrack(fmv->lba) && img.findTrack(fmv->lba)->session == (gd ? 2 : 1));
    if (fmv) {
        uint8_t s[2048];
        img.readUserData(fmv->lba, s);
        BS_CHECK(std::memcmp(s, "Sofdec Stream", 13) == 0);
        img.readUserData(fmv->lba + 4, s);
        BS_CHECK(s[0] == 0 && s[1] == 0 && s[2] == 1 && s[3] == 0xBA);
    }

    std::printf("[%s] real_disc\n", bs::test::failures() ? "FAIL" : " ok ");
    return bs::test::failures() ? 1 : 0;
}

int main() {
    try {
        return run();
    } catch (const std::exception& e) {
        // caminho errado/arquivo ilegivel: falha com mensagem, nao com abort silencioso
        std::printf("[FAIL] real_disc: %s\n", e.what());
        return 1;
    }
}
