#include "video/sfd_source.h"

#include "video/sfd_media.h"

#include <fstream>
#include <iterator>

namespace bs::builder::video {

namespace fs = std::filesystem;

bool analyze_sfd(const fs::path& sfd, SfdSource& r, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    r = {};
    std::vector<uint8_t> file;
    {
        std::ifstream f(sfd, std::ios::binary);
        if (!f) return fail("nao abre " + sfd.string());
        file.assign(std::istreambuf_iterator<char>(f), {});
    }
    SfdStreams s;
    if (!demux_sfd(file, s, err)) return false;
    std::vector<int16_t> pcm;
    AdxInfo              a;
    if (!adx_decode(s.audio, pcm, &a, err)) return false;
    if (a.sampleRate == 0) return fail("ADX sem taxa de amostragem");
    Mpeg1 m(std::move(s.video));
    if (!m.v) return fail("MPEG-1 sem cabecalho de sequencia");
    plm_frame_t* f = m.next();
    if (!f) return fail("MPEG-1 sem quadros");
    r.width = f->width, r.height = f->height, r.fps = plm_video_get_framerate(m.v);
    if (!(r.fps > 0)) return fail("MPEG-1 sem taxa de quadros");
    for (; f; f = m.next()) ++r.frames;
    r.sampleRate = a.sampleRate, r.channels = a.channels, r.audioSamples = a.totalSamples;
    r.videoEnd100ns = video_end_100ns(r.frames, r.fps);
    r.audioEnd100ns = audio_end_100ns(a.totalSamples, a.sampleRate);
    return true;
}

} // namespace bs::builder::video
