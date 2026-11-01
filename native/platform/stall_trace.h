#pragma once
// N8.8 (diagnostico do H9): travas, esperas e a fila de audio numa linha do tempo correlacionavel.
// BS_TRAVAS=<arquivo.tsv>[;<limite_ms>] liga (limite padrao 8 ms; valor invalido desliga com aviso). Desligado, cada
// ponto custa a leitura de um bool: nada e alocado e nenhum relogio e lido. Ligado: contagens e maximos por tipo e, num
// anel em memoria, os eventos com duracao acima do limite e todos os eventos da saida de audio (falta, reserva,
// descarte, fila baixa). Nada vai para o disco no caminho critico: o anel e o resumo sao escritos so no fim do processo.
// Dentro das travas do Vulkan so se leem instantes (mark); o registro (span) e feito depois de solta-las.
#include <chrono>
#include <cstdint>
#include <string>

namespace bs::native::diag {

enum class Stall : uint8_t {
    GameQueueLock,   // jogo: espera pela trava da fila do Vulkan (compartilhada com a apresentacao)
    GameSubmit,      // jogo: vkQueueSubmit do render (com a trava na mao)
    GameFence,       // jogo: vkWaitForFences do render
    GameWaitIdle,    // jogo: vkQueueWaitIdle depois da cerca (com a trava na mao)
    GameReadback,    // jogo: leituras da GPU (imagem maior, capturas): trava + envio + cerca
    PresentFence,    // apresentacao: cerca do quadro anterior
    PresentAcquire,  // apresentacao: vkAcquireNextImageKHR
    PresentLock,     // apresentacao: espera pela trava da fila (envio e present)
    PresentSubmit,   // apresentacao: vkQueueSubmit (com a trava na mao)
    PresentCall,     // apresentacao: vkQueuePresentKHR (com a trava na mao; com V-Sync pode esperar o monitor)
    Field,           // jogo: trabalho ativo do campo (o "trabalho por campo" do desempenho: do fim do VBlank anterior,
                     // depois do ritmo, ate o VBlank atual); extra = numero do campo
    AudioWriteGap,   // saida de audio: intervalo entre dois blocos produzidos (extra = fila em ms x 10)
    AudioLowQueue,   // saida de audio: fila abaixo de 20 ms antes do bloco novo (extra = fila em ms x 10)
    AudioUnderrun,   // saida de audio: falta de dados (fila vazia depois do primeiro bloco)
    AudioCushion,    // saida de audio: reserva de silencio inserida
    AudioDrop,       // saida de audio: bloco descartado (fila no teto)
    Count
};

bool     enabled();
uint64_t now_ns();                                            // relogio monotonico desde o inicio do rastreio, em ns
uint64_t ns_of(std::chrono::steady_clock::time_point t);      // o mesmo relogio para um instante ja lido
inline uint64_t start() { return enabled() ? now_ns() : 0; }  // 0 = desligado
inline uint64_t mark(uint64_t t0) { return t0 ? now_ns() : 0; }  // so le o relogio se t0 veio ligado
void     finish(Stall k, uint64_t t0, int64_t extra = 0);     // registra now - t0 (nada se t0 == 0)
void     between(Stall k, uint64_t a, uint64_t b, int64_t extra = 0);  // registra b - a (nada se a ou b == 0)
void     event(Stall k, int64_t extra = 0);                    // evento pontual (sempre vai ao anel)
void     span(Stall k, uint64_t t0, uint64_t dur_ns, int64_t extra = 0);  // duracao ja medida
void     set_vblank(uint64_t n);
void     set_render(uint64_t n);
void     name_thread(const char* name);  // rotulo (literal) da thread atual no arquivo (ex.: "jogo", "janela")

// Le "<arquivo>[;<limite_ms>]". Falso se vazio, sem arquivo ou com limite invalido (nao numerico, <= 0, infinito ou
// grande demais para ns em 64 bits).
bool parse_config(const char* v, std::string& path, uint64_t& limit_ns);

} // namespace bs::native::diag
