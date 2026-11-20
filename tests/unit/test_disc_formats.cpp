// Imagens GDI e CUE/BIN e a identificacao da versao (docs/IMAGENS-DISCO-PLANO.md, I1-I3 e I7). Sem dados do jogo:
// folhas de faixas e arquivos sinteticos. A identificacao com o executavel real fica no bs_real_disc_test.
#include "test.h"

#include "boot_identity.h"
#include "disc/disc_open.h"
#include "disc/gdi_export.h"
#include "disc/track_files_image.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <map>
#include <string>

using namespace bs;
namespace fs = std::filesystem;

namespace {

disc::TrackFilesImage::SizeFn sizes(std::map<std::string, uint64_t> m) {
    return [m](const std::string& n) -> uint64_t {
        auto it = m.find(n);
        if (it == m.end()) throw disc::DiscError("sem arquivo " + n);
        return it->second;
    };
}

template <class F> bool throws(F f) {
    try {
        f();
    } catch (const disc::DiscError&) {
        return true;
    }
    return false;
}

// o layout do Blue Stinger: faixa 1 de dados (8683 setores), audio, area de alta densidade na LBA 45000
const char kGdi[] = "3\r\n"
                    "1 0 4 2352 track01.bin 0\r\n"
                    "2 8833 0 2352 track02.raw 0\r\n"
                    "3 45000 4 2352 track03.bin 0\r\n";

const char kCue[] = "REM SINGLE-DENSITY AREA\n"
                    "FILE \"Jogo (USA) (Track 1).bin\" BINARY\n"
                    "  TRACK 01 MODE1/2352\n"
                    "    INDEX 01 00:00:00\n"
                    "FILE \"Jogo (USA) (Track 2).bin\" BINARY\n"
                    "  TRACK 02 AUDIO\n"
                    "    INDEX 00 00:00:00\n"
                    "    INDEX 01 00:02:00\n"
                    "REM HIGH-DENSITY AREA\n"
                    "FILE \"Jogo (USA) (Track 3).bin\" BINARY\n"
                    "  TRACK 03 MODE1/2352\n"
                    "    INDEX 01 00:00:00\n";

} // namespace

BS_TEST(gdi_parses_gd_layout) {
    std::vector<std::string> files;
    const auto t = disc::TrackFilesImage::parseGdi(
        kGdi, sizes({{"track01.bin", 8683ull * 2352}, {"track02.raw", 1051ull * 2352 + 100}, {"track03.bin", 504150ull * 2352}}), files);
    BS_CHECK(t.size() == 3 && files.size() == 3 && files[2] == "track03.bin");
    BS_CHECK(t[0].isData() && t[0].session == 1 && t[0].start_lba == 0 && t[0].length == 8683);
    BS_CHECK(!t[1].isData() && t[1].start_lba == 8833 && t[1].length == 1051);  // audio: setor incompleto no fim ignorado
    BS_CHECK(t[2].isData() && t[2].session == 2 && t[2].start_lba == 45000 && t[2].length == 504150 && t[2].file_index == 2);
    BS_CHECK(t[2].file_offset == 0 && t[2].userDataOffset() == 16);
}

BS_TEST(gdi_quoted_names_and_rejections) {
    std::vector<std::string> f1;
    const auto t = disc::TrackFilesImage::parseGdi("1\n1 45000 4 2048 \"faixa com espaco.iso\" 0\n", sizes({{"faixa com espaco.iso", 4096}}), f1);
    BS_CHECK(t.size() == 1 && f1[0] == "faixa com espaco.iso" && t[0].sector_size == 2048 && t[0].length == 2);
    auto sz = sizes({{"a.bin", 2352}, {"b.bin", 2352}});
    std::vector<std::string> f;
    BS_CHECK(throws([&] { f.clear(); disc::TrackFilesImage::parseGdi("2\n1 0 4 2352 a.bin 0\n", sz, f); }));          // conta errada
    BS_CHECK(throws([&] { f.clear(); disc::TrackFilesImage::parseGdi("1\n1 0 4 2352 a.bin 5\n", sz, f); }));          // deslocamento
    BS_CHECK(throws([&] { f.clear(); disc::TrackFilesImage::parseGdi("1\n1 0 7 2352 a.bin 0\n", sz, f); }));          // tipo
    BS_CHECK(throws([&] { f.clear(); disc::TrackFilesImage::parseGdi("1\n1 0 4 2000 a.bin 0\n", sz, f); }));          // setor
    BS_CHECK(throws([&] { f.clear(); disc::TrackFilesImage::parseGdi("2\n1 0 4 2352 a.bin 0\n2 0 4 2352 b.bin 0\n", sz, f); }));  // sobrepoe
    BS_CHECK(throws([&] { f.clear(); disc::TrackFilesImage::parseGdi("1\n1 0 4 2352 c.bin 0\n", sz, f); }));          // sem arquivo
    BS_CHECK(throws([&] { f.clear(); disc::TrackFilesImage::parseGdi("1\n1 0 4 2352 a.bin 0\n", sizes({{"a.bin", 2353}}), f); }));  // nao inteiro
}

BS_TEST(cue_parses_redump_gd_layout) {
    std::vector<std::string> files;
    const auto t = disc::TrackFilesImage::parseCue(kCue,
                                                   sizes({{"Jogo (USA) (Track 1).bin", 8683ull * 2352},
                                                          {"Jogo (USA) (Track 2).bin", 1201ull * 2352},
                                                          {"Jogo (USA) (Track 3).bin", 504150ull * 2352}}),
                                                   files);
    BS_CHECK(t.size() == 3 && files.size() == 3);
    BS_CHECK(t[0].start_lba == 0 && t[0].length == 8683 && t[0].session == 1);
    // o mesmo layout do GDI: a faixa 2 comeca 2 s (150 setores) depois do inicio do arquivo dela
    BS_CHECK(t[1].start_lba == 8833 && t[1].pregap == 150 && t[1].file_offset == 150ull * 2352 && t[1].length == 1051);
    BS_CHECK(t[2].start_lba == 45000 && t[2].session == 2 && t[2].length == 504150 && t[2].file_index == 2);
}

BS_TEST(cue_multitrack_file_pregap_and_rejections) {
    std::vector<std::string> f;
    // duas faixas num arquivo: a 1a termina no INDEX 00 da 2a
    const auto t = disc::TrackFilesImage::parseCue("FILE \"x.bin\" BINARY\n TRACK 01 MODE1/2352\n  INDEX 01 00:00:00\n"
                                                   " TRACK 02 AUDIO\n  INDEX 00 00:00:08\n  INDEX 01 00:00:10\n",
                                                   sizes({{"x.bin", 100ull * 2352}}), f);
    BS_CHECK(t.size() == 2);
    BS_CHECK(t[0].length == 8 && t[1].start_lba == 10 && t[1].pregap == 2 && t[1].length == 90 && t[1].file_offset == 10ull * 2352);
    // PREGAP (setores fora do arquivo) na 1a faixa desloca o arquivo; numa faixa do meio e recusado
    f.clear();
    const auto p = disc::TrackFilesImage::parseCue("FILE \"x.bin\" BINARY\n TRACK 01 MODE1/2352\n  PREGAP 00:00:05\n  INDEX 01 00:00:00\n",
                                                   sizes({{"x.bin", 100ull * 2352}}), f);
    BS_CHECK(p.size() == 1 && p[0].start_lba == 5 && p[0].pregap == 5 && p[0].length == 100 && p[0].file_offset == 0);
    auto sz = sizes({{"x.bin", 100ull * 2352}});
    BS_CHECK(throws([&] {
        f.clear();
        disc::TrackFilesImage::parseCue("FILE \"x.bin\" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n TRACK 02 AUDIO\n PREGAP 00:02:00\n"
                                        " INDEX 01 00:00:10\n", sz, f);
    }));
    BS_CHECK(throws([&] { f.clear(); disc::TrackFilesImage::parseCue("FILE \"x.bin\" BINARY\n TRACK 01 MODE1/2352\n", sz, f); }));  // sem INDEX 01
    BS_CHECK(throws([&] { f.clear(); disc::TrackFilesImage::parseCue("TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n", sz, f); }));       // sem FILE
    BS_CHECK(throws([&] { f.clear(); disc::TrackFilesImage::parseCue("FILE \"x.bin\" WAVE\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n", sz, f); }));
    BS_CHECK(throws([&] { f.clear(); disc::TrackFilesImage::parseCue("FILE \"x.bin\" BINARY\n TRACK 01 CDG\n INDEX 01 00:00:00\n", sz, f); }));
    BS_CHECK(throws([&] { f.clear(); disc::TrackFilesImage::parseCue("FILE \"x.bin\" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:61:00\n", sz, f); }));
    // a area de baixa densidade nao pode passar da LBA 45000
    BS_CHECK(throws([&] {
        f.clear();
        disc::TrackFilesImage::parseCue("FILE \"x.bin\" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\nREM HIGH-DENSITY AREA\n"
                                        "FILE \"x.bin\" BINARY\n TRACK 02 MODE1/2352\n INDEX 01 00:00:00\n",
                                        sizes({{"x.bin", 46000ull * 2352}}), f);
    }));
}

BS_TEST(gdi_reads_user_data_from_each_track_file) {
    const fs::path d = fs::temp_directory_path() / "bs gdi teste \xC3\xA7";
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    auto track = [&](const char* name, uint32_t sectors, uint8_t mark) {
        std::ofstream o(d / name, std::ios::binary);
        std::vector<uint8_t> s(2352, 0);
        for (uint32_t i = 0; i < sectors; ++i) {
            std::fill(s.begin(), s.end(), uint8_t(0xEE));       // sync/cabecalho: nao pode vazar nos dados
            std::fill(s.begin() + 16, s.begin() + 16 + 2048, uint8_t(mark + i));
            o.write(reinterpret_cast<const char*>(s.data()), std::streamsize(s.size()));
        }
    };
    track("t1.bin", 3, 0x10);
    track("t2.bin", 4, 0x40);
    std::ofstream(d / "jogo.gdi") << "2\n1 0 4 2352 t1.bin 0\n2 45000 4 2352 t2.bin 0\n";
    {
        const auto img = disc::disc_open(d / "jogo.gdi");
        BS_CHECK(img->formatName() == "GDI" && img->bootTrack().start_lba == 45000);
        uint8_t u[2048];
        img->readUserData(2, u);
        BS_CHECK(u[0] == 0x12 && u[2047] == 0x12);
        img->readUserData(45003, u);
        BS_CHECK(u[0] == 0x43 && u[2047] == 0x43);
        BS_CHECK(throws([&] { img->readUserData(10, u); }));  // fora das faixas
    }
    // CUE equivalente, com o nome do arquivo entre aspas e espacos
    fs::rename(d / "t2.bin", d / "faixa 2.bin");
    std::ofstream(d / "jogo.cue") << "FILE \"t1.bin\" BINARY\n TRACK 01 MODE1/2352\n  INDEX 01 00:00:00\nREM HIGH-DENSITY AREA\n"
                                     "FILE \"faixa 2.bin\" BINARY\n TRACK 02 MODE1/2352\n  INDEX 01 00:00:00\n";
    {
        const auto img = disc::disc_open(d / "jogo.cue");
        uint8_t    u[2048];
        img->readUserData(45001, u);
        BS_CHECK(img->formatName() == "CUE/BIN" && u[0] == 0x41);
    }
    BS_CHECK(throws([&] { disc::disc_open(d / "jogo.iso"); }));  // extensao nao suportada
    fs::remove_all(d, ec);
}

BS_TEST(boot_identity_rejects_unknown_with_hash) {
    std::vector<uint8_t> x(1975556, 0x5A);
    const BootIdentity   id = identify_boot(x, 45000);
    BS_CHECK(!id.version && !id.variant && id.data.size() == x.size() && id.sha256.size() == 64);
    const std::string m = boot_rejection_text("T13001N", "V1.000", id);
    BS_CHECK(m.find("1975556 bytes") != std::string::npos && m.find(id.sha256) != std::string::npos);
    BS_CHECK(identify_boot({}, UINT32_MAX).version == nullptr);
}

// PAL P4: a versao do jogo de cada id reconhecido (o executavel so roda os dados da sua versao; o Builder recusa o PAL
// ate o P8)
BS_TEST(boot_identity_versao_do_jogo) {
    BS_CHECK(bs::game_version_of("bluestinger_us_v1000") == 0);
    BS_CHECK(bs::game_version_of("bluestinger_us_v1000_gd") == 0);
    BS_CHECK(bs::game_version_of("bluestinger_us_v1000_variante") == 0);
    BS_CHECK(bs::game_version_of("bluestinger_pal_de_v1000") == 1);
    BS_CHECK(bs::game_version_of("bluestinger_pal_en_v1000") == 2);
    BS_CHECK(bs::game_version_of("bluestinger_jp_v1006") == 3);
    BS_CHECK(bs::game_version_of("bluestinger_dc_en_v1006") == 4);
    BS_CHECK(bs::game_version_of("bluestinger_dc_fr_v1006") == 5);
    BS_CHECK(bs::game_version_of("bluestinger_pal_fr_v1000") == 6);
    BS_CHECK(bs::game_version_of("") == -1);
    BS_CHECK(bs::game_version_of("outro_jogo") == -1);
    bool achou = false;
    for (size_t i = 0; i < bs::kExtraVersionCount; ++i)
        if (std::string(bs::kExtraVersions[i].id) == "bluestinger_pal_de_v1000") {
            achou = true;
            BS_CHECK(std::string(bs::kExtraVersions[i].product) == "T13001D");
            BS_CHECK(bs::kExtraVersions[i].boot_size == 1979556);
        }
    BS_CHECK(achou);
    // os ids de kExtraVersions sempre tem versao do jogo (senao o boot recusaria os proprios dados)
    for (size_t i = 0; i < bs::kExtraVersionCount; ++i) BS_CHECK(bs::game_version_of(bs::kExtraVersions[i].id) >= 0);
}

// Director's Cut (disc/gdi_export.h): o setor bruto de cada faixa e a folha .gdi. Imagem falsa em memoria com quadros de
// 2448 bytes como no CHD (dados MODE1_RAW com user_offset 16; audio big-endian, que sai little-endian como no GDI) e uma
// faixa cozida de 2048 (recusada: nao guarda o setor bruto). Revisao Codex 2026-11-19: faltava teste do readRawSector.
namespace {
class FakeChd final : public bs::disc::DiscImage {
public:
    std::vector<uint8_t> bytes;  // a "imagem": quadros de 2448
    bool                 bigEndianAudio = true;
    FakeChd() {
        using bs::disc::Track;
        using bs::disc::TrackMode;
        Track d;
        d.session = 1, d.number = 1, d.mode = TrackMode::Mode1, d.start_lba = 0, d.length = 2, d.sector_size = 2448, d.user_offset = 16;
        Track a;
        a.session = 1, a.number = 2, a.mode = TrackMode::Audio, a.start_lba = 600, a.length = 1, a.sector_size = 2448,
        a.file_offset = 2 * 2448, a.user_offset = 0;
        tracks_ = {d, a};
        bytes.resize(3 * 2448);
        for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = uint8_t(i * 7 + i / 2448);
    }
    std::string formatName() const override { return "fake"; }
    void        addCooked() {
        bs::disc::Track c;
        c.session = 2, c.number = 3, c.mode = bs::disc::TrackMode::Mode1, c.start_lba = 45000, c.length = 1, c.sector_size = 2048;
        tracks_.push_back(c);
    }

protected:
    void readRaw(const bs::disc::Track&, uint64_t offset, void* dst, size_t n) const override {
        if (offset + n > bytes.size()) throw bs::disc::DiscError("alem do fim");
        std::memcpy(dst, bytes.data() + offset, n);
    }
    bool audioBigEndian() const override { return bigEndianAudio; }
};
} // namespace

BS_TEST(gdi_export_setor_bruto_e_folha) {
    FakeChd img;
    uint8_t s[2352];
    img.readRawSector(img.tracks()[0], 1, s);  // dados: os 2352 primeiros bytes do quadro, sem troca
    BS_CHECK(std::memcmp(s, img.bytes.data() + 2448, 2352) == 0);
    img.readRawSector(img.tracks()[1], 0, s);  // audio: pares de bytes trocados
    bool troca = true;
    for (size_t i = 0; i < 2352; i += 2) troca &= s[i] == img.bytes[2 * 2448 + i + 1] && s[i + 1] == img.bytes[2 * 2448 + i];
    BS_CHECK(troca);
    bool fora = false;
    try {
        img.readRawSector(img.tracks()[0], 2, s);
    } catch (const bs::disc::DiscError&) {
        fora = true;
    }
    BS_CHECK(fora);  // alem do fim da faixa

    const fs::path dir = fs::temp_directory_path() / "bs unit gdi export";
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path gdi = bs::disc::export_gdi(img, dir, "disco");
    std::ifstream  g(gdi, std::ios::binary);
    const std::string folha((std::istreambuf_iterator<char>(g)), {});
    BS_CHECK(folha == "2\r\n1 0 4 2352 track01.bin 0\r\n2 600 0 2352 track02.raw 0\r\n");
    BS_CHECK(fs::file_size(dir / "track01.bin") == 2 * 2352 && fs::file_size(dir / "track02.raw") == 2352);
    g.close();

    img.addCooked();  // faixa cozida: sem o setor bruto, a exportacao recusa
    bool recusa = false;
    try {
        bs::disc::export_gdi(img, dir, "disco");
    } catch (const bs::disc::DiscError& e) {
        recusa = std::string(e.what()).find("setor bruto") != std::string::npos;
    }
    BS_CHECK(recusa);
    fs::remove_all(dir, ec);
}
