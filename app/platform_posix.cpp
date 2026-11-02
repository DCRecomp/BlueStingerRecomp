#include "platform.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>

namespace bs::platform {

namespace fs = std::filesystem;

namespace {

fs::path homeDir() {
    const char* h = std::getenv("HOME");
    return h && *h ? fs::path(h) : fs::path("/tmp");
}

fs::path xdg(const char* var, const char* fallback) {
    const char* v = std::getenv(var);
    return v && *v == '/' ? fs::path(v) : homeDir() / fallback;
}

} // namespace

std::string to_utf8(const std::wstring& w) {
    std::string s;
    for (const wchar_t wc : w) {
        const uint32_t c = uint32_t(wc);
        if (c < 0x80) s += char(c);
        else if (c < 0x800) s += char(0xC0 | c >> 6), s += char(0x80 | (c & 63));
        else if (c < 0x10000) s += char(0xE0 | c >> 12), s += char(0x80 | (c >> 6 & 63)), s += char(0x80 | (c & 63));
        else s += char(0xF0 | c >> 18), s += char(0x80 | (c >> 12 & 63)), s += char(0x80 | (c >> 6 & 63)), s += char(0x80 | (c & 63));
    }
    return s;
}

std::wstring from_utf8(const std::string& s) {
    std::wstring w;
    for (size_t i = 0; i < s.size();) {
        const uint8_t b    = uint8_t(s[i]);
        const int     more = b < 0x80 ? 0 : b >> 5 == 6 ? 1 : b >> 4 == 14 ? 2 : b >> 3 == 30 ? 3 : -1;
        if (more < 0 || i + size_t(more) >= s.size() + (more == 0)) {  // byte invalido ou sequencia cortada
            w += L'\xFFFD', ++i;
            continue;
        }
        uint32_t c = more == 0 ? b : b & (0x3Fu >> more);
        for (int k = 1; k <= more; ++k) c = c << 6 | (uint8_t(s[i + size_t(k)]) & 63);
        w += wchar_t(c), i += size_t(more) + 1;
    }
    return w;
}

std::vector<std::wstring> command_line() {
    std::ifstream in("/proc/self/cmdline", std::ios::binary);
    const std::string          raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::wstring>  args;
    for (size_t a = 0; a < raw.size();) {
        const size_t b = std::min(raw.find('\0', a), raw.size());
        args.push_back(from_utf8(raw.substr(a, b - a)));
        a = b + 1;
    }
    if (!args.empty()) args.erase(args.begin());
    return args;
}

void message_box(const std::string& title, const std::string& text, BoxKind kind) {
    const SDL_MessageBoxFlags f = kind == BoxKind::Error     ? SDL_MESSAGEBOX_ERROR
                                  : kind == BoxKind::Warning ? SDL_MESSAGEBOX_WARNING
                                                             : SDL_MESSAGEBOX_INFORMATION;
    if (!SDL_ShowSimpleMessageBox(f, title.c_str(), text.c_str(), nullptr)) std::fprintf(stderr, "%s: %s\n", title.c_str(), text.c_str());
}

fs::path exe_dir() {
    std::error_code ec;
    const fs::path  p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : p.parent_path();
}

fs::path user_data_dir() { return xdg("XDG_CONFIG_HOME", ".config") / "BlueStingerRecomp"; }

fs::path default_saves_dir() { return xdg("XDG_DATA_HOME", ".local/share") / "DC-Recomp" / "Blue Stinger" / "saves"; }

uint32_t process_id() { return uint32_t(getpid()); }

std::optional<std::wstring> env(const wchar_t* name) {
    const char* v = std::getenv(to_utf8(name).c_str());
    if (!v) return std::nullopt;
    return from_utf8(v);
}

std::FILE* open_for_write(const fs::path& p) { return std::fopen(p.c_str(), "w"); }

void redirect_output(const fs::path& log) {
    // Terminal, arquivo ou pipe: quem abriu o jogo quer ver a saida. So /dev/null (atalho da area de trabalho) vai para o log
    struct stat err {};
    if (fstat(STDERR_FILENO, &err) == 0 && (!S_ISCHR(err.st_mode) || isatty(STDERR_FILENO))) return;
    if (FILE* t = std::fopen(log.c_str(), "w")) std::fclose(t);
    const bool errOk = std::freopen(log.c_str(), "a", stderr) != nullptr;
    const bool outOk = std::freopen(log.c_str(), "a", stdout) != nullptr;
    if (errOk) std::setvbuf(stderr, nullptr, _IONBF, 0);
    if (outOk) std::setvbuf(stdout, nullptr, _IONBF, 0);
}

void log_runtime_failures() {}

} // namespace bs::platform
