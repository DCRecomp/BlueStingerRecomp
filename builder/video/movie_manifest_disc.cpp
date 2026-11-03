// N9-L (L5): manifesto dos filmes tocados direto do disco (movie_build.h) e o que os dois sistemas tem em comum no passo
// de filmes (a tabela dos IDs e a limpeza do --sem-filmes).
#include "video/movie_build.h"

#include "util/hash.h"
#include "video/sfd_source.h"

#include <toml++/toml.hpp>

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace bs::builder::video {

namespace fs = std::filesystem;

const char* const kMovieNames[17] = {"OPENING.SFD", "NEWGAME.SFD", "E017.SFD",  "E001.SFD",  "E107.SFD",   "E179.SFD",
                                     "E180.SFD",    "E185A.SFD",   "E185B.SFD", "E193.SFD",  "E195.SFD",   "E200A.SFD",
                                     "E200B.SFD",   "ENDING.SFD",  "E016.SFD",  "E134.SFD",  "ENDING2.SFD"};

namespace {

bool fail(std::string* err, const std::string& m) {
    if (err) *err = m;
    return false;
}

std::string fileSha(const fs::path& p) {
    std::ifstream     f(p, std::ios::binary);
    util::Sha256      sha;
    std::vector<char> buf(1 << 20);
    while (f) {
        f.read(buf.data(), std::streamsize(buf.size()));
        sha.update(buf.data(), size_t(f.gcount()));
    }
    return sha.hexdigest();
}

std::string q(const std::string& s) { return "\"" + s + "\""; }

} // namespace

bool clear_movies(const fs::path& outDir, std::string* err) {
    std::error_code ec;
    fs::remove_all(outDir, ec);  // pacotes anteriores ficam intactos: tem hard links proprios
    if (ec) return fail(err, "nao foi possivel apagar " + outDir.string() + " (" + ec.message() + ")");
    return true;
}

bool build_disc_manifest(const fs::path& bsdata, const fs::path& outDir, const MovieBuildOptions& o, MovieBuildResult& r,
                         std::string* err) {
    r = {};
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) return fail(err, "nao foi possivel criar " + outDir.string());
    // manifesto anterior (desta mesma forma): original -> entrada
    std::map<std::string, toml::table> prev;
    const fs::path                     man = outDir / "manifest.toml";
    if (fs::is_regular_file(man, ec)) {
        try {
            std::ifstream      in(man, std::ios::binary);
            std::ostringstream text;
            text << in.rdbuf();
            const toml::table t = toml::parse(text.str());
            if (t["conversor"].value_or(std::string()) == kDiscManifestTag)
                if (const auto* arr = t["filme"].as_array())
                    for (const auto& n : *arr)
                        if (const auto* e = n.as_table())
                            if (auto orig = (*e)["original"].value<std::string>()) prev[*orig] = *e;
        } catch (const toml::parse_error&) {
        }
    }
    // os MP4 de uma compilacao do Windows nao servem aqui: a pasta fica so com o manifesto
    for (const auto& e : fs::directory_iterator(outDir, ec))
        if (e.is_regular_file() && e.path().filename() != "manifest.toml") fs::remove(e.path(), ec);

    std::ostringstream m;
    m << "# Gerado pelo Blue Stinger Builder a partir do disco do usuario (N9-L, docs/N9-L-PLANO.md). Nao versionar.\n"
      << "# Os filmes tocam direto do disco (data/disc/BSDATA/*.SFD): nada foi convertido.\n"
      << "versao = 1\nconversor = " << q(kDiscManifestTag) << "\nfonte = \"disco\"\n";
    for (uint32_t id = 0; id < 17; ++id) {
        if (o.cancel && o.cancel->load()) return fail(err, "cancelado");
        const std::string original = kMovieNames[id];
        const fs::path    src      = bsdata / original;
        m << "\n[[filme]]\nid = " << id << "\noriginal = " << q(original) << "\n";
        if (!fs::is_regular_file(src, ec)) {
            r.absent.push_back(std::to_string(id) + " " + original);
            m << "ausente = true  # nao existe no disco: o jogo original trata a ausencia\n";
            continue;
        }
        if (o.progress) o.progress(double(id) / 17.0, original);
        const std::string sha = fileSha(src);
        SfdSource         s;
        if (const auto it = prev.find(original); it != prev.end() && it->second["fonte_sha256"].value_or(std::string()) == sha) {
            const toml::table& t = it->second;
            s.frames             = uint32_t(t["quadros"].value_or(int64_t(0)));
            s.fps                = t["fps"].value_or(0.0);
            s.width              = uint32_t(t["largura"].value_or(int64_t(0)));
            s.height             = uint32_t(t["altura"].value_or(int64_t(0)));
            s.videoEnd100ns      = t["fim_video_100ns"].value_or(int64_t(0));
            s.audioEnd100ns      = t["fim_audio_100ns"].value_or(int64_t(0));
            s.audioSamples       = uint32_t(t["amostras_audio"].value_or(int64_t(0)));
            ++r.reused;
        } else {
            std::string aerr;
            if (!analyze_sfd(src, s, &aerr)) return fail(err, original + ": " + aerr);
            ++r.converted;  // aqui: lido e medido (nada e convertido)
            if (o.log) {
                char b[200];
                std::snprintf(b, sizeof b, "filme %s: %u quadros a %.3f fps, %u amostras de audio, fim %lld/%lld (100 ns)",
                              original.c_str(), s.frames, s.fps, s.audioSamples, static_cast<long long>(s.videoEnd100ns),
                              static_cast<long long>(s.audioEnd100ns));
                o.log(b);
            }
        }
        char b[512];
        std::snprintf(b, sizeof b,
                      "quadros = %u\nfps = %.6f\nlargura = %u\naltura = %u\nfim_video_100ns = %lld\nfim_audio_100ns = %lld\n"
                      "amostras_audio = %u\natraso_audio_amostras = 0\nfonte_sha256 = \"%s\"\n",
                      s.frames, s.fps, s.width, s.height, static_cast<long long>(s.videoEnd100ns),
                      static_cast<long long>(s.audioEnd100ns), s.audioSamples, sha.c_str());
        m << b;
    }
    // gravado por troca: o catalogo nunca ve um manifesto pela metade
    const fs::path mtmp = outDir / "manifest.toml.tmp";
    {
        std::ofstream f(mtmp, std::ios::binary | std::ios::trunc);
        f << m.str();
        f.close();
        if (!f) return fail(err, "nao foi possivel gravar " + mtmp.string());
    }
    fs::rename(mtmp, man, ec);
    if (ec) {  // (o rename do MSVC nao substitui: so nos testes do Windows)
        fs::remove(man, ec);
        fs::rename(mtmp, man, ec);
        if (ec) return fail(err, "nao foi possivel gravar " + man.string() + " (" + ec.message() + ")");
    }
    if (o.progress) o.progress(1.0, "filmes prontos");
    return true;
}

} // namespace bs::builder::video
