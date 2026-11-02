// N9-L: memoria observada do tempo experimental, no Linux. O detector so precisa das paginas que MUDARAM desde a ultima visita, e
// o resultado dele vem da comparacao com a copia. Aqui a RAM inteira e comparada com a copia, o que da o mesmo resultado do
// MEM_WRITE_WATCH: toda pagina diferente foi escrita. mprotect com SIGSEGV custaria menos em ocioso, mas um read() sobre pagina
// protegida falharia com EFAULT em vez de gerar o sinal, e a leitura de disco grava direto nessa RAM.
#include "cri_idle.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>

namespace bs::transitional::cri {

namespace {
size_t g_bytes = 0;
} // namespace

uint8_t* alloc_watched_ram(size_t bytes) {
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) throw std::runtime_error("tempo experimental: sem memoria para a RAM observada; use --tempo nativo");
    g_bytes = bytes;
    return static_cast<uint8_t*>(p);
}
void free_watched_ram(uint8_t* ram) {
    if (ram) munmap(ram, g_bytes);
}

void watch_reset(uint8_t*, size_t) {}

size_t watch_pages(uint8_t* ram, const uint8_t* shadow, size_t bytes, std::vector<void*>& pages, size_t* page_bytes) {
    constexpr size_t kPage = 4096;
    size_t           n     = 0;
    for (size_t off = 0; off < bytes; off += kPage)
        if (std::memcmp(ram + off, shadow + off, std::min(kPage, bytes - off)) != 0) pages[n++] = ram + off;
    *page_bytes = kPage;
    return n;
}

} // namespace bs::transitional::cri
