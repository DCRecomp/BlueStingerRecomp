#include "platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")  // SHGetKnownFolderPath (pasta dos saves)
#pragma comment(lib, "ole32.lib")    // CoTaskMemFree

#include <cstdlib>

namespace bs::platform {

namespace fs = std::filesystem;

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring from_utf8(const std::string& s) {
    if (s.empty()) return {};
    const int    n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// Argumentos em UTF-16 (o argv de main usa a pagina de codigo ANSI e perde caracteres).
std::vector<std::wstring> command_line() {
    int     argc  = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv) return {};
    std::vector<std::wstring> args(wargv + 1, wargv + argc);
    LocalFree(wargv);
    return args;
}

// Caixa de mensagem nativa (antes do SDL ou se ele falhar), em UTF-16.
void message_box(const std::string& title, const std::string& text, BoxKind kind) {
    const UINT icon = kind == BoxKind::Error ? MB_ICONERROR : kind == BoxKind::Warning ? MB_ICONWARNING : MB_ICONINFORMATION;
    MessageBoxW(nullptr, from_utf8(text).c_str(), from_utf8(title).c_str(), icon | MB_OK);
}

fs::path exe_dir() {
    std::wstring buf(32768, L'\0');
    const DWORD  n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    buf.resize(n);
    return fs::path(buf).parent_path();
}

fs::path user_data_dir() {
    const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
    return local ? fs::path(local) / L"BlueStingerRecomp" : exe_dir();
}

fs::path default_saves_dir() {
    PWSTR    docs = nullptr;
    fs::path root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &docs)) && docs)
        root = fs::path(docs) / L"DC-Recomp" / L"Blue Stinger" / L"saves";
    CoTaskMemFree(docs);
    return root;
}

uint32_t process_id() { return uint32_t(GetCurrentProcessId()); }

std::optional<std::wstring> env(const wchar_t* name) {
    const wchar_t* v = _wgetenv(name);
    if (!v) return std::nullopt;
    return std::wstring(v);
}

std::FILE* open_for_write(const fs::path& p) { return _wfopen(p.c_str(), L"w"); }

// Sem console (subsistema grafico) e sem redirecionamento pelo processo pai: stdout e
// stderr vao para bluestinger.log.
void redirect_output(const fs::path& log) {
    const HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) return;  // saida redirecionada (testes, scripts)
    // o arquivo e zerado uma vez e as duas streams abrem em modo de acrescimo: cada escrita
    // vai para o fim do arquivo, entao stdout e stderr nao se sobrescrevem (no subsistema
    // grafico as streams comecam sem descritor; as duas precisam ser reabertas)
    // Compartilhado (_SH_DENYNO): as variantes _s (_wfreopen_s) abrem sem compartilhar, entao
    // a segunda abertura (stdout) falhava, deixava a stream invalida e o primeiro printf
    // abortava o processo por parametro invalido. Tambem permite ler o log com o jogo aberto.
    FILE* t = _wfsopen(log.c_str(), L"w", _SH_DENYNO);  // zera o arquivo
    if (!t) return;
    std::fclose(t);
    // _wfreopen (sem _s) reassocia as proprias streams e abre compartilhado, como fopen
    const bool errOk = _wfreopen(log.c_str(), L"a", stderr) != nullptr;
    const bool outOk = _wfreopen(log.c_str(), L"a", stdout) != nullptr;
    if (errOk) std::setvbuf(stderr, nullptr, _IONBF, 0);
    if (outOk) std::setvbuf(stdout, nullptr, _IONBF, 0);
}

void log_runtime_failures() {
    _set_invalid_parameter_handler([](const wchar_t* expr, const wchar_t* func, const wchar_t* file, unsigned line, uintptr_t) {
        std::fprintf(stderr, "ERRO FATAL: parametro invalido no CRT: %s em %s (%s:%u)\n", to_utf8(expr ? expr : L"?").c_str(),
                     to_utf8(func ? func : L"?").c_str(), to_utf8(file ? file : L"?").c_str(), line);
        std::fflush(stderr);
        std::abort();
    });
}

} // namespace bs::platform
