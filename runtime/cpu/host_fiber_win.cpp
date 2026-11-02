#include "cpu/host_fiber.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace bs::host {

void* fiber_adopt(bool* adopted, uint32_t* error) {
    // FLOAT_SWITCH: XMM6-XMM15 sao preservados entre chamadas na ABI x64 e podem estar
    // vivos numa fibra suspensa (revisao Codex 2026-10-14)
    // a thread do host ja pode ser fibra (de quem a criou); senao, e convertida aqui. Falha
    // na conversao e erro (sem fibra nao ha troca de threads), nunca "ja era fibra"
    // (revisao Codex 2026-10-14)
    if (IsThreadAFiber()) {
        *adopted = false;
        return GetCurrentFiber();
    }
    void* f = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
    if (!f) *error = uint32_t(GetLastError());
    *adopted = f != nullptr;
    return f;
}

void* fiber_create(size_t stack_bytes, FiberFn fn, void* arg) {
    return CreateFiberEx(64 << 10, stack_bytes, FIBER_FLAG_FLOAT_SWITCH, reinterpret_cast<LPFIBER_START_ROUTINE>(fn), arg);
}

void fiber_switch(void* to) { SwitchToFiber(to); }
void fiber_delete(void* fiber) { DeleteFiber(fiber); }
bool fiber_release_thread() { return ConvertFiberToThread() != 0; }  // libera o que ConvertThreadToFiberEx alocou

} // namespace bs::host
