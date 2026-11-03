#pragma once
// N5 (docs/N5-PLANO.md, secao 4.1): passo do Builder que converte os filmes do disco do usuario
// (data\disc\BSDATA\*.SFD, a copia ja conferida setor a setor) em data\video\*.mp4 e grava o
// data\video\manifest.toml lido pelo catalogo do runtime (native/video/movie_catalog.h). Reaproveita
// filmes ja convertidos quando a fonte (SHA-256), o conversor e o MP4 batem.
#include "video/sfd_convert.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace bs::builder::video {

// Os 17 IDs da funcao de video do jogo (tabela 8C157688 do executavel NTSC-U). O ID 0 nao existe no
// disco: fica no manifesto como ausente (o jogo original tem o seu proprio comportamento para isso).
extern const char* const kMovieNames[17];

// Muda quando a conversao muda (forca reconverter os filmes ja preparados).
inline constexpr const char* kConverterTag = "n5-1 h264-q100 aac-192 bt601-tv";

struct MovieBuildOptions {
    ConvertOptions                                    conv;
    std::function<void(double, const std::string&)>   progress;  // 0..1
    std::function<void(const std::string&)>           log;
    const std::atomic<bool>*                          cancel = nullptr;
};

struct MovieBuildResult {
    int                      converted = 0, reused = 0;
    std::vector<std::string> absent;  // IDs sem arquivo no disco
    // Sem codificador H.264/AAC no Windows (edicoes N sem o Media Feature Pack): os filmes ficam "nao
    // convertidos" (com o motivo) e o resto da compilacao segue; o jogo os trata como ausentes.
    std::string encoderUnavailable;
};

bool build_movies(const std::filesystem::path& bsdata, const std::filesystem::path& outDir, const MovieBuildOptions& o,
                  MovieBuildResult& r, std::string* err = nullptr);

// Sem filmes (--sem-filmes): apaga os de uma compilacao anterior, para o pacote nao leva-los.
bool clear_movies(const std::filesystem::path& outDir, std::string* err = nullptr);

// N9-L (L5): manifesto dos filmes que o jogo toca direto do disco (Linux: native/video/movie_decoder_posix.cpp). Nada e
// convertido nem copiado: para cada SFD, os mesmos campos que o conversor do Windows grava sobre a fonte (quadros, fps,
// tamanho, amostras e os fins arredondados de sfd_source.h) e o SHA-256 da fonte, sem `arquivo` (o catalogo usa o SFD
// pelo nome original). Reaproveita a entrada anterior com a mesma fonte e a mesma marca.
inline constexpr const char* kDiscManifestTag = "n9l-sfd-direto-1";
bool build_disc_manifest(const std::filesystem::path& bsdata, const std::filesystem::path& outDir, const MovieBuildOptions& o,
                         MovieBuildResult& r, std::string* err = nullptr);

// O passo de filmes do Builder nesta plataforma (Windows: build_movies; Linux: build_disc_manifest), o prefixo da
// mensagem de falha e a linha do log.
bool        prepare_movies(const std::filesystem::path& bsdata, const std::filesystem::path& outDir, const MovieBuildOptions& o,
                           MovieBuildResult& r, std::string* err = nullptr);
const char* movie_failure_prefix();
std::string movie_summary(const MovieBuildResult& r);

} // namespace bs::builder::video
