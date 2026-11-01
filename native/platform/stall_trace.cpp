// N8.8 (diagnostico do H9): ver stall_trace.h.
#include "platform/stall_trace.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>

namespace bs::native::diag {
namespace {

constexpr const char* kNames[] = {"jogo-trava-fila", "jogo-envio",  "jogo-cerca",   "jogo-fila-ociosa", "jogo-leitura-gpu",
                                  "apr-cerca",       "apr-aquisicao", "apr-trava-fila", "apr-envio",     "apr-present",
                                  "campo",           "audio-intervalo", "audio-fila-baixa", "audio-falta", "audio-reserva",
                                  "audio-descarte"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(Stall::Count));

struct Event {
    uint64_t t_ns = 0, dur_ns = 0, vblank = 0, render = 0;
    int64_t  extra = 0;
    uint32_t tid = 0;
    uint8_t  kind = 0;
};
// Cada entrada e escrita uma vez por quem reservou o indice e publicada pela marca `ready` (release); o relatorio so le
// as entradas com a marca (acquire), entao uma escrita em andamento quando o processo sai (std::exit numa thread com as
// outras vivas) fica de fora em vez de ser lida pela metade (revisao Codex 2026-11-01, ALTA)
struct Slot {
    Event             e;
    std::atomic<bool> ready{false};
};
constexpr size_t kRing = size_t(1) << 17;  // 131072 eventos (~7 MB): o resto conta como transbordo

struct Kind {
    std::atomic<uint64_t> count{0}, over{0}, total_ns{0}, max_ns{0};
    uint64_t              max_t = 0;  // instante do maximo: com max_ns sob `m` (o par fica consistente)
    std::mutex            m;
};

struct State {
    uint64_t              limit_ns = 8'000'000;
    std::string           path;
    std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();
    std::chrono::system_clock::time_point wall  = std::chrono::system_clock::now();  // a mesma epoca no relogio do sistema
    std::atomic<uint64_t> vblank{0}, render{0}, next{0};
    std::atomic<uint32_t> tids{0};
    std::atomic<bool>     closing{false};
    std::array<Kind, size_t(Stall::Count)> kinds;
    std::unique_ptr<Slot[]> ring{new Slot[kRing]};
    std::array<std::atomic<const char*>, 64> threadNames{};
};

// Desligado: so o getenv e a validacao, uma vez; nada e alocado e nenhum relogio e lido (revisao Codex, MEDIA)
struct Config {
    bool        on = false;
    std::string path;
    uint64_t    limit_ns = 8'000'000;
};
const Config& config() {
    static const Config c = [] {
        Config      r;
        const char* v = std::getenv("BS_TRAVAS");
        if (!v || !*v) return r;
        if (!parse_config(v, r.path, r.limit_ns)) {
            std::fprintf(stderr, "[travas] BS_TRAVAS invalida (\"%s\"): use <arquivo.tsv>[;<limite_ms>]; rastreio desligado\n", v);
            return r;
        }
        r.on = true;
        return r;
    }();
    return c;
}

void report();

State& st() {  // so chamado com o rastreio ligado
    static State* s = [] {
        auto* p     = new State;  // nunca destruido: o relatorio do atexit ainda o usa
        p->path     = config().path;
        p->limit_ns = config().limit_ns;
        std::atexit(report);
        return p;
    }();
    return *s;
}

thread_local uint32_t tTid = 0;
uint32_t tid() {
    if (!tTid) tTid = st().tids.fetch_add(1) + 1;
    return tTid;
}

void push(Stall k, uint64_t t0, uint64_t dur, int64_t extra) {
    State& s = st();
    if (s.closing.load(std::memory_order_relaxed)) return;
    const uint64_t i = s.next.fetch_add(1, std::memory_order_relaxed);
    if (i >= kRing) return;  // cheio: so as contagens continuam (o transbordo aparece no resumo)
    Slot& sl = s.ring[i];
    sl.e     = Event{t0, dur, s.vblank.load(std::memory_order_relaxed), s.render.load(std::memory_order_relaxed), extra,
                     tid(), uint8_t(k)};
    sl.ready.store(true, std::memory_order_release);
}

void account(Stall k, uint64_t t0, uint64_t dur, int64_t extra, bool always) {
    State& s = st();
    Kind&  c = s.kinds[size_t(k)];
    c.count.fetch_add(1, std::memory_order_relaxed);
    c.total_ns.fetch_add(dur, std::memory_order_relaxed);
    if (dur > c.max_ns.load(std::memory_order_relaxed)) {  // novo maximo (raro): o par duracao/instante sob a trava
        std::lock_guard<std::mutex> lock(c.m);
        if (dur > c.max_ns.load(std::memory_order_relaxed)) c.max_ns.store(dur, std::memory_order_relaxed), c.max_t = t0;
    }
    const bool over = dur >= s.limit_ns;
    if (over) c.over.fetch_add(1, std::memory_order_relaxed);
    if (over || always) push(k, t0, dur, extra);
}

void report() {
    State& s = st();
    s.closing.store(true, std::memory_order_relaxed);  // nenhum evento novo entra no anel daqui em diante
    const uint64_t reserved = s.next.load();
    const uint64_t n        = std::min<uint64_t>(reserved, kRing);
    std::printf("[travas] limite %.1f ms; eventos no anel %llu%s\n", s.limit_ns / 1e6, (unsigned long long)n,
                reserved > kRing ? " (transbordou: os seguintes so nas contagens)" : "");
    // a epoca (t=0 do arquivo) nos dois relogios, para alinhar com um trace do Windows (ETW): o steady_clock do MSVC e o
    // QueryPerformanceCounter em ns; o do sistema em ns desde 1970 (UTC)
    const long long epocaSteady = (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(s.epoch.time_since_epoch()).count();
    const long long epocaWall   = (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(s.wall.time_since_epoch()).count();
    std::printf("[travas] epoca: steady %lld ns, sistema %lld ns desde 1970 UTC\n", epocaSteady, epocaWall);
    for (size_t k = 0; k < size_t(Stall::Count); ++k) {
        Kind& c = s.kinds[k];
        if (!c.count.load()) continue;
        uint64_t mx, mt;
        {
            std::lock_guard<std::mutex> lock(c.m);
            mx = c.max_ns.load(), mt = c.max_t;
        }
        std::printf("[travas] %s: %llu, acima do limite %llu, maximo %.2f ms em t=%.3f s, media %.3f ms\n", kNames[k],
                    (unsigned long long)c.count.load(), (unsigned long long)c.over.load(), mx / 1e6, mt / 1e9,
                    c.total_ns.load() / 1e6 / double(c.count.load()));
    }
    uint64_t incompletos = 0;
    if (FILE* f = std::fopen(s.path.c_str(), "w")) {
        std::fprintf(f, "# epoca: steady %lld ns, sistema %lld ns desde 1970 UTC\n", epocaSteady, epocaWall);
        std::fprintf(f, "t_s\tdur_ms\ttipo\tthread\tvblank\trender\textra\n");
        for (uint64_t i = 0; i < n; ++i) {
            const Slot& sl = s.ring[i];
            if (!sl.ready.load(std::memory_order_acquire)) {  // reservado e ainda sendo escrito na saida: fica de fora
                ++incompletos;
                continue;
            }
            const Event& e  = sl.e;
            const char*  nm = e.tid < s.threadNames.size() ? s.threadNames[e.tid].load() : nullptr;
            std::fprintf(f, "%.6f\t%.3f\t%s\t%u%s%s\t%llu\t%llu\t%lld\n", e.t_ns / 1e9, e.dur_ns / 1e6,
                         e.kind < size_t(Stall::Count) ? kNames[e.kind] : "?", e.tid, nm ? ":" : "", nm ? nm : "",
                         (unsigned long long)e.vblank, (unsigned long long)e.render, (long long)e.extra);
        }
        std::fclose(f);
    } else {
        std::printf("[travas] nao foi possivel gravar %s\n", s.path.c_str());
    }
    if (incompletos) std::printf("[travas] %llu evento(s) em escrita na saida ficaram fora do arquivo\n", (unsigned long long)incompletos);
    std::fflush(stdout);
}

} // namespace

bool parse_config(const char* v, std::string& path, uint64_t& limit_ns) {
    if (!v || !*v) return false;
    const char* sep = std::strchr(v, ';');
    path.assign(v, sep ? size_t(sep - v) : std::strlen(v));
    if (path.empty()) return false;
    if (!sep) return true;  // limite padrao
    const char* num = sep + 1;
    char*       end = nullptr;
    errno           = 0;
    const double ms = std::strtod(num, &end);
    if (end == num || *end != '\0' || errno == ERANGE || !std::isfinite(ms) || ms <= 0 ||
        ms > double(UINT64_MAX) / 1e6 / 2)
        return false;
    limit_ns = uint64_t(ms * 1e6);
    return limit_ns > 0;
}

bool enabled() {
    static const bool on = config().on;
    return on;
}

uint64_t now_ns() { return ns_of(std::chrono::steady_clock::now()); }

uint64_t ns_of(std::chrono::steady_clock::time_point t) {
    const auto d = std::chrono::duration_cast<std::chrono::nanoseconds>(t - st().epoch).count();
    return d > 0 ? uint64_t(d) : 1;  // 0 significa "desligado" para mark/between
}

void finish(Stall k, uint64_t t0, int64_t extra) {
    if (!t0) return;
    const uint64_t t = now_ns();
    account(k, t0, t > t0 ? t - t0 : 0, extra, false);
}

void between(Stall k, uint64_t a, uint64_t b, int64_t extra) {
    if (a && b) account(k, a, b > a ? b - a : 0, extra, false);
}

void span(Stall k, uint64_t t0, uint64_t dur_ns, int64_t extra) {
    if (enabled()) account(k, t0, dur_ns, extra, false);
}

void event(Stall k, int64_t extra) {
    if (enabled()) account(k, now_ns(), 0, extra, true);
}

void set_vblank(uint64_t n) {
    if (enabled()) st().vblank.store(n, std::memory_order_relaxed);
}

void set_render(uint64_t n) {
    if (enabled()) st().render.store(n, std::memory_order_relaxed);
}

void name_thread(const char* name) {
    if (!enabled()) return;
    const uint32_t t = tid();
    if (t < st().threadNames.size()) st().threadNames[t].store(name);
}

} // namespace bs::native::diag
