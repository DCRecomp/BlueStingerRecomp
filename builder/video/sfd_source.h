#pragma once
// N9-L (L5): o que o Builder sabe de um filme do disco (SFD) sem converter nada. Usado pelo conversor do Windows (que
// grava esses valores no manifesto ao lado do MP4) e pelo manifesto do Linux (que toca o SFD direto do disco): os dois
// leem a fonte do mesmo jeito (demux, ADX e pl_mpeg com os ultimos quadros) e arredondam os fins pelas mesmas funcoes.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
#include "pl_mpeg.h"
plm_frame_t* bs_plm_video_flush(plm_video_t* self);  // native/video/pl_mpeg_impl.c
}

namespace bs::builder::video {

// Fins de cada stream na fonte, em 100 ns (o que o catalogo usa para acabar o filme no mesmo passo de jogo que o
// original). Video: quadros / fps, arredondado ao mais proximo. Audio: amostras / taxa, truncado.
inline int64_t video_end_100ns(uint32_t frames, double fps) { return int64_t(std::llround(frames * 1e7 / fps)); }
inline int64_t audio_end_100ns(uint64_t samples, uint32_t rate) { return int64_t(samples) * 10'000'000 / rate; }
// Amostras (por canal) ate o instante t, arredondado para cima: a conta do corte do I5 e da intercalacao do conversor
inline uint64_t samples_until_100ns(int64_t t100ns, uint32_t rate) {
    return t100ns <= 0 ? 0 : (uint64_t(t100ns) * rate + 9'999'999) / 10'000'000;
}

// I5 (docs/IMAGENS-DISCO-PLANO.md): no disco original, o ADX de cada SFD declara bem mais amostras que o filme (em 15 dos
// 16, o dobro): depois do som do filme vem um preenchimento (silencio com nivel DC na maioria; no NEWGAME, 7,5 min de
// lixo saturado sem relacao com o filme). A conversao do CDI do projeto cortou tudo no fim do video. Pela regra de fim
// (o que acabar por ultimo) o filme ficaria esse tempo parado no ultimo quadro, o que o console nao faz. Devolve o
// numero de amostras por canal a manter: se o audio passa do fim do video por 1 s ou mais, o fim do video; senao, tudo
// (o arredondamento dos blocos do ADX, milissegundos, fica como esta: os filmes do CDI nao mudam). So para os SFD do
// disco (Builder); mods nao passam por aqui.
inline uint32_t audio_samples_without_padding(const std::vector<int16_t>& pcm, uint32_t channels, uint32_t rate,
                                              int64_t videoEnd100ns, uint32_t samples) {
    if (!channels || !rate || !samples || pcm.size() < size_t(samples) * channels || videoEnd100ns <= 0) return samples;
    const uint64_t videoSamples = samples_until_100ns(videoEnd100ns, rate);
    if (videoSamples >= samples || samples - videoSamples < rate) return samples;
    return uint32_t(videoSamples);
}

// Decodificador MPEG-1 (pl_mpeg) sobre o elementary stream ja separado pelo demux.
struct Mpeg1 {
    std::vector<uint8_t> es;
    plm_video_t*         v = nullptr;
    explicit Mpeg1(std::vector<uint8_t> bytes) : es(std::move(bytes)) {
        plm_buffer_t* b = plm_buffer_create_with_memory(es.data(), es.size(), 0);
        plm_buffer_signal_end(b);
        v = plm_video_create_with_buffer(b, 1);
    }
    ~Mpeg1() {
        if (v) plm_video_destroy(v);
    }
    Mpeg1(const Mpeg1&)            = delete;
    Mpeg1& operator=(const Mpeg1&) = delete;
    plm_frame_t* next() {
        plm_frame_t* f = plm_video_decode(v);
        return f ? f : bs_plm_video_flush(v);  // os ultimos quadros (defeitos de fim do pl_mpeg)
    }
};

struct SfdSource {
    uint32_t width = 0, height = 0, frames = 0;
    double   fps = 0;
    uint32_t sampleRate = 0, channels = 0, audioSamples = 0;  // amostras por canal (ADX)
    int64_t  videoEnd100ns = 0, audioEnd100ns = 0;
};

// Le o SFD inteiro (todos os quadros decodificados, como na conversao). false: arquivo ilegivel ou fora do formato.
bool analyze_sfd(const std::filesystem::path& sfd, SfdSource& out, std::string* err = nullptr);

} // namespace bs::builder::video
