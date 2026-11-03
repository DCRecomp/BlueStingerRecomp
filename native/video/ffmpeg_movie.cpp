// N9-L (L4): filme de mod (MP4 ou outro formato) no Linux pelo ffmpeg do sistema, como processo separado. Decisao do
// mantenedor (2026-11-03), no lugar da libav por dlopen da D-L3: o jogo nao liga a biblioteca, nao le estruturas dela e nao
// depende da versao. O `ffprobe` informa o formato e o tempo de apresentacao de cada quadro; um `ffmpeg` entrega o video em
// BGRA (quadros na ordem de apresentacao, sem duplicar nem descartar) e outro o audio em PCM 16 bits, cada um pelo seu pipe.
// Como no Media Foundation do Windows, cada quadro leva o seu tempo do arquivo (taxa variavel e inicio diferente de zero
// incluidos) e o audio comeca no tempo inicial do seu stream (revisao Codex 20261103-030426).
#include "video/movie_backends_posix.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

extern char** environ;

namespace bs::native::video {

namespace {

std::optional<std::string> findInPath(const char* name) {
    const char* path = std::getenv("PATH");
    if (!path) return std::nullopt;
    std::stringstream ss(path);
    std::string       dir;
    while (std::getline(ss, dir, ':')) {
        if (dir.empty()) dir = ".";
        const std::string full = dir + "/" + name;
        if (::access(full.c_str(), X_OK) == 0) return full;
    }
    return std::nullopt;
}

// prazo sem nenhum byte do filho antes de da-lo por travado (um quadro leva milissegundos; 10 s cobrem maquina lenta e disco frio)
constexpr int kDefaultTimeoutMs = 10'000;

// Um processo filho com a saida padrao num pipe (entrada e erro em /dev/null). Sem shell: os argumentos vao como estao.
class Child {
public:
    ~Child() { stop(); }
    bool start(const std::string& exe, const std::vector<std::string>& args) {
        int fds[2];
        if (::pipe2(fds, O_CLOEXEC) != 0) return false;
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
        posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(exe.c_str()));
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        const int rc = posix_spawn(&pid_, exe.c_str(), &fa, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&fa);
        ::close(fds[1]);
        if (rc != 0) {
            ::close(fds[0]);
            pid_ = -1;
            return false;
        }
        fd_ = fds[0];
        return true;
    }
    void set_timeout(int ms) { timeoutMs_ = ms; }
    bool timed_out() const { return timedOut_; }
    // le exatamente n bytes; menos so no fim do stream, ou se o filho ficar sem mandar nada pelo prazo: entao ele e encerrado
    // (um ffmpeg que escreveu meio quadro e parou nao prende a thread do jogo; revisao Codex 20261103-034502)
    size_t read(void* dst, size_t n) {
        size_t got = 0;
        while (fd_ >= 0 && got < n) {
            pollfd    p{fd_, POLLIN, 0};
            const int pr = ::poll(&p, 1, timeoutMs_);
            if (pr == 0) {
                timedOut_ = true;
                stop();
                break;
            }
            if (pr < 0) {
                if (errno == EINTR) continue;
                break;
            }
            const ssize_t r = ::read(fd_, static_cast<char*>(dst) + got, n - got);
            if (r > 0) got += size_t(r);
            else if (r < 0 && errno == EINTR) continue;
            else break;
        }
        return got;
    }
    std::string read_all() {
        std::string out;
        char        buf[4096];
        for (size_t r; (r = read(buf, sizeof buf)) > 0;) out.append(buf, r);
        return out;
    }
    // depois do fim do pipe: o codigo de saida (-1 se o processo nao saiu normalmente)
    int wait() {
        if (fd_ >= 0) ::close(fd_), fd_ = -1;
        if (pid_ <= 0) return -1;
        int st = 0;
        while (::waitpid(pid_, &st, 0) < 0 && errno == EINTR) {}
        pid_ = -1;
        return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    }
    // so o processo que este objeto criou: SIGTERM, ate 0,5 s para sair, depois SIGKILL (um filho travado nao prende o jogo)
    void stop() {
        if (fd_ >= 0) ::close(fd_), fd_ = -1;
        if (pid_ <= 0) return;
        ::kill(pid_, SIGTERM);
        int st = 0;
        for (int i = 0; i < 50; ++i) {
            const pid_t r = ::waitpid(pid_, &st, WNOHANG);
            if (r == pid_ || (r < 0 && errno != EINTR)) {
                pid_ = -1;
                return;
            }
            const timespec ts{0, 10'000'000};
            ::nanosleep(&ts, nullptr);
        }
        ::kill(pid_, SIGKILL);
        while (::waitpid(pid_, &st, 0) < 0 && errno == EINTR) {}
        pid_ = -1;
    }

private:
    pid_t pid_       = -1;
    int   fd_        = -1;
    int   timeoutMs_ = kDefaultTimeoutMs;
    bool  timedOut_  = false;
};

double parseRate(const std::string& s) {  // "30000/1001" ou "30"
    const size_t slash = s.find('/');
    const double num   = std::strtod(s.c_str(), nullptr);
    if (slash == std::string::npos) return num;
    const double den = std::strtod(s.c_str() + slash + 1, nullptr);
    return den > 0 ? num / den : 0;
}

// a saida do ffprobe em linhas "chave=valor" (ou so valores), sem o \r de um ffprobe de outra plataforma
std::vector<std::string> runLines(const std::string& exe, const std::vector<std::string>& args, int* rc, int timeoutMs = kDefaultTimeoutMs) {
    Child c;
    c.set_timeout(timeoutMs);
    *rc = -1;
    if (!c.start(exe, args)) return {};
    std::istringstream       in(c.read_all());
    std::vector<std::string> out;
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(l);
    }
    *rc = c.timed_out() ? -1 : c.wait();
    return out;
}

struct Probe {
    MovieFormat fmt;
    double      start = 0;  // inicio do conteiner: o ffmpeg o desconta de todos os streams na saida
};

bool failMsg(std::string* err, std::string m) {
    if (err) *err = std::move(m);
    return false;
}

// "file:" impede que um nome com ':' seja lido como protocolo
std::string inputArg(const std::filesystem::path& file) { return "file:" + file.string(); }

bool probe(const std::filesystem::path& file, Probe& p, std::string* err) {
    p = {};
    const auto ffprobe = findInPath("ffprobe");
    if (!ffprobe || !findInPath("ffmpeg")) return failMsg(err, "ffmpeg ou ffprobe nao encontrados no PATH");
    int        rc    = 0;
    const auto lines = runLines(*ffprobe,
                                {"-v", "error", "-show_entries",
                                 "stream=codec_type,width,height,avg_frame_rate,r_frame_rate,sample_rate,channels:format=start_time,duration",
                                 "-of", "default", inputArg(file)},
                                &rc);
    if (rc != 0) return failMsg(err, file.filename().string() + ": o ffprobe nao leu o arquivo");
    // secoes [STREAM] ... [/STREAM] e [FORMAT] ... [/FORMAT]
    std::string type;
    bool        video = false, audio = false, inFormat = false;
    double      avg = 0, rfr = 0;
    for (const auto& line : lines) {
        if (line == "[STREAM]" || line == "[FORMAT]") inFormat = line == "[FORMAT]", type.clear();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (inFormat) {
            if (k == "duration") p.fmt.duration100ns = int64_t(std::strtod(v.c_str(), nullptr) * 1e7);
            else if (k == "start_time") p.start = std::strtod(v.c_str(), nullptr);
        } else if (k == "codec_type") {
            type = (v == "video" && !video) ? "video" : (v == "audio" && !audio) ? "audio" : "";
            video |= type == "video", audio |= type == "audio";
        } else if (type == "video" && k == "width") p.fmt.width = uint32_t(std::strtoul(v.c_str(), nullptr, 10));
        else if (type == "video" && k == "height") p.fmt.height = uint32_t(std::strtoul(v.c_str(), nullptr, 10));
        else if (type == "video" && k == "avg_frame_rate") avg = parseRate(v);
        else if (type == "video" && k == "r_frame_rate") rfr = parseRate(v);
        else if (type == "audio" && k == "sample_rate") p.fmt.sampleRate = uint32_t(std::strtoul(v.c_str(), nullptr, 10));
        else if (type == "audio" && k == "channels") p.fmt.channels = uint32_t(std::strtoul(v.c_str(), nullptr, 10));
    }
    p.fmt.fps = avg > 0 ? avg : rfr;
    if (!video || !p.fmt.width || !p.fmt.height || p.fmt.fps <= 0)
        return failMsg(err, file.filename().string() + ": sem video legivel pelo ffmpeg");
    if (p.fmt.width > 8192 || p.fmt.height > 8192) return failMsg(err, file.filename().string() + ": video maior que 8192 pixels");
    p.fmt.hasAudio = audio && p.fmt.sampleRate > 0 && p.fmt.channels >= 1;
    if (p.fmt.hasAudio) p.fmt.channels = std::min<uint32_t>(p.fmt.channels, 2);  // a saida do filme e mono ou estereo
    return true;
}

constexpr size_t kAudioBlockFrames = 4096;

class FfmpegMovieReader final : public IMovieDecoder {
public:
    explicit FfmpegMovieReader(int timeoutMs) : timeoutMs_(timeoutMs) {}
    ~FfmpegMovieReader() override { close(); }

    bool open(const std::filesystem::path& file, std::string* err) override {
        close();
        Probe p;
        if (!probe(file, p, err)) return false;
        fmt_ = p.fmt;
        const std::string ffmpeg = *findInPath("ffmpeg"), ffprobe = *findInPath("ffprobe"), input = inputArg(file);
        // O tempo de apresentacao de cada quadro, em ordem (os pacotes vem na ordem de decodificacao), contado do inicio do
        // conteiner como a saida do ffmpeg. Pacotes que o decodificador descarta (marca D, da edit list) nao viram quadro.
        int rc = 0;
        for (const auto& l : runLines(ffprobe, {"-v", "error", "-select_streams", "v:0", "-show_entries", "packet=pts_time,flags", "-of",
                                                "csv=p=0", input},
                                      &rc, timeoutMs_)) {
            const size_t comma = l.find(',');
            const std::string t = l.substr(0, comma), flags = comma == std::string::npos ? "" : l.substr(comma + 1);
            if (!t.empty() && t != "N/A" && flags.find('D') == std::string::npos) pts_.push_back(std::strtod(t.c_str(), nullptr) - p.start);
        }
        std::sort(pts_.begin(), pts_.end());
        if (rc != 0) pts_.clear();  // sem os tempos: pela taxa media, a partir de zero
        video_.set_timeout(timeoutMs_), audio_.set_timeout(timeoutMs_);
        if (!video_.start(ffmpeg, {"-nostdin", "-v", "error", "-i", input, "-map", "0:v:0", "-fps_mode", "passthrough", "-f", "rawvideo",
                                   "-pix_fmt", "bgra", "-"}))
            return fail(err, "nao foi possivel executar o ffmpeg (video)");
        // o audio na linha de tempo do arquivo: silencio no lugar do atraso inicial e das lacunas (como os tempos das amostras no
        // Media Foundation do Windows), entao o tempo de cada bloco e o numero de amostras ate ele
        if (fmt_.hasAudio &&
            !audio_.start(ffmpeg, {"-nostdin", "-v", "error", "-i", input, "-map", "0:a:0", "-af", "aresample=async=1:first_pts=0", "-f",
                                   "s16le", "-acodec", "pcm_s16le", "-ac", std::to_string(fmt_.channels), "-ar",
                                   std::to_string(fmt_.sampleRate), "-"}))
            return fail(err, "nao foi possivel executar o ffmpeg (audio)");
        return true;
    }

    const MovieFormat& format() const override { return fmt_; }

    bool next_frame(VideoFrame& f, std::string* err) override {
        if (!fmt_.width || videoDone_) return false;
        f.width = fmt_.width, f.height = fmt_.height;
        f.argb.resize(size_t(f.width) * f.height);
        if (video_.read(f.argb.data(), f.argb.size() * 4) != f.argb.size() * 4) return finish(video_, "video", err);
        for (auto& p : f.argb) p |= 0xFF000000u;
        const double t  = timeOf(frame_);
        f.time100ns     = int64_t(t * 1e7 + 0.5);
        f.duration100ns = std::max<int64_t>(1, int64_t((timeOf(frame_ + 1) - t) * 1e7 + 0.5));
        ++frame_;
        return true;
    }

    bool next_audio(std::vector<int16_t>& pcm, int64_t& time100ns, std::string* err) override {
        if (!fmt_.hasAudio || audioDone_) return false;
        pcm.resize(kAudioBlockFrames * fmt_.channels);
        const size_t frames = audio_.read(pcm.data(), pcm.size() * 2) / (2 * fmt_.channels);
        if (!frames) return finish(audio_, "audio", err);
        pcm.resize(frames * fmt_.channels);
        time100ns = int64_t(audioFrames_) * 10'000'000 / fmt_.sampleRate;
        audioFrames_ += frames;
        return true;
    }

    void close() override {
        video_.stop(), audio_.stop();
        fmt_ = {}, frame_ = 0, audioFrames_ = 0, pts_.clear(), videoDone_ = audioDone_ = false;
    }

private:
    // o quadro i: o tempo do arquivo; alem da lista (ou sem ela), pela taxa media depois do ultimo conhecido
    double timeOf(uint64_t i) const {
        if (i < pts_.size()) return pts_[i];
        if (pts_.empty()) return double(i) / fmt_.fps;
        return pts_.back() + double(i - (pts_.size() - 1)) / fmt_.fps;
    }

    // fim do pipe: fim normal com o ffmpeg saindo com 0; senao, erro de decodificacao com mensagem (travado pelo prazo, ou codigo)
    bool finish(Child& c, const char* what, std::string* err) {
        (&c == &video_ ? videoDone_ : audioDone_) = true;
        if (c.timed_out()) {
            if (err) *err = std::string("o ffmpeg (") + what + ") parou de responder e foi encerrado";
            return false;
        }
        const int rc = c.wait();
        if (rc != 0 && err) *err = std::string("o ffmpeg (") + what + ") terminou com erro (" + std::to_string(rc) + ")";
        return false;
    }

    bool fail(std::string* err, const std::string& m) {
        close();
        if (err) *err = m;
        return false;
    }

    int                 timeoutMs_;
    MovieFormat         fmt_;
    Child               video_, audio_;
    std::vector<double> pts_;
    uint64_t            frame_ = 0, audioFrames_ = 0;
    bool                videoDone_ = false, audioDone_ = false;
};

} // namespace

std::unique_ptr<IMovieDecoder> make_ffmpeg_decoder(int timeoutMs) { return std::make_unique<FfmpegMovieReader>(timeoutMs > 0 ? timeoutMs : kDefaultTimeoutMs); }
bool                           ffmpeg_available() { return findInPath("ffmpeg") && findInPath("ffprobe"); }
// a sonda real: o ffprobe le o formato e o ffmpeg decodifica o primeiro quadro (um ffmpeg sem o decodificador do video falha
// aqui, e o catalogo fica com o filme original; revisao Codex 20261103-034502)
bool ffmpeg_can_read(const std::filesystem::path& file, std::string* err) {
    Probe p;
    if (!probe(file, p, err)) return false;
    int rc = 0;
    runLines(*findInPath("ffmpeg"), {"-nostdin", "-v", "error", "-i", inputArg(file), "-map", "0:v:0", "-frames:v", "1", "-f", "null", "-"}, &rc);
    return rc == 0 || failMsg(err, file.filename().string() + ": o ffmpeg nao decodifica o video deste arquivo");
}

} // namespace bs::native::video
