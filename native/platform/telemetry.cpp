#include "platform/telemetry.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <psapi.h>
#else
#include <ctime>
#include <fcntl.h>
#include <sys/vfs.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace bs::native::telemetry {
namespace {

using Clock = std::chrono::steady_clock;
constexpr size_t kMem = size_t(Mem::Count);

// o tempo de CPU da thread que chama (ns); 0 = indisponivel
uint64_t thread_cpu_ns() {
#ifdef _WIN32
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0;
    auto v = [](const FILETIME& f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return (v(k) + v(u)) * 100;  // unidades de 100 ns
#else
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0;
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
#endif
}

// a memoria privada do processo (bytes; -1 = indisponivel): a verificacao de vazamento ao longo de uma rota
int64_t process_bytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pm{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof pm)) return -1;
    return int64_t(pm.PrivateUsage);
#else
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return -1;
    long size = 0, resident = 0;
    const int n = std::fscanf(f, "%ld %ld", &size, &resident);
    std::fclose(f);
    return n == 2 ? int64_t(resident) * sysconf(_SC_PAGESIZE) : -1;
#endif
}

#ifdef _WIN32
bool unc_text(const std::wstring& x) { return x.size() >= 2 && (x[0] == L'\\' || x[0] == L'/') && (x[1] == L'\\' || x[1] == L'/'); }

// um caminho ja resolvido (\\?\X:\..., \\?\UNC\..., X:\...): "" = volume local; senao, o motivo
std::string resolved_volume_problem(std::wstring r) {
    if (r.rfind(L"\\\\?\\UNC\\", 0) == 0 || (unc_text(r) && r.rfind(L"\\\\?\\", 0) != 0)) return "caminho de rede";
    if (r.rfind(L"\\\\?\\", 0) == 0) r = r.substr(4);
    if (r.size() < 2 || r[1] != L':') return "volume desconhecido";
    const std::wstring root = r.substr(0, 2) + L"\\";
    const UINT         kind = GetDriveTypeW(root.c_str());
    if (kind == DRIVE_REMOTE) return "caminho de rede";
    if (kind == DRIVE_UNKNOWN || kind == DRIVE_NO_ROOT_DIR) return "volume desconhecido";
    return {};
}
#else
// os sistemas de arquivos de rede conhecidos (NFS, SMB, CIFS, SMB2, AFS, 9P, Ceph)
bool remote_fs(unsigned long type) {
    for (const unsigned long m : {0x6969ul, 0x517Bul, 0xFF534D42ul, 0xFE534D42ul, 0x5346414Ful, 0x01021997ul, 0x00C36400ul})
        if (type == m) return true;
    return false;
}
#endif

double now_ms() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

double pct(std::vector<double> v, double p) {  // percentil por posicao (o valor medido, sem interpolar)
    if (v.empty()) return NAN;
    std::sort(v.begin(), v.end());
    const size_t i = std::min(v.size() - 1, size_t(std::ceil(p / 100.0 * double(v.size()))) - (p > 0 ? 1 : 0));
    return v[i];
}

struct State {
    std::mutex  mu;
    std::FILE*  out = nullptr;
    bool        header = false, finished = false;
    Clock::time_point start{}, lastLine{};
    FrameClock  clock;  // as apresentacoes vistas seguidas (frame_presented / frame_gap)
    bool        windowSeen = false;
    // o segundo em curso
    std::vector<double> ft;     // frametimes (ms): os intervalos entre apresentacoes vistas seguidas
    uint64_t    presented = 0;  // quadros apresentados e vistos (revisao Codex 20261110-041638: nao os intervalos)
    uint64_t    gpuN = 0;
    double      gpu[4] = {0, 0, 0, 0}, gpuTotal = 0;
    bool        gpuSeen[4] = {false, false, false, false};
    uint64_t    vblanks = 0;
    uint64_t    gameCpu0 = 0, winCpu0 = 0;
    // a execucao inteira (o resumo)
    std::vector<double> allFt, allGpu;
    uint64_t    allFrames = 0, allVblanks = 0, lines = 0;
    double      sumGameCpu = 0, sumWinCpu = 0;
    uint64_t    cpuLines = 0;
    std::array<int64_t, kMem> peak{};
    int64_t     peakLocal = 0, peakUsage = -1, peakProcess = -1, firstProcess = -1, lastProcess = -1;
    // contexto
    std::string device;
    bool        timestamps = false;
    uint32_t    height = 480;
    bool        wide = false;
};

// revisao Codex 20261110-014042: o estado nunca e destruido (uma thread que sobreviva ao fim do processo nao encontra um
// objeto morto) e o fechamento e atomico (os produtores param de escrever depois dele)
State& st() {
    static State* s = new State;
    return *s;
}

std::atomic<int64_t>  g_mem[kMem][2];  // [categoria][0 = local da GPU, 1 = da CPU]
std::atomic<uint64_t> g_gameCpu{0}, g_winCpu{0}, g_vblanks{0};
std::atomic<int64_t>  g_usage{-1}, g_budget{-1};
std::atomic<bool>     g_closed{false};
uint64_t              g_from = 0;  // BS_TELEMETRIA_DE: o VBlank a partir do qual a medicao conta (0 = desde o inicio)
uint64_t              g_to   = 0;  // BS_TELEMETRIA_ATE: o VBlank em que a medicao termina (o resumo sai ali; 0 = no fim)

bool counting() { return !g_closed.load() && g_vblanks.load() >= g_from; }

void atexit_finish();

// abre o destino pedido ou recusa (com o motivo); so o nome do arquivo vai para o log
bool open_target() {
#ifdef _WIN32
    const wchar_t* w = _wgetenv(L"BS_TELEMETRIA");
    if (!w || !*w) return false;
    const fs::path p(w);
#else
    const char* v = std::getenv("BS_TELEMETRIA");
    if (!v || !*v) return false;
    const fs::path p(v);
#endif
    std::string why = target_problem(p);
    if (why.empty()) st().out = create_exclusive(p, why);
    const std::string name = p.filename().string();
    if (!st().out) {
        std::fprintf(stderr, "[telemetria] recusada (%s): desligada\n", why.c_str());
        return false;
    }
    if (const char* d = std::getenv("BS_TELEMETRIA_DE"); d && *d) g_from = std::strtoull(d, nullptr, 10);
    if (const char* d = std::getenv("BS_TELEMETRIA_ATE"); d && *d) g_to = std::strtoull(d, nullptr, 10);
    std::fprintf(stderr, "[telemetria] ligada: %s%s%s\n", name.c_str(),
                 g_from ? (" (a partir do VBlank " + std::to_string(g_from) + ")").c_str() : "",
                 g_to ? (" (ate o VBlank " + std::to_string(g_to) + ")").c_str() : "");
    st().start = st().lastLine = Clock::now();
    st().gameCpu0 = g_gameCpu.load(), st().winCpu0 = g_winCpu.load();
    std::atexit(atexit_finish);  // so o recurso: o jogo chama finish() depois de parar as threads
    return true;
}

std::string num(double v, int places = 2) {
    if (!std::isfinite(v)) return "na";
    char b[32];
    std::snprintf(b, sizeof b, "%.*f", places, v);
    return b;
}

void write_header(State& s) {
    s.header = true;
    std::fprintf(s.out,
                 "# BS_TELEMETRIA (N9.5 fase 5C): uma linha por segundo de relogio; na = indisponivel\n"
                 "# medido: quadros, fps e frametime (relogio monotonico entre duas apresentacoes; sem janela: na), CPU por "
                 "thread (tempo de CPU do sistema / tempo de relogio, em %% de um nucleo), GPU por etapa (timestamps do Vulkan, "
                 "ms medios por render), memoria por categoria (o tamanho das alocacoes do jogo pedido pelo driver, em MiB)\n"
                 "# medido pelo driver: vram_uso e vram_orcamento (VK_EXT_memory_budget, a memoria local inteira do processo), "
                 "quando a extensao existe\n"
                 "# estimado: nada neste arquivo\n");
    std::fprintf(s.out, "t_s,quadros,fps,ft_p50_ms,ft_p95_ms,ft_p99_ms,ft_max_ms,acima_16_7_pct,acima_25_pct,vblanks,cpu_jogo_pct,cpu_janela_pct,"
                        "renders_gpu,gpu_envios_ms,gpu_raster_ms,gpu_resolve_ms,gpu_saida_ms,gpu_total_ms");
    for (size_t m = 0; m < kMem; ++m) std::fprintf(s.out, ",mem_%s_mib", mem_name(Mem(m)));
    std::fprintf(s.out, ",mem_cpu_mib,vram_uso_mib,vram_orcamento_mib,processo_mib,altura,proporcao\n");
}

void write_line(State& s, Clock::time_point now) {
    if (!s.header) write_header(s);
    const double secs = std::chrono::duration<double>(now - s.lastLine).count();
    const double t    = std::chrono::duration<double>(now - s.start).count();
    const bool   win  = s.windowSeen;
    const uint64_t gc = g_gameCpu.load(), wc = g_winCpu.load(), vb = g_vblanks.load();
    const double gameP = gc && s.gameCpu0 && secs > 0 ? 100.0 * double(gc - s.gameCpu0) / (secs * 1e9) : NAN;
    const double winP  = wc && s.winCpu0 && secs > 0 && win ? 100.0 * double(wc - s.winCpu0) / (secs * 1e9) : NAN;
    s.gameCpu0 = gc, s.winCpu0 = wc;
    const uint64_t vbl = vb - s.vblanks;
    s.vblanks = vb;
    size_t over = 0, lost = 0;  // acima de 16,7 ms; acima de 25 ms (um quadro perdido com V-Sync a 60 Hz)
    double mx = 0;
    for (double f : s.ft) over += f > 16.7, lost += f > 25.0, mx = std::max(mx, f);
    std::fprintf(s.out, "%s,%llu,%s,%s,%s,%s,%s,%s,%s,%llu,%s,%s,%llu", num(t, 1).c_str(),
                 static_cast<unsigned long long>(s.presented), win ? num(double(s.presented) / secs, 1).c_str() : "na", win ? num(pct(s.ft, 50)).c_str() : "na",
                 win ? num(pct(s.ft, 95)).c_str() : "na", win ? num(pct(s.ft, 99)).c_str() : "na",
                 win && !s.ft.empty() ? num(mx).c_str() : "na",
                 win && !s.ft.empty() ? num(100.0 * double(over) / double(s.ft.size()), 1).c_str() : "na",
                 win && !s.ft.empty() ? num(100.0 * double(lost) / double(s.ft.size()), 1).c_str() : "na",
                 static_cast<unsigned long long>(vbl), num(gameP, 1).c_str(), num(winP, 1).c_str(),
                 static_cast<unsigned long long>(s.gpuN));
    for (int k = 0; k < 4; ++k)
        std::fprintf(s.out, ",%s", s.gpuN && s.gpuSeen[k] ? num(s.gpu[k] / double(s.gpuN), 3).c_str() : "na");
    std::fprintf(s.out, ",%s", s.gpuN && s.gpuSeen[1] ? num(s.gpuTotal / double(s.gpuN), 3).c_str() : "na");
    int64_t local = 0;
    for (size_t m = 0; m < kMem; ++m) {
        const int64_t b = g_mem[m][0].load();
        local += b, s.peak[m] = std::max(s.peak[m], b);
        std::fprintf(s.out, ",%s", num(double(b) / 1048576.0, 1).c_str());
    }
    s.peakLocal = std::max(s.peakLocal, local);
    int64_t host = 0;
    for (size_t m = 0; m < kMem; ++m) host += g_mem[m][1].load();
    const int64_t u = g_usage.load(), b = g_budget.load();
    s.peakUsage = std::max(s.peakUsage, u);
    const int64_t pb = process_bytes();
    s.peakProcess = std::max(s.peakProcess, pb);
    if (s.firstProcess < 0) s.firstProcess = pb;
    s.lastProcess = pb;
    std::fprintf(s.out, ",%s,%s,%s,%s,%u,%s\n", num(double(host) / 1048576.0, 1).c_str(), u >= 0 ? num(double(u) / 1048576.0, 1).c_str() : "na",
                 b >= 0 ? num(double(b) / 1048576.0, 1).c_str() : "na", pb >= 0 ? num(double(pb) / 1048576.0, 1).c_str() : "na",
                 s.height, s.wide ? "16:9" : "4:3");
    std::fflush(s.out);
    s.allFt.insert(s.allFt.end(), s.ft.begin(), s.ft.end());
    s.allFrames += s.presented, s.allVblanks += vbl, ++s.lines;
    if (std::isfinite(gameP)) s.sumGameCpu += gameP, ++s.cpuLines;
    if (std::isfinite(winP)) s.sumWinCpu += winP;
    s.ft.clear(), s.presented = 0, s.gpuN = 0, s.gpuTotal = 0;
    for (int k = 0; k < 4; ++k) s.gpu[k] = 0;
    s.lastLine = now;
}

} // namespace

std::string target_problem(const fs::path& p) {
    const std::wstring raw = p.wstring();
    std::error_code    ec;
    if (raw.empty()) return "caminho vazio";
    if (raw.size() >= 2 && (raw[0] == L'\\' || raw[0] == L'/') && (raw[1] == L'\\' || raw[1] == L'/')) return "caminho de rede";
    const bool there = fs::exists(p, ec);
    if (ec) return "o destino nao pode ser inspecionado";
    if (there) return "o arquivo ja existe (a telemetria nunca sobrescreve)";
    const fs::path dir = p.parent_path().empty() ? fs::current_path(ec) : p.parent_path();
    if (ec || !fs::is_directory(dir, ec) || ec) return "a pasta nao existe";
    // revisao Codex 20261110-014042: o volume da pasta resolvida (junções, links e unidades mapeadas seguidos), nao o texto
    const fs::path real = fs::canonical(dir, ec);
    if (ec) return "a pasta nao pode ser resolvida";
#ifdef _WIN32
    return resolved_volume_problem(real.wstring());
#else
    struct statfs fsinfo{};
    if (statfs(real.c_str(), &fsinfo) != 0) return "a pasta nao pode ser inspecionada";
    return remote_fs((unsigned long)fsinfo.f_type) ? "caminho de rede" : std::string{};
#endif
}

std::FILE* create_exclusive(const fs::path& p, std::string& why) {
    why = "o arquivo nao pode ser criado (ja existe ou a pasta recusa)";
    std::FILE* f = nullptr;
#ifdef _WIN32
    // sem o acesso DELETE no handle que grava (com ele, quem le o arquivo durante a execucao teria de compartilhar a
    // exclusao); FILE_SHARE_DELETE so para a reabertura que apaga, na recusa
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    // o volume do arquivo aberto (o caminho final do handle), nao o do texto conferido antes
    std::wstring      fin(1024, L'\0');
    const DWORD       n       = GetFinalPathNameByHandleW(h, fin.data(), DWORD(fin.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    const std::string problem = n == 0 || n >= fin.size() ? "o destino nao pode ser resolvido" : resolved_volume_problem(fin.substr(0, n));
    if (!problem.empty()) {
        // apaga o arquivo que acabou de criar, pelo handle (nunca por caminho): a reabertura do mesmo arquivo com DELETE
        if (HANDLE hd = ReOpenFile(h, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0); hd != INVALID_HANDLE_VALUE) {
            FILE_DISPOSITION_INFO del{TRUE};
            SetFileInformationByHandle(hd, FileDispositionInfo, &del, sizeof del);
            CloseHandle(hd);
        }
        CloseHandle(h);
        why = problem;
        return nullptr;
    }
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(h), _O_WRONLY | _O_BINARY);
    if (fd < 0) {
        CloseHandle(h);
        return nullptr;
    }
    f = _fdopen(fd, "wb");
    if (!f) _close(fd);
#else
    const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0) return nullptr;
    struct statfs fsinfo{};
    if (fstatfs(fd, &fsinfo) != 0 || remote_fs((unsigned long)fsinfo.f_type)) {
        ::close(fd);  // fica um arquivo vazio: nada e gravado e nada e apagado por caminho
        why = "caminho de rede";
        return nullptr;
    }
    f = fdopen(fd, "wb");
    if (!f) ::close(fd);
#endif
    if (f) why.clear();
    return f;
}

const char* mem_name(Mem m) {
    static constexpr const char* n[] = {"alvos", "slots", "texturas", "buffers_quadro", "pos", "apresentacao", "outros"};
    return n[size_t(m)];
}

bool enabled() {
    static const bool on = open_target();
    return on;
}

void mem_add(Mem m, bool device_local, int64_t bytes) {
    if (!enabled()) return;
    g_mem[size_t(m)][device_local ? 0 : 1].fetch_add(bytes);
}

void game_tick() {
    if (!enabled() || g_closed.load()) return;
    g_gameCpu.store(thread_cpu_ns()), g_vblanks.fetch_add(1);
    if (g_to && g_vblanks.load() >= g_to) {  // BS_TELEMETRIA_ATE: o trecho acabou (o resumo sai aqui, antes de encerrarem o jogo)
        finish();
        return;
    }
    bool headless;
    {
        std::lock_guard<std::mutex> l(st().mu);
        headless = !st().windowSeen;
    }
    if (g_from && g_vblanks.load() == g_from) {  // o trecho medido comeca: as bases do segundo recomecam
        std::lock_guard<std::mutex> l(st().mu);
        State& s = st();
        s.start = s.lastLine = Clock::now(), s.clock.gap(), s.ft.clear(), s.presented = 0, s.gpuN = 0, s.gpuTotal = 0;
        for (int k = 0; k < 4; ++k) s.gpu[k] = 0;
        s.gameCpu0 = g_gameCpu.load(), s.winCpu0 = g_winCpu.load(), s.vblanks = g_vblanks.load();
    }
    if (headless) tick();  // sem janela, o laco do jogo grava as linhas
}

void window_tick() {
    if (!enabled() || g_closed.load()) return;
    g_winCpu.store(thread_cpu_ns());
}

void frame_presented() {
    if (!enabled() || !counting()) return;
    const double now = now_ms();
    std::lock_guard<std::mutex> l(st().mu);
    State& s = st();
    s.windowSeen = true, ++s.presented;
    if (const double ft = s.clock.present(now); ft >= 0) s.ft.push_back(ft);
}

void frame_gap() {
    if (!enabled() || !counting()) return;
    std::lock_guard<std::mutex> l(st().mu);
    st().clock.gap();
}

void gpu_frame(const GpuFrame& g) {
    if (!enabled() || !counting()) return;
    std::lock_guard<std::mutex> l(st().mu);
    State&       s = st();
    const double v[4] = {g.uploads, g.raster, g.resolve, g.output};
    double       total = 0;
    for (int k = 0; k < 4; ++k)
        if (v[k] >= 0) s.gpu[k] += v[k], s.gpuSeen[k] = true, total += v[k];
    ++s.gpuN, s.gpuTotal += total;
    if (v[1] >= 0) s.allGpu.push_back(total);
}

void set_budget(int64_t usage, int64_t budget) {
    if (!enabled()) return;
    g_usage.store(usage), g_budget.store(budget);
}

void set_device(const std::string& name, bool timestamps) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> l(st().mu);
    st().device = name, st().timestamps = timestamps;
}

void set_image(uint32_t internal_height, bool wide) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> l(st().mu);
    st().height = internal_height, st().wide = wide;
}

void tick() {
    if (!enabled() || !counting()) return;
    std::lock_guard<std::mutex> l(st().mu);
    State& s = st();
    if (!s.out || s.finished) return;
    const auto now = Clock::now();
    if (now - s.lastLine >= std::chrono::seconds(1)) write_line(s, now);
}

namespace {
// o fim: a ultima linha e o resumo (com a trava na mao)
void finish_locked(State& s) {
    if (!s.out || s.finished) return;
    s.finished = true;
    if (g_from && g_vblanks.load() < g_from) {  // revisao Codex 20261110-014042: o trecho pedido nao chegou
        std::fprintf(s.out, "# resumo: o trecho medido nao foi alcancado (BS_TELEMETRIA_DE=%llu; ultimo VBlank %llu): sem medicao\n",
                     static_cast<unsigned long long>(g_from), static_cast<unsigned long long>(g_vblanks.load()));
        std::fclose(s.out), s.out = nullptr;
        return;
    }
    write_line(s, Clock::now());
    size_t over = 0, lost = 0;
    double mx = 0;
    for (double f : s.allFt) over += f > 16.7, lost += f > 25.0, mx = std::max(mx, f);
    const double total = std::chrono::duration<double>(Clock::now() - s.start).count();
    std::fprintf(s.out, "# resumo: dispositivo %s; timestamps %s; %.1f s; %llu VBlanks\n", s.device.empty() ? "na" : s.device.c_str(),
                 s.timestamps ? "sim" : "nao", total, static_cast<unsigned long long>(s.allVblanks));
    if (s.windowSeen && !s.allFt.empty())
        std::fprintf(s.out, "# resumo: %llu quadros apresentados; fps %s; frametime p50 %s, p95 %s, p99 %s, max %s ms; acima de 16,7 ms %s %%, acima de 25 ms %s %%\n",
                     static_cast<unsigned long long>(s.allFrames), num(double(s.allFrames) / total, 1).c_str(), num(pct(s.allFt, 50)).c_str(),
                     num(pct(s.allFt, 95)).c_str(), num(pct(s.allFt, 99)).c_str(), num(mx).c_str(),
                     num(100.0 * double(over) / double(s.allFt.size()), 2).c_str(),
                     num(100.0 * double(lost) / double(s.allFt.size()), 2).c_str());
    else
        std::fprintf(s.out, "# resumo: quadros apresentados na (sem janela)\n");
    std::fprintf(s.out, "# resumo: GPU por render medio %s ms, p99 %s ms (%zu renders com timestamps)\n",
                 s.allGpu.empty() ? "na" : num([&] { double a = 0; for (double g : s.allGpu) a += g; return a / double(s.allGpu.size()); }(), 3).c_str(),
                 num(pct(s.allGpu, 99), 3).c_str(), s.allGpu.size());
    std::fprintf(s.out, "# resumo: CPU media da thread do jogo %s %%, da janela %s %% de um nucleo\n",
                 s.cpuLines ? num(s.sumGameCpu / double(s.cpuLines), 1).c_str() : "na",
                 s.cpuLines && s.windowSeen ? num(s.sumWinCpu / double(s.cpuLines), 1).c_str() : "na");
    std::fprintf(s.out, "# resumo: pico da memoria local do jogo %s MiB (", num(double(s.peakLocal) / 1048576.0, 1).c_str());
    for (size_t m = 0; m < kMem; ++m) std::fprintf(s.out, "%s%s %s", m ? ", " : "", mem_name(Mem(m)), num(double(s.peak[m]) / 1048576.0, 1).c_str());
    std::fprintf(s.out, "); pico do uso do driver %s MiB\n", s.peakUsage >= 0 ? num(double(s.peakUsage) / 1048576.0, 1).c_str() : "na");
    std::fprintf(s.out, "# resumo: memoria privada do processo: primeira linha %s, ultima %s, pico %s MiB\n",
                 s.firstProcess >= 0 ? num(double(s.firstProcess) / 1048576.0, 1).c_str() : "na",
                 s.lastProcess >= 0 ? num(double(s.lastProcess) / 1048576.0, 1).c_str() : "na",
                 s.peakProcess >= 0 ? num(double(s.peakProcess) / 1048576.0, 1).c_str() : "na");
    std::fclose(s.out), s.out = nullptr;
}

// o recurso do atexit: nunca bloqueia (uma thread viva pode ter a trava); sem ela, o arquivo fica sem o resumo
void atexit_finish() {
    if (g_closed.exchange(true)) return;
    std::unique_lock<std::mutex> l(st().mu, std::try_to_lock);
    if (l.owns_lock()) finish_locked(st());
}
} // namespace

// o fim pedido pelo jogo, depois de parar as threads que produzem (ou no BS_TELEMETRIA_ATE, pela thread do jogo)
void finish() {
    if (!enabled() || g_closed.exchange(true)) return;
    std::lock_guard<std::mutex> l(st().mu);
    finish_locked(st());
}

} // namespace bs::native::telemetry
