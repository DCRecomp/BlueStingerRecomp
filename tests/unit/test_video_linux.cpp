// N9-L (L4): filmes no Linux, sem conteudo do jogo. Um SFD montado aqui (cabecalho "Sofdec Stream", packs MPEG-1, o video de
// tests/data/video/barras_64x48.m1v, barras de cor do ffmpeg, e um ADX gerado pelo proprio teste): identificacao, decodificacao
// (cores, quadros, tempos), audio, fim e interrupcao pelo MoviePlayback, arquivos invalidos ou ausentes sem queda, a escolha do
// decodificador pelo conteudo e o MP4 de mod pelo ffmpeg (aqui um ffmpeg falso, por script, para o teste nao depender dele).
#include "test.h"
#include "sfd_fixture.h"

#include "video/movie_backends_posix.h"
#include "video/movie_catalog.h"
#include "video/movie_playback.h"
#include "video/sfd_info.h"
#include "video/sfd_media.h"

#include <sys/stat.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace nv = bs::native::video;

namespace {

struct TempDir {
    fs::path p;
    explicit TempDir(const char* tag) {
        p = fs::temp_directory_path() / (std::string("bs-vlinux-") + tag + "-" + std::to_string(std::random_device{}()));
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
};

// troca o PATH durante o teste
struct PathGuard {
    std::string old;
    explicit PathGuard(const std::string& v) {
        const char* o = std::getenv("PATH");
        old           = o ? o : "";
        setenv("PATH", v.c_str(), 1);
    }
    ~PathGuard() { setenv("PATH", old.c_str(), 1); }
};

std::vector<uint8_t> readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void writeAll(const fs::path& p, const std::vector<uint8_t>& d) {
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
}
void writeText(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }

// o SFD sintetico (makeSfd) e comum com o manifesto do Builder (L5): tests/unit/sfd_fixture.h
std::vector<uint8_t> makeSfd(std::vector<int16_t>* pcmOut = nullptr) { return bs::test::make_sfd(pcmOut); }

uint32_t px(const nv::VideoFrame& f, uint32_t x, uint32_t y) { return f.argb[size_t(y) * f.width + x]; }
bool near(uint32_t c, int r, int g, int b, int tol = 28) {
    return std::abs(int((c >> 16) & 0xFF) - r) <= tol && std::abs(int((c >> 8) & 0xFF) - g) <= tol && std::abs(int(c & 0xFF) - b) <= tol;
}

} // namespace

BS_TEST(video_linux_sfd_identify) {
    const auto  f = makeSfd();
    nv::SfdInfo i;
    std::string err;
    BS_CHECK(nv::sfd_identify(f, i, &err));
    std::printf("  barras: %ux%u %.2f fps, %u quadros (I %u P %u B %u), %u GOPs, ADX %u Hz %u ch %u amostras\n", i.width, i.height, i.fps,
                i.pictures, i.iPictures, i.pPictures, i.bPictures, i.gops, i.adx.sampleRate, i.adx.channels, i.adx.totalSamples);
    BS_CHECK(i.sofdecHeader && i.headerBytes == 0x800 && i.endCode && i.systemHeaders == 1);
    BS_CHECK(i.width == 64 && i.height == 48 && i.frameRateCode == 5 && i.fps == 30.0 && !i.mpeg2);
    // tipos conferidos pelo ffprobe (oraculo independente): I B B P B B I B B P B I
    BS_CHECK(i.pictures == 12 && i.iPictures == 3 && i.pPictures == 2 && i.bPictures == 7 && i.gops == 3);
    BS_CHECK(i.hasAudio && i.adx.channels == 2 && i.adx.sampleRate == 22050 && i.adx.totalSamples == 8820);
    BS_CHECK(i.firstVideoPts == 90000 && i.firstAudioPts == 90000 && i.pesById[0xE0] > 1 && i.pesById[0xC0] > 1);
    // nao e SFD: mensagem clara
    BS_CHECK(!nv::sfd_identify(std::vector<uint8_t>(4096, 0x41), i, &err) && err.find("Program Stream") != std::string::npos);
    auto bad = f;
    for (size_t k = 0; k + 4 < bad.size(); ++k)  // o cabecalho de sequencia vira MPEG-2 (extensao 000001B5 no lugar do GOP)
        if (bad[k] == 0 && bad[k + 1] == 0 && bad[k + 2] == 1 && bad[k + 3] == 0xB8) {
            bad[k + 3] = 0xB5;
            break;
        }
    BS_CHECK(!nv::sfd_identify(bad, i, &err) && err.find("MPEG-2") != std::string::npos);
}

BS_TEST(video_linux_sniff_picks_decoder) {
    TempDir d("sniff");
    writeAll(d.p / "a.sfd", makeSfd());
    writeAll(d.p / "b.mpg", {0, 0, 1, 0xBA, 0x21, 0, 1});
    writeAll(d.p / "c.mp4", {0, 0, 0, 0x18, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm'});
    BS_CHECK(nv::sniff_movie(d.p / "a.sfd") == nv::MovieContainer::MpegProgramStream);
    BS_CHECK(nv::sniff_movie(d.p / "b.mpg") == nv::MovieContainer::MpegProgramStream);
    BS_CHECK(nv::sniff_movie(d.p / "c.mp4") == nv::MovieContainer::Other);
    BS_CHECK(nv::sniff_movie(d.p / "nao-existe.sfd") == nv::MovieContainer::Unreadable);
    // a extensao nao decide: um SFD com nome .mp4 toca pelo pl_mpeg
    fs::copy_file(d.p / "a.sfd", d.p / "sfd-com-nome.mp4");
    BS_CHECK(nv::can_decode(d.p / "sfd-com-nome.mp4"));
    auto        dec = nv::make_movie_decoder();
    std::string err;
    BS_CHECK(dec->open(d.p / "sfd-com-nome.mp4", &err) && dec->format().width == 64);
    PathGuard nada("/nao-existe");
    BS_CHECK(!nv::can_decode(d.p / "c.mp4") && !nv::ffmpeg_available());
    BS_CHECK(!dec->open(d.p / "c.mp4", &err) && err.find("ffmpeg") != std::string::npos);
    BS_CHECK(!dec->open(d.p / "nao-existe.sfd", &err) && err.find("nao encontrado") != std::string::npos);
}

BS_TEST(video_linux_sfd_decodes_frames_colors_audio) {
    TempDir              d("decode");
    std::vector<int16_t> ref;
    writeAll(d.p / "BARRAS.SFD", makeSfd(&ref));
    auto        dec = nv::make_movie_decoder();
    std::string err;
    BS_CHECK(dec->open(d.p / "BARRAS.SFD", &err));
    const auto& fmt = dec->format();
    BS_CHECK(fmt.width == 64 && fmt.height == 48 && fmt.fps == 30.0 && fmt.hasAudio && fmt.sampleRate == 22050 && fmt.channels == 2);
    nv::VideoFrame f;
    int            n = 0;
    bool           colors = true, times = true;
    while (dec->next_frame(f, &err)) {
        times &= f.time100ns == int64_t(n) * 10'000'000 / 30 && f.duration100ns == 10'000'000 / 30;
        // barras do alto (75 %) de 10 px (largura do smptebars do ffmpeg: (64 + 6) / 7): cinza, amarelo, ciano, verde, magenta,
        // vermelho, azul; o centro de cada uma
        static const int kBar[7][3] = {{191, 191, 191}, {191, 191, 0}, {0, 191, 191}, {0, 191, 0}, {191, 0, 191}, {191, 0, 0}, {0, 0, 191}};
        for (int b = 0; b < 7; ++b) colors &= near(px(f, uint32_t(std::min(10 * b + 5, 62)), 12), kBar[b][0], kBar[b][1], kBar[b][2]);
        colors &= (px(f, 3, 3) >> 24) == 0xFF;
        ++n;
    }
    std::printf("  %d quadros, cores %d, tempos %d\n", n, int(colors), int(times));
    BS_CHECK(n == 12 && colors && times);
    std::vector<int16_t> pcm, all;
    int64_t              t = 0, expect = 0;
    bool                 audioTimes = true;
    while (dec->next_audio(pcm, t, &err)) {
        audioTimes &= t == expect * 10'000'000 / 22050;
        expect += int64_t(pcm.size() / 2);
        all.insert(all.end(), pcm.begin(), pcm.end());
    }
    BS_CHECK(all == ref && audioTimes);  // o PCM do ADX, inteiro e na ordem
}

// o filme tocado pelo relogio logico: fim pela regra (o ultimo dos dois), interrupcao no meio, sincronia do audio com o video
BS_TEST(video_linux_playback_end_interrupt_sync) {
    TempDir d("play");
    writeAll(d.p / "BARRAS.SFD", makeSfd());
    nv::MoviePlayback p;
    std::string       err;
    BS_CHECK(p.open(d.p / "BARRAS.SFD", {}, &err));
    size_t  audio = 0;
    int64_t endAt = -1;
    bool    sync  = true;
    for (int64_t tick = 0; tick < 60 && endAt < 0; ++tick) {  // 60 Hz
        const int64_t pos = tick * 10'000'000 / 60;
        p.advance(pos, &err);
        audio += p.take_audio(pos).size() / 2;
        // o quadro mostrado e o do instante (depois do ultimo, ele fica); o audio entregue nunca fica atras dele
        sync &= p.has_frame() && p.frame().time100ns <= pos && (pos >= 4'000'000 || pos - p.frame().time100ns < 10'000'000 / 30 + 1);
        sync &= int64_t(audio) * 10'000'000 / 22050 >= std::min<int64_t>(pos, 4'000'000);
        if (p.finished(pos)) endAt = pos;
    }
    std::printf("  sync %d, audio %zu, quadros %llu + %llu, fim %lld\n", int(sync), audio, static_cast<unsigned long long>(p.frames_shown()),
                static_cast<unsigned long long>(p.frames_skipped()), static_cast<long long>(endAt));
    BS_CHECK(sync && audio == 8820 && p.frames_shown() + p.frames_skipped() == 12);
    BS_CHECK(endAt >= 4'000'000 && endAt <= 4'000'000 + 10'000'000 / 60);  // 0,4 s: video e audio acabam juntos
    // interrompido (START): fechar no meio nao quebra e o seguinte abre do zero
    nv::MoviePlayback q;
    BS_CHECK(q.open(d.p / "BARRAS.SFD", {}, &err));
    q.advance(1'000'000), q.take_audio(1'000'000);
    BS_CHECK(!q.finished(1'000'000));
    nv::MoviePlayback r;
    BS_CHECK(r.open(d.p / "BARRAS.SFD", {}, &err) && r.advance(0) && r.frame().time100ns == 0);
}

// arquivos ruins: nenhum derruba o processo. Truncado ou corrompido toca o que der ou recusa com mensagem
BS_TEST(video_linux_invalid_files_never_crash) {
    TempDir    d("ruins");
    const auto good = makeSfd();
    auto       dec  = nv::make_movie_decoder();
    std::string err;
    writeAll(d.p / "vazio.sfd", {});
    BS_CHECK(!dec->open(d.p / "vazio.sfd", &err) && !err.empty());
    writeAll(d.p / "lixo.sfd", std::vector<uint8_t>(5000, 0x5A));
    BS_CHECK(!dec->open(d.p / "lixo.sfd", &err) && !err.empty());
    writeAll(d.p / "so-cabecalho.sfd", std::vector<uint8_t>(good.begin(), good.begin() + 0x800 + 30));
    BS_CHECK(!dec->open(d.p / "so-cabecalho.sfd", &err) && !err.empty());
    std::mt19937 rng(1234);
    int          opened = 0;
    const char*  vezes  = std::getenv("BS_FUZZ_VEZES");  // mais casos sob o AddressSanitizer (desenvolvimento)
    const int    total  = vezes ? std::max(1, std::atoi(vezes)) : 60;
    for (int k = 0; k < total; ++k) {
        auto bad = good;
        if (k % 3 == 0) bad.resize(0x800 + rng() % (good.size() - 0x800));  // truncado
        else
            for (int j = 0; j < 40; ++j) bad[0x800 + rng() % (bad.size() - 0x800)] ^= uint8_t(1 + rng() % 255);  // bytes trocados
        writeAll(d.p / "ruim.sfd", bad);
        if (!dec->open(d.p / "ruim.sfd", &err)) continue;
        ++opened;
        nv::VideoFrame f;
        // um cabecalho de sequencia corrompido no meio muda as dimensoes; o que quem usa o quadro precisa e o buffer do tamanho dele
        bool consistent = true;
        for (int n = 0; n < 100 && dec->next_frame(f, &err); ++n)
            consistent &= f.width > 0 && f.height > 0 && f.argb.size() == size_t(f.width) * f.height;
        BS_CHECK(consistent);
        std::vector<int16_t> pcm;
        int64_t              t;
        while (dec->next_audio(pcm, t, &err)) {}
    }
    BS_CHECK(opened > 0);  // alguns ainda tocam (o resto recusou com mensagem)
}

// Revisao Codex 20261103-030426 (ALTA): cabecalho PES curto no fim do arquivo. Cada pacote de 0 a 9 bytes com o primeiro byte de
// PTS (0x2x), PTS+DTS (0x3x), STD (0x4x) ou stuffing termina exatamente no fim do vetor: o demux recusa com mensagem sem ler
// alem dele (o teste roda tambem com AddressSanitizer)
BS_TEST(video_linux_pes_truncated_headers) {
    int refused = 0;
    for (uint8_t first : {uint8_t(0x21), uint8_t(0x31), uint8_t(0x40), uint8_t(0xFF)})
        for (size_t len = 0; len <= 9; ++len) {
            std::vector<uint8_t> f = {0, 0, 1, 0xBA, 0x21, 0, 1, 0, 1, 0x80, 0, 1, 0, 0, 1, 0xE0, 0, uint8_t(len)};
            for (size_t k = 0; k < len; ++k) f.push_back(k == 0 ? first : uint8_t(0x01));
            const std::vector<uint8_t> exact(f.begin(), f.end());  // capacidade = tamanho: ler alem e erro de memoria
            nv::SfdStreams             s;
            std::string                err;
            if (!nv::demux_sfd(exact, s, &err)) {
                ++refused;
                BS_CHECK(!err.empty());
            }
            nv::SfdInfo i;
            nv::sfd_identify(exact, i, &err);
        }
    BS_CHECK(refused >= 30);
}

namespace {
// ffmpeg e ffprobe falsos, por script: 3 quadros 4x2 (bytes 1, 2, 3) com tempos 0; 0,1; 0,25 s (taxa variavel; o ffprobe da os
// pacotes fora de ordem, como na ordem de decodificacao, mais um pacote descartado pela edit list, marca D, que nao vira quadro;
// o conteiner comeca em 0,5 s e o ffmpeg desconta isso) e 1000 quadros de audio estereo de zeros. Linhas com CRLF, como o ffprobe
// do Windows por interop (o leitor tira o \r). Marcas na pasta: "falha" = o ffmpeg do video sai com 3 depois do 1o quadro;
// "travado" = ignora o SIGTERM e nao termina; "parcial" = escreve meio quadro e para sem fechar; "semcodec" = o ffmpeg nao
// decodifica o video (a sonda falha)
void fakeFfmpeg(const fs::path& bin) {
    writeText(bin / "ffprobe", "#!/bin/sh\ncase \"$*\" in\n"
                               "*packet=pts_time*) printf '0.400000,K_D\\r\\n0.500000,K__\\r\\n0.750000,___\\r\\n0.600000,___\\r\\n';;\n"
                               "*) printf '[STREAM]\\r\\ncodec_type=video\\r\\nwidth=4\\r\\nheight=2\\r\\navg_frame_rate=10/1\\r\\nr_frame_rate=10/1\\r\\n"
                               "[/STREAM]\\r\\n[STREAM]\\r\\ncodec_type=audio\\r\\nsample_rate=8000\\r\\nchannels=2\\r\\n[/STREAM]\\r\\n"
                               "[FORMAT]\\r\\nstart_time=0.500000\\r\\nduration=0.300000\\r\\n[/FORMAT]\\r\\n';;\nesac\n");
    writeText(bin / "ffmpeg", "#!/bin/sh\nd=$(dirname \"$0\")\ncase \"$*\" in\n"
                              "*\"-f null\"*) [ -e \"$d/semcodec\" ] && exit 1; exit 0;;\n"
                              "*rawvideo*) if [ -e \"$d/travado\" ]; then trap '' TERM; head -c 32 /dev/zero; sleep 20; exit 0; fi\n"
                              "  if [ -e \"$d/parcial\" ]; then head -c 16 /dev/zero; sleep 20; exit 0; fi\n"
                              "  for v in 001 002 003; do head -c 32 /dev/zero | tr '\\000' \"\\\\$v\"; [ -e \"$d/falha\" ] && exit 3; done; exit 0;;\n"
                              "*s16le*) head -c 4000 /dev/zero;;\nesac\n");
    ::chmod((bin / "ffprobe").c_str(), 0755), ::chmod((bin / "ffmpeg").c_str(), 0755);
}
} // namespace

// Revisao Codex 20261103-030426: o mod pelo ffmpeg com os tempos do arquivo (taxa variavel, audio com inicio diferente de zero),
// erro do ffmpeg como erro (nao fim normal), filho travado que nao prende o fechamento, MPEG-PS que nao e Sofdec indo para o ffmpeg
BS_TEST(video_linux_ffmpeg_times_errors_and_stop) {
    TempDir        d("ffmpeg");
    const fs::path bin = d.p / "bin";
    fs::create_directories(bin);
    fakeFfmpeg(bin);
    writeAll(d.p / "mod.mp4", {0, 0, 0, 0x18, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm'});
    PathGuard   g(bin.string() + ":/usr/bin:/bin");
    std::string err;
    {
        auto dec = nv::make_movie_decoder();
        BS_CHECK(dec->open(d.p / "mod.mp4", &err));
        nv::VideoFrame f;
        int64_t        times[3] = {-1, -1, -1}, durs[3] = {};
        for (int i = 0; i < 3 && dec->next_frame(f, &err); ++i) times[i] = f.time100ns, durs[i] = f.duration100ns;
        BS_CHECK(times[0] == 0 && times[1] == 1'000'000 && times[2] == 2'500'000);
        BS_CHECK(durs[0] == 1'000'000 && durs[1] == 1'500'000 && durs[2] == 1'000'000);
        BS_CHECK(!dec->next_frame(f, &err) && err.empty());  // fim normal: o ffmpeg saiu com 0
        std::vector<int16_t> pcm;
        int64_t              t = -1;
        BS_CHECK(dec->next_audio(pcm, t, &err) && t == 0);  // o audio vem na linha do arquivo (aresample preenche o atraso)
    }
    {   // pelo MoviePlayback: em 0,2 s ainda o 2o quadro, em 0,25 s o 3o
        nv::MoviePlayback p;
        BS_CHECK(p.open(d.p / "mod.mp4", {}, &err));
        p.advance(2'000'000);
        BS_CHECK(p.has_frame() && (p.frame().argb[0] & 0xFF) == 2);
        p.advance(2'500'000);
        BS_CHECK(p.has_frame() && (p.frame().argb[0] & 0xFF) == 3);
    }
    {   // o ffmpeg falha no meio: erro com mensagem, nao fim natural
        writeText(bin / "falha", "");
        nv::MoviePlayback p;
        BS_CHECK(p.open(d.p / "mod.mp4", {}, &err));
        std::string e;
        for (int64_t pos = 0; pos <= 4'000'000 && e.empty(); pos += 500'000) p.advance(pos, &e);
        BS_CHECK(e.find("terminou com erro (3)") != std::string::npos);
        fs::remove(bin / "falha");
    }
    {   // filho que ignora o SIGTERM: o fechamento termina com SIGKILL em menos de 2 s
        writeText(bin / "travado", "");
        auto dec = nv::make_movie_decoder();
        BS_CHECK(dec->open(d.p / "mod.mp4", &err));
        nv::VideoFrame f;
        BS_CHECK(dec->next_frame(f, &err));  // o quadro sai depois do trap: o filho ja ignora o SIGTERM quando o fechamento comeca
        const auto t0 = std::chrono::steady_clock::now();
        dec->close();
        BS_CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
        fs::remove(bin / "travado");
    }
    {   // meio quadro e o filho para sem fechar: a leitura desiste no prazo, encerra o filho e devolve erro (nao prende o jogo)
        writeText(bin / "parcial", "");
        auto dec = nv::make_ffmpeg_decoder(300);
        BS_CHECK(dec->open(d.p / "mod.mp4", &err));
        nv::VideoFrame f;
        std::string    e;
        const auto     t0 = std::chrono::steady_clock::now();
        BS_CHECK(!dec->next_frame(f, &e) && e.find("parou de responder") != std::string::npos);
        BS_CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
        dec->close();
        fs::remove(bin / "parcial");
    }
    {   // o ffmpeg le o conteiner mas nao decodifica o video: a sonda falha e o mod nao e aceito
        writeText(bin / "semcodec", "");
        BS_CHECK(!nv::can_decode(d.p / "mod.mp4"));
        fs::remove(bin / "semcodec");
        BS_CHECK(nv::can_decode(d.p / "mod.mp4"));
    }
    {   // MPEG-PS com audio MPEG (nao ADX): o leitor do SFD nao serve; com o ffmpeg, toca por ele
        std::vector<uint8_t> ps = {0, 0, 1, 0xBA, 0x21, 0, 1, 0, 1, 0x80, 0, 1};
        bs::test::sfd_pes(ps, 0xC0, std::vector<uint8_t>{0xFF, 0xFB, 0x90, 0x00}, 0);
        writeAll(d.p / "comum.mpg", ps);
        BS_CHECK(!nv::sfd_reader_fits(d.p / "comum.mpg") && nv::can_decode(d.p / "comum.mpg"));
        auto dec = nv::make_movie_decoder();
        BS_CHECK(dec->open(d.p / "comum.mpg", &err) && dec->format().width == 4);
        PathGuard nada("/nao-existe");
        BS_CHECK(!nv::can_decode(d.p / "comum.mpg"));
    }
}

// Revisao Codex 20261103-030426: o catalogo guarda o caminho canonico conferido. Um link dentro da pasta do mod trocado depois
// do carregamento para apontar para fora nao muda o arquivo que o jogo abre; um link que ja aponta para fora e recusado
BS_TEST(video_linux_catalog_keeps_checked_path) {
    TempDir        d("links");
    const fs::path data = d.p / "data", mods = d.p / "mods", fora = d.p / "fora";
    fs::create_directories(data), fs::create_directories(mods), fs::create_directories(fora);
    writeText(data / "manifest.toml", "versao = 1\n[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.sfd\"\n");
    writeAll(data / "E001.sfd", makeSfd());
    writeAll(mods / "real.sfd", makeSfd());
    writeAll(fora / "outro.sfd", makeSfd());
    fs::create_symlink(mods / "real.sfd", mods / "link.sfd");
    fs::create_symlink(fora / "outro.sfd", mods / "fuga.sfd");
    writeText(mods / "manifest.toml", "[[filme]]\nid = 3\narquivo = \"link.sfd\"\n[[filme]]\nid = 4\noriginal = \"E002.SFD\"\narquivo = \"fuga.sfd\"\n");
    const auto  c = nv::MovieCatalog::load({data, mods});
    const auto* e = c.find(3u);
    BS_CHECK(e && e->mod && e->file == fs::weakly_canonical(mods / "real.sfd"));
    fs::remove(mods / "link.sfd");
    fs::create_symlink(fora / "outro.sfd", mods / "link.sfd");  // trocado depois da conferencia
    BS_CHECK(e && e->file == fs::weakly_canonical(mods / "real.sfd"));
    BS_CHECK(c.find(4u) == nullptr);  // o que ja aponta para fora nao entra
    // achados pelo nome (sem manifesto): o filme e as legendas guardam o canonico; link para fora e recusado
    TempDir        d2("links-nome");
    const fs::path data2 = d2.p / "data", mods2 = d2.p / "mods", fora2 = d2.p / "fora";
    fs::create_directories(data2), fs::create_directories(mods2), fs::create_directories(fora2);
    writeText(data2 / "manifest.toml", "versao = 1\n[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.sfd\"\n"
                                       "[[filme]]\nid = 5\noriginal = \"E005.SFD\"\narquivo = \"E005.sfd\"\n");
    writeAll(data2 / "E001.sfd", makeSfd()), writeAll(data2 / "E005.sfd", makeSfd());
    writeAll(fora2 / "outro.sfd", makeSfd());
    writeText(fora2 / "fora.srt", "1\n00:00:00,000 --> 00:00:01,000\nx\n");
    writeAll(mods2 / "real.sfd", makeSfd());
    writeText(mods2 / "E001.pt.srt", "1\n00:00:00,000 --> 00:00:01,000\nOi\n");
    fs::create_symlink(mods2 / "real.sfd", mods2 / "E001.mp4");
    fs::create_symlink(fora2 / "outro.sfd", mods2 / "E005.mp4");
    fs::create_symlink(fora2 / "fora.srt", mods2 / "E005.en.srt");
    const auto  c2 = nv::MovieCatalog::load({data2, mods2});
    const auto* a  = c2.find(3u);
    const auto* b  = c2.find(5u);
    BS_CHECK(a && a->mod && a->file == fs::weakly_canonical(mods2 / "real.sfd"));
    BS_CHECK(a && a->subtitles.count("pt") && a->subtitles.at("pt") == fs::weakly_canonical(mods2 / "E001.pt.srt"));
    BS_CHECK(b && !b->mod && !b->subtitles.count("en"));
}

// MP4 de mod: com o ffmpeg no PATH o mod toca; sem ele, o catalogo fica com o filme original e avisa
BS_TEST(video_linux_mod_mp4_ffmpeg_or_original) {
    TempDir        d("mod");
    const fs::path data = d.p / "data" / "video", disc = d.p / "disc", mods = d.p / "mods" / "video", bin = d.p / "bin";
    fs::create_directories(data), fs::create_directories(disc), fs::create_directories(mods), fs::create_directories(bin);
    writeText(data / "manifest.toml", "versao = 1\n[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.mp4\"\nquadros = 12\nfps = 30.0\n");
    writeAll(disc / "E001.SFD", makeSfd());
    writeAll(mods / "e001.Mp4", {0, 0, 0, 0x18, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm'});  // o nome em outra caixa
    fakeFfmpeg(bin);
    {
        PathGuard g(bin.string() + ":/usr/bin:/bin");
        const auto  c = nv::MovieCatalog::load({data, mods, disc});
        const auto* e = c.find("E001.SFD");
        BS_CHECK(e && e->mod && e->file.filename() == "e001.Mp4");
        nv::MoviePlayback p;
        std::string       err;
        BS_CHECK(p.open(e->file, {}, &err) && p.format().width == 4 && p.format().fps == 10.0 && p.format().channels == 2);
        BS_CHECK(p.format().duration100ns == 3'000'000);
        size_t audio = 0;
        int    seen  = 0;
        for (int64_t pos = 0; pos <= 4'000'000; pos += 250'000) {
            if (p.advance(pos, &err)) seen |= 1 << ((p.frame().argb[0] & 0xFF) - 1);
            audio += p.take_audio(pos).size() / 2;
        }
        BS_CHECK(seen == 7 && audio == 1000 && p.finished(4'000'000));
    }
    {
        PathGuard   g(d.p.string() + "/sem-ffmpeg");
        const auto  c = nv::MovieCatalog::load({data, mods, disc});
        const auto* e = c.find("E001.SFD");
        BS_CHECK(e && !e->mod && e->file == disc / "E001.SFD");
        bool warned = false;
        for (const auto& pr : c.problems()) warned |= pr.find("sem decodificador") != std::string::npos && pr.find("filme original") != std::string::npos;
        BS_CHECK(warned);
    }
}
