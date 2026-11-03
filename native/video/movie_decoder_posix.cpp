// N9-L (L4): o decodificador de filme do Linux, escolhido pelo conteudo do arquivo e nao pela extensao: o SFD do disco (e
// qualquer MPEG-1 Program Stream com audio ADX) pelo pl_mpeg; o resto (MP4 de mod) pelo ffmpeg do sistema, se houver. Um MPEG-PS
// que o leitor do SFD recusa ainda tenta o ffmpeg (revisao Codex 20261103-030426).
#include "video/movie_backends_posix.h"
#include "video/sfd_info.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace bs::native::video {

MovieContainer sniff_movie(std::span<const uint8_t> h) {
    if (h.size() >= 13 && std::memcmp(h.data(), "Sofdec Stream", 13) == 0) return MovieContainer::MpegProgramStream;
    if (h.size() >= 4 && h[0] == 0 && h[1] == 0 && h[2] == 1 && h[3] == 0xBA) return MovieContainer::MpegProgramStream;
    return MovieContainer::Other;
}

MovieContainer sniff_movie(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return MovieContainer::Unreadable;
    uint8_t head[16] = {};
    in.read(reinterpret_cast<char*>(head), sizeof head);
    return sniff_movie(std::span<const uint8_t>(head, size_t(in.gcount())));
}

// o arquivo inteiro, como o leitor ao abrir (revisao Codex 20261103-034502: uma janela do comeco deixava passar audio MPEG que
// aparecesse depois dela). So os mods passam por aqui, uma vez, na carga do catalogo.
bool sfd_reader_fits(const std::filesystem::path& file) {
    std::ifstream              in(file, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    SfdInfo                    info;
    return in.good() || in.eof() ? sfd_identify(bytes, info) : false;
}

namespace {

class AutoDecoder final : public IMovieDecoder {
public:
    bool open(const std::filesystem::path& file, std::string* err) override {
        close();
        switch (sniff_movie(file)) {
        case MovieContainer::Unreadable:
            if (err) *err = "filme nao encontrado: " + file.filename().string();
            return false;
        case MovieContainer::MpegProgramStream: {
            impl_ = make_sfd_decoder();
            std::string e;
            if (impl_->open(file, &e)) return true;
            if (!ffmpeg_available()) {  // o erro do leitor do SFD
                impl_.reset();
                if (err) *err = e;
                return false;
            }
            break;  // MPEG-PS que nao e Sofdec (audio MPEG): o ffmpeg tenta
        }
        case MovieContainer::Other:
            if (!ffmpeg_available()) {
                if (err) *err = file.filename().string() + ": formato sem decodificador neste sistema (instale o ffmpeg)";
                return false;
            }
            break;
        }
        impl_ = make_ffmpeg_decoder();
        if (impl_->open(file, err)) return true;
        impl_.reset();
        return false;
    }
    const MovieFormat& format() const override { return impl_ ? impl_->format() : none_; }
    bool next_frame(VideoFrame& f, std::string* err) override { return impl_ && impl_->next_frame(f, err); }
    bool next_audio(std::vector<int16_t>& pcm, int64_t& t, std::string* err) override { return impl_ && impl_->next_audio(pcm, t, err); }
    void close() override {
        if (impl_) impl_->close();
        impl_.reset();
    }

private:
    std::unique_ptr<IMovieDecoder> impl_;
    MovieFormat                    none_;
};

} // namespace

std::unique_ptr<IMovieDecoder> make_movie_decoder() { return std::make_unique<AutoDecoder>(); }
bool                           plays_disc_movies() { return true; }
// a sonda real: o leitor do SFD so para Sofdec/MPEG-PS com ADX; o resto, so se o ffprobe ler um video no arquivo
bool can_decode(const std::filesystem::path& file) {
    switch (sniff_movie(file)) {
    case MovieContainer::MpegProgramStream: return sfd_reader_fits(file) || ffmpeg_can_read(file);
    case MovieContainer::Other: return ffmpeg_can_read(file);
    default: return false;
    }
}

} // namespace bs::native::video
