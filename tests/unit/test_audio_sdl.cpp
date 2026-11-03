// N9-L (L3): a saida de audio do Linux, pelo driver "disk" do SDL3: o que o dispositivo recebeu fica num arquivo, entao o teste
// confere o conteudo e a ordem, sem hardware de som. A escala de tempo do driver acelera o dispositivo.
#include "test.h"

#include "audio/audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace na = bs::native::audio;
namespace fs = std::filesystem;

namespace {

constexpr uint32_t kRate = 44100;

struct Disco {
    fs::path arq;
    Disco() {
        arq = fs::temp_directory_path() / ("bs-audio-sdl-" + std::to_string(getpid()) + ".raw");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk");
        SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, arq.c_str());
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
    }
    ~Disco() {
        std::error_code ec;
        fs::remove(arq, ec);
    }
    std::vector<int16_t> lido() const {
        std::ifstream     in(arq, std::ios::binary);
        const std::string b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::vector<int16_t> v(b.size() / 2);
        std::memcpy(v.data(), b.data(), v.size() * 2);
        return v;
    }
};

std::vector<int16_t> rampa(size_t quadros, uint8_t ch, int inicio = 1) {
    std::vector<int16_t> v(quadros * ch);
    for (size_t i = 0; i < v.size(); ++i) v[i] = int16_t(inicio + int(i));
    return v;
}

template <class F>
bool espera(F f, int ms = 3000) {
    const auto fim = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < fim) {
        if (f()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return f();
}

} // namespace

BS_TEST(audio_sdl_registrada_e_nao_nula) {
    Disco d;
    auto  o = na::make_system_output();
    BS_CHECK(static_cast<bool>(o->open(kRate, 2)));
    BS_CHECK(o->writable_frames() == kRate / 5);  // 200 ms; a nula aceita sempre 1 M
    o->close();
}

BS_TEST(audio_sdl_conteudo_e_ordem) {
    Disco d;
    {
        auto o = na::make_system_output(200, false);
        BS_CHECK(static_cast<bool>(o->open(kRate, 2)));
        const auto pcm = rampa(4410, 2);
        BS_CHECK(static_cast<bool>(o->write(pcm)));
        BS_CHECK(espera([&] { return o->played_frames() == 4410 && o->queued_frames() == 0; }));
        BS_CHECK(o->stats().blocks == 1 && o->stats().underruns == 0 && o->stats().cushions == 0);
        o->close();
        // antes e depois do bloco o dispositivo grava silencio; a rampa tem de aparecer inteira, contigua e na ordem
        const auto v = d.lido();
        BS_CHECK(v.size() >= pcm.size());
        BS_CHECK(std::search(v.begin(), v.end(), pcm.begin(), pcm.end()) != v.end());
    }
}

BS_TEST(audio_sdl_pausa_e_descarte) {
    Disco d;
    auto  o = na::make_system_output(200, false);
    BS_CHECK(static_cast<bool>(o->open(kRate, 1)));
    o->pause(true);
    BS_CHECK(static_cast<bool>(o->write(rampa(2000, 1))));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    BS_CHECK(o->played_frames() == 0 && o->queued_frames() == 2000);  // pausado, nada toca
    o->flush();
    BS_CHECK(o->queued_frames() == 0 && o->played_frames() == 0);     // o descartado nao conta como tocado
    o->pause(false);
    BS_CHECK(static_cast<bool>(o->write(rampa(441, 1))));
    BS_CHECK(espera([&] { return o->played_frames() == 441; }));
    o->flush();
    BS_CHECK(static_cast<bool>(o->write(rampa(441, 1))));
    BS_CHECK(espera([&] { return o->played_frames() == 882; }));      // cresce sem regredir depois do descarte
    BS_CHECK(o->stats().underruns == 0);                              // a fila vazia do descarte foi pedida
    o->close();
}

BS_TEST(audio_sdl_seguir_reserva_teto_e_falta) {
    Disco d;
    auto  o = na::make_system_output(200, true);
    BS_CHECK(static_cast<bool>(o->open(kRate, 1)));
    o->pause(true);  // sem consumo, as contas da fila sao exatas
    const uint64_t reserva = uint64_t(kRate) * 66 / 1000;
    BS_CHECK(static_cast<bool>(o->write(rampa(1000, 1))));
    BS_CHECK(o->stats().cushions == 1);
    BS_CHECK(o->queued_frames() == reserva + 1000);
    const uint64_t teto  = kRate / 5;
    const size_t   resto = size_t(teto - o->queued_frames());
    BS_CHECK(static_cast<bool>(o->write(rampa(resto, 1))));  // cabe exato no teto
    BS_CHECK(o->queued_frames() == teto && o->stats().dropped == 0);
    BS_CHECK(o->writable_frames() == 0);
    BS_CHECK(static_cast<bool>(o->write(rampa(1, 1))));      // passou do teto: descarta, sem erro
    BS_CHECK(o->stats().dropped == 1 && o->queued_frames() == teto);
    BS_CHECK(o->stats().maxQueuedFrames == teto);
    o->pause(false);
    BS_CHECK(espera([&] { return o->queued_frames() == 0; }));
    BS_CHECK(espera([&] { return o->played_frames() == teto; }));
    BS_CHECK(static_cast<bool>(o->write(rampa(100, 1))));    // fila vazia sem descarte pedido: falta e reserva nova
    BS_CHECK(o->stats().underruns == 1 && o->stats().cushions == 2);
    BS_CHECK(o->healthy());
    o->close();
}

BS_TEST(audio_sdl_duas_saidas_e_reabertura) {
    Disco d;
    auto  a = na::make_system_output(200, false);
    auto  b = na::make_system_output(200, true);  // filme e jogo ao mesmo tempo
    BS_CHECK(static_cast<bool>(a->open(kRate, 2)));
    BS_CHECK(static_cast<bool>(b->open(kRate, 1)));
    BS_CHECK(static_cast<bool>(a->write(rampa(441, 2))));
    BS_CHECK(static_cast<bool>(b->write(rampa(441, 1))));
    BS_CHECK(espera([&] { return a->played_frames() == 441; }));
    a->close();
    BS_CHECK(a->played_frames() == 441 && a->writable_frames() == 0);
    const uint64_t antes = b->played_frames();
    BS_CHECK(static_cast<bool>(b->write(rampa(4410, 1))));   // fechar uma nao derruba a outra: ela continua tocando
    BS_CHECK(espera([&] { return b->played_frames() > antes && b->queued_frames() == 0; }));
    BS_CHECK(static_cast<bool>(a->open(kRate, 2)));          // reabrir comeca do zero
    BS_CHECK(a->played_frames() == 0 && a->stats().blocks == 0);
}

BS_TEST(audio_sdl_abrir_e_fechar_em_paralelo_nao_para_a_outra) {
    Disco d;
    auto  fixa = na::make_system_output(200, false);
    BS_CHECK(static_cast<bool>(fixa->open(kRate, 1)));
    std::atomic<int> falhas{0};
    std::vector<std::thread> t;
    for (int i = 0; i < 4; ++i)
        t.emplace_back([&] {
            for (int r = 0; r < 25; ++r) {
                auto o = na::make_system_output(200, true);
                if (!o->open(kRate, 1) || !o->write(rampa(100, 1))) falhas.fetch_add(1);
                o->close();
            }
        });
    const auto pcm = rampa(8000, 1);
    BS_CHECK(static_cast<bool>(fixa->write(pcm)));
    for (std::thread& x : t) x.join();
    BS_CHECK(falhas.load() == 0);
    BS_CHECK(espera([&] { return fixa->played_frames() == 8000; }));
    fixa->close();
}

BS_TEST(audio_sdl_seguir_recupera_abertura_que_falhou) {
    // nenhum driver com esse nome: a abertura falha. Depois o driver aparece, e a escrita seguinte abre com a taxa pedida
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "nao-existe");
    auto o = na::make_system_output(200, true);
    BS_CHECK(!static_cast<bool>(o->open(22050, 2)));
    BS_CHECK(o->healthy());
    BS_CHECK(!static_cast<bool>(o->write(rampa(10, 2))));    // ainda sem dispositivo: erro contado
    BS_CHECK(o->stats().writeErrors == 1 && o->stats().reopens == 1);
    Disco d;  // o driver "disk" de volta
    BS_CHECK(!static_cast<bool>(o->write(rampa(10, 2))));    // no mesmo segundo nao tenta de novo
    BS_CHECK(espera([&] { return static_cast<bool>(o->write(rampa(10, 2))); }, 2000));
    BS_CHECK(o->stats().reopens == 2);
    BS_CHECK(o->writable_frames() < 22050u / 5 + 1);         // teto calculado com a taxa pedida, nao com o padrao
    BS_CHECK(o->stats().cushions == 1);
    o->close();
}

BS_TEST(audio_sdl_fechada_recusa_e_nao_quebra) {
    Disco d;
    auto  o = na::make_system_output(200, false);
    BS_CHECK(!o->healthy());
    BS_CHECK(!static_cast<bool>(o->write(rampa(10, 1))));
    BS_CHECK(o->stats().writeErrors == 1);
    o->pause(true), o->flush(), o->close();
    BS_CHECK(o->played_frames() == 0 && o->queued_frames() == 0 && o->writable_frames() == 0);
}
