#pragma once
// N9.5 fase 3A (so validacao; docs/N9-5-AUDITORIA.md secao 14): a sombra dos buffers da copia do canal 2 (DMA). A copia
// marca os blocos de 32 bytes da RAM que le (mark); depois disso, cada escrita da CPU num bloco marcado anota a cadeia de
// chamadas de quem escreveu e os bytes cobertos (note). Ao copiar o bloco de novo (take), o escritor so vale se os 32
// bytes foram escritos pela mesma cadeia desde a copia anterior; senao (escrita parcial, varios escritores, conteudo que
// nao passou pela CPU) nao ha escritor. A copia consome a anotacao: a proxima precisa de escrita nova.
#if BS_VALIDACAO
#include "kamui/list_sink.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

namespace bs::kamui {

class DmaOriginShadow {
public:
    static constexpr uint32_t kBlocks = (16u << 20) / 32;  // a RAM principal em blocos de 32 bytes

    void enable() {
        marked_.assign(kBlocks / 8, 0);
        bytes_.assign(kBlocks, 0);
        mixed_.assign(kBlocks, 0);
        origin_.assign(kBlocks, {});
    }
    bool     on() const { return !marked_.empty(); }
    // o mapa de bits dos blocos marcados (o teste rapido de sh4_mem.h le direto dele)
    uint8_t* bitmap() { return marked_.data(); }

    void reset() {
        std::fill(marked_.begin(), marked_.end(), uint8_t(0));
        std::fill(bytes_.begin(), bytes_.end(), 0u);
        std::fill(mixed_.begin(), mixed_.end(), uint8_t(0));
    }
    // a copia leu este bloco (off = offset na RAM)
    void mark(uint32_t off) {
        const uint32_t b = block(off);
        marked_[b >> 3] |= uint8_t(1u << (b & 7));
    }
    bool marked(uint32_t off) const {
        const uint32_t b = block(off);
        return (marked_[b >> 3] >> (b & 7)) & 1;
    }
    // uma escrita de `size` bytes (1, 2 ou 4, alinhada) num bloco marcado
    void note(uint32_t off, uint32_t size, const ParamOrigin& o) {
        const uint32_t b    = block(off);
        const uint32_t mask = (size >= 4 ? 0xFu : (size == 2 ? 0x3u : 0x1u)) << (off & 31);
        if (!bytes_[b]) origin_[b] = o;
        else if (!std::equal(std::begin(o.fn), std::end(o.fn), std::begin(origin_[b].fn))) mixed_[b] = 1;
        bytes_[b] |= mask;
    }
    // a copia le o bloco de novo: o escritor, se os 32 bytes vieram da mesma cadeia; a anotacao e consumida
    bool take(uint32_t off, ParamOrigin& out) {
        const uint32_t b  = block(off);
        const bool     ok = bytes_[b] == 0xFFFFFFFFu && !mixed_[b];
        if (ok) out = origin_[b];
        bytes_[b] = 0, mixed_[b] = 0;
        return ok;
    }

private:
    static uint32_t          block(uint32_t off) { return (off & 0x00FFFFFFu) >> 5; }
    std::vector<uint8_t>     marked_, mixed_;
    std::vector<uint32_t>    bytes_;
    std::vector<ParamOrigin> origin_;
};

} // namespace bs::kamui
#endif
