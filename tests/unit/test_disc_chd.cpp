// CHD e pacotes .zip/.7z (docs/IMAGENS-DISCO-PLANO.md, I6 e I8). Sem dados do jogo: metadados e pacotes sinteticos.
// A leitura de um CHD real fica no bs_real_disc_test (BS_DISC_IMAGE apontando para o .chd).
#include "test.h"

#include "disc/archive.h"
#include "disc/chd_image.h"
#include "disc/disc_open.h"

#include <miniz.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace bs;
namespace fs = std::filesystem;

namespace {
template <class F> bool throws(F f) {
    try {
        f();
    } catch (const disc::DiscError&) {
        return true;
    }
    return false;
}
} // namespace

BS_TEST(chd_layout_gd_rom) {
    // chdman createcd de um GD-ROM: faixa 1 de dados, audio com o pregap guardado (V), a 3a faixa na area alta
    std::vector<disc::ChdTrackMeta> m = {
        disc::ChdImage::parseMeta("TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:8683 PAD:0 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0", true),
        disc::ChdImage::parseMeta("TRACK:2 TYPE:AUDIO SUBTYPE:NONE FRAMES:1201 PAD:0 PREGAP:150 PGTYPE:VAUDIO PGSUB:NONE POSTGAP:0", true),
        disc::ChdImage::parseMeta("TRACK:3 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:504150 PAD:0 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0", true),
    };
    const auto t = disc::ChdImage::layout(m, true, 2448);
    BS_CHECK(t.size() == 3);
    BS_CHECK(t[0].start_lba == 0 && t[0].length == 8683 && t[0].session == 1 && t[0].userDataOffset() == 16);
    BS_CHECK(t[1].start_lba == 8833 && t[1].length == 1051 && !t[1].isData());                 // o mesmo do GDI e do CUE
    BS_CHECK(t[1].file_offset == (8683ull + 1 + 150) * 2448);                                   // pula o pregap guardado
    BS_CHECK(t[2].start_lba == 45000 && t[2].session == 2 && t[2].length == 504150);
    BS_CHECK(t[2].file_offset == (8683ull + 1 + 1201 + 3) * 2448);
}

BS_TEST(chd_layout_redump_convertido_pelo_chdman) {
    // os metadados do CHD do Redump "Blue Stinger (USA)" (chdman): o PAD cobre o vao ate 45000 e nao e preenchimento
    std::vector<disc::ChdTrackMeta> m = {
        disc::ChdImage::parseMeta("TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:8833 PAD:150 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0", true),
        disc::ChdImage::parseMeta("TRACK:2 TYPE:AUDIO SUBTYPE:NONE FRAMES:36167 PAD:35116 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0", true),
        disc::ChdImage::parseMeta("TRACK:3 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:504150 PAD:0 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0", true),
    };
    const auto t = disc::ChdImage::layout(m, true, 2448);
    BS_CHECK(t[0].start_lba == 0 && t[1].start_lba == 8833 && t[2].start_lba == 45000 && t[2].session == 2);
    BS_CHECK(t[0].length == 8683 && t[1].length == 1051 && t[2].length == 504150);  // os mesmos do GDI e do CUE (Codex)
    BS_CHECK(t[2].file_offset == (8833ull + 3 + 36167 + 1) * 2448);  // multiplos de 4 quadros
}

BS_TEST(chd_layout_cd_and_rejections) {
    // CD (CHT2): preenchimento ate multiplo de 4 quadros; pregap nao guardado so desloca a posicao
    std::vector<disc::ChdTrackMeta> m = {
        disc::ChdImage::parseMeta("TRACK:1 TYPE:MODE2_RAW SUBTYPE:NONE FRAMES:1001 PREGAP:0 PGTYPE:MODE2 PGSUB:NONE POSTGAP:0", false),
        disc::ChdImage::parseMeta("TRACK:2 TYPE:MODE1 SUBTYPE:NONE FRAMES:10 PREGAP:150 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0", false),
    };
    BS_CHECK(m[0].pad == 3 && m[1].pad == 2);
    const auto t = disc::ChdImage::layout(m, false, 2448);
    BS_CHECK(t[0].userDataOffset() == 24 && t[1].userDataOffset() == 0);
    BS_CHECK(t[1].start_lba == 1001 + 150 && t[1].file_offset == 1004ull * 2448 && t[1].session == 1);
    // formato antigo (CHTR) so com os 4 campos
    BS_CHECK(disc::ChdImage::parseMeta("TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:20", false).frames == 20);
    BS_CHECK(throws([] { disc::ChdImage::parseMeta("lixo", true); }));
    BS_CHECK(throws([] {
        disc::ChdImage::layout({disc::ChdImage::parseMeta("TRACK:1 TYPE:CDG SUBTYPE:NONE FRAMES:5 PAD:3 PREGAP:0 PGTYPE:X PGSUB:NONE POSTGAP:0", true)},
                               true, 2448);
    }));
    BS_CHECK(throws([] {
        disc::ChdImage::layout({disc::ChdImage::parseMeta("TRACK:2 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:5 PAD:3 PREGAP:0 PGTYPE:X PGSUB:NONE POSTGAP:0", true)},
                               true, 2448);
    }));  // comeca na faixa 2
    BS_CHECK(throws([] { disc::ChdImage::layout({}, true, 2448); }));
}

BS_TEST(archive_picks_image_and_rejects_unsafe_names) {
    using E = disc::ArchiveEntry;
    BS_CHECK(disc::archive_image_name({{"Jogo (USA).cue", 300}, {"Jogo (USA) (Track 1).bin", 1}, {"x.cdi", 1}}) == "Jogo (USA).cue");
    BS_CHECK(disc::archive_image_name({{"pasta/", 0, true}, {"pasta/jogo.GDI", 88}, {"pasta/jogo.cue", 1}}) == "pasta/jogo.GDI");
    BS_CHECK(disc::archive_image_name({{"a.chd", 9}, {"b.cdi", 9}}) == "a.chd");
    BS_CHECK(throws([] { disc::archive_image_name({E{"leia.txt", 1}}); }));
    BS_CHECK(throws([] { disc::archive_image_name({E{"../fora.gdi", 1}}); }));
    BS_CHECK(throws([] { disc::archive_image_name({E{"C:/x.gdi", 1}}); }));
    BS_CHECK(throws([] { disc::archive_image_name({E{"/abs.gdi", 1}}); }));
    BS_CHECK(throws([] { disc::archive_image_name({E{"a\\..\\..\\b.gdi", 1}}); }));
    BS_CHECK(disc::archive_supported("x.ZIP") && disc::archive_supported("y.7z") && !disc::archive_supported("z.gdi"));
    // limites (revisao Codex): entradas demais ou mais de 8 GB extraidos
    std::vector<E> muitos(300, E{"f.bin", 1});
    muitos[0].name = "a.gdi";
    for (size_t i = 1; i < muitos.size(); ++i) muitos[i].name = "f" + std::to_string(i) + ".bin";
    BS_CHECK(throws([&] { disc::archive_image_name(muitos); }));
    BS_CHECK(throws([] { disc::archive_image_name({E{"a.gdi", 1}, E{"t.bin", 5ull << 30}, E{"u.bin", 4ull << 30}}); }));
    BS_CHECK(throws([] { disc::archive_image_name({E{"a.gdi", 1}, E{"t.bin", ~0ull}}); }));
}

BS_TEST(archive_zip_extracts_gdi_and_tracks) {
    const fs::path d = fs::temp_directory_path() / "bs pacote teste \xC3\xA7";
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    // um GDI de uma faixa de 2 setores dentro de um .zip (miniz grava so neste teste)
    std::string track(2 * 2352, '\0');
    for (int s = 0; s < 2; ++s)
        for (int i = 0; i < 2048; ++i) track[size_t(s) * 2352 + 16 + size_t(i)] = char(0x30 + s);
    const std::string gdi = "1\r\n1 45000 4 2352 \"faixa 1.bin\" 0\r\n";
    {
        mz_zip_archive z{};
        BS_CHECK(mz_zip_writer_init_heap(&z, 0, 0));
        BS_CHECK(mz_zip_writer_add_mem(&z, "Jogo/jogo.gdi", gdi.data(), gdi.size(), MZ_DEFAULT_COMPRESSION));
        BS_CHECK(mz_zip_writer_add_mem(&z, "Jogo/faixa 1.bin", track.data(), track.size(), MZ_DEFAULT_COMPRESSION));
        void*  buf = nullptr;
        size_t n   = 0;
        BS_CHECK(mz_zip_writer_finalize_heap_archive(&z, &buf, &n));
        std::ofstream(d / "jogo.zip", std::ios::binary).write(static_cast<const char*>(buf), std::streamsize(n));
        mz_zip_writer_end(&z);
    }
    const auto l = disc::archive_list(d / "jogo.zip");
    BS_CHECK(l.size() == 2 && disc::archive_image_name(l) == "Jogo/jogo.gdi");
    double           last = 0;
    const fs::path   img  = disc::archive_extract_image(d / "jogo.zip", d / "extraido", [&](double f) { last = f; });
    BS_CHECK(last == 1.0 && fs::exists(img) && fs::file_size(d / "extraido" / "Jogo" / "faixa 1.bin") == track.size());
    {
        const auto i = disc::disc_open(img);
        uint8_t    u[2048];
        i->readUserData(45001, u);
        BS_CHECK(u[0] == 0x31 && u[2047] == 0x31);
    }
    // uma folha dentro do pacote nao pode citar faixas fora da pasta extraida (revisao Codex 2026-11-19)
    std::ofstream(d / "segredo.bin", std::ios::binary).write(track.data(), std::streamsize(track.size()));
    const auto u8abs = (d / "segredo.bin").u8string();  // a folha e UTF-8
    for (const std::string& ref : {std::string("../segredo.bin"), std::string(u8abs.begin(), u8abs.end())}) {
        mz_zip_archive z{};
        BS_CHECK(mz_zip_writer_init_heap(&z, 0, 0));
        const std::string cue = "FILE \"" + ref + "\" BINARY\n TRACK 01 MODE1/2352\n  INDEX 01 00:00:00\n";
        BS_CHECK(mz_zip_writer_add_mem(&z, "jogo.cue", cue.data(), cue.size(), MZ_DEFAULT_COMPRESSION));
        void*  buf = nullptr;
        size_t n   = 0;
        BS_CHECK(mz_zip_writer_finalize_heap_archive(&z, &buf, &n));
        std::ofstream(d / "fora.zip", std::ios::binary | std::ios::trunc).write(static_cast<const char*>(buf), std::streamsize(n));
        mz_zip_writer_end(&z);
        const fs::path x = d / "fora";
        fs::remove_all(x, ec);
        const fs::path cueImg = disc::archive_extract_image(d / "fora.zip", x);
        BS_CHECK(throws([&] { disc::disc_open(cueImg, x); }));  // confinado: recusa
        BS_CHECK(!throws([&] { disc::disc_open(cueImg); }));    // fora de pacote, o CUE do jogador pode citar caminhos
    }
    std::atomic<bool> cancel{true};
    BS_CHECK(throws([&] { disc::archive_extract_image(d / "jogo.zip", d / "cancelado", {}, &cancel); }));
    BS_CHECK(throws([&] { disc::archive_list(d / "jogo.gdi"); }));
    fs::remove_all(d, ec);
}
