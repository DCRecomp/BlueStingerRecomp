#pragma once
// N9-L (L5): o que o Builder sabe de um filme do disco (SFD) sem converter nada. Usado pelo conversor do Windows (que
// grava esses valores no manifesto ao lado do MP4) e pelo manifesto do Linux (que toca o SFD direto do disco): os dois
// leem a fonte do mesmo jeito (demux, ADX e pl_mpeg com os ultimos quadros) e arredondam os fins pelas mesmas funcoes.
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
