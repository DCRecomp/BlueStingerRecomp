// N9-L (L5): o Builder no Windows. Codigo movido de builder_core.cpp, main_cli.cpp e movie_build.cpp sem mudanca de
// comportamento (mensagens, nomes, limites de caminho e pastas de cache sao os aprovados no N8).
#include "builder_platform.h"

#include "child_process.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <random>

namespace fs = std::filesystem;

namespace bs::builder::plat {

namespace {

std::wstring envVar(const wchar_t* name) {
    wchar_t     b[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(name, b, MAX_PATH);
    return n && n < MAX_PATH ? std::wstring(b, n) : std::wstring();
}

std::atomic<bool>* g_cancel = nullptr;

BOOL WINAPI onCtrl(DWORD) {
    if (g_cancel) *g_cancel = true;
    return TRUE;
}

} // namespace

uint64_t now_ticks() {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    return (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

std::wstring fold_name(const std::wstring& in) {
    std::wstring s = in;
    CharLowerBuffW(s.data(), DWORD(s.size()));
    return s;
}

fs::path to_path(const std::wstring& s) { return fs::path(s); }

std::error_code probe_writable(const fs::path& dir) {
    std::random_device rd;
    char               sfx[24];
    std::snprintf(sfx, sizeof sfx, "%08x%08x", rd(), rd());
    const fs::path p = dir / (L".bs-probe-" + widen(sfx));
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::error_code(int(GetLastError()), std::system_category());
    CloseHandle(h);
    return {};
}

DirLock::~DirLock() {
    if (h_ != -1) CloseHandle(reinterpret_cast<HANDLE>(h_));
}

bool DirLock::acquire(const fs::path& file) {
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    h_ = reinterpret_cast<intptr_t>(h);
    return true;
}

bool create_dir_exclusive(const fs::path& p, std::error_code& ec) {
    ec.clear();
    if (CreateDirectoryW(p.c_str(), nullptr)) return true;
    if (const DWORD le = GetLastError(); le != ERROR_ALREADY_EXISTS) ec = std::error_code(int(le), std::system_category());
    return false;
}

ErrorKind error_kind_of(const std::error_code& ec, ErrorKind otherwise) {
    // codigos do Windows primeiro: o MSVC equipara ERROR_SHARING_VIOLATION (arquivo em uso, o jogo aberto) a
    // permission_denied, e isso nao e falta de permissao
    if (ec.category() == std::system_category()) {
        switch (ec.value()) {
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL: return ErrorKind::NoSpace;
        case ERROR_ACCESS_DENIED:
        case ERROR_WRITE_PROTECT: return ErrorKind::NoPermission;
        default: return otherwise;
        }
    }
    if (ec == std::errc::no_space_on_device) return ErrorKind::NoSpace;
    if (ec == std::errc::permission_denied || ec == std::errc::read_only_file_system) return ErrorKind::NoPermission;
    return otherwise;
}

uint32_t cache_key(const fs::path& out) {
    std::wstring key = out.native();
    CharLowerBuffW(key.data(), DWORD(key.size()));
    uint32_t h = 2166136261u;  // FNV-1a
    for (wchar_t c : key) h = (h ^ uint32_t(c)) * 16777619u;
    return h;
}

std::vector<fs::path> cache_roots(const fs::path& work) {
    std::vector<fs::path> roots;
    if (const auto v = envVar(L"BS_BUILDER_CACHE"); !v.empty()) roots.push_back(v);  // testes
    if (const auto v = envVar(L"LOCALAPPDATA"); !v.empty()) roots.push_back(fs::path(v) / L"BlueStingerRecomp");
    if (const auto v = envVar(L"ProgramData"); !v.empty()) roots.push_back(fs::path(v) / L"BlueStingerRecomp");
    roots.push_back(work);
    return roots;
}

const char* cache_hint() {
    return "nenhuma pasta de cache de compilacao utilizavel (o caminho precisa ser curto e so ter "
           "caracteres da pagina de codigo do Windows). Defina BS_BUILDER_CACHE com uma pasta como "
           "C:\\BSCache.";
}

const char* example_out_dir() { return "C:\\Jogos\\BlueStinger"; }

bool        build_path_ok(const fs::path& p) { return ansi_safe(p); }
std::string build_path_problem(const fs::path&) { return {}; }
size_t max_out_path() { return 200; }   // o jogo instalado e o trabalho (sem compilacao) ficam ali
size_t max_cache_path() { return 90; }  // objetos do SDL3 somam ~160 caracteres ao cache
size_t max_direct_kit() { return 90; }  // kit mais longo que isso e copiado para o cache

std::wstring cmake_path(const fs::path& p) {
    std::wstring s = p.native();
    for (auto& c : s)
        if (c == L'\\') c = L'/';
    return s;
}

const wchar_t* game_binary_built() { return L"bluestinger.exe"; }
const wchar_t* game_binary_installed() { return L"BlueStinger.exe"; }
const wchar_t* readme_in_kit() { return L"tools/dist/LEIA-ME.txt"; }

bool is_package_file(const fs::path& rel) {
    const std::wstring first = rel.begin() == rel.end() ? L"" : rel.begin()->native();
    auto               ieq   = [](const std::wstring& a, const wchar_t* b) {
        return CompareStringOrdinal(a.c_str(), int(a.size()), b, -1, TRUE) == CSTR_EQUAL;
    };
    if (ieq(first, L"BlueStinger.exe") || ieq(first, L"LEIA-ME.txt") || ieq(first, L"VERSAO.txt") ||
        ieq(first, L"licencas"))
        return true;
    std::wstring ext = rel.extension().native();
    return std::distance(rel.begin(), rel.end()) == 1 && ieq(ext, L".dll");
}

namespace {

// Um .ico que o compilador de recursos aceita: cabecalho (0, tipo 1, n > 0) e cada imagem dentro do arquivo (revisao
// Codex 2026-10-23: um arquivo vazio passava como kit completo).
bool ico_valid(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    const std::string b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto u16 = [&](size_t o) { return uint32_t(uint8_t(b[o])) | uint32_t(uint8_t(b[o + 1])) << 8; };
    auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };
    if (b.size() < 6 || u16(0) != 0 || u16(2) != 1 || u16(4) == 0 || b.size() < 6 + 16 * size_t(u16(4))) return false;
    for (size_t i = 0; i < u16(4); ++i) {
        const uint64_t size = u32(6 + 16 * i + 8), off = u32(6 + 16 * i + 12);
        if (size == 0 || off < 6 + 16 * uint64_t(u16(4)) || off + size > b.size()) return false;
    }
    return true;
}

} // namespace

// N9.2: o FreeType do kit (third_party/freetype); sem ele (o repositorio como kit), o do build, ao lado das fontes do SDL3
// (_deps/freetype-src), como o toml++ (toml_dir em builder_core.cpp)
fs::path freetype_dir(const fs::path& kit, const fs::path& sdl3) {
    std::error_code ec;
    const fs::path  inKit = kit / L"third_party" / L"freetype";
    if (fs::is_regular_file(inKit / L"CMakeLists.txt", ec)) return inKit;
    return sdl3.parent_path() / L"freetype-src";
}

std::string check_kit_platform(const fs::path& kit, const fs::path& sdl3) {
    std::error_code ec;
    for (const wchar_t* f : {L"CMakeLists.txt", L"LICENSE", L"config/bluestinger_us.toml", L"app/CMakeLists.txt",
                             L"app/bluestinger.rc.in", L"app/res/bluestinger.ico",  // icone do executavel
                             L"runtime/CMakeLists.txt", L"tools/extractor/CMakeLists.txt", L"tools/dist/LEIA-ME.txt",
                             L"tools/dcmap/CMakeLists.txt", L"native/CMakeLists.txt", L"legacy/CMakeLists.txt",
                             L"tools/build/msvc_srcdeps.cmake",
                             L"transitional/CMakeLists.txt",
                             // N9.2: o texto da interface da Remastered (FreeType + Noto Sans, como no Linux)
                             L"third_party/fonts/noto-sans/NotoSans-Regular.ttf", L"third_party/fonts/noto-sans/NotoSans-SemiBold.ttf",
                             L"third_party/fonts/noto-sans/OFL.txt"})
        if (!fs::is_regular_file(kit / f, ec)) return "kit de fontes incompleto: falta " + path_text(kit / f);
    for (const wchar_t* f : {L"app/res/bluestinger.ico", L"icone-local/bluestinger.ico"})  // o local e opcional
        if (fs::exists(kit / f, ec) && !ico_valid(kit / f)) return "icone invalido no kit: " + path_text(kit / f);
    if (!fs::is_regular_file(sdl3 / L"CMakeLists.txt", ec))
        return "fontes do SDL3 nao encontradas em " + path_text(sdl3);
    if (!fs::is_regular_file(freetype_dir(kit, sdl3) / L"CMakeLists.txt", ec))
        return "fontes do FreeType nao encontradas em " + path_text(freetype_dir(kit, sdl3));
    return {};
}

fs::path default_sdl3(const fs::path& kit) { return kit / L"third_party" / L"SDL3"; }

std::vector<std::wstring> dependency_args(const fs::path& kit, const fs::path& sdl3, const fs::path& toml) {
    return {L"-DFETCHCONTENT_SOURCE_DIR_SDL3=" + cmake_path(sdl3), L"-DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=" + cmake_path(toml),
            L"-DFETCHCONTENT_SOURCE_DIR_FREETYPE=" + cmake_path(freetype_dir(kit, sdl3))};
}

// N9.2: o FreeType (FTL) e a Noto Sans (OFL 1.1) entram no jogo do Windows pela interface da Remastered
std::vector<std::pair<fs::path, std::wstring>> extra_licenses(const fs::path& kit, const fs::path& sdl3) {
    const fs::path ft = freetype_dir(kit, sdl3);
    return {{ft / L"LICENSE.TXT", L"FreeType-LICENSE.txt"}, {ft / L"docs" / L"FTL.TXT", L"FreeType-FTL.txt"},
            {kit / L"third_party" / L"fonts" / L"noto-sans" / L"OFL.txt", L"NotoSans-OFL-1.1.txt"}};
}

void make_executable(const fs::path&) {}

void toolchain_env(Env&, const fs::path&) {}

bool supports_project() { return true; }

std::vector<std::wstring> cli_args(int, char**) {
    SetConsoleOutputCP(CP_UTF8);
    int       argc = 0;
    wchar_t** wv   = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args(wv, wv + argc);
    LocalFree(wv);
    return args;
}

void cli_on_interrupt(std::atomic<bool>* cancel) {
    g_cancel = cancel;
    SetConsoleCtrlHandler(onCtrl, TRUE);
}

fs::path exe_dir() {
    std::wstring b(32768, L'\0');
    b.resize(GetModuleFileNameW(nullptr, b.data(), DWORD(b.size())));
    return fs::path(b).parent_path();
}

bool        cli_toolchain_options() { return false; }
const char* cli_usage_extra() { return ""; }

bool env_present(const wchar_t* name) { return GetEnvironmentVariableW(name, nullptr, 0) > 0; }

} // namespace bs::builder::plat
