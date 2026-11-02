// N9-L: filmes do disco no Linux, sem reconversao. O SFD (Sofdec) e decodificado na hora: pl_mpeg para o MPEG-1 e o ADX de
// sfd_media.cpp, os mesmos que o Builder do Windows usa como referencia. O MP4 de um mod vai por outro decodificador.
#include "video/movie_decoder.h"
#include "video/sfd_media.h"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "pl_mpeg.h"
extern "C" plm_frame_t* bs_plm_video_flush(plm_video_t* self);  // pl_mpeg_impl.c

namespace bs::native::video {

namespace {

constexpr size_t kAudioBlockFrames = 4096;

class SfdMovieReader final : public IMovieDecoder {
public:
    ~SfdMovieReader() override { close(); }

    bool open(const std::filesystem::path& file, std::string* err) override {
        close();
        std::ifstream in(file, std::ios::binary);
        if (!in) return fail(err, "filme nao encontrado");
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        SfdStreams s;
        if (!demux_sfd(bytes, s, err)) return false;
        if (s.video.empty()) return fail(err, "SFD sem video");
        es_ = std::move(s.video);
        plm_buffer_t* b = plm_buffer_create_with_memory(es_.data(), es_.size(), 0);
        plm_buffer_signal_end(b);
        video_ = plm_video_create_with_buffer(b, 1);
        if (!video_ || !plm_video_has_header(video_)) return fail(err, "SFD sem cabecalho MPEG-1");
        fmt_.width  = uint32_t(plm_video_get_width(video_));
        fmt_.height = uint32_t(plm_video_get_height(video_));
        fmt_.fps    = plm_video_get_framerate(video_);
        if (!s.audio.empty()) {
            AdxInfo adx;
            if (!adx_decode(s.audio, pcm_, &adx, err)) return false;
            fmt_.hasAudio   = true;
            fmt_.sampleRate = adx.sampleRate;
            fmt_.channels   = adx.channels;
        }
        return true;
    }

    const MovieFormat& format() const override { return fmt_; }

    bool next_frame(VideoFrame& f, std::string*) override {
        if (!video_) return false;
        plm_frame_t* p = plm_video_decode(video_);
        if (!p) p = bs_plm_video_flush(video_);  // os ultimos quadros
        if (!p) return false;
        f.width = p->width, f.height = p->height;
        f.argb.resize(size_t(p->width) * p->height);
        plm_frame_to_bgra(p, reinterpret_cast<uint8_t*>(f.argb.data()), int(p->width) * 4);  // B,G,R,A = 0xAARRGGBB
        for (auto& px : f.argb) px |= 0xFF000000u;
        f.time100ns     = int64_t(double(frame_) * 1e7 / fmt_.fps);
        f.duration100ns = int64_t(1e7 / fmt_.fps);
        ++frame_;
        return true;
    }

    bool next_audio(std::vector<int16_t>& pcm, int64_t& time100ns, std::string*) override {
        if (!fmt_.hasAudio || audioPos_ >= pcm_.size()) return false;
        const size_t n = std::min(kAudioBlockFrames * fmt_.channels, pcm_.size() - audioPos_);
        pcm.assign(pcm_.begin() + ptrdiff_t(audioPos_), pcm_.begin() + ptrdiff_t(audioPos_ + n));
        time100ns = int64_t(audioPos_ / fmt_.channels) * 10'000'000 / fmt_.sampleRate;
        audioPos_ += n;
        return true;
    }

    void close() override {
        if (video_) plm_video_destroy(video_);
        video_ = nullptr;
        es_.clear(), pcm_.clear();
        fmt_ = {}, frame_ = 0, audioPos_ = 0;
    }

private:
    static bool fail(std::string* err, const char* m) {
        if (err) *err = m;
        return false;
    }

    std::vector<uint8_t> es_;
    std::vector<int16_t> pcm_;
    plm_video_t*         video_ = nullptr;
    MovieFormat          fmt_;
    uint64_t             frame_ = 0;
    size_t               audioPos_ = 0;
};

} // namespace

std::unique_ptr<IMovieDecoder> make_movie_decoder() { return std::make_unique<SfdMovieReader>(); }
bool                           plays_disc_movies() { return true; }

} // namespace bs::native::video
