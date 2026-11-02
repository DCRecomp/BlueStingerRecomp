#pragma once
// N9-L: pilhas das threads do guest, atras de um contrato pequeno. Windows: fibras do Win32 (host_fiber_win.cpp).
// Linux: troca de contexto propria (host_fiber_posix.cpp). A logica das threads fica em sh4_threads.cpp.
#include <cstddef>
#include <cstdint>

namespace bs::host {

using FiberFn = void (*)(void*);  // nao retorna: termina trocando para outra fibra

// A thread do host passa a ser uma fibra. `adopted` diz se foi convertida agora (e deve ser desfeita no fim).
// Falha: nullptr, com o codigo do sistema em `error`.
void* fiber_adopt(bool* adopted, uint32_t* error);
void* fiber_create(size_t stack_bytes, FiberFn fn, void* arg);
void  fiber_switch(void* to);
void  fiber_delete(void* fiber);
bool  fiber_release_thread();  // desfaz fiber_adopt

} // namespace bs::host
