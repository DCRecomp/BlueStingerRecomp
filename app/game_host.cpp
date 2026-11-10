#include "game_host.h"
#include "platform/stall_trace.h"
#include "platform/telemetry.h"  // N9.5 fase 5C

#include "boot/native_boot.h"
#include "disc/disc_image.h"
#include "cpu/hud_scope.h"
#include "cpu/sh4_runtime.h"
#include "cpu/sh4_threads.h"
#include "system/syscall_service.h"
#include "runtime_data.h"
#if BS_VALIDACAO
#include "gdrom/data_source.h"
#include "gdrom/gdrom.h"
#include "hle/bios.h"
#include "hle/flashrom.h"
#endif
#include "debug/call_trace.h"
#include "debug/cri_diag.h"
#include "bu_adapter.h"
#include "gdfs_adapter.h"
#include "pad_adapter.h"
#include "pad_contract.h"
#include "pad_probe.h"
#include "input/input.h"
#include "time_adapter.h"
#include "sound_adapter.h"
#include "video_mw_core.h"
#include "render_capture.h"
#include "video_adapter.h"
#include "audio/audio.h"
#include "audio/voice_player.h"
#include "localization/dialogue_bridge.h"
#include "localization/mod_packages.h"
#include "localization/subtitle_service.h"
#include "cri_idle.h"
#include "free_camera.h"
#include "mods/mod_api.h"
#include "pvr/holly_intc.h"
#include "pvr/pvr_video.h"
#include "system/maple_bus.h"
#include "system/sound_bus.h"
#if BS_VALIDACAO
#include "aica/aica_regs.h"
#include "maple/maple.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <stdexcept>

namespace bs {
namespace {

// Parada pedida (janela fechada, limite de VBlanks): nao e erro.
struct StopRequest {};

struct FatalError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void onFatal(Sh4Context*, const char* what, uint32_t pc, uint32_t extra) {
    char b[256];
    std::snprintf(b, sizeof(b), "%s em PC=%08X (valor %08X)", what, pc, extra);
    throw FatalError(b);
}

// N8.4: o soft reset (A+B+X+Y+START) pede o menu do sistema (MISC r4 = 1): no console o jogo sai para o menu do BIOS;
// aqui a sessao termina sem erro (o servico nativo registra a mensagem). Nao e recuo: o pedido tem esse significado.
[[noreturn]] void onSystemMenu(Sh4Context*) { throw StopRequest{}; }

// Diagnostico: BS_APP_TRACE=1 registra as etapas do encerramento.
// Hora do sistema no boot (RTC, segundos desde 1950). Jogo normal: hora local do host. Testes
// e roteiros (D5): BS_RELOGIO_FIXO=<segundos Unix, hora local> fixa o relogio.
uint32_t rtcAtBoot() {
    if (const char* v = std::getenv("BS_RELOGIO_FIXO")) {
        char*         end = nullptr;
        const int64_t t   = std::strtoll(v, &end, 10);
        if (end == v || *end) throw std::runtime_error(std::string("BS_RELOGIO_FIXO invalido: ") + v);
        return uint32_t(t + 7305ll * 86400);  // 1950 -> 1970
    }
    return boot_rtc_now();
}

bool traceOn() {
    static const bool on = std::getenv("BS_APP_TRACE") != nullptr;
    return on;
}

// Diagnostico BS_STATE_PASSO (ver threadMain): estado logico no inicio de cada passo de jogo.
struct StepLog {
    FILE*           f     = nullptr;
    uint64_t        every = 10;
    const uint8_t*  ram   = nullptr;
    const GameHost* host  = nullptr;
    bool            noCamera = false;  // BS_STATE_PASSO_SEM_CAMERA=1: sem o objeto camera do SDK (N7c, camera livre)
} g_stepLog;

// Diagnostico BS_PAD_TRACE (N3-0): a cada passo de jogo, no inicio do passo (antes do servidor
// pd do passo), o PDS_PERIPHERAL da porta A0 como o jogo o deixou no passo anterior (depois do
// servidor e do pos-processamento 8C064878), o ponteiro que o jogo usa, a info apontada e a
// palavra de acoes do jogo (8C24C5D0..E0, 8C03AC2E) e o pedido de soft reset (bit 2 de 8C24F878).
// Comparavel entre execucoes passo a passo (tools/dev/compara_pad.py): e a referencia do N3-2.
FILE*          g_padTrace = nullptr;
const uint8_t* g_padRam   = nullptr;

// Leituras do controle da porta A: no legado, os GET_CONDITION respondidos pelo Maple; no
// --controles nativo (N3-2), as leituras do modelo do pd nativo (mesmo indice).
uint64_t controllerReads() { return transitional::pad::installed() ? transitional::pad::reads() : periph_cond_seq(0); }

// Diagnostico BS_RAM_WATCH=<arquivo>:<end>[,<end>...] (N5, START durante os filmes): a cada VBlank, as
// palavras de 32 bits vigiadas que mudaram (vblank, passo, leituras do controle, endereco, valor). Durante
// um filme o passo de jogo nao anda (o laco do filme roda dentro de um passo), entao o BS_PAD_TRACE e o
// BS_STATE_PASSO nao veem o que acontece ali. Desligado por padrao.
struct RamWatch {
    FILE*                 f = nullptr;
    std::vector<uint32_t> addr, last;
    bool                  first = true;
} g_ramWatch;

void ramWatchInit() {
    const char* v = std::getenv("BS_RAM_WATCH");
    if (!v || !*v) return;
    const std::string s(v);
    const size_t      colon = s.rfind(':');
    if (colon == std::string::npos || colon < 2) throw std::runtime_error("BS_RAM_WATCH=<arquivo>:<end>[,<end>]");
    for (size_t i = colon + 1; i < s.size();) {
        const size_t j = s.find(',', i);
        g_ramWatch.addr.push_back(uint32_t(std::strtoul(s.substr(i, j - i).c_str(), nullptr, 16)));
        i = j == std::string::npos ? s.size() : j + 1;
    }
    g_ramWatch.f = std::fopen(s.substr(0, colon).c_str(), "w");
    if (!g_ramWatch.f) throw std::runtime_error("BS_RAM_WATCH: nao abre " + s.substr(0, colon));
    std::fprintf(g_ramWatch.f, "vblank\tpasso\tleituras\tendereco\tvalor\n");
    g_ramWatch.last.assign(g_ramWatch.addr.size(), 0);
}

// fecha o arquivo e limpa o estado global em todo retorno da sessao (inclusive excecao na configuracao)
struct RamWatchGuard {
    ~RamWatchGuard() {
        if (g_ramWatch.f) std::fclose(g_ramWatch.f);
        g_ramWatch = {};
    }
};

void ramWatchVblank(const uint8_t* ram, uint64_t vblank) {
    if (!g_ramWatch.f || !ram) return;
    for (size_t i = 0; i < g_ramWatch.addr.size(); ++i) {
        uint32_t v;
        std::memcpy(&v, ram + (g_ramWatch.addr[i] & 0x00FFFFFCu), 4);
        if (g_ramWatch.first || v != g_ramWatch.last[i])
            std::fprintf(g_ramWatch.f, "%llu\t%llu\t%llu\t%08X\t%08X\n", static_cast<unsigned long long>(vblank),
                         static_cast<unsigned long long>(transitional::time::game_steps()),
                         static_cast<unsigned long long>(controllerReads()), g_ramWatch.addr[i], v);
        g_ramWatch.last[i] = v;
    }
    g_ramWatch.first = false;
}

void padTraceWrite(uint64_t step) {
    namespace pad = transitional::pad;
    const uint8_t* ram = g_padRam;
    auto u32 = [&](uint32_t a) {
        uint32_t v;
        std::memcpy(&v, ram + (a & 0xFFFFFF), 4);
        return v;
    };
    const pad::Pds p     = pad::pds_load(ram + (pad::kTable & 0xFFFFFF));
    const uint32_t ptr   = u32(pad::kGamePdsPtr);
    const bool     infoOk = (p.info >> 24) == 0x8C;
    std::string    name;
    if ((p.name >> 24) == 0x8C)
        for (uint32_t i = 0; i < 32; ++i) {
            const char ch = char(ram[(p.name + i) & 0xFFFFFF]);
            if (ch < 0x20 || ch > 0x7E) break;
            name += ch;
        }
    std::fprintf(g_padTrace,
                 "%llu\t%llu\t%llu\t%08X\t%08X\t%08X\t%08X\t%08X\t%08X\t%08X\t%u\t%u\t%d\t%d\t%d\t%d\t%08X\t%08X\t%s\t%08X\t%08X\t"
                 "%08X\t%08X\t%08X\t%u\n",
                 static_cast<unsigned long long>(step), static_cast<unsigned long long>(g_stepLog.host->vblanks()),
                 static_cast<unsigned long long>(controllerReads()), ptr, p.id, p.support, p.on, p.off, p.press, p.release,
                 p.r, p.l, p.x1, p.y1, p.x2, p.y2, p.old, infoOk ? u32(p.info) : 0u, name.empty() ? "-" : name.c_str(),
                 u32(0x8C24C5D0), u32(0x8C24C5D4), u32(0x8C24C5D8), u32(0x8C24C5DC), u32(0x8C24C5E0),
                 (u32(0x8C24F878) >> 2) & 1u);
    if (step % 250 == 0) std::fflush(g_padTrace);
}

void stepLogWrite(uint64_t step, void*) {
    if (g_padTrace) padTraceWrite(step);
    if (!g_stepLog.f || step % g_stepLog.every) return;
    const uint8_t* ram = g_stepLog.ram;
    uint64_t       h   = 0xCBF29CE484222325ull;
    for (uint32_t a = 0x240000; a < 0x260000; ++a) {
        // N7c: o objeto camera do SDK (0x8C24CC38, 0x78 bytes: matriz, posicao, vetor de visao) e o que a camera livre
        // muda; so o SDK o le (docs/N7-PLANO.md secao 23.1). Sem ele, Original e camera livre devem dar o mesmo estado.
        if (g_stepLog.noCamera && a >= 0x24CC38 && a < 0x24CCB0) continue;
        h = (h ^ ram[a]) * 0x100000001B3ull;
    }
    int16_t life;
    float   p[3];
    std::memcpy(&life, ram + 0x24FBCC, 2);
    std::memcpy(p, ram + 0x24D668, 12);
    std::fprintf(g_stepLog.f, "%llu\t%llu\t%016llX\t%d\t%.3f\t%.3f\t%.3f\n", static_cast<unsigned long long>(step),
                 static_cast<unsigned long long>(g_stepLog.host->vblanks()), static_cast<unsigned long long>(h), life, p[0],
                 p[1], p[2]);
    // a cada ~2 s de jogo no disco: uma queda perde no maximo a cauda (as gravacoes de sessao conferem a continuidade)
    if (step % 64 == 0) std::fflush(g_stepLog.f);
}

// Periodo de um campo NTSC: 1001 / 60000 s (59,94 Hz).
constexpr auto kField = std::chrono::nanoseconds(16683333);

#if BS_VALIDACAO
// N9.5 fase 0 (so investigacao, BS_TELA_LOGICA43): roda a rotina do jogo com a projecao do 4:3 (o cx da copia do desenho
// 8C2D9C7C e a largura e o cx da estrutura 8C24CCB4 voltam a 320/640/320 so durante a chamada)
uint8_t* g_ramTela = nullptr;
void     tela43(uint32_t addr, Sh4Context* cc) {
    // os tres campos voltam ao valor de antes mesmo se a rotina sair por excecao (revisao Codex 2026-11-05)
    struct Restaura {
        uint8_t salvo[12];
        Restaura() {
            std::memcpy(salvo, g_ramTela + 0x2D9C7C, 4), std::memcpy(salvo + 4, g_ramTela + 0x24CCB8, 4);
            std::memcpy(salvo + 8, g_ramTela + 0x24CCC0, 4);
        }
        ~Restaura() {
            std::memcpy(g_ramTela + 0x2D9C7C, salvo, 4), std::memcpy(g_ramTela + 0x24CCB8, salvo + 4, 4);
            std::memcpy(g_ramTela + 0x24CCC0, salvo + 8, 4);
        }
    } restaura;
    const float w = 640.f, cx = 320.f;
    std::memcpy(g_ramTela + 0x2D9C7C, &cx, 4), std::memcpy(g_ramTela + 0x24CCB8, &w, 4);
    std::memcpy(g_ramTela + 0x24CCC0, &cx, 4);
    bs::mods::call_original(addr, cc);
}
template <uint32_t A> void tela43(Sh4Context* cc) { tela43(A, cc); }
#endif

} // namespace

// N6 fase E: as falas nativas avisam inicio, fim, interrupcao e pausa (native/audio/voice_player.h); a ponte as
// leva ao SubtitleService no tempo logico do mixer. N7c: o texto ativo vai com o quadro do mesmo VBlank
// (GameFrame::subtitle) e quem apresenta o desenha por cima do gameplay; o log ("[legenda] ...") continua.
struct GameHost::Dialogue {
    native::localization::ModPackages     mods;
    native::localization::SubtitleService subs;
    native::localization::DialogueBridge  bridge;
    std::string                           last;
    std::wstring                          text;  // as falas ativas, uma por linha (o que aparece na tela)
    // 2026-10-23: cada legenda (ocorrencia + linha/parte) entra e sai sozinha; o diagnostico BS_LEGENDA_LOG=<arquivo>
    // registra, por ocorrencia, o pedido, o inicio do audio, a entrada e a saida de cada legenda e o fim ou a interrupcao,
    // com o VBlank e o atraso da saida de audio (o que foi misturado e ainda esta na fila do dispositivo)
    std::vector<std::pair<uint64_t, size_t>> shown;
    FILE*                                    log = legendLog();  // um arquivo por processo (a troca de idioma recria isto)
    uint64_t occurrences = 0, simultaneous = 0, latencyFrames = 0, latencyMax = 0, latencySamples = 0;
    Dialogue(native::localization::ModPackages m, const std::string& lang)
        : mods(std::move(m)), subs(mods, lang), bridge(subs, 44100) {
        if (log) std::fprintf(log, "-\tidioma\t%s\t-\t-\t-\t\n", lang.empty() ? "desligadas" : lang.c_str()), std::fflush(log);
    }
    static FILE* legendLog() {
        static FILE* f = [] {
            const char* v = std::getenv("BS_LEGENDA_LOG");
            FILE*       o = v && *v ? std::fopen(v, "w") : nullptr;
            if (o) std::fprintf(o, "vblank\tevento\tid\tocorrencia\tlinha\tatraso_saida_ms\ttexto\n");
            return o;
        }();
        return f;
    }
};

void GameHost::DialogueDelete::operator()(Dialogue* d) const { delete d; }

// Os eventos das falas (VoicePlayer) para as legendas: contagem, diagnostico e a fala que esta soando. `replay`: o inicio da
// fala em curso reapresentado ao servico novo depois de uma troca de idioma (mesma ocorrencia, nao conta de novo).
void GameHost::dialogueEvent(const native::audio::DialogueEvent& e, bool replay) {
    using K     = native::audio::DialogueKind;
    Dialogue& d = *dialogue_;
    d.occurrences += e.kind == K::Started && !replay;
    if (d.log) {
        static const char* const kNomes[] = {"inicio", "fim-logico", "fim-fisico", "interrompida", "ausente", "pausa", "retomada", "pedido"};
        std::fprintf(d.log, "%llu\tfala-%s\t%s\t-\t-\t-\tquadro do mixer %llu\n", static_cast<unsigned long long>(vblanks_.load()),
                     replay ? "reapresentada" : kNomes[size_t(e.kind)], e.id.c_str(), static_cast<unsigned long long>(e.frame));
    }
    // a fala que esta soando (a troca de idioma no meio dela a reapresenta ao servico novo, no tempo original)
    if (e.kind == K::Started && !replay) soundingId_ = e.id, soundingName_ = e.name, soundingFrame_ = e.frame;
    else if ((e.kind == K::PhysicalEnd || e.kind == K::Interrupted) && e.id == soundingId_) soundingId_.clear();
    d.bridge.on_event(e);
}

void GameHost::dialogueVblank(uint64_t n) {
    Dialogue&      d      = *dialogue_;
    const uint64_t mixed  = n * 44100ull * 1001 / 60000;  // o mesmo relogio do mixer (sound_adapter.cpp)
    // a legenda segue o que se OUVE: o quadro do mixer que esta saindo do dispositivo agora (sem deslocamento fixo; headless:
    // o proprio tempo misturado)
    const uint64_t audible = transitional::sound::audible_frame(mixed), queued = mixed > audible ? mixed - audible : 0;
    d.bridge.logical_frame(audible);
    d.latencyFrames += queued, d.latencyMax = std::max(d.latencyMax, queued), ++d.latencySamples;
    std::string                              now;
    std::vector<std::pair<uint64_t, size_t>> shown;
    std::vector<uint64_t>                    instances;
    d.text.clear();
    const bool speaker = subtitleSpeaker_.load();
    for (const auto& a : d.subs.active())
        if (a.source == native::localization::ActiveSubtitle::Source::Dialogue) {
            now += (now.empty() ? "" : ", ") + a.id + " (" + std::to_string(a.text.size()) + " caracteres)";
            d.text += (d.text.empty() ? L"" : L"\n") + (speaker && !a.speaker.empty() ? a.speaker + L": " : L"") + a.text;
            shown.emplace_back(a.instance, a.line);
            if (std::find(instances.begin(), instances.end(), a.instance) == instances.end()) instances.push_back(a.instance);
            if (d.log && std::find(d.shown.begin(), d.shown.end(), shown.back()) == d.shown.end()) {
                std::string utf8;  // so para o log (ASCII basta para conferir; acentos viram '?')
                for (wchar_t ch : a.text) utf8 += ch == L'\n' ? ' ' : ch < 128 ? char(ch) : '?';
                std::fprintf(d.log, "%llu\tlegenda-entra\t%s\t%llu\t%zu\t%.1f\t%s\n", static_cast<unsigned long long>(n), a.id.c_str(),
                             static_cast<unsigned long long>(a.instance), a.line, queued * 1000.0 / 44100, utf8.c_str());
            }
        }
    if (instances.size() > 1) ++d.simultaneous;  // falas diferentes na tela ao mesmo tempo
    if (d.log)
        for (const auto& s : d.shown)
            if (std::find(shown.begin(), shown.end(), s) == shown.end())
                std::fprintf(d.log, "%llu\tlegenda-sai\t-\t%llu\t%zu\t%.1f\t\n", static_cast<unsigned long long>(n),
                             static_cast<unsigned long long>(s.first), s.second, queued * 1000.0 / 44100);
    d.shown = std::move(shown);
    if (now != d.last) {
        std::fprintf(stderr, "[legenda] VBlank %llu: %s\n", static_cast<unsigned long long>(n), now.empty() ? "nenhuma" : now.c_str());
        d.last = std::move(now);
    }
}
GameHost::~GameHost() {
    requestStop();
    join();
}

void GameHost::start(GameOptions opt) {
    if (running_.load() || thread_.joinable()) throw std::logic_error("GameHost ja iniciado");
    opt_ = std::move(opt);
    stop_.store(false);
    running_.store(true);
    thread_ = std::thread([this] { threadMain(); });
}

void GameHost::requestStop() {
    {
        std::lock_guard<std::mutex> lock(pauseMutex_);
        stop_.store(true);
    }
    pauseCv_.notify_all();  // um jogo pausado pelo menu tambem para
}

void GameHost::join() {
    if (thread_.joinable()) thread_.join();
}

void GameHost::restart() {
    requestStop();
    join();
    paused_.store(false);
    vblanks_.store(0);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_.clear();
    }
    GameOptions again = opt_;
    start(std::move(again));
}

std::string GameHost::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

bool GameHost::latestFrame(GameFrame& out, uint64_t& seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (frameSeq_ == seq) return false;
    out = frame_;
    seq = frameSeq_;
    return true;
}

void GameHost::onVblank(void* self) { static_cast<GameHost*>(self)->vblank(); }

void GameHost::setPaused(bool on) {
    {
        std::lock_guard<std::mutex> lock(pauseMutex_);
        paused_.store(on);
    }
    pauseCv_.notify_all();
}

void GameHost::notifyRequest() {  // com o jogo pausado, a espera acorda e aplica na thread do jogo (revisao Codex)
    {
        std::lock_guard<std::mutex> lock(pauseMutex_);
        requestsPending_.store(true);
    }
    pauseCv_.notify_all();
}

void GameHost::setVolumes(float music, float voices, float effects) {
    {
        std::lock_guard<std::mutex> lock(reqMutex_);
        volumes_[0] = music, volumes_[1] = voices, volumes_[2] = effects, volumesPending_ = true;
    }
    notifyRequest();
}

void GameHost::setSubtitleLanguage(const std::string& language) {
    {
        std::lock_guard<std::mutex> lock(reqMutex_);
        lang_ = language, langPending_ = true;
    }
    notifyRequest();
}

void GameHost::setRenderScale(float scale) {
    scalePending_.store(scale > 0 ? scale : 1.f);
    notifyRequest();
}

void GameHost::setWideWidth(uint32_t width) {
    widePending_.store(int64_t(width));
    notifyRequest();
}

void GameHost::setAmbientOcclusion(bool on, uint32_t quality) {
    aoPending_.store((on ? 1 : 0) | int(std::min<uint32_t>(quality, 2) << 1));  // bit 0 ligado; bits 1-2 a qualidade
    notifyRequest();
}

void GameHost::setCamera(const native::settings::CameraSettings& s) {
    {
        std::lock_guard<std::mutex> lock(reqMutex_);
        camera_ = s, cameraPending_ = true;
    }
    notifyRequest();
}

void GameHost::setRenderer(RenderProvider r) {
    rendererPending_.store(int(r));
    notifyRequest();
}

void GameHost::applyRequests() {
    requestsPending_.store(false);
    float       v[3];
    bool        vol = false, lang = false, cam = false;
    std::string language;
    native::settings::CameraSettings camera;
    {
        std::lock_guard<std::mutex> lock(reqMutex_);
        std::swap(vol, volumesPending_), std::swap(lang, langPending_), std::swap(cam, cameraPending_);
        std::copy(volumes_, volumes_ + 3, v);
        language = lang_, camera = camera_;
    }
    if (cam) {  // N7c: Original desinstala os slots; os outros modos instalam (na thread do jogo, entre VBlanks)
        transitional::free_camera::set_settings(transitional::boundary_registry(), camera);
        std::fprintf(stderr, "[camera] modo %s\n", native::settings::camera_mode_name(camera.mode));
    }
    if (vol) transitional::sound::set_bus_volumes(v[0], v[1], v[2]);
    if (lang) {
        transitional::video::set_subtitle_preference(language);  // filmes
        if (voicesNative_) {  // falas: o servico recomeca no idioma novo; a fala em curso continua nele, no ponto em que esta
            Dialogue* prev = dialogue_.get();
            if (prev && prev->log)  // as legendas que estavam na tela saem com o idioma antigo
                for (const auto& s : prev->shown)
                    std::fprintf(prev->log, "%llu\tlegenda-sai\t-\t%llu\t%zu\t-\ttroca de idioma\n", static_cast<unsigned long long>(vblanks_.load()),
                                 static_cast<unsigned long long>(s.first), s.second);
            const uint64_t inst = prev ? prev->subs.instances() : 0;
            const uint64_t occ = prev ? prev->occurrences : 0, sim = prev ? prev->simultaneous : 0, lf = prev ? prev->latencyFrames : 0,
                           lm = prev ? prev->latencyMax : 0, ls = prev ? prev->latencySamples : 0;
            dialogue_.reset(new Dialogue(native::localization::ModPackages::discover(opt_.modAudio.parent_path(), "T13001N V1.000"),
                                         language));
            dialogue_->occurrences = occ, dialogue_->simultaneous = sim, dialogue_->latencyFrames = lf, dialogue_->latencyMax = lm;
            dialogue_->latencySamples = ls;  // os contadores do diagnostico continuam
            dialogue_->subs.set_instances(inst);  // e a numeracao das ocorrencias
            if (!soundingId_.empty())
                dialogueEvent({native::audio::DialogueKind::Started, soundingId_, soundingName_, soundingFrame_}, true);
            std::fprintf(stderr, "[legenda] idioma: %s\n", language.empty() ? "desligadas" : language.c_str());
        }
    }
    if (const int64_t wv = widePending_.exchange(-1); wv >= 0 && uint32_t(wv) != transitional::render_capture::wide()) {
        transitional::render_capture::set_wide(uint32_t(wv));
        // N9.5 fase 3B: a marca leve de HUD so com o quadro largo (a Remastered 16:9); no 4:3 e na Original fica desligada
        hud_marks_enable(wv != 0);
        std::fprintf(stderr, "[render] quadro: %s\n", wv ? ("largo " + std::to_string(wv) + "x480 (hor+), HUD ancorado").c_str() : "4:3");
    }
    if (const int ao = aoPending_.exchange(-1);
        ao >= 0 && (((ao & 1) != 0) != transitional::render_capture::ambient_occlusion() ||
                    ((ao & 1) && uint32_t(ao >> 1) != transitional::render_capture::ambient_occlusion_quality()))) {
        static const char* const kQ[] = {"baixa", "media", "alta"};
        transitional::render_capture::set_ambient_occlusion_quality(uint32_t(ao >> 1));
        transitional::render_capture::set_ambient_occlusion((ao & 1) != 0);
        if (ao & 1) std::fprintf(stderr, "[render] oclusao ambiente: ligada (qualidade %s)\n", kQ[std::min(ao >> 1, 2)]);  // N9.6
        else std::fprintf(stderr, "[render] oclusao ambiente: desligada\n");
    }
    if (const float s = scalePending_.exchange(0.f); s > 0 && s != transitional::render_capture::scale()) {
        transitional::render_capture::set_scale(s);
        std::fprintf(stderr, "[render] resolucao interna: escala %.3g (%ux%u)\n", s, unsigned(640 * s + 0.5f), unsigned(480 * s + 0.5f));
    }
    if (const int r = rendererPending_.exchange(-1); r >= 0 && !std::getenv("BS_RENDER_IR")) {
        transitional::render_capture::install(RenderProvider(r) == RenderProvider::Native ? transitional::render_capture::Mode::Native
                                                                                           : transitional::render_capture::Mode::Software);
        renderer_.store(r);
        std::fprintf(stderr, "[render] renderizador: %s\n", RenderProvider(r) == RenderProvider::Native ? "Vulkan" : "software");
    }
}

// Na thread do jogo, no inicio de cada VBlank: publica o quadro exibido, segura o ritmo e
// atende pedidos de parada.
void GameHost::vblank() {
    const uint64_t n   = vblanks_.fetch_add(1) + 1;
    native::telemetry::game_tick();  // N9.5 fase 5C: a CPU da thread do jogo e, sem janela, a linha do segundo
    periph_vblank_in();  // N3-0: indice "vblank" dos roteiros e negativo do ponto de amostragem
#if BS_VALIDACAO
    // N9.5 fase 5B (H7 de tools/dev/n95_headless.py), so validacao: BS_PROPORCAO_TROCA=<vblank>:<largura>[,...] pede a
    // largura do quadro (0 = 4:3, 854 = 16:9) nesse VBlank, pelo mesmo caminho do menu (setWideWidth, atendido logo abaixo);
    // BS_VK_FALHAR=<vblank>[:sempre] injeta uma falha do dispositivo Vulkan no render seguinte (transitoria: a recuperacao;
    // "sempre": o render fica no software ate o fim da sessao)
    {
        static const std::vector<std::pair<uint64_t, uint32_t>> kTrocas = [] {
            std::vector<std::pair<uint64_t, uint32_t>> v;
            if (const char* e = std::getenv("BS_PROPORCAO_TROCA"))
                for (const char* p = e; *p;) {
                    unsigned long long at = 0;
                    unsigned           w = 0;
                    int                used = 0;
                    if (std::sscanf(p, "%llu:%u%n", &at, &w, &used) != 2) break;
                    v.push_back({at, w}), p += used;
                    if (*p == ',') ++p;
                }
            return v;
        }();
        for (const auto& [at, w] : kTrocas)
            if (at == n) std::fprintf(stderr, "[validacao] proporcao no VBlank %llu: %u\n", static_cast<unsigned long long>(n), w), setWideWidth(w);
        static const std::pair<uint64_t, bool> kFalha = [] {
            unsigned long long at = 0;
            const char*        e  = std::getenv("BS_VK_FALHAR");
            if (!e || std::sscanf(e, "%llu", &at) != 1) return std::pair<uint64_t, bool>{0, false};
            return std::pair<uint64_t, bool>{at, std::strstr(e, ":sempre") != nullptr};
        }();
        if (kFalha.first && kFalha.first == n) {
            std::fprintf(stderr, "[validacao] falha do Vulkan injetada no VBlank %llu (%s)\n", static_cast<unsigned long long>(n),
                         kFalha.second ? "permanente" : "transitoria");
            transitional::render_capture::inject_fault(kFalha.second ? transitional::render_capture::Fault::Device
                                                                     : transitional::render_capture::Fault::DeviceOnce);
        }
    }
#endif
    applyRequests();  // N7c: pedidos do menu (volumes, idioma das legendas, renderizador)
    transitional::free_camera::vblank(n);  // N7c: um passo da camera livre (nada no modo Original)
#if BS_VALIDACAO
    // N9.5, so investigacao (docs/N9-5-AUDITORIA.md): BS_TELA_TESTE=<largura>,<cx>[,estrutura] reescreve a cada VBlank
    // a largura e o centro da estrutura de tela do Ninja (8C24CCB8 e 8C24CCC0) e o centro da copia que o desenho usa
    // (8C2D9C7C: o njSetScreen 8C1741D4 copia distancia, cx e cy para 8C2D9C70, e cada modelo le dali). Com
    // ",estrutura", so a estrutura (o experimento da auditoria: o quadro nao muda, porque a copia e rara). Com um terceiro
    // numero (<largura>,<cx>,<cx_copia>), a copia recebe esse valor: separa o efeito da estrutura do efeito do desenho.
    // Um quarto numero (<largura>,<cx>,<cx_copia>,<largura_copia>) grava tambem a largura da copia (8C2D9C74, float, e
    // 8C2D9CBC, inteiro), que o modo de video do Ninja (8C172642) fixa em 640 e da qual 8C1736A0 monta a regiao de recorte
    // 2D das sprites (N9.5 fase 1: a causa das faiscas do cais).
    struct Tela {
        float largura = 0, cx = 0, cxCopia = 0, larguraCopia = 0;
        bool  copia = true;
    };
    static const Tela kTela = [] {
        Tela t;
        if (const char* v = std::getenv("BS_TELA_TESTE")) {
            if (std::sscanf(v, "%f,%f,%f,%f", &t.largura, &t.cx, &t.cxCopia, &t.larguraCopia) < 3) t.cxCopia = t.cx;
            t.copia = std::strstr(v, ",estrutura") == nullptr;
        }
        return t;
    }();
    // N9.5 fase 3B, so validacao: BS_HUD_MARCA=1 liga a marca leve de HUD sem o quadro largo (a captura confere o papel do
    // produto contra a tabela da fase 3A em qualquer cena, tools/dev/n95_proveniencia.py --marca)
    static const bool kHudMarca = [] {
        const char* v = std::getenv("BS_HUD_MARCA");
        return v && std::string(v) == "1";
    }();
    if (kHudMarca && !g_hud_marks) hud_marks_enable(true);
    if (kTela.largura > 0 && ram_) {
        std::memcpy(ram_ + 0x24CCB8, &kTela.largura, 4);
        std::memcpy(ram_ + 0x24CCC0, &kTela.cx, 4);
        if (kTela.copia) std::memcpy(ram_ + 0x2D9C7C, &kTela.cxCopia, 4);
        if (kTela.larguraCopia > 0) {
            const uint32_t li = uint32_t(kTela.larguraCopia);
            std::memcpy(ram_ + 0x2D9C74, &kTela.larguraCopia, 4), std::memcpy(ram_ + 0x2D9CBC, &li, 4);
        }
    }
#endif
    transitional::sound::vblank(n);  // N6: tempo logico do mixer nativo (nada se os provedores sao o legado)
    if (dialogue_) dialogueVblank(n);
    // Diagnostico: BS_RAM_DUMP=<vblank>[,<vblank>...]:<arquivo> grava os 16 MB da RAM do guest
    // nesses VBlanks (com mais de um, em <arquivo>_<vblank>)
    static const std::pair<std::vector<uint64_t>, std::string> kRamDump = [] {
        std::pair<std::vector<uint64_t>, std::string> d;
        const char* v = std::getenv("BS_RAM_DUMP");
        if (!v || !std::strchr(v, ':')) return d;
        for (const char* p = v; p < std::strchr(v, ':');) {
            char* end = nullptr;
            d.first.push_back(std::strtoull(p, &end, 10));
            p = *end == ',' ? end + 1 : end;
            if (end == p && *end != ',') break;
        }
        d.second = std::strchr(v, ':') + 1;
        return d;
    }();
    if (ram_)
        for (uint64_t at : kRamDump.first)
            if (n == at) {
                const std::string path = kRamDump.first.size() > 1 ? kRamDump.second + "_" + std::to_string(n) : kRamDump.second;
                if (FILE* f = std::fopen(path.c_str(), "wb")) {
                    std::fwrite(ram_, 1, 16u << 20, f);
                    std::fclose(f);
                }
            }
    // Diagnostico (N2b-0, criterio B6): BS_STATE_LOG=<arquivo> grava a cada 250 VBlanks o hash da
    // regiao de variaveis do jogo (0x8C240000-0x8C25FFFF: estado logico provisorio, ate os campos
    // terem nome). BS_RAM_MUTACAO=<vblank>:<endereco> troca um byte nesse VBlank (negativo).
    static FILE* stateLog = [] {
        const char* v = std::getenv("BS_STATE_LOG");
        FILE*       f = v && *v ? std::fopen(v, "w") : nullptr;
        if (f) std::fprintf(f, "vblank\testado\n");
        return f;
    }();
    static const std::pair<uint64_t, uint32_t> kRamMut = [] {
        const char* v = std::getenv("BS_RAM_MUTACAO");
        if (!v || !std::strchr(v, ':')) return std::pair<uint64_t, uint32_t>{0, 0};
        return std::pair<uint64_t, uint32_t>{std::strtoull(v, nullptr, 10),
                                             uint32_t(std::strtoul(std::strchr(v, ':') + 1, nullptr, 16))};
    }();
    if (kRamMut.first && n == kRamMut.first && ram_) ram_[kRamMut.second & 0xFFFFFF] ^= 0x01;
    if (stateLog && ram_ && n % 250 == 0) {
        uint64_t h = 0xCBF29CE484222325ull;
        for (uint32_t a = 0x240000; a < 0x260000; ++a) h = (h ^ ram_[a]) * 0x100000001B3ull;
        std::fprintf(stateLog, "%llu\t%016llX\n", static_cast<unsigned long long>(n), static_cast<unsigned long long>(h));
        std::fflush(stateLog);
    }
    const auto     now = std::chrono::steady_clock::now();
    {
        const double work = std::chrono::duration<double, std::milli>(now - workStart_).count();
        if (bs::native::diag::enabled()) {  // BS_TRAVAS (N8.8, H9): o mesmo intervalo do "trabalho por campo", exato
            const uint64_t a = bs::native::diag::ns_of(workStart_), z = bs::native::diag::ns_of(now);
            bs::native::diag::name_thread("jogo");
            bs::native::diag::span(bs::native::diag::Stall::Field, a, z > a ? z - a : 0, int64_t(n));
            bs::native::diag::set_vblank(n);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        workSumMs_ += work;
        stats_.vblanks   = n;
        stats_.workAvgMs = workSumMs_ / double(n);
        stats_.workMaxMs = std::max(stats_.workMaxMs, work);
        if (work > 16.683) ++stats_.overBudget;
        // Diagnostico: BS_FIELD_LOG=1 lista os campos acima do orcamento
        static const bool fieldLog = std::getenv("BS_FIELD_LOG") != nullptr;
        if (fieldLog && work > 16.683) std::fprintf(stderr, "[campo] %llu: %.2f ms\n", (unsigned long long)n, work);
        const double since = std::chrono::duration<double>(now - rateStart_).count();
        if (since >= 1.0) {
            stats_.fieldsPerSecond = double(n - rateVblank_) / since;
            rateStart_             = now;
            rateVblank_            = n;
        }
    }
    pvr_video_frame(scratch_.argb, scratch_.width, scratch_.height);
    // N5: durante um filme nativo o quadro exibido e o dele (o PVR nao desenha o filme)
    const bool movie = transitional::video::playing() && movie_.width;
    if (movie) scratch_ = movie_;
    scratch_.hires.reset(), scratch_.hiresSeq = 0, scratch_.hiresWidth = scratch_.hiresHeight = 0;
    if (!movie && renderer_.load() == int(RenderProvider::Native)) {  // N7c: o slot da imagem maior do render exibido
        const auto hi     = transitional::render_capture::hires_for(scratch_.argb, scratch_.width, scratch_.height);
        scratch_.hiresSeq = hi.seq, scratch_.hiresWidth = hi.width, scratch_.hiresHeight = hi.height;
        scratch_.hiresY0 = hi.y0, scratch_.hiresRows = hi.rows;
        // capturas (--capture): a imagem maior volta para a CPU so nos quadros capturados
        if (hi.seq && opt_.captureEvery && opt_.capture && n % opt_.captureEvery == 0) {
            auto v = std::make_shared<std::vector<uint32_t>>();
            if (transitional::render_capture::read_hires(hi, *v)) scratch_.hires = std::move(v), scratch_.hiresY0 = 0;
        }
    }
    // Diagnostico (N2b-2): BS_FRAME_LOG=<arquivo> grava o hash do quadro exibido a cada VBlank
    // (compara a sequencia de quadros entre modos de tempo com linhas do tempo deslocadas)
    static FILE* frameLog = [] {
        const char* v = std::getenv("BS_FRAME_LOG");
        FILE*       f = v && *v ? std::fopen(v, "w") : nullptr;
        // entrada: leituras do controle (indice do roteiro); passo: voltas do laco principal do jogo
        if (f) std::fprintf(f, "vblank\tquadro\tentrada\tpasso\n");
        return f;
    }();
    if (frameLog) {
        uint64_t h = 0xCBF29CE484222325ull ^ (uint64_t(scratch_.width) << 32 | scratch_.height);
        for (uint32_t px : scratch_.argb) h = (h ^ px) * 0x100000001B3ull;
        std::fprintf(frameLog, "%llu\t%016llX\t%llu\t%llu\n", static_cast<unsigned long long>(n),
                     static_cast<unsigned long long>(h), static_cast<unsigned long long>(controllerReads()),
                     static_cast<unsigned long long>(transitional::time::game_steps()));
        if (n % 250 == 0) std::fflush(frameLog);
    }
    scratch_.vblank   = n;
    scratch_.subtitle = dialogue_ ? dialogue_->text : std::wstring();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::swap(frame_, scratch_);
        ++frameSeq_;
    }
    ramWatchVblank(ram_, n);
    if (opt_.captureEvery && opt_.capture && n % opt_.captureEvery == 0) opt_.capture(frame_);
    if (opt_.vblankHook &&
        !opt_.vblankHook({n, transitional::time::game_steps(), controllerReads(), frame_, ram_}))
        stop_.store(true);
    if (stop_.load() || (opt_.vblankLimit && n >= opt_.vblankLimit)) {
        if (traceOn()) std::fprintf(stderr, "[host] parada vista no VBlank %llu\n", (unsigned long long)n);
        throw StopRequest{};
    }
    if (paused_.load()) {  // N7c: menu aberto; o quadro ja foi publicado e o jogo fica aqui ate fechar
        transitional::sound::pause(true);
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(pauseMutex_);
                pauseCv_.wait(lock, [&] { return !paused_.load() || stop_.load() || requestsPending_.load(); });
            }
            if (requestsPending_.load()) applyRequests();  // volumes, legendas, renderizador: ja valem com o menu aberto
            if (!paused_.load() || stop_.load()) break;
        }
        transitional::sound::pause(false);
        epoch_ = std::chrono::steady_clock::now(), epochVblank_ = n;  // o ritmo recomeca: sem correr atras do tempo parado
        if (stop_.load()) throw StopRequest{};
    }
    if (opt_.realtime) {
        const auto t      = std::chrono::steady_clock::now();
        const auto target = epoch_ + kField * (n - epochVblank_);
        if (target > t) std::this_thread::sleep_until(target);
        else if (t - target > std::chrono::milliseconds(250)) {
            epoch_       = t;  // atrasado demais (carga, depurador): recomeca a contagem
            epochVblank_ = n;
        }
    }
    workStart_ = std::chrono::steady_clock::now();  // o proximo campo comeca aqui
}

GameStats GameHost::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void GameHost::threadMain() {
    // N2b-2: no tempo experimental a RAM tem rastreamento de escrita (ociosidade da thread 3); no
    // padrao, a alocacao de sempre
    std::vector<uint8_t> ram;
    uint8_t*             watched = nullptr;
    std::string          err;
    try {
        if (opt_.time == TimeProviderKind::Experimental) watched = transitional::cri::alloc_watched_ram(16u << 20);
        else ram.resize(16u << 20);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = e.what();
        running_.store(false);
        return;
    }
    Sh4Context c{};
    c.ram = watched ? watched : ram.data();
    ram_  = c.ram;  // so diagnostico (BS_RAM_DUMP, BS_RAM_WATCH)
    RamWatchGuard ramWatchGuard;
    try {
        ramWatchInit();  // configuracao invalida vira error() da sessao, nao std::terminate na thread
        if (opt_.registerFunctions) opt_.registerFunctions();
        bs::mods::reset();     // sessao nova: nada herdado de uma anterior
        bs::mods::init_all();  // mods compilados no executavel (projeto editavel; BS_MOD)
        if (opt_.onSessionStart) opt_.onSessionStart();  // N9.2: pontos de entrada da Remastered
#if BS_VALIDACAO
        // N9.2, so investigacao: BS_DESLIGAR_FUNCOES=<end>[,<end>] troca rotinas chamadas por ponteiro por uma que nao faz
        // nada (r0 = 0). Serve para achar quem desenha o que na tela (o menu do titulo), comparando quadros.
        if (const char* v = std::getenv("BS_DESLIGAR_FUNCOES"); v && *v) {
            std::stringstream ss(v);
            for (std::string t; std::getline(ss, t, ',');)
                if (!t.empty()) {
                    const uint32_t a = uint32_t(std::stoul(t, nullptr, 16));
                    const bool     ok = bs::mods::replace(a, [](Sh4Context* cc) { cc->r[0] = 0; });
                    std::fprintf(stderr, "[desligar] %08X %s\n", a, ok ? "desligada" : "NAO ENCONTRADA");
                }
        }
        // N9.5 fase 0, so investigacao: BS_TELA_LOGICA43=1 (com BS_TELA_TESTE) roda o teste "na tela" (8C05D1C0: projeta
        // e compara com 640x480 fixos) e o pan do som (8C064560: projeta, limita a [0, largura] e subtrai o cx) com a
        // projecao do 4:3 (o cx da copia 8C2D9C7C e a largura e o cx da estrutura 8C24CCB4 voltam a 320/640/320 so durante
        // a chamada). Com ele, os efeitos sonoros da rota de combate ficaram iguais aos do 4:3 (sem ele, o pan muda); as
        // particulas nao (docs/N9-5-AUDITORIA.md, secao 11). Precisa das chamadas diretas por slot (build com mods:
        // msvc-release-mods).
        // BS_TELA_LOGICA43=<end>[,<end>] escolhe outras rotinas (so as da tabela abaixo: os chamadores do njProjectScreen
        // 8C170A54 e ele mesmo), para achar por bissecao quem mais depende da projecao.
        if (const char* v = std::getenv("BS_TELA_LOGICA43"); v && *v) {
            g_ramTela = c.ram;
            static const std::pair<uint32_t, Sh4Func> kTabela[] = {
                {0x8C05D1C0, &tela43<0x8C05D1C0>}, {0x8C064560, &tela43<0x8C064560>},
                {0x8C170A54, &tela43<0x8C170A54>}, {0x8C031B80, &tela43<0x8C031B80>},
                {0x8C032708, &tela43<0x8C032708>}, {0x8C034FC0, &tela43<0x8C034FC0>},
                {0x8C03BF78, &tela43<0x8C03BF78>}, {0x8C03D548, &tela43<0x8C03D548>},
                {0x8C0584CC, &tela43<0x8C0584CC>},
                // as rotinas do jogo com as constantes 640,0 ou 320,0 no literal pool
                {0x8C032838, &tela43<0x8C032838>}, {0x8C0151DC, &tela43<0x8C0151DC>},
                {0x8C063F4C, &tela43<0x8C063F4C>}, {0x8C09BA04, &tela43<0x8C09BA04>},
                {0x8C063CE0, &tela43<0x8C063CE0>}, {0x8C0351E0, &tela43<0x8C0351E0>},
                {0x8C01F500, &tela43<0x8C01F500>}, {0x8C0221A0, &tela43<0x8C0221A0>}};
            const std::string lista = std::strcmp(v, "1") == 0 ? std::string("8C05D1C0,8C064560") : std::string(v);
            std::stringstream ss(lista);
            for (std::string t; std::getline(ss, t, ',');) {
                const uint32_t a  = uint32_t(std::stoul(t, nullptr, 16));
                Sh4Func        fn = nullptr;
                for (const auto& [ea, ef] : kTabela)
                    if (ea == a) fn = ef;
                if (!fn || !bs::mods::has_slot(a) || !bs::mods::replace(a, fn))
                    throw std::runtime_error("BS_TELA_LOGICA43: " + t + " fora da tabela ou sem slot (precisa do build com mods)");
                std::fprintf(stderr, "[tela] %08X na projecao do 4:3\n", a);
            }
        }
#endif
        bs::call_trace_install();  // diagnostico BS_CALL_TRACE (desligado por padrao)
        bs::cri_diag_install();    // diagnostico BS_CRI_DIAG (N2b-0; desligado por padrao)
        transitional::pad::probe_install();  // teste de contrato BS_PAD_CONTRATO (N3-0; desligado por padrao)
        transitional::pad::closure_install();  // prova de fechamento BS_PAD_FECHAMENTO (N3-2; desligado por padrao)
        // N3-2: controles pelo provedor nativo do SDK pd; a camada per continua a sondar as portas.
        // N8.4: no produto o barramento e o servico sem dispositivos (system/maple_bus). O modelo do Maple (controle no
        // barramento: --controles legado, a bu original: --saves legado, o negativo "maple") so existe na validacao.
        const char*    mv  = std::getenv("BS_PAD_NATIVO_MUTACAO");
        const uint32_t mut = transitional::pad::parse_mutations(mv ? mv : "");
        const bool     mapleModel = opt_.controls == ControlsProvider::Legacy || opt_.saves == SavesProvider::Legacy ||
                                (opt_.controls == ControlsProvider::Native && (mut & transitional::pad::kNatMaple));
        if (mapleModel) {
#if BS_VALIDACAO
            periph_use(maple_bus_backend());
            maple_set_controller_present(opt_.controls == ControlsProvider::Legacy || (mut & transitional::pad::kNatMaple));
#else
            throw std::runtime_error("o modelo do Maple (--controles legado, --saves legado, BS_PAD_NATIVO_MUTACAO=maple) "
                                     "so existe no build de validacao");
#endif
        } else {
            periph_use(nullptr);
        }
        // N8.6: a area de som do produto e a nativa (system/sound_bus); o modelo da AICA (o audio legado: o SDK e o driver
        // originais escrevem canais e o DSP; os diagnosticos BS_AICA_*) so existe na validacao
        const bool aicaModel = opt_.audioEffects == AudioEffectsProvider::Legacy || opt_.audioVoices == AudioVoicesProvider::Legacy ||
                               opt_.audioMusic == AudioMusicProvider::Legacy || std::getenv("BS_AICA_TRACE") ||
                               std::getenv("BS_AICA_LOG") || std::getenv("BS_AICA_MUTACAO") ||
                               std::getenv("BS_AICA_DUMP_AT") || std::getenv("BS_AICA_LOG_TUDO");  // todos os BS_AICA_* do modelo
        if (aicaModel) {
#if BS_VALIDACAO
            snd_bus_use(aica_bus_backend());
#else
            throw std::runtime_error("o audio legado e o modelo da AICA (--audio legado, BS_AICA_*) so existem no build "
                                     "de validacao");
#endif
        } else {
            snd_bus_use(nullptr);
        }
        if (opt_.controls == ControlsProvider::Native) {
            if (!opt_.input) throw std::runtime_error("--controles nativo sem fonte de entrada (InputSystem)");
            if (std::getenv("BS_PAD_CONTRATO")) throw std::runtime_error("BS_PAD_CONTRATO confere o servidor legado: use --controles legado");
            // coleta onde o legado entrega o fim do DMA: logo depois do servidor com tempo por ciclos
            // (nativo/legado), no disparo seguinte com o tempo experimental (pad_adapter.cpp, collect)
            transitional::pad::install(transitional::boundary_registry(), *opt_.input, mut,
                                       opt_.time != TimeProviderKind::Experimental);
        }
        // N7c: camera livre (secao 23 do N7-PLANO). Original: nada instalado. Diagnostico: BS_CAMERA_ROTEIRO (analogico por
        // VBlank, no lugar do controle), BS_CAMERA_LOG (uma linha por montagem), BS_CAMERA_FIXA=yaw,pitch (deslocamento fixo),
        // BS_CAMERA_MOVIMENTO=camera|original (a movimentacao desta execucao)
        {
            transitional::free_camera::Options co;
            co.cfg = opt_.camera;
            if (opt_.input) co.input = [in = opt_.input] { return in->peek(); };
            if (const char* v = std::getenv("BS_CAMERA_ROTEIRO")) co.script = v;
            if (const char* v = std::getenv("BS_CAMERA_LOG")) co.log = v;
            if (const char* v = std::getenv("BS_CAMERA_FIXA"); v && *v) {  // "yaw,pitch" em graus
                if (std::sscanf(v, "%f,%f", &co.fixedYaw, &co.fixedPitch) != 2) throw std::runtime_error("BS_CAMERA_FIXA: use yaw,pitch");
                co.fixed = true;
            }
            if (const char* v = std::getenv("BS_CAMERA_MOVIMENTO"); v && *v) {  // diagnostico: "camera" ou "original"
                if (std::strcmp(v, "camera") == 0) co.cfg.movement = native::settings::MoveMode::Camera;
                else if (std::strcmp(v, "original") == 0) co.cfg.movement = native::settings::MoveMode::Original;
                else throw std::runtime_error("BS_CAMERA_MOVIMENTO: use camera ou original");
            }
            // movimentacao relativa a camera: o analogico esquerdo girado pelo yaw da vista, na amostra do jogo (antes da
            // gravacao, que guarda o efetivo). Roteiros so com os diagnosticos da camera: a reproducao ja traz o efetivo.
            // Por isso BS_CAMERA_FIXA/ROTEIRO sao para roteiros com a entrada crua: com uma gravacao do BS_GRAVAR eles
            // girariam de novo o que ja e efetivo (revisao Codex 2026-10-23).
            const bool diagnostic = !co.script.empty() || co.fixed;
            transitional::free_camera::configure(transitional::boundary_registry(), std::move(co));
            if (opt_.input)
                opt_.input->set_transform([](const native::input::SampleContext&, const native::input::ControllerState& s) {
                    return transitional::free_camera::relative_input(s);
                }, diagnostic);
            if (transitional::free_camera::installed())
                std::fprintf(stderr, "[camera] modo %s\n", native::settings::camera_mode_name(opt_.camera.mode));
        }
        // N4: saves pelo provedor nativo (API bu sobre arquivos; sem VMU nem Maple)
        if (opt_.saves == SavesProvider::Native) {
            if (opt_.savesRoot.empty()) throw std::runtime_error("--saves nativo sem pasta de saves");
            native::saves::FileStoreOptions so;
            so.log = [](const std::string& m) { std::fprintf(stderr, "[saves] %s\n", m.c_str()); };
            saves_ = native::saves::make_file_store(opt_.savesRoot, so);
            std::fprintf(stderr, "[saves] nativo: %s\n", disc::pathText(opt_.savesRoot).c_str());
            transitional::bu::install(transitional::boundary_registry(), *saves_);
            transitional::bu::h1_install();
        }
        // N1: arquivos pelo provedor nativo (API gdFs sobre a pasta de recursos)
        if (!opt_.data.empty() && opt_.files == FilesProvider::Native) {
            files_ = std::make_unique<native::files::DataFileSystem>(opt_.data / L"disc", opt_.modAssets);
            for (const auto& m : files_->mods())
                std::fprintf(stderr, "[mods] recurso substituido: %s (%llu -> %llu bytes)\n", m.logical.c_str(),
                             static_cast<unsigned long long>(m.originalSize), static_cast<unsigned long long>(m.modSize));
            transitional::gdfs::check_sizes(*files_);
            transitional::gdfs::install(transitional::boundary_registry(), *files_);
        }
        // N5: filmes pelo provedor nativo (MP4 do Builder em data\video, mods em mods\video). N8.5: o Sofdec original
        // (--video legado) so existe no build de validacao.
#if !BS_VALIDACAO
        if (opt_.video == VideoProvider::Legacy)
            throw std::runtime_error("o filme legado (Sofdec, --video legado) so existe no build de validacao");
#endif
        if (opt_.video == VideoProvider::Native) {
            transitional::video::MwCoreOptions vo;
            vo.catalog      = {opt_.data / L"video", opt_.modVideo};
            if (native::video::plays_disc_movies()) vo.catalog.discDir = opt_.data / L"disc" / L"BSDATA";
            vo.subtitleLang = opt_.subtitles;
            // cor do sprite confirmada pelo O1b (NEWGAME e E001 no Flycast, tools/dev/n5_cor.py): RGB do Sofdec
            // na faixa de TV, modulado pela cor base; offset aditivo (offset color do PVR)
            vo.color = {true, true, true};
            // com o passo de jogo: os roteiros por passo (START durante o filme) saem deste log
            vo.log = [](const std::string& m) {
                std::fprintf(stderr, "%s (passo %llu)\n", m.c_str(), static_cast<unsigned long long>(transitional::time::game_steps()));
            };
            // som do filme no dispositivo so em tempo real; no headless, saida nula (mesmo relogio logico)
            if (opt_.realtime) vo.makeAudio = [] { return native::audio::make_system_output(); };
            movie_ = {};
            transitional::video::install(transitional::boundary_registry(), std::move(vo),
                                         [this](const native::video::VideoFrame& f) {
                                             movie_.argb = f.argb, movie_.width = f.width, movie_.height = f.height;
                                         });
            std::fprintf(stderr, "[video] nativo: %s\n", disc::pathText(opt_.data / L"video").c_str());
        }
        // N6 fase D: efeitos sonoros pelo provedor nativo (as 5 entradas do SDK de som; o gerenciador do jogo
        // continua o recompilado). Sem o BS_SND.MLT nos dados, a sessao falha com a mensagem do adaptador.
        // N6 fase E: falas pelo provedor nativo (o MW WAV da CRI; o player do jogo continua o recompilado), no mesmo
        // mixer dos efeitos.
        // N6 fase F: musica pelo provedor nativo (as entradas do SDK do gerenciador de musica e o download de SMPB/SMSB;
        // o gerenciador continua o recompilado), no mesmo mixer. Sem o MANATEE.DRV conferido, a sessao falha.
        if (opt_.audioEffects == AudioEffectsProvider::Native || opt_.audioVoices == AudioVoicesProvider::Native ||
            opt_.audioMusic == AudioMusicProvider::Native) {
            transitional::sound::Options so;
            so.effects   = opt_.audioEffects == AudioEffectsProvider::Native;
            so.mlt       = opt_.data / L"disc" / L"BSDATA" / L"BS_SND.MLT";
            so.voices    = opt_.audioVoices == AudioVoicesProvider::Native;
            so.voiceDir  = opt_.data / L"disc" / L"BSDATA";
            so.music     = opt_.audioMusic == AudioMusicProvider::Native;
            so.driver    = opt_.data / L"disc" / L"BSDATA" / L"MANATEE.DRV";
            so.modsAudio = opt_.modAudio;
            so.language  = opt_.dubbing;
            // o audio do jogo segue a saida padrao do Windows e sobrevive a fila cheia (N6-H; os filmes ficam como no N5)
            if (opt_.realtime) so.makeOutput = [] { return native::audio::make_system_output(200, true); };
            voicesNative_ = so.voices;
            if (so.voices) {  // legendas das falas (mods\<pacote>\legendas\falas.<idioma>.toml); "" = desligadas
                // a versao do jogo que o Builder valida (o jogo = ... dos pacotes; revisao Codex 2026-10-21)
                dialogue_.reset(new Dialogue(native::localization::ModPackages::discover(opt_.modAudio.parent_path(), "T13001N V1.000"),
                                             opt_.subtitles));
                for (const auto& p : dialogue_->mods.packages())
                    if (p.provisional)
                        std::fprintf(stderr, "[legenda] pacote %s %s: provisorio (nao revisado)\n", p.id.c_str(), p.version.c_str());
                so.onDialogue = [this](const native::audio::DialogueEvent& e) { dialogueEvent(e, false); };
            }
            const std::string dirText = disc::pathText(so.voiceDir);  // antes do move
            const bool        fx = so.effects, vz = so.voices, mu = so.music;
            transitional::sound::install(transitional::boundary_registry(), std::move(so));
            std::fprintf(stderr, "[audio] nativo: %s%s%s%s%s (%s)\n", fx ? "efeitos" : "", fx && vz ? " e " : "",
                         vz ? "falas" : "", (fx || vz) && mu ? " e " : "", mu ? "musica" : "", dirText.c_str());
        }
        // N2a: tempo e eventos pelo provedor nativo (antes do boot: o reset do hardware o inicia)
        if (opt_.time == TimeProviderKind::Native || opt_.time == TimeProviderKind::Experimental) {
            transitional::time::Options to;
            to.experimental = opt_.time == TimeProviderKind::Experimental;
            to.ram          = watched;
            if (const char* v = std::getenv("BS_TIME_TRACE")) to.trace = v;
            if (const char* v = std::getenv("BS_TEMPO_MUTACAO")) to.mutation = v;
            if (const char* v = std::getenv("BS_PROGRESS_TRACE")) to.progress_trace = v;
            if (const char* v = std::getenv("BS_PROGRESSO_LIMITE")) to.stall_units = std::strtoull(v, nullptr, 10);
            else to.stall_units = transitional::time::kDefaultStallUnits;
            transitional::time::install(to);
            input_set_step_counter([] { return transitional::time::game_steps(); });  // roteiros por passo
        } else {
            transitional::time::install_legacy_steps();  // os passos de jogo tambem no legado (falas nativas por passo)
        }
        if (opt_.time == TimeProviderKind::Native || opt_.time == TimeProviderKind::Experimental) {
            // Diagnostico (gameplay assistido): BS_STATE_PASSO=<arquivo> grava a cada BS_STATE_PASSO_CADA
            // passos de jogo (padrao 10), no inicio do passo, o hash da regiao de variaveis do jogo
            // (0x8C240000-0x8C25FFFF), a vida do Eliot (int16 em 0x8C24FBCC) e a posicao dele (floats em
            // 0x8C24D668). Comparavel entre modos de tempo passo a passo (tools/dev/compara_modos.py).
            if (const char* v = std::getenv("BS_STATE_PASSO"); v && *v) {
                g_stepLog.f = std::fopen(v, "w");
                if (!g_stepLog.f) throw std::runtime_error(std::string("BS_STATE_PASSO: nao abre ") + v);
                g_stepLog.every = 10;
                if (const char* k = std::getenv("BS_STATE_PASSO_SEM_CAMERA")) g_stepLog.noCamera = std::strcmp(k, "1") == 0;
                if (const char* k = std::getenv("BS_STATE_PASSO_CADA"))
                    g_stepLog.every = std::max<uint64_t>(1, std::strtoull(k, nullptr, 10));
                std::fprintf(g_stepLog.f, "passo\tvblank\testado\tvida\tx\ty\tz\n");
            }
            if (const char* v = std::getenv("BS_PAD_TRACE"); v && *v) {
                g_padTrace = std::fopen(v, "w");
                if (!g_padTrace) throw std::runtime_error(std::string("BS_PAD_TRACE: nao abre ") + v);
                std::fprintf(g_padTrace, "passo\tvblank\tleituras\tptr\tid\tsupport\ton\toff\tpress\trelease\tr\tl\tx1\ty1\t"
                                         "x2\ty2\told\tinfo\tnome\tacao0\tacao1\tacao2\tacao3\tacao4\treset\n");
                g_padRam = c.ram;
            }
            g_stepLog.ram  = c.ram;
            g_stepLog.host = this;
            if (g_stepLog.f || g_padTrace) transitional::time::set_step_observer(&stepLogWrite, nullptr);
        }
        // N7a: IR de render capturada do decodificador do TA (BS_RENDER_IR=capturar|conferir; BS_RENDER_IR_CADA=N
        // confere 1 render a cada N). O rasterizador em software continua o mesmo e e a referencia.
        // N7b: --render nativo (Vulkan) instala a captura no modo nativo; BS_RENDER_IR=comparar mantem o software e
        // compara o Vulkan (BS_RENDER_IR_BMP=<pasta> grava as comparacoes, 1 a cada BS_RENDER_IR_BMP_CADA renders).
        // Independe do provedor de tempo (revisao Codex 2026-10-21).
        if (const char* dir = std::getenv("BS_RENDER_IR_BMP"); dir && *dir) {
            const char* cada = std::getenv("BS_RENDER_IR_BMP_CADA");
            transitional::render_capture::set_compare_dump(dir, cada ? uint32_t(std::strtoul(cada, nullptr, 10)) : 300u);
        }
#if !BS_VALIDACAO
        if (std::getenv("BS_RENDER_IR"))  // N8.7: o caminho do TA nao desenha no produto (main_sdl recusa antes)
            throw std::runtime_error("BS_RENDER_IR so existe no build de validacao (N8.7)");
#endif
        if (const char* ir = std::getenv("BS_RENDER_IR"); ir && *ir) {
            const char*       cada  = std::getenv("BS_RENDER_IR_CADA");
            const uint32_t    every = cada ? uint32_t(std::strtoul(cada, nullptr, 10)) : 1u;
            const std::string m     = ir;
            using RM                = transitional::render_capture::Mode;
            transitional::render_capture::install(m == "conferir" ? RM::Check : (m == "comparar" ? RM::Compare : RM::Capture),
                                                  every);
        } else if (opt_.render == RenderProvider::Native) {
            renderer_.store(int(RenderProvider::Native));
            transitional::render_capture::install(transitional::render_capture::Mode::Native);
        } else {
            // N8.7: o renderizador em software desenha a IR (native/render/soft), sem ler a VRAM
            transitional::render_capture::install(transitional::render_capture::Mode::Software);
        }
        sh4_set_fatal_handler(onFatal);
        if (opt_.prepare) opt_.prepare(c);
        else boot_prepare(c, boot_read_data(opt_.data), rtcAtBoot());
        // N8.3: as syscalls do sistema vao para o servico nativo (o boot o instala); as leituras de setor (no jogo, so
        // os metadados do volume no boot) vem dos dados do jogo. Os mods valem pelo provedor nativo de arquivos.
        if (opt_.system == SystemProvider::Hle) {
#if BS_VALIDACAO
            // modelo HLE (validacao): BIOS, drive e flash simulados, como antes do N8.3; com o provedor legado de
            // arquivos, os mods valem pelos setores
            gdrom_reset();
            flashrom_reset();  // volatil por sessao, como o servico nativo (revisao Codex); --flash carrega depois
            bios_hle_install(&c, true);
            sys_disc_use(gdrom_disc_backend());
            if (!opt_.data.empty())
                gdrom_set_disc(open_runtime_data(opt_.data, files_ ? std::filesystem::path() : opt_.modAssets));
            if (!opt_.flash.empty() && !flashrom_load(opt_.flash))
                throw std::runtime_error("flash invalida (esperado 128 KB): " + disc::pathText(opt_.flash));
#else
            throw std::runtime_error("o modelo HLE do sistema (BIOS, GD-ROM e flash) so existe no build de validacao");
#endif
        } else if (!opt_.data.empty()) {
            syscall_service_set_data(std::make_unique<disc::RuntimeDisc>(opt_.data));
        }
        syscall_service_set_menu_hook(onSystemMenu);
        epoch_       = std::chrono::steady_clock::now();
        epochVblank_ = 0;
        workStart_ = rateStart_ = epoch_;
        pvr_set_vblank_hook(&GameHost::onVblank, this);
        const Sh4Func entry = sh4_lookup(c.pc);
        if (!entry) throw std::runtime_error("entrada do jogo sem codigo recompilado");
        entry(&c);
        err = "a funcao de entrada do jogo retornou";
    } catch (const StopRequest&) {
    } catch (const std::exception& e) {
        err = e.what();
    }
    pvr_set_vblank_hook(nullptr, nullptr);
    if (traceOn()) std::fprintf(stderr, "[host] execucao encerrada (%s); liberando fibras\n", err.empty() ? "ok" : err.c_str());
    if (!sh4_threads_shutdown() && err.empty()) err = "falha ao liberar as fibras das threads do guest";
    if (traceOn()) std::fprintf(stderr, "[host] fibras liberadas\n");
    syscall_service_set_data(nullptr);  // fecha os dados do jogo
    syscall_service_set_menu_hook(nullptr);
#if BS_VALIDACAO
    gdrom_set_disc(nullptr);  // fecha a imagem de disco do modelo HLE
#endif
    sys_disc_use(nullptr);
    std::fputs(cri_diag_summary().c_str(), stderr);
    transitional::pad::probe_finish();
    transitional::pad::closure_finish(transitional::pad::installed());
    if (transitional::pad::installed()) {
        std::fputs(transitional::pad::summary().c_str(), stderr);
        transitional::pad::uninstall(transitional::boundary_registry());
    }
#if BS_VALIDACAO
    maple_set_controller_present(true);
#endif
    {
        const MapleCounters m = periph_counters();
        std::fprintf(stderr, "[maple] DMAs %llu; porta A: DEVICE_INFO %llu, GET_CONDITION %llu; respostas do controle %llu, "
                             "leituras da fonte %llu\n",
                     static_cast<unsigned long long>(m.dmas), static_cast<unsigned long long>(m.devInfo[0]),
                     static_cast<unsigned long long>(m.getCond[0]), static_cast<unsigned long long>(m.controllerResponses),
                     static_cast<unsigned long long>(m.sourceReads));
    }
    periph_use(nullptr);   // N8.4: a proxima sessao escolhe de novo
    snd_bus_use(nullptr);  // N8.6
    // N2b (criterio B4): VBlank-In que chegaram com o bit ainda aceso (um atendimento para dois)
    std::fprintf(stderr, "[vblank] fundidos %llu\n", static_cast<unsigned long long>(holly_vblank_fused()));
    if (!transitional::time::installed()) transitional::time::uninstall();  // o contador de passos do legado
    if (transitional::time::installed()) {
        input_set_step_counter(nullptr);
        transitional::time::set_step_observer(nullptr, nullptr);
        if (g_stepLog.f) std::fclose(g_stepLog.f), g_stepLog.f = nullptr;
        if (g_padTrace) std::fclose(g_padTrace), g_padTrace = nullptr;
        transitional::time::uninstall();
        std::fputs(transitional::time::progress_summary().c_str(), stderr);
        const auto s = transitional::time::stats();
        std::fprintf(stderr, "[tempo] %s: VBlank-In %llu, VBlank-Out %llu, render %llu, Maple %llu, GD-ROM %llu; "
                             "agendados %llu, cancelados %llu, recuos do relogio %llu, VBlank-In colapsados %llu\n",
                     opt_.time == TimeProviderKind::Experimental ? "experimental" : "nativo",
                     static_cast<unsigned long long>(s.delivered[0]), static_cast<unsigned long long>(s.delivered[1]),
                     static_cast<unsigned long long>(s.delivered[2]), static_cast<unsigned long long>(s.delivered[3]),
                     static_cast<unsigned long long>(s.delivered[4]), static_cast<unsigned long long>(s.posted),
                     static_cast<unsigned long long>(s.cancelled), static_cast<unsigned long long>(s.clockRegressions),
                     static_cast<unsigned long long>(s.vblankCollapsed));
    }
    if (transitional::free_camera::stats().builds) std::fputs(transitional::free_camera::summary().c_str(), stderr);
    transitional::free_camera::uninstall(transitional::boundary_registry());  // sempre (o menu pode ter instalado)
    if (opt_.input) opt_.input->set_transform({});
    if (transitional::render_capture::stats().renders)  // N7a: BS_RENDER_IR
        std::fputs(transitional::render_capture::summary().c_str(), stderr);
    transitional::render_capture::install(transitional::render_capture::Mode::Off);  // sempre (revisao Codex 2026-10-21)
    if (transitional::sound::installed()) {
        std::fputs(transitional::sound::summary().c_str(), stderr);
        transitional::sound::uninstall(transitional::boundary_registry());
        if (dialogue_ && (dialogue_->occurrences || dialogue_->log))
            std::fprintf(stderr, "[legenda] falas: ocorrencias %llu; quadros com falas diferentes na tela ao mesmo tempo %llu; atraso da "
                                 "saida de audio: medio %.1f ms, maximo %.1f ms\n",
                         static_cast<unsigned long long>(dialogue_->occurrences), static_cast<unsigned long long>(dialogue_->simultaneous),
                         dialogue_->latencySamples ? dialogue_->latencyFrames * 1000.0 / 44100 / double(dialogue_->latencySamples) : 0.0,
                         dialogue_->latencyMax * 1000.0 / 44100);
    dialogue_.reset();
    }
    if (transitional::video::installed()) {
        std::fputs(transitional::video::summary().c_str(), stderr);
        transitional::video::uninstall(transitional::boundary_registry());
    }
    if (transitional::bu::installed()) {
        std::fputs(transitional::bu::summary().c_str(), stderr);
        transitional::bu::uninstall(transitional::boundary_registry());
    }
    saves_.reset();
    if (files_) {
        const auto s = transitional::gdfs::stats();
        std::fprintf(stderr, "[arquivos] nativo: %llu aberturas, %llu leituras, %llu bytes, %llu nao encontrados\n",
                     static_cast<unsigned long long>(s.opens), static_cast<unsigned long long>(s.reads),
                     static_cast<unsigned long long>(s.bytes), static_cast<unsigned long long>(s.notFound));
        transitional::gdfs::uninstall(transitional::boundary_registry());
        files_.reset();
    }
    ram_ = nullptr;
    transitional::cri::free_watched_ram(watched);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = err;
    }
    running_.store(false);
}

} // namespace bs
