// N9-L (L5): o passo de filmes do Builder no Linux. O jogo toca os SFD do disco (pl_mpeg e ADX, sem conversao, D-L3);
// o Builder so grava o manifesto com os fins de cada filme, iguais aos do conversor do Windows (movie_manifest_disc.cpp).
#include "video/movie_build.h"

namespace bs::builder::video {

bool prepare_movies(const std::filesystem::path& bsdata, const std::filesystem::path& outDir, const MovieBuildOptions& o,
                    MovieBuildResult& r, std::string* err) {
    return build_disc_manifest(bsdata, outDir, o, r, err);
}

const char* movie_failure_prefix() { return "falha ao ler os filmes do disco: "; }

std::string movie_summary(const MovieBuildResult& r) {
    return "filmes: " + std::to_string(r.converted) + " lidos do disco, " + std::to_string(r.reused) +
           " reaproveitados, " + std::to_string(r.absent.size()) + " ausentes no disco (tocados direto do SFD)";
}

} // namespace bs::builder::video
