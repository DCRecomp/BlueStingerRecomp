// N9-L (L4, A11): mods com nomes em outra caixa valem igual no Windows e no Linux. Um mod feito no Windows chama os arquivos e
// pastas como quiser ("Manifest.TOML", "PT-BR", "VOZ", "Mod.toml"); onde o sistema de arquivos diferencia maiusculas, o
// jogo os acha do mesmo jeito. No Windows o teste passa pelo caminho exato (o comportamento de antes).
#include "test.h"

#include "audio/sound_catalog.h"
#include "core/path_case.h"
#include "localization/mod_packages.h"
#include "video/movie_catalog.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path p;
    TempDir() {
        p = fs::temp_directory_path() / ("bs-mods-case-" + std::to_string(std::random_device{}()));
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
};
void put(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}
bool sameFile(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return fs::equivalent(a, b, ec);
}
// um MPEG Program Stream valido (o video sintetico de tests/data/video num pack): tocavel no Linux sem o ffmpeg, porque o teste
// e da caixa dos nomes, nao do formato
std::string tinyPs() {
    std::ifstream     in(fs::path(BS_TEST_DATA_DIR) / "video" / "barras_64x48.m1v", std::ios::binary);
    const std::string es((std::istreambuf_iterator<char>(in)), {});
    std::string       ps("\0\0\x01\xBA\x21\0\x01\0\x01\x80\0\x01", 12);
    const size_t      len = 1 + es.size();
    ps += std::string("\0\0\x01\xE0", 4) + char(len >> 8) + char(len & 0xFF) + '\x0F' + es + std::string("\0\0\x01\xB9", 4);
    return ps;
}

} // namespace

BS_TEST(mods_case_resolve_path) {
    TempDir d;
    put(d.p / "Mods" / "Video" / "E001.Mp4", "x");
    BS_CHECK(sameFile(bs::native::resolve_case(d.p, fs::path("mods") / "video" / "e001.mp4"), d.p / "Mods" / "Video" / "E001.Mp4"));
    BS_CHECK(sameFile(bs::native::resolve_case(d.p, fs::path("MODS") / "VIDEO"), d.p / "Mods" / "Video"));
    // o que nao existe de jeito nenhum volta como pedido
    BS_CHECK(bs::native::resolve_case(d.p, fs::path("mods") / "nada.txt") == d.p / "mods" / "nada.txt");
    BS_CHECK(!fs::exists(bs::native::resolve_case(d.p, fs::path("mods") / "nada.txt")));
}

BS_TEST(mods_case_movie_catalog) {
    TempDir        d;
    const fs::path data = d.p / "data" / "video", mods = d.p / "mods" / "video";
    put(data / "manifest.toml", "versao = 1\n[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.mp4\"\nquadros = 1\n");
    put(data / "E001.mp4", "x");
    put(mods / "MANIFEST.TOML", "[[filme]]\nid = 3\narquivo = \"filmes/novo.mp4\"\nlegendas = { pt-BR = \"Legendas/E001.PT.srt\" }\n");
    put(mods / "Filmes" / "NOVO.MP4", tinyPs());
    put(mods / "legendas" / "e001.pt.SRT", "1\n00:00:00,000 --> 00:00:01,000\nOi\n");
    const auto  c = bs::native::video::MovieCatalog::load({data, mods});
    const auto* e = c.find(3u);
    BS_CHECK(e && e->mod && sameFile(e->file, mods / "Filmes" / "NOVO.MP4"));
    BS_CHECK(e && e->subtitles.count("pt-br") && sameFile(e->subtitles.at("pt-br"), mods / "legendas" / "e001.pt.SRT"));
}

BS_TEST(mods_case_sound_catalog) {
    TempDir        d;
    const fs::path mods = d.p / "mods" / "audio";
    put(mods / "PT-BR" / "VOZ" / "E001_01.WAV", "x");
    put(mods / "Musica" / "m2a.Ogg", "x");
    put(mods / "Manifest.Toml", "[[som]]\nid = \"musica/M2A\"\nsincronia = \"arquivo\"\n");
    const auto c = bs::native::audio::SoundCatalog::load({d.p / "data" / "audio", mods});
    const auto v = c.resolve("voz/E001_01", "pt-BR");
    BS_CHECK(v && v->mod && v->lang == "pt-br" && sameFile(v->file, mods / "PT-BR" / "VOZ" / "E001_01.WAV"));
    const auto m = c.resolve("musica/M2A");
    BS_CHECK(m && m->mod && m->sync == bs::native::audio::SyncPolicy::File && sameFile(m->file, mods / "Musica" / "m2a.Ogg"));
}

BS_TEST(mods_case_subtitle_packages) {
    TempDir d;
    put(d.p / "legendas-pt" / "MOD.TOML", "formato = 1\nid = \"legendas-pt\"\nidiomas = [\"pt-BR\"]\n");
    put(d.p / "legendas-pt" / "Filmes" / "PT-BR" / "E001.SRT", "x");
    const auto m = bs::native::localization::ModPackages::discover(d.p);
    BS_CHECK(m.packages().size() == 1);
    const auto f = m.find("filmes/{idioma}/E001.srt", "pt-BR");
    BS_CHECK(f && sameFile(f->file, d.p / "legendas-pt" / "Filmes" / "PT-BR" / "E001.SRT"));
}
