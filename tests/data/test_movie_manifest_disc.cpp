// N9-L (L5): manifesto dos filmes Windows x Linux com os dados do usuario (fora do repositorio).
// Gera o manifesto do Linux (build_disc_manifest) a partir de <BS_DATA_DIR>/disc/BSDATA e compara, filme a filme, com o
// manifesto do Builder do Windows (<BS_DATA_DIR>/video/manifest.toml ou BS_MANIFESTO_WINDOWS): quadros, fps, largura,
// altura, fins de video e de audio (100 ns), amostras de audio, SHA-256 da fonte e os ausentes. Os campos so do MP4
// (arquivo, sha256, PSNR, SSIM, atraso do AAC) nao existem no Linux, onde o SFD toca direto.
// BS_MANIFESTO_LINUX=<manifest.toml de uma compilacao real no Linux>: tambem precisa ser identico, byte a byte, ao gerado
// aqui. BS_MANIFESTO_RELATORIO=<arquivo.tsv>: tabela dos 16. Sem BS_DATA_DIR: SKIP (77).
#include "video/movie_build.h"

#include <toml++/toml.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
namespace bv = bs::builder::video;

namespace {

std::string readText(const fs::path& p) {
    std::ifstream      in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

std::map<std::string, const toml::table*> byOriginal(const toml::table& t) {
    std::map<std::string, const toml::table*> m;
    if (const auto* arr = t["filme"].as_array())
        for (const auto& n : *arr)
            if (const auto* e = n.as_table())
                if (auto o = (*e)["original"].value<std::string>()) m[*o] = e;
    return m;
}

} // namespace

int main() {
    const char* dir = std::getenv("BS_DATA_DIR");
    if (!dir || !*dir) return std::puts("SKIP: BS_DATA_DIR (pasta data com disc/BSDATA e o video/manifest.toml do Windows)"), 77;
    const fs::path data = dir;
    const char*    w    = std::getenv("BS_MANIFESTO_WINDOWS");
    const fs::path winManifest = w && *w ? fs::path(w) : data / "video" / "manifest.toml";
    toml::table    win;
    try {
        win = toml::parse(readText(winManifest));
    } catch (const toml::parse_error& e) {
        return std::printf("manifesto do Windows ilegivel (%s): %s\n", winManifest.string().c_str(), std::string(e.description()).c_str()), 1;
    }
    if (win["conversor"].value_or(std::string()).rfind("n5-", 0) != 0)
        return std::printf("%s nao e um manifesto do Builder do Windows (conversor n5-*)\n", winManifest.string().c_str()), 1;

    const fs::path tmp = fs::temp_directory_path() / ("bs-manifesto-disco-" + std::to_string(std::random_device{}()));
    bv::MovieBuildOptions o;
    bv::MovieBuildResult  r;
    std::string           err;
    if (!bv::build_disc_manifest(data / "disc" / "BSDATA", tmp, o, r, &err)) {
        std::error_code ec;
        fs::remove_all(tmp, ec);
        return std::printf("falha ao gerar o manifesto do Linux: %s\n", err.c_str()), 1;
    }
    const std::string linText = readText(tmp / "manifest.toml");
    std::error_code   ec;
    fs::remove_all(tmp, ec);
    const toml::table lin = toml::parse(linText);

    int  falhas = 0, filmes = 0;
    auto check  = [&](bool ok, const std::string& what) {
        if (!ok) std::printf("  FALHA: %s\n", what.c_str()), ++falhas;
    };
    const auto W = byOriginal(win), L = byOriginal(lin);
    check(W.size() == 17 && L.size() == 17, "17 entradas nos dois manifestos");
    std::ostringstream rep;
    rep << "filme\tausente\tquadros\tfps\tlargura\taltura\tfim_video_100ns\tfim_audio_100ns\tamostras_audio\tfonte_sha256\tigual\n";
    for (const auto& [orig, we] : W) {
        const auto it = L.find(orig);
        check(it != L.end(), orig + " no manifesto do Linux");
        if (it == L.end()) continue;
        const toml::table& a = *we;
        const toml::table& b = *it->second;
        const bool absentW = a["ausente"].value_or(false), absentL = b["ausente"].value_or(false);
        check(absentW == absentL, orig + ": ausente nos dois ou em nenhum");
        bool igual = absentW == absentL;
        if (!absentW && !absentL) {
            ++filmes;
            for (const char* k : {"quadros", "largura", "altura", "fim_video_100ns", "fim_audio_100ns", "amostras_audio"}) {
                const bool ok = a[k].value<int64_t>() && a[k].value<int64_t>() == b[k].value<int64_t>();
                check(ok, orig + ": " + k + " " + std::to_string(a[k].value_or(int64_t(-1))) + " x " + std::to_string(b[k].value_or(int64_t(-1))));
                igual = igual && ok;
            }
            const bool fpsOk = a["fps"].value<double>() && a["fps"].value_or(0.0) == b["fps"].value_or(-1.0);
            const bool shaOk = a["fonte_sha256"].value_or(std::string("w")) == b["fonte_sha256"].value_or(std::string("l"));
            check(fpsOk, orig + ": fps");
            check(shaOk, orig + ": fonte_sha256");
            check(!b["arquivo"], orig + ": o do Linux nao aponta para um arquivo convertido");
            igual = igual && fpsOk && shaOk;
        }
        char line[512];
        std::snprintf(line, sizeof line, "%s\t%d\t%lld\t%.6f\t%lld\t%lld\t%lld\t%lld\t%lld\t%s\t%s\n", orig.c_str(), int(absentL),
                      static_cast<long long>(b["quadros"].value_or(int64_t(0))), b["fps"].value_or(0.0),
                      static_cast<long long>(b["largura"].value_or(int64_t(0))), static_cast<long long>(b["altura"].value_or(int64_t(0))),
                      static_cast<long long>(b["fim_video_100ns"].value_or(int64_t(0))),
                      static_cast<long long>(b["fim_audio_100ns"].value_or(int64_t(0))),
                      static_cast<long long>(b["amostras_audio"].value_or(int64_t(0))),
                      b["fonte_sha256"].value_or(std::string("-")).c_str(), igual ? "sim" : "NAO");
        rep << line;
    }
    check(filmes == 16, "16 filmes presentes (" + std::to_string(filmes) + ")");
    if (const char* lm = std::getenv("BS_MANIFESTO_LINUX"); lm && *lm) {
        const std::string real = readText(lm);
        check(!real.empty() && real == linText, std::string("manifesto da compilacao no Linux (") + lm + ") identico ao gerado aqui");
    }
    if (const char* rp = std::getenv("BS_MANIFESTO_RELATORIO"); rp && *rp) std::ofstream(rp, std::ios::binary) << rep.str();
    std::printf("%s", rep.str().c_str());
    std::printf("%d filmes comparados com o manifesto do Windows; %d falha(s)\n", filmes, falhas);
    return falhas ? 1 : 0;
}
