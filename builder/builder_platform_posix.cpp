// N9-L (L5): o Builder no Linux.
// - Nomes com caixa (o sistema de arquivos diferencia), caminhos UTF-8, sem limite pratico de comprimento.
// - Cache de compilacao em $BS_BUILDER_CACHE, $XDG_CACHE_HOME/BlueStingerRecomp (~/.cache) ou na pasta de trabalho.
// - Trava da saida por flock (solta sozinha se o processo morrer).
// - Kit: SDL3 pre-compilado (third_party/SDL3-linux, D-L2), FreeType e toml++ em fontes, Noto Sans e pl_mpeg.
#include "builder_platform.h"

#include "child_process.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <random>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace bs::builder::plat {

namespace {

std::atomic<bool>* g_cancel = nullptr;

void onSignal(int) {
    if (g_cancel) g_cancel->store(true);
}

fs::path homeDir() {
    const char* h = std::getenv("HOME");
    return h && *h == '/' ? fs::path(h) : fs::path("/tmp");
}

} // namespace

uint64_t now_ticks() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return uint64_t(ts.tv_sec) * 10'000'000u + uint64_t(ts.tv_nsec) / 100u;
}

std::wstring fold_name(const std::wstring& s) { return s; }

fs::path to_path(const std::wstring& s) { return fs::path(narrow(s)); }

std::error_code probe_writable(const fs::path& dir) {
    std::random_device rd;
    char               sfx[24];
    std::snprintf(sfx, sizeof sfx, "%08x%08x", rd(), rd());
    const fs::path p  = dir / (std::string(".bs-probe-") + sfx);
    const int      fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) return std::error_code(errno, std::generic_category());
    ::close(fd);
    ::unlink(p.c_str());
    return {};
}

DirLock::~DirLock() {
    if (h_ != -1) ::close(int(h_));  // fechar solta o flock
}

bool DirLock::acquire(const fs::path& file) {
    const int fd = ::open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return false;
    }
    h_ = fd;
    return true;
}

bool create_dir_exclusive(const fs::path& p, std::error_code& ec) {
    ec.clear();
    if (::mkdir(p.c_str(), 0755) == 0) return true;
    if (errno != EEXIST) ec = std::error_code(errno, std::generic_category());
    return false;
}

ErrorKind error_kind_of(const std::error_code& ec, ErrorKind otherwise) {
    if (ec == std::errc::no_space_on_device || ec.value() == EDQUOT) return ErrorKind::NoSpace;
    if (ec == std::errc::permission_denied || ec == std::errc::read_only_file_system ||
        ec == std::errc::operation_not_permitted)
        return ErrorKind::NoPermission;
    return otherwise;
}

uint32_t cache_key(const fs::path& out) {
    uint32_t h = 2166136261u;  // FNV-1a sobre os bytes (o sistema de arquivos diferencia maiusculas)
    for (unsigned char c : out.native()) h = (h ^ uint32_t(c)) * 16777619u;
    return h;
}

std::vector<fs::path> cache_roots(const fs::path& work) {
    std::vector<fs::path> roots;
    if (const char* v = std::getenv("BS_BUILDER_CACHE"); v && *v == '/') roots.push_back(v);
    if (const char* v = std::getenv("XDG_CACHE_HOME"); v && *v == '/') roots.push_back(fs::path(v) / "BlueStingerRecomp");
    else roots.push_back(homeDir() / ".cache" / "BlueStingerRecomp");
    roots.push_back(work);
    return roots;
}

const char* cache_hint() {
    return "nenhuma pasta de cache de compilacao utilizavel (sem permissao de gravacao em ~/.cache nem na pasta de "
           "saida). Defina BS_BUILDER_CACHE com uma pasta gravavel, por exemplo ~/bs-cache.";
}

const char* example_out_dir() { return "~/Jogos/BlueStinger"; }

// O CMake nao aceita, no kit, no compilador e no cache (N9-L L5, B6): ';' (separa listas: o caminho vira dois), '\' (vira
// '/': o CMake do pacote deixa de achar os proprios modulos, CMAKE_ROOT), '"' (o caminho do arquivo de toolchain vai sem
// escape para o CMakeSystem.cmake) e '$' (referencia de variavel nos arquivos que ele gera).
bool build_path_ok(const fs::path& p) { return p.native().find_first_of(";\\\"$") == std::string::npos; }
std::string build_path_problem(const fs::path& p) {
    if (build_path_ok(p)) return {};
    return "o caminho " + p.native() + " tem ; \\ \" ou $, que o CMake nao aceita em caminhos. Instale (ou extraia) o Builder "
           "numa pasta sem eles";
}
size_t max_out_path() { return 3000; }  // PATH_MAX e 4096; o cache e os objetos ficam abaixo disso
size_t max_cache_path() { return 3000; }
size_t max_direct_kit() { return 3000; }

std::wstring cmake_path(const fs::path& p) { return path_wide(p); }

const wchar_t* game_binary_built() { return L"bluestinger"; }
const wchar_t* game_binary_installed() { return L"BlueStinger"; }
const wchar_t* readme_in_kit() { return L"tools/dist/linux/LEIA-ME-JOGO.txt"; }

bool is_package_file(const fs::path& rel) {
    if (rel.begin() == rel.end()) return false;
    const std::string first = rel.begin()->native();
    return first == "BlueStinger" || first == "LEIA-ME.txt" || first == "VERSAO.txt" || first == "licencas";
}

std::string check_kit_platform(const fs::path& kit, const fs::path& sdl3) {
    std::error_code ec;
    for (const char* f : {"CMakeLists.txt", "LICENSE", "config/bluestinger_us.toml", "app/CMakeLists.txt",
                          "runtime/CMakeLists.txt", "tools/extractor/CMakeLists.txt", "tools/dist/linux/LEIA-ME-JOGO.txt",
                          "tools/dcmap/CMakeLists.txt", "native/CMakeLists.txt", "legacy/CMakeLists.txt",
                          "transitional/CMakeLists.txt", "native/text/fonts_embed.S.in",
                          "third_party/fonts/noto-sans/NotoSans-Regular.ttf",
                          "third_party/fonts/noto-sans/NotoSans-SemiBold.ttf", "third_party/fonts/noto-sans/OFL.txt",
                          "third_party/pl_mpeg/pl_mpeg.h", "third_party/freetype/CMakeLists.txt",
                          "third_party/freetype/LICENSE.TXT"})
        if (!fs::is_regular_file(kit / f, ec)) return "kit de fontes incompleto: falta " + path_text(kit / f);
    for (const char* f : {"lib/cmake/SDL3/SDL3Config.cmake", "lib/libSDL3.a", "LICENSE.txt"})
        if (!fs::is_regular_file(sdl3 / f, ec))
            return "SDL3 pre-compilado incompleto: falta " + path_text(sdl3 / f) + " (reinstale o Builder)";
    return {};
}

fs::path default_sdl3(const fs::path& kit) { return kit / "third_party" / "SDL3-linux"; }

std::vector<std::wstring> dependency_args(const fs::path& kit, const fs::path& sdl3, const fs::path& toml) {
    return {L"-DBS_SDL3_PREBUILT=" + cmake_path(sdl3), L"-DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=" + cmake_path(toml),
            L"-DFETCHCONTENT_SOURCE_DIR_FREETYPE=" + cmake_path(kit / "third_party" / "freetype")};
}

std::vector<std::pair<fs::path, std::wstring>> extra_licenses(const fs::path& kit, const fs::path&) {
    return {{kit / "third_party" / "freetype" / "LICENSE.TXT", L"FreeType-LICENSE.txt"},
            {kit / "third_party" / "freetype" / "docs" / "FTL.TXT", L"FreeType-FTL.txt"},
            {kit / "third_party" / "fonts" / "noto-sans" / "OFL.txt", L"NotoSans-OFL-1.1.txt"},
            {kit / "third_party" / "pl_mpeg" / "LICENSE", L"pl_mpeg-MIT.txt"},
            {kit / "third_party" / "dr_libs" / "LICENSE", L"dr_flac-MIT0.txt"},
            {kit / "third_party" / "xiph" / "libogg" / "COPYING", L"libogg-BSD.txt"},
            {kit / "third_party" / "xiph" / "libvorbis" / "COPYING", L"libvorbis-BSD.txt"},
            {kit / "third_party" / "vulkan" / "Apache-2.0.txt", L"Vulkan-Headers-Apache-2.0.txt"}};
}

void make_executable(const fs::path& p) {
    std::error_code ec;
    fs::permissions(p, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
}

void toolchain_env(Env& env, const fs::path& cache) {
    // cache = <raiz>/build/<chave>: o Zig guarda o libc++ e os objetos da glibc em <raiz>/zig (compartilhado entre saidas).
    // So <raiz>/build/<chave> passou na prova de gravacao: <raiz>/zig e criada e provada aqui; sem gravacao nela, o cache do
    // Zig fica dentro do de compilacao (revisao Codex 20261103-143212). Variaveis ZIG_* do usuario valem como estao.
    fs::path        zig = cache.parent_path().parent_path() / "zig";
    std::error_code ec;
    fs::create_directories(zig, ec);
    if (ec || probe_writable(zig)) zig = cache / "zig";
    if (!env.count(L"ZIG_GLOBAL_CACHE_DIR")) env[L"ZIG_GLOBAL_CACHE_DIR"] = path_wide(zig);
    if (!env.count(L"ZIG_LOCAL_CACHE_DIR")) env[L"ZIG_LOCAL_CACHE_DIR"] = env[L"ZIG_GLOBAL_CACHE_DIR"];
}

bool supports_project() { return false; }

std::vector<std::wstring> cli_args(int argc, char** argv) {
    std::vector<std::wstring> a;
    for (int i = 0; i < argc; ++i) a.push_back(widen(argv[i]));
    return a;
}

void cli_on_interrupt(std::atomic<bool>* cancel) {
    g_cancel = cancel;
    struct sigaction sa {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGHUP, &sa, nullptr);
    ::signal(SIGPIPE, SIG_IGN);  // o log num pipe fechado nao derruba o Builder no meio da troca
}

fs::path exe_dir() {
    std::error_code ec;
    const fs::path  p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : p.parent_path();
}

bool        cli_toolchain_options() { return true; }
const char* cli_usage_extra() {
    return "  [--compilador <pasta>] (padrao: toolchain/ ao lado do bs-builder) | --compilador-sistema\n";
}

bool env_present(const wchar_t* name) { return std::getenv(narrow(name).c_str()) != nullptr; }

} // namespace bs::builder::plat
