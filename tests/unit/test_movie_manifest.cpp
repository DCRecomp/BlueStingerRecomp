// N9-L (L5): manifesto dos filmes tocados direto do disco (builder/video/movie_manifest_disc.cpp), nos dois sistemas.
// - os fins (video arredondado, audio truncado) sao os do conversor do Windows (sfd_source.h);
// - o manifesto de um SFD sintetico tem os mesmos campos da fonte que o do Windows, sem `arquivo`;
// - reaproveitamento, fonte trocada, SFD invalido (o manifesto anterior fica), cancelamento, MP4 velho fora;
// - o catalogo: com discDir le o manifesto sem `arquivo` (o SFD do disco); sem discDir (Windows) recusa como antes.
#include "test.h"
#include "sfd_fixture.h"

#include "video/movie_build.h"
#include "video/movie_catalog.h"
#include "video/sfd_source.h"

#include <toml++/toml.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
namespace bv = bs::builder::video;
namespace nv = bs::native::video;

namespace {

struct TempDir {
    fs::path p;
    explicit TempDir(const char* tag) {
        p = fs::temp_directory_path() / (std::string("bs-manifesto-") + tag + "-" + std::to_string(std::random_device{}()));
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
};

void writeAll(const fs::path& p, const std::vector<uint8_t>& d) {
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
}
std::string readText(const fs::path& p) {
    std::ifstream      in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

const toml::table* film(const toml::table& t, int64_t id) {
    if (const auto* arr = t["filme"].as_array())
        for (const auto& n : *arr)
            if (const auto* e = n.as_table(); e && (*e)["id"].value_or(int64_t(-1)) == id) return e;
    return nullptr;
}

} // namespace

BS_TEST(movie_end_rounding_matches_windows_converter) {
    // valores do manifesto do Windows (N5) para filmes reais: video arredondado ao mais proximo, audio truncado
    BS_CHECK(bv::video_end_100ns(13583, 30.0) == 4527666667);  // NEWGAME
    BS_CHECK(bv::video_end_100ns(1001, 30.0) == 333666667);    // E193
    BS_CHECK(bv::video_end_100ns(2153, 30.0) == 717666667);    // ENDING2
    BS_CHECK(bv::video_end_100ns(238, 30.0) == 79333333);      // E017
    BS_CHECK(bv::video_end_100ns(0, 30.0) == 0);
    BS_CHECK(bv::audio_end_100ns(19967010, 44100) == 4527666666);
    BS_CHECK(bv::audio_end_100ns(1471470, 44100) == 333666666);
    BS_CHECK(bv::audio_end_100ns(3164910, 44100) == 717666666);
    BS_CHECK(bv::audio_end_100ns(349860, 44100) == 79333333);
    // o que a conta truncada do tempo do quadro daria (1 passo de 16,7 ms antes em 3 filmes): o manifesto evita
    BS_CHECK(int64_t(13583) * (10'000'000 / 30) < bv::video_end_100ns(13583, 30.0));
}

BS_TEST(movie_manifest_disc_synthetic_sfd) {
    TempDir d("sint");
    const fs::path bsdata = d.p / "BSDATA", video = d.p / "video";
    fs::create_directories(bsdata);
    std::vector<int16_t> pcm;
    writeAll(bsdata / "NEWGAME.SFD", bs::test::make_sfd(&pcm));
    writeAll(bsdata / "E017.SFD", bs::test::make_sfd());
    // um MP4 de uma compilacao do Windows nesta pasta nao serve no Linux: sai
    fs::create_directories(video);
    writeAll(video / "NEWGAME.mp4", {1, 2, 3});

    bv::SfdSource src;
    std::string   err;
    BS_CHECK(bv::analyze_sfd(bsdata / "NEWGAME.SFD", src, &err));
    BS_CHECK(src.frames == 12 && src.width == 64 && src.height == 48 && src.fps == 30.0);
    BS_CHECK(src.sampleRate == 22050 && src.channels == 2 && src.audioSamples == pcm.size() / 2);
    BS_CHECK(src.videoEnd100ns == bv::video_end_100ns(12, 30.0) && src.videoEnd100ns == 4'000'000);
    BS_CHECK(src.audioEnd100ns == bv::audio_end_100ns(src.audioSamples, 22050));

    bv::MovieBuildOptions o;
    bv::MovieBuildResult  r;
    BS_CHECK(bv::build_disc_manifest(bsdata, video, o, r, &err));
    BS_CHECK(r.converted == 2 && r.reused == 0 && r.absent.size() == 15);
    BS_CHECK(!fs::exists(video / "NEWGAME.mp4"));
    toml::table t = toml::parse(readText(video / "manifest.toml"));
    BS_CHECK(t["conversor"].value_or(std::string()) == bv::kDiscManifestTag);
    BS_CHECK(t["filme"].as_array() && t["filme"].as_array()->size() == 17);
    const toml::table* e = film(t, 1);
    BS_CHECK(e != nullptr);
    if (e) {
        BS_CHECK((*e)["original"].value_or(std::string()) == "NEWGAME.SFD");
        BS_CHECK(!(*e)["arquivo"]);  // o SFD toca do disco: nenhum arquivo convertido
        BS_CHECK((*e)["quadros"].value_or(int64_t(0)) == 12);
        BS_CHECK((*e)["largura"].value_or(int64_t(0)) == 64 && (*e)["altura"].value_or(int64_t(0)) == 48);
        BS_CHECK((*e)["fim_video_100ns"].value_or(int64_t(0)) == src.videoEnd100ns);
        BS_CHECK((*e)["fim_audio_100ns"].value_or(int64_t(0)) == src.audioEnd100ns);
        BS_CHECK((*e)["amostras_audio"].value_or(int64_t(0)) == int64_t(src.audioSamples));
        BS_CHECK((*e)["fonte_sha256"].value_or(std::string()).size() == 64);
    }
    const toml::table* a = film(t, 0);
    BS_CHECK(a && (*a)["ausente"].value_or(false));

    // de novo, sem mudanca: tudo reaproveitado, mesmos bytes
    const std::string first = readText(video / "manifest.toml");
    BS_CHECK(bv::build_disc_manifest(bsdata, video, o, r, &err));
    BS_CHECK(r.converted == 0 && r.reused == 2);
    BS_CHECK(readText(video / "manifest.toml") == first);

    // fonte trocada (outro conteudo): lida de novo
    auto other = bs::test::make_sfd();
    other.insert(other.end(), 0x800, 0);
    writeAll(bsdata / "E017.SFD", other);
    BS_CHECK(bv::build_disc_manifest(bsdata, video, o, r, &err));
    BS_CHECK(r.converted == 1 && r.reused == 1);
}

BS_TEST(movie_manifest_disc_invalid_or_cancelled_keeps_previous) {
    TempDir d("ruim");
    const fs::path bsdata = d.p / "BSDATA", video = d.p / "video";
    fs::create_directories(bsdata);
    writeAll(bsdata / "NEWGAME.SFD", bs::test::make_sfd());
    bv::MovieBuildOptions o;
    bv::MovieBuildResult  r;
    std::string           err;
    BS_CHECK(bv::build_disc_manifest(bsdata, video, o, r, &err));
    const std::string good = readText(video / "manifest.toml");

    writeAll(bsdata / "E001.SFD", std::vector<uint8_t>(4096, 0x5A));  // nao e SFD
    err.clear();
    BS_CHECK(!bv::build_disc_manifest(bsdata, video, o, r, &err));
    BS_CHECK(err.find("E001.SFD") != std::string::npos);
    BS_CHECK(readText(video / "manifest.toml") == good);  // o anterior continua inteiro
    BS_CHECK(!fs::exists(video / "manifest.toml.tmp"));

    fs::remove(bsdata / "E001.SFD");
    std::atomic<bool> cancel{true};
    o.cancel = &cancel;
    BS_CHECK(!bv::build_disc_manifest(bsdata, video, o, r, &err));
    BS_CHECK(readText(video / "manifest.toml") == good);

    // --sem-filmes: a pasta sai inteira
    BS_CHECK(bv::clear_movies(video, &err));
    BS_CHECK(!fs::exists(video));
}

BS_TEST(movie_catalog_reads_disc_manifest_only_with_disc_dir) {
    TempDir d("cat");
    const fs::path bsdata = d.p / "disc" / "BSDATA", video = d.p / "video";
    fs::create_directories(bsdata);
    writeAll(bsdata / "NEWGAME.SFD", bs::test::make_sfd());
    bv::MovieBuildOptions o;
    bv::MovieBuildResult  r;
    std::string           err;
    BS_CHECK(bv::build_disc_manifest(bsdata, video, o, r, &err));

    // quem toca os SFD (Linux): o filme e o SFD do disco, com os fins do manifesto, sem problemas
    const auto cat = nv::MovieCatalog::load({video, {}, bsdata});
    const auto* e  = cat.find(1u);
    BS_CHECK(e != nullptr);
    if (e) {
        std::error_code ec;
        BS_CHECK(!e->absent && fs::equivalent(e->file, bsdata / "NEWGAME.SFD", ec));
        BS_CHECK(e->videoEnd100ns == bv::video_end_100ns(12, 30.0));
        BS_CHECK(e->audioEnd100ns > 0 && e->audioDelaySamples == 0 && e->frames == 12);
    }
    const auto* ab = cat.find(3u);  // E001: ausente no disco
    BS_CHECK(ab && ab->absent);
    BS_CHECK(cat.problems().empty());

    // nomes originais que saem da pasta do disco: recusados, com o motivo (revisao Codex 20261103-105354)
    {
        writeAll(d.p / "fora.sfd", bs::test::make_sfd());
        std::ofstream(video / "manifest.toml", std::ios::binary | std::ios::trunc)
            << "versao = 1\n[[filme]]\nid = 1\noriginal = \"../../fora.sfd\"\nquadros = 12\n"
            << "[[filme]]\nid = 2\noriginal = \"" << (d.p / "fora.sfd").generic_string() << "\"\nquadros = 12\n"
            << "[[filme]]\nid = 3\noriginal = \"sub/E001.SFD\"\nquadros = 12\n"
            << "[[filme]]\nid = 4\noriginal = \"LINK.SFD\"\nquadros = 12\n";
        std::error_code lec;
        fs::create_symlink(d.p / "fora.sfd", bsdata / "LINK.SFD", lec);  // no Windows sem privilegio: so os tres primeiros
        const auto bad = nv::MovieCatalog::load({video, {}, bsdata});
        for (uint32_t id : {1u, 2u, 3u}) BS_CHECK(bad.find(id) == nullptr || bad.find(id)->absent);
        if (!lec) BS_CHECK(bad.find(4u) == nullptr || bad.find(4u)->absent);
        BS_CHECK(bad.problems().size() >= (lec ? 3u : 4u));
        fs::remove(bsdata / "LINK.SFD", lec);
    }

    // sem discDir (o catalogo do Windows): a entrada sem `arquivo` e recusada como sempre foi
    BS_CHECK(bv::build_disc_manifest(bsdata, video, o, r, &err));
    const auto win = nv::MovieCatalog::load({video, {}, {}});
    BS_CHECK(win.find(1u) == nullptr || win.find(1u)->absent);
    BS_CHECK(!win.problems().empty());
}
