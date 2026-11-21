// v0.9.7-5: compilador portatil do Windows (Zig/MinGW). O printf do jogo do MSVC e o do UCRT do Windows com as opcoes
// do cabecalho do MSVC, entre elas o arredondamento padrao (_CRT_INTERNAL_PRINTF_STANDARD_ROUNDING), que respeita o modo de
// arredondamento corrente: na thread do jogo e o do FPSCR do Dreamcast (para zero), e 711,9 sai "711.8999" com %.4f. O
// mingw-w64 chama a mesma funcao do UCRT com as opcoes 0 (arredondamento antigo, sempre ao mais proximo) e o mesmo valor
// sai "711.9000". O estado do jogo e igual; os diagnosticos em texto (estado.tsv, musica.tsv das referencias A3) nao.
// Estas definicoes (objetos antes das bibliotecas .a do compilador) chamam o UCRT com as opcoes do MSVC. So no Zig/MinGW.
#if defined(__MINGW32__)

#include <stdarg.h>
#include <stdio.h>

namespace {
// as do MSVC (corecrt_stdio_config.h do SDK do Windows): especificadores largos antigos + arredondamento padrao
constexpr unsigned long long kOpcoes          = 0x0004ull | 0x0020ull;
constexpr unsigned long long kSnprintfPadrao  = 0x0002ull;  // _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR
constexpr unsigned long long kVsprintfTermina = 0x0001ull;  // _CRT_INTERNAL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION
} // namespace

extern "C" {

int __cdecl vfprintf(FILE* f, const char* fmt, va_list ap) { return __stdio_common_vfprintf(kOpcoes, f, fmt, nullptr, ap); }

int __cdecl fprintf(FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __stdio_common_vfprintf(kOpcoes, f, fmt, nullptr, ap);
    va_end(ap);
    return r;
}

int __cdecl vprintf(const char* fmt, va_list ap) { return __stdio_common_vfprintf(kOpcoes, stdout, fmt, nullptr, ap); }

int __cdecl printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __stdio_common_vfprintf(kOpcoes, stdout, fmt, nullptr, ap);
    va_end(ap);
    return r;
}

int __cdecl vsnprintf(char* b, size_t n, const char* fmt, va_list ap) {
    const int r = __stdio_common_vsprintf(kOpcoes | kSnprintfPadrao, b, n, fmt, nullptr, ap);
    return r < 0 ? -1 : r;
}

int __cdecl snprintf(char* b, size_t n, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = vsnprintf(b, n, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl vsprintf(char* b, const char* fmt, va_list ap) {
    const int r = __stdio_common_vsprintf(kOpcoes | kVsprintfTermina, b, size_t(-1), fmt, nullptr, ap);
    return r < 0 ? -1 : r;
}

int __cdecl sprintf(char* b, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = vsprintf(b, fmt, ap);
    va_end(ap);
    return r;
}

} // extern "C"

#endif
