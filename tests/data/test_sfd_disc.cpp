// N9-L (L4): os 16 filmes do disco do usuario no Linux, contra o que o Builder do Windows mediu ao converte-los (data/video/
// manifest.toml): o SFD e o mesmo (fonte_sha256), o formato e o esperado (sfd_identify), e o decodificador do Linux da o mesmo
// numero de quadros, as mesmas amostras de audio e os mesmos fins de video e audio. O filme tocado pelo MoviePlayback com o
// relogio logico de 60 Hz e os fins do manifesto (o que o jogo passa, nas duas plataformas) acaba no primeiro passo depois do
// ultimo dos dois fins, como no Windows. Sem o manifesto (so os fins do proprio SFD, com o tempo do quadro truncado) a conta
// pode acabar um passo antes: a coluna fim_sem_manifesto registra quando (o Builder do Linux, na L5, grava os fins como o do
// Windows).
// Tambem o SHA-256 da imagem em RGB24 (o formato do BS_PROBE_RAW do bs-video-probe), para comparar com a decodificacao do
// Windows. Dados do usuario: BS_DATA_DIR=<pasta data> (video/manifest.toml e disc/BSDATA); sem ela, SKIP (77).
//   BS_SFD_REPORT=<arquivo.tsv>: uma linha por filme.
//   BS_SFD_REF=<arquivo.tsv>: a decodificacao do Windows (tools/dev/sfd_referencia_windows.ps1: filme, SHA-256 do RGB24 do
//   pl_mpeg e do PCM do ADX, pelo bs-video-probe compilado com o MSVC). Com ela, imagem e audio tem de ser identicos bit a bit
//   (revisao Codex 20261103-030426: comparar o conteudo, nao so as contagens).
#include "util/hash.h"
#include "video/movie_decoder.h"
#include "video/movie_playback.h"
#include "video/sfd_info.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace nv = bs::native::video;

namespace {

std::string sha256(const std::vector<uint8_t>& d) {
    bs::util::Sha256 s;
    s.update(d.data(), d.size());
    return s.hexdigest();
}

int g_fail = 0;
void check(bool ok, const std::string& what) {
    if (!ok) std::printf("  FALHA: %s\n", what.c_str()), ++g_fail;
}

} // namespace

int main(int argc, char** argv) {
    const char* dir = std::getenv("BS_DATA_DIR");
    if (!dir || !*dir) return std::puts("SKIP: BS_DATA_DIR (pasta data com video/manifest.toml e disc/BSDATA)"), 77;
    // --conteudo: a comparacao de imagem e audio com o Windows e o objetivo; sem a referencia, SKIP (o teste sfd_disc cobre o resto)
    const bool  content = argc > 1 && std::string(argv[1]) == "--conteudo";
    const char* refEnv  = std::getenv("BS_SFD_REF");
    if (content && (!refEnv || !*refEnv)) return std::puts("SKIP: BS_SFD_REF (tools/dev/sfd_referencia_windows.ps1)"), 77;
    const fs::path data(dir), disc = data / "disc" / "BSDATA";
    toml::table    man;
    try {
        man = toml::parse_file((data / "video" / "manifest.toml").string());
    } catch (const toml::parse_error& e) {
        return std::printf("manifesto ilegivel: %s\n", std::string(e.description()).c_str()), 1;
    }
    std::ofstream rep;
    if (const char* r = std::getenv("BS_SFD_REPORT"); r && *r) {
        rep.open(r, std::ios::binary);
        rep << "filme\tsha256_ok\tlargura\taltura\tfps\tbitrate\tquadros\tquadros_manifesto\tI\tP\tB\tgops\tgops_fechados\tadx_hz\tadx_canais\t"
               "amostras\tamostras_manifesto\tpts_video\tpts_audio\tfim_video_100ns\tfim_audio_100ns\tfim_logico_100ns\tfim_sem_manifesto\t"
               "rgb24_sha256\tpcm_sha256\n";
    }
    std::map<std::string, std::pair<std::string, std::string>> ref;  // filme -> (rgb24, pcm) do Windows
    if (const char* r = std::getenv("BS_SFD_REF"); r && *r) {
        std::ifstream rin(r);
        for (std::string l; std::getline(rin, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            const size_t a = l.find('\t'), b = a == std::string::npos ? a : l.find('\t', a + 1);
            if (b != std::string::npos && l.compare(0, a, "filme") != 0) ref[l.substr(0, a)] = {l.substr(a + 1, b - a - 1), l.substr(b + 1)};
        }
        if (ref.empty()) return std::printf("BS_SFD_REF sem linhas: %s\n", r), 1;
    }
    int films = 0;
    for (const auto& node : *man["filme"].as_array()) {
        const auto& f = *node.as_table();
        if (f["ausente"].value_or(false)) continue;
        const std::string orig = f["original"].value_or(std::string());
        const fs::path    sfd  = disc / orig;
        std::printf("%s\n", orig.c_str());
        std::ifstream in(sfd, std::ios::binary);
        check(bool(in), "arquivo no disco");
        if (!in) continue;
        ++films;
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        const bool                 shaOk = sha256(bytes) == f["fonte_sha256"].value_or(std::string());
        check(shaOk, "SHA-256 igual ao fonte_sha256 do manifesto");
        nv::SfdInfo i;
        std::string err;
        check(nv::sfd_identify(bytes, i, &err), "identificacao: " + err);
        check(i.sofdecHeader && i.endCode && !i.mpeg2 && i.hasAudio, "Sofdec, MPEG-1 com fim e audio ADX");
        check(i.width == f["largura"].value_or(0u) && i.height == f["altura"].value_or(0u), "dimensoes do manifesto");
        check(std::abs(i.fps - f["fps"].value_or(0.0)) < 1e-6, "quadros por segundo do manifesto");
        // decodificacao inteira pelo decodificador do jogo
        auto dec = nv::make_movie_decoder();
        check(dec->open(sfd, &err), "abrir: " + err);
        nv::VideoFrame   fr;
        bs::util::Sha256 rgb;
        std::vector<uint8_t> line;
        uint32_t         frames = 0;
        while (dec->next_frame(fr, &err)) {
            line.resize(size_t(fr.width) * fr.height * 3);
            for (size_t k = 0; k < fr.argb.size(); ++k)
                line[3 * k] = uint8_t(fr.argb[k] >> 16), line[3 * k + 1] = uint8_t(fr.argb[k] >> 8), line[3 * k + 2] = uint8_t(fr.argb[k]);
            rgb.update(line.data(), line.size());
            ++frames;
        }
        std::vector<int16_t> pcm;
        int64_t              t       = 0;
        uint64_t             samples = 0;
        bs::util::Sha256     pcmSha;  // PCM s16le intercalado, como o .s16 do BS_PROBE_DUMP
        while (dec->next_audio(pcm, t, &err)) {
            samples += pcm.size() / std::max<uint32_t>(1, dec->format().channels);
            pcmSha.update(pcm.data(), pcm.size() * 2);
        }
        const uint32_t rate     = dec->format().sampleRate;
        const int64_t  vEnd     = int64_t(double(frames) * 1e7 / i.fps + 0.5);
        const int64_t  aEnd     = rate ? int64_t(samples) * 10'000'000 / rate : 0;
        const int64_t  manV     = f["fim_video_100ns"].value_or(int64_t(0)), manA = f["fim_audio_100ns"].value_or(int64_t(0));
        check(frames == f["quadros"].value_or(0u), "quadros: " + std::to_string(frames));
        check(samples == f["amostras_audio"].value_or(uint64_t(0)), "amostras de audio: " + std::to_string(samples));
        check(std::abs(vEnd - manV) <= 1, "fim do video " + std::to_string(vEnd) + " x " + std::to_string(manV));
        check(std::abs(aEnd - manA) <= 1, "fim do audio " + std::to_string(aEnd) + " x " + std::to_string(manA));
        // fim logico a 60 Hz, com os fins do manifesto (como o jogo passa) e sem eles (os do SFD)
        int64_t endAt[2] = {-1, -1};
        for (int mode = 0; mode < 2; ++mode) {
            nv::MoviePlayback   p;
            nv::PlaybackOptions po;
            if (mode == 0) po.sourceVideoEnd100ns = manV, po.sourceAudioEnd100ns = manA;
            if (!p.open(sfd, po, &err)) break;
            for (int64_t tick = 0; tick < 60 * 60 * 20 && endAt[mode] < 0; ++tick) {
                const int64_t pos = tick * 10'000'000 / 60;
                p.advance(pos), p.take_audio(pos);
                if (p.finished(pos)) endAt[mode] = pos;
            }
        }
        int64_t want = 0;
        for (int64_t tick = 0;; ++tick)
            if (const int64_t pos = tick * 10'000'000 / 60; pos >= std::max(manV, manA)) {
                want = pos;
                break;
            }
        check(endAt[0] == want, "fim logico com os fins do manifesto: " + std::to_string(endAt[0]) + " x " + std::to_string(want));
        if (endAt[1] != endAt[0]) std::printf("  sem o manifesto o fim logico seria %lld (1 passo antes)\n", static_cast<long long>(endAt[1]));
        const std::string rgbSha = rgb.hexdigest(), pcmHex = pcmSha.hexdigest();
        if (!ref.empty()) {
            const auto it = ref.find(orig);
            check(it != ref.end(), "filme na referencia do Windows");
            if (it != ref.end()) {
                check(it->second.first == rgbSha, "imagem (RGB24) identica a do Windows");
                check(it->second.second == pcmHex, "audio (PCM) identico ao do Windows");
            }
        }
        std::printf("  %ux%u %.3f fps %u kbit/s | %u quadros (I %u P %u B %u, %u GOPs, %u fechados) | ADX %u Hz %u ch %llu amostras | "
                    "PTS v %lld a %lld | fim %lld/%lld logico %lld | rgb24 %.16s\n",
                    i.width, i.height, i.fps, i.bitRate / 1000, frames, i.iPictures, i.pPictures, i.bPictures, i.gops, i.closedGops,
                    i.adx.sampleRate, i.adx.channels, static_cast<unsigned long long>(samples), static_cast<long long>(i.firstVideoPts),
                    static_cast<long long>(i.firstAudioPts), static_cast<long long>(vEnd), static_cast<long long>(aEnd),
                    static_cast<long long>(endAt[0]), rgbSha.c_str());
        if (rep)
            rep << orig << '\t' << shaOk << '\t' << i.width << '\t' << i.height << '\t' << i.fps << '\t' << i.bitRate << '\t' << frames << '\t'
                << f["quadros"].value_or(0u) << '\t' << i.iPictures << '\t' << i.pPictures << '\t' << i.bPictures << '\t' << i.gops << '\t'
                << i.closedGops << '\t' << i.adx.sampleRate << '\t' << unsigned(i.adx.channels) << '\t' << samples << '\t'
                << f["amostras_audio"].value_or(uint64_t(0)) << '\t' << i.firstVideoPts << '\t' << i.firstAudioPts << '\t' << vEnd << '\t'
                << aEnd << '\t' << endAt[0] << '\t' << endAt[1] << '\t' << rgbSha << '\t' << pcmHex << '\n';
    }
    std::printf("%d filmes, %d falhas\n", films, g_fail);
    return films > 0 && g_fail == 0 ? 0 : 1;
}
