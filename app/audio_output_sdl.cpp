// N9-L (L3): a saida de audio do Linux, pelo SDL3. Fica no app porque o SDL3 e restrito a este alvo. Mesmo contrato da saida
// XAudio2 (audio_output_xaudio2.cpp): teto da fila, reserva de silencio e descarte no modo de seguir a saida padrao.
// O proprio SDL segue a saida padrao: o fluxo aberto em SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK migra sozinho quando ela muda.
#include "audio/audio.h"
#include "platform/stall_trace.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <mutex>
#include <vector>

namespace bs::native::audio {

namespace {

std::mutex& initLock() {  // duas saidas podem abrir e fechar ao mesmo tempo (filme e jogo); a contagem do SDL nao e protegida
    static std::mutex m;
    return m;
}

class SdlOutput final : public IAudioOutput {
public:
    SdlOutput(uint32_t latencyMs, bool follow) : latencyMs_(latencyMs), follow_(follow) {}
    ~SdlOutput() override { close(); }

    Result<void> open(uint32_t rate, uint8_t ch) override {
        close();
        primed_ = paused_ = lost_ = false, stats_ = {}, written_ = 0, played_ = 0;
        rate_ = rate, channels_ = ch;  // antes de abrir: no modo de seguir, a abertura que falhar e repetida com estes valores
        return start();
    }

    size_t writable_frames() const override {
        if (!stream_ || lost_) return 0;
        const uint64_t cap = uint64_t(rate_) * latencyMs_ / 1000, q = queued_frames();
        return q >= cap ? 0 : size_t(cap - q);
    }

    Result<void> write(std::span<const int16_t> pcm) override {
        if (follow_ && (lost_ || !stream_)) retry();
        if (!stream_ || lost_) return ++stats_.writeErrors, Error::Io;
        if (pcm.empty()) return {};
        namespace dg = bs::native::diag;
        const bool     tr     = dg::enabled();
        const uint64_t queued = queued_frames();
        int64_t        fila10 = 0;
        if (tr) {
            fila10            = rate_ ? int64_t(queued * 10000 / rate_) : 0;
            const uint64_t tw = dg::now_ns();
            if (lastWriteNs_) dg::span(dg::Stall::AudioWriteGap, lastWriteNs_, tw - lastWriteNs_, fila10);
            lastWriteNs_ = tw;
            if (primed_ && !paused_ && fila10 < 200) dg::event(dg::Stall::AudioLowQueue, fila10);
        }
        // como no XAudio2: o teto conta o bloco novo e a reserva, se ela for entrar
        const uint64_t cushion = uint64_t(rate_) * kCushionMs / 1000;
        const uint64_t novo    = pcm.size() / channels_ + (queued == 0 ? cushion : 0);
        if (follow_ && queued + novo > uint64_t(rate_) * latencyMs_ / 1000) {
            ++stats_.dropped;
            if (tr) dg::event(dg::Stall::AudioDrop, fila10);
            return {};
        }
        if (primed_ && queued == 0 && !paused_) {
            ++stats_.underruns;
            if (tr) dg::event(dg::Stall::AudioUnderrun, fila10);
        }
        if (follow_ && queued == 0) {
            const std::vector<int16_t> silence(size_t(cushion) * channels_, int16_t(0));
            if (!put(silence)) return Error::Io;
            ++stats_.cushions;
            if (tr) dg::event(dg::Stall::AudioCushion, fila10);
        }
        if (!put(pcm)) return Error::Io;
        primed_ = true, ++stats_.blocks, stats_.maxQueuedFrames = std::max(stats_.maxQueuedFrames, queued_frames());
        return {};
    }

    void close() override {
        if (!stream_) return;
        played_frames();  // os tocados continuam validos depois de fechar
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        const std::lock_guard<std::mutex> g(initLock());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    void pause(bool p) override {
        if (!stream_) return;
        paused_ = p;
        p ? SDL_PauseAudioStreamDevice(stream_) : SDL_ResumeAudioStreamDevice(stream_);
    }

    void flush() override {
        if (!stream_) return;
        played_frames();
        SDL_ClearAudioStream(stream_);
        written_ = played_;  // o que foi descartado nao tocou e nao vai tocar
        primed_  = false;    // a fila vazia depois do descarte e pedida, nao falta
    }

    // O SDL3 nao expoe cursor de reproducao: "tocado" aqui e o que saiu do fluxo para o dispositivo. Adianta-se ao som real em
    // ate um bloco do dispositivo, enquanto o SamplesPlayed do XAudio2 conta o que a voz processou. Fica na resolucao de um
    // bloco que o contrato de queued_frames() ja admite, e a sincronia se confere de ouvido na maquina real (A8).
    uint64_t played_frames() const override {
        if (stream_) {
            const uint64_t q = queued_frames();
            played_          = std::max(played_, written_ > q ? written_ - q : 0);
        }
        return played_;
    }
    uint64_t queued_frames() const override {
        if (!stream_) return 0;
        const int b = SDL_GetAudioStreamQueued(stream_);
        return b > 0 ? uint64_t(b) / (2 * channels_) : 0;
    }

    // seguindo a saida padrao, a saida nunca desiste: como no XAudio2, tenta de novo a cada segundo
    bool healthy() const override { return follow_ || (stream_ && !lost_); }
    Stats stats() const override {
        Stats s = stats_;
        s.lost  = lost_;
        return s;
    }

private:
    Result<void> start() {
        {
            const std::lock_guard<std::mutex> g(initLock());
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return Error::Unsupported;
        }
        const SDL_AudioSpec spec{SDL_AUDIO_S16, int(channels_), int(rate_)};
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream_) {
            const std::lock_guard<std::mutex> g(initLock());
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return Error::NotFound;
        }
        if (!paused_) SDL_ResumeAudioStreamDevice(stream_);  // o dispositivo do fluxo abre pausado
        return {};
    }

    bool put(std::span<const int16_t> pcm) {
        if (!SDL_PutAudioStreamData(stream_, pcm.data(), int(pcm.size_bytes()))) {
            lost_ = true, ++stats_.writeErrors;
            return false;
        }
        written_ += pcm.size() / channels_;
        return true;
    }

    // Dispositivo perdido, ou que nunca abriu, no modo de seguir: reabre uma vez por segundo, somando o que ja tocou e mantendo
    // as estatisticas. A primeira tentativa e imediata (lastRetry_ zerado). Falhando, quem chama recebe erro e o bloco seguinte
    // tenta de novo.
    void retry() {
        const uint64_t now = SDL_GetTicks();
        if (lastRetry_ && now - lastRetry_ < 1000) return;
        lastRetry_ = now ? now : 1;
        const Stats keep = stats_;
        close();
        written_ = played_, primed_ = false;
        const bool ok = static_cast<bool>(start());
        stats_ = keep, ++stats_.reopens, lost_ = !ok;
    }

    SDL_AudioStream*          stream_   = nullptr;
    uint32_t                  rate_     = 44100, latencyMs_;
    uint32_t                  channels_ = 1;
    bool                      follow_, primed_ = false, paused_ = false, lost_ = false;
    uint64_t                  written_ = 0;  // quadros postos no fluxo, sem os descartados
    mutable uint64_t          played_  = 0;  // nunca regride
    uint64_t                  lastRetry_ = 0, lastWriteNs_ = 0;
    Stats                     stats_;
    static constexpr uint64_t kCushionMs = 66;  // 4 campos NTSC
};

std::unique_ptr<IAudioOutput> make_sdl_output(uint32_t latencyMs, bool follow) { return std::make_unique<SdlOutput>(latencyMs, follow); }

// este arquivo entra direto no executavel: sem ele (ferramentas, testes do nucleo), a saida do sistema e a nula
const bool g_registered = (set_system_output_factory(&make_sdl_output), true);

} // namespace

} // namespace bs::native::audio
