#include "video/sfd_info.h"

#include <algorithm>
#include <cstring>

namespace bs::native::video {

namespace {

bool fail(std::string* err, const std::string& m) {
    if (err) *err = m;
    return false;
}
uint16_t be16(std::span<const uint8_t> d, size_t o) { return uint16_t(d[o] << 8 | d[o + 1]); }
int64_t  pts(std::span<const uint8_t> d, size_t o) {
    return (int64_t(d[o] >> 1) & 7) << 30 | int64_t(be16(d, o + 1) >> 1) << 15 | int64_t(be16(d, o + 3) >> 1);
}

// PTS de um pacote PES do MPEG-1 (cabecalho depois do comprimento), -1 sem PTS
int64_t pesPts(std::span<const uint8_t> f, size_t k, size_t end) {
    while (k < end && f[k] == 0xFF) ++k;
    if (k < end && (f[k] & 0xC0) == 0x40) k += 2;
    if (k + 5 <= end && ((f[k] & 0xF0) == 0x20 || (f[k] & 0xF0) == 0x30)) return pts(f, k);
    return -1;
}

} // namespace

double mpeg1_frame_rate(uint32_t code) {
    static constexpr double kRates[] = {0, 24000.0 / 1001, 24, 25, 30000.0 / 1001, 30, 50, 60000.0 / 1001, 60};
    return code < 9 ? kRates[code] : 0;
}

bool sfd_identify(std::span<const uint8_t> f, SfdInfo& s, std::string* err) {
    s = {};
    s.sofdecHeader = f.size() >= 13 && std::memcmp(f.data(), "Sofdec Stream", 13) == 0;
    // 1) o sistema: packs, pacotes por stream, PTS (o mesmo percurso do demux, contando)
    size_t i = 0;
    while (i + 4 <= f.size() && !(f[i] == 0 && f[i + 1] == 0 && f[i + 2] == 1 && f[i + 3] == 0xBA)) ++i;
    if (i + 4 > f.size()) return fail(err, "nao e um MPEG Program Stream: nenhum pack (000001BA)");
    s.headerBytes = i;
    while (i + 4 <= f.size()) {
        if (f[i] != 0 || f[i + 1] != 0 || f[i + 2] != 1) {
            if (std::all_of(f.begin() + ptrdiff_t(i), f.end(), [](uint8_t b) { return b == 0; })) break;
            return fail(err, "codigo de inicio esperado no deslocamento " + std::to_string(i));
        }
        const uint8_t id = f[i + 3];
        if (id == 0xB9) {
            s.endCode = true;
            break;
        }
        if (id == 0xBA) {
            if (i + 12 > f.size()) return fail(err, "pack truncado");
            if ((f[i + 4] & 0xF0) != 0x20) return fail(err, "pack que nao e MPEG-1");
            ++s.packs, i += 12;
            continue;
        }
        if (i + 6 > f.size()) return fail(err, "pacote truncado");
        const size_t end = i + 6 + be16(f, i + 4);
        if (end > f.size()) return fail(err, "pacote alem do fim do arquivo");
        ++s.pesById[id];
        if (id == 0xBB) ++s.systemHeaders;
        else if (id == 0xBE) ++s.paddingPes;
        else if (id == 0xE0 || id == 0xC0) {
            const int64_t p = pesPts(f, i + 6, end);
            if (p >= 0) {
                int64_t& first = id == 0xE0 ? s.firstVideoPts : s.firstAudioPts;
                int64_t& last  = id == 0xE0 ? s.lastVideoPts : s.lastAudioPts;
                if (first < 0) first = p;
                last = std::max(last, p);
            }
        } else ++s.otherPes;
        i = end;
    }
    // 2) os streams separados
    SfdStreams st;
    if (!demux_sfd(f, st, err)) return false;
    s.videoBytes = st.video.size(), s.audioBytes = st.audio.size();
    // 3) o video MPEG-1: cabecalhos de sequencia, GOP e quadro
    const auto& v = st.video;
    for (size_t k = 0; k + 4 <= v.size(); ++k) {
        if (v[k] != 0 || v[k + 1] != 0 || v[k + 2] != 1) continue;
        const uint8_t code = v[k + 3];
        if (code == 0xB3 && k + 12 <= v.size()) {
            if (!s.sequenceHeaders) {
                s.width         = uint32_t(v[k + 4]) << 4 | v[k + 5] >> 4;
                s.height        = uint32_t(v[k + 5] & 0x0F) << 8 | v[k + 6];
                s.aspectCode    = v[k + 7] >> 4;
                s.frameRateCode = v[k + 7] & 0x0F;
                s.fps           = mpeg1_frame_rate(s.frameRateCode);
                s.bitRate       = (uint32_t(v[k + 8]) << 10 | uint32_t(v[k + 9]) << 2 | v[k + 10] >> 6) * 400;
                s.vbvBufferSize = ((uint32_t(v[k + 10]) & 0x1F) << 5 | v[k + 11] >> 3) * 16 * 1024;
            }
            ++s.sequenceHeaders;
        } else if (code == 0xB5) s.mpeg2 = true;
        else if (code == 0xB8 && k + 8 <= v.size()) {
            ++s.gops;
            s.closedGops += (v[k + 7] & 0x40) ? 1 : 0;
        } else if (code == 0x00 && k + 6 <= v.size()) {
            ++s.pictures;
            switch ((v[k + 5] >> 3) & 7) {
            case 1: ++s.iPictures; break;
            case 2: ++s.pPictures; break;
            case 3: ++s.bPictures; break;
            case 4: ++s.dPictures; break;
            default: break;
            }
        }
        k += 3;
    }
    if (!s.sequenceHeaders) return fail(err, "video sem cabecalho de sequencia MPEG-1");
    if (s.mpeg2) return fail(err, "video MPEG-2 (o Sofdec do disco e MPEG-1)");
    if (!s.fps) return fail(err, "codigo de quadros por segundo reservado no MPEG-1");
    if (!st.audio.empty()) {
        if (!adx_parse_header(st.audio, s.adx, err)) return false;
        s.hasAudio = true;
    }
    return true;
}

} // namespace bs::native::video
