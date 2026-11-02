// N9-L: troca de contexto em x86-64 System V. Como FIBER_FLAG_FLOAT_SWITCH no Windows, preserva o MXCSR e a palavra de controle
// do x87. Nesta ABI nenhum XMM e preservado entre chamadas, entao nao ha o que salvar neles. O ucontext ficou de fora porque faz
// uma chamada de sistema a cada troca.
#include "cpu/host_fiber.h"

#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>

#if !defined(__x86_64__)
#error "host_fiber_posix.cpp: so x86-64"
#endif

extern "C" void bs_fiber_swap(void** save_sp, void* load_sp);
__asm__(
    ".text\n"
    ".p2align 4\n"
    ".globl bs_fiber_swap\n"
    ".hidden bs_fiber_swap\n"
    ".type bs_fiber_swap,@function\n"
    "bs_fiber_swap:\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    subq $8, %rsp\n"
    "    stmxcsr (%rsp)\n"
    "    fnstcw 4(%rsp)\n"
    "    movq %rsp, (%rdi)\n"
    "    movq %rsi, %rsp\n"
    "    ldmxcsr (%rsp)\n"
    "    fldcw 4(%rsp)\n"
    "    addq $8, %rsp\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %rbx\n"
    "    popq %rbp\n"
    "    ret\n"
    ".size bs_fiber_swap,.-bs_fiber_swap\n");

namespace bs::host {
namespace {

struct Fiber {
    void*   sp    = nullptr;
    void*   map   = nullptr;  // pilha mapeada (a thread adotada usa a dela)
    size_t  bytes = 0;
    FiberFn fn    = nullptr;
    void*   arg   = nullptr;
};

thread_local Fiber* t_current = nullptr;

void entry() {
    Fiber* f = t_current;
    f->fn(f->arg);
    std::abort();  // o contrato: uma fibra termina trocando para outra
}

} // namespace

void* fiber_adopt(bool* adopted, uint32_t*) {
    if (t_current) {
        *adopted = false;
        return t_current;
    }
    t_current = new Fiber;
    *adopted  = true;
    return t_current;
}

void* fiber_create(size_t stack_bytes, FiberFn fn, void* arg) {
    const size_t page  = size_t(sysconf(_SC_PAGESIZE));
    const size_t bytes = (stack_bytes + page - 1) / page * page + page;  // + pagina de guarda, no fim baixo
    void*        m     = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (m == MAP_FAILED) return nullptr;
    if (mprotect(m, page, PROT_NONE) != 0) {
        munmap(m, bytes);
        return nullptr;
    }
    auto* f = new Fiber;
    f->map = m, f->bytes = bytes, f->fn = fn, f->arg = arg;
    // Quadro que bs_fiber_swap desempilha. Ao entrar em entry(), rsp fica como depois de um call (8 mod 16), apontando para um
    // retorno nulo, que encerra a pilha para quem a percorre.
    auto* top = reinterpret_cast<uint64_t*>((reinterpret_cast<uintptr_t>(m) + bytes) & ~uintptr_t(15));
    top[-1]   = 0;                                   // retorno de entry()
    top[-2]   = reinterpret_cast<uint64_t>(&entry);  // ret de bs_fiber_swap
    for (int i = 3; i <= 8; ++i) top[-i] = 0;        // rbp, rbx, r12 a r15
    uint32_t mxcsr = 0;
    uint16_t fcw   = 0;
    __asm__("stmxcsr %0\n\tfnstcw %1" : "=m"(mxcsr), "=m"(fcw));
    top[-9] = uint64_t(mxcsr) | uint64_t(fcw) << 32;
    f->sp   = top - 9;
    return f;
}

void fiber_switch(void* to) {
    Fiber* from = t_current;
    t_current   = static_cast<Fiber*>(to);
    bs_fiber_swap(&from->sp, t_current->sp);
}

void fiber_delete(void* fiber) {
    auto* f = static_cast<Fiber*>(fiber);
    if (f->map) munmap(f->map, f->bytes);
    delete f;
}

bool fiber_release_thread() {
    delete t_current;
    t_current = nullptr;
    return true;
}

} // namespace bs::host
