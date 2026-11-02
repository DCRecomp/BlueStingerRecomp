// Memoria observada do tempo experimental, no Windows: MEM_WRITE_WATCH.
#include "cri_idle.h"

#include <stdexcept>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace bs::transitional::cri {

uint8_t* alloc_watched_ram(size_t bytes) {
    void* p = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
    if (!p)
        throw std::runtime_error("tempo experimental: o sistema nao ofereceu memoria com rastreamento de escrita "
                                 "(MEM_WRITE_WATCH); use --tempo nativo");
    return static_cast<uint8_t*>(p);
}
void free_watched_ram(uint8_t* ram) {
    if (ram) VirtualFree(ram, 0, MEM_RELEASE);
}

void watch_reset(uint8_t* ram, size_t bytes) { ResetWriteWatch(ram, bytes); }

size_t watch_pages(uint8_t* ram, const uint8_t*, size_t bytes, std::vector<void*>& pages, size_t* page_bytes) {
    ULONG_PTR count = pages.size();
    ULONG     gran  = 0;
    if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, ram, bytes, pages.data(), &count, &gran) != 0)
        throw std::runtime_error("tempo experimental: GetWriteWatch falhou");
    *page_bytes = gran;
    return count;
}

} // namespace bs::transitional::cri
