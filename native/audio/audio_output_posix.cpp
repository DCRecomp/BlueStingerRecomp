// N9-L: a saida real pelo SDL3 entra na L3. Ate la, o jogo sem janela nao precisa de som.
#include "audio/audio.h"

namespace bs::native::audio {

std::unique_ptr<IAudioOutput> make_system_output(uint32_t, bool) { return make_null_output(); }

} // namespace bs::native::audio
