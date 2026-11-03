// N9-L: a saida do sistema no Linux e a do SDL3 (app/audio_output_sdl.cpp), que se registra aqui: native/ nao depende do SDL3.
#include "audio/audio.h"

namespace bs::native::audio {

namespace {
SystemOutputFactory g_factory = nullptr;
}

void set_system_output_factory(SystemOutputFactory f) { g_factory = f; }

std::unique_ptr<IAudioOutput> make_system_output(uint32_t latency_ms, bool follow_default) {
    return g_factory ? g_factory(latency_ms, follow_default) : make_null_output();
}

} // namespace bs::native::audio
