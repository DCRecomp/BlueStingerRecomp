// N9-L (L5): processos filhos do Builder no Linux (cmake, ninja, o compilador, o jogo na verificacao).
// - Sem shell: argv vai direto ao execve; o programa sem barra e procurado no PATH do ambiente do filho.
// - O filho abre um grupo de processos proprio: cancelar ou estourar o tempo mata a arvore (kill do grupo).
// - So stdin (/dev/null), stdout e stderr (o pipe) passam para o filho; o resto dos descritores fecha no exec.
// - A saida chega linha a linha em UTF-8 (byte invalido vira '?').
#include "child_process.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace bs::builder {

namespace fs = std::filesystem;

std::wstring widen(const std::string& s) {
    std::wstring w;
    for (size_t i = 0; i < s.size();) {
        const uint8_t b    = uint8_t(s[i]);
        const int     more = b < 0x80 ? 0 : b >> 5 == 6 ? 1 : b >> 4 == 14 ? 2 : b >> 3 == 30 ? 3 : -1;
        bool          ok   = more >= 0 && i + size_t(more) < s.size() + (more == 0 ? 1 : 0);
        uint32_t      c    = ok ? (more == 0 ? b : b & (0x3Fu >> more)) : 0;
        for (int k = 1; ok && k <= more; ++k) {
            const uint8_t n = uint8_t(s[i + size_t(k)]);
            if ((n & 0xC0) != 0x80) ok = false;
            c = c << 6 | (n & 63);
        }
        if (!ok) {
            w += L'\xFFFD', ++i;
            continue;
        }
        w += wchar_t(c), i += size_t(more) + 1;
    }
    return w;
}

std::string narrow(const std::wstring& w) {
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

std::string  path_text(const fs::path& p) { return p.native(); }
std::wstring path_wide(const fs::path& p) { return widen(p.native()); }
std::wstring path_wide_generic(const fs::path& p) { return widen(p.generic_string()); }

// So para exibir no log: aspas simples quando ha espaco ou caractere do shell.
std::wstring quote_arg(const std::wstring& a) {
    if (!a.empty() && a.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_=./:,+@%") ==
                          std::wstring::npos)
        return a;
    std::wstring r = L"'";
    for (wchar_t c : a) r += c == L'\'' ? std::wstring(L"'\\''") : std::wstring(1, c);
    return r + L"'";
}

std::wstring display_command(const std::vector<std::wstring>& argv) {
    std::wstring s;
    for (const auto& a : argv) s += (s.empty() ? L"" : L" ") + quote_arg(a);
    return s;
}

bool ansi_safe(const fs::path&) { return true; }

std::string error_text(const std::error_code& ec) { return ec.message() + " (" + std::to_string(ec.value()) + ")"; }

Env current_env() {
    Env env;
    for (char** e = environ; e && *e; ++e) {
        const std::string s = *e;
        const size_t      eq = s.find('=');
        if (eq != std::string::npos && eq > 0) env[widen(s.substr(0, eq))] = widen(s.substr(eq + 1));
    }
    return env;
}

// Linha de comando montada (Windows): nao existe no Linux, onde so a lista de argumentos e aceita.
ProcessResult run_process(const std::wstring&, const fs::path&, const Env&, const std::function<void(const std::string&)>&,
                          const std::atomic<bool>*, bool, unsigned) {
    return {};
}

namespace {

std::string utf8Line(std::string l) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    for (size_t i = 0; i < l.size();) {  // byte invalido vira '?'
        const uint8_t b    = uint8_t(l[i]);
        const int     more = b < 0x80 ? 0 : b >> 5 == 6 ? 1 : b >> 4 == 14 ? 2 : b >> 3 == 30 ? 3 : -1;
        bool          ok   = more >= 0 && i + size_t(more) < l.size() + (more == 0 ? 1 : 0);
        for (int k = 1; ok && k <= more; ++k) ok = (uint8_t(l[i + size_t(k)]) & 0xC0) == 0x80;
        if (!ok) {
            l[i++] = '?';
            continue;
        }
        i += size_t(more) + 1;
    }
    return l;
}

// O programa: com barra, como veio; sem barra, o primeiro executavel no PATH do ambiente do filho.
std::string resolveProgram(const std::string& prog, const std::string& pathVar) {
    if (prog.find('/') != std::string::npos) return prog;
    for (size_t a = 0; a <= pathVar.size();) {
        const size_t      b   = std::min(pathVar.find(':', a), pathVar.size());
        const std::string dir = b > a ? pathVar.substr(a, b - a) : std::string(".");
        const std::string p   = dir + "/" + prog;
        struct stat       st {};
        if (::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && ::access(p.c_str(), X_OK) == 0) return p;
        a = b + 1;
    }
    return {};
}

} // namespace

ProcessResult run_process_argv(const std::vector<std::wstring>& argvW, const fs::path& cwd, const Env& env,
                               const std::function<void(const std::string&)>& onLine, const std::atomic<bool>* cancel,
                               unsigned timeoutMs) {
    ProcessResult res;
    if (argvW.empty()) return res;
    // tudo o que o filho precisa e montado antes do fork (depois dele, so chamadas seguras para sinais)
    std::vector<std::string> args, envs;
    for (const auto& a : argvW) args.push_back(narrow(a));
    const Env& e = env.empty() ? current_env() : env;
    std::string pathVar = "/usr/local/bin:/usr/bin:/bin";
    for (const auto& [k, v] : e) {
        envs.push_back(narrow(k) + "=" + narrow(v));
        if (k == L"PATH") pathVar = narrow(v);
    }
    const std::string prog = resolveProgram(args[0], pathVar);
    if (prog.empty()) return res;
    std::vector<char*> argvC, envC;
    for (auto& a : args) argvC.push_back(a.data());
    argvC.push_back(nullptr);
    for (auto& s : envs) envC.push_back(s.data());
    envC.push_back(nullptr);
    const std::string dir = cwd.native();

    int pfd[2];
    if (::pipe2(pfd, O_CLOEXEC) != 0) return res;
    const int nul = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (nul < 0) {
        ::close(pfd[0]), ::close(pfd[1]);
        return res;
    }
    long maxFd = ::sysconf(_SC_OPEN_MAX);  // so para o recurso sem close_range
    if (maxFd < 0) maxFd = 1 << 20;
    int errp[2];  // o filho avisa por aqui se o exec falhar (fecha sozinho no exec que deu certo)
    if (::pipe2(errp, O_CLOEXEC) != 0) {
        ::close(pfd[0]), ::close(pfd[1]), ::close(nul);
        return res;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pfd[0]), ::close(pfd[1]), ::close(nul), ::close(errp[0]), ::close(errp[1]);
        return res;
    }
    if (pid == 0) {
        ::setpgid(0, 0);
        ::signal(SIGINT, SIG_DFL), ::signal(SIGTERM, SIG_DFL), ::signal(SIGPIPE, SIG_DFL);
        sigset_t none;
        sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr);
        if (::dup2(nul, 0) < 0 || ::dup2(pfd[1], 1) < 0 || ::dup2(pfd[1], 2) < 0) ::_exit(127);
        // o aviso de falha do exec vai para o descritor 3 (ainda fechado no exec que der certo); todos os outros herdados (log,
        // trava, qualquer numero: revisao Codex 20261103-105354) fecham aqui, pelo close_range do kernel (5.9+) ou um a um
        int keep = errp[1];
        if (keep != 3) {
            if (::dup3(keep, 3, O_CLOEXEC) < 0) ::_exit(127);
            keep = 3;
        }
        if (::syscall(SYS_close_range, 4u, ~0u, 0u) != 0)
            for (long fd = 4; fd < maxFd; ++fd) ::close(int(fd));
        if (!dir.empty() && ::chdir(dir.c_str()) != 0) {
            const int err = errno;
            (void)!::write(keep, &err, sizeof err);
            ::_exit(127);
        }
        ::execve(prog.c_str(), argvC.data(), envC.data());
        const int err = errno;
        (void)!::write(keep, &err, sizeof err);
        ::_exit(127);
    }
    ::setpgid(pid, pid);  // as duas pontas: o grupo existe antes de qualquer kill
    ::close(pfd[1]), ::close(nul), ::close(errp[1]);
    int  childErr = 0;
    const bool execFailed = ::read(errp[0], &childErr, sizeof childErr) == ssize_t(sizeof childErr);
    ::close(errp[0]);
    if (execFailed) {
        int st = 0;
        ::waitpid(pid, &st, 0);
        ::close(pfd[0]);
        return res;  // started = false
    }
    res.started = true;

    std::string     pending;
    char            buf[8192];
    const auto      t0     = std::chrono::steady_clock::now();
    bool            exited = false, killed = false, eof = false;
    int             status = 0;
    auto emit = [&](bool all) {
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            if (onLine) onLine(utf8Line(pending.substr(0, pos)));
            pending.erase(0, pos + 1);
        }
        if (all && !pending.empty()) {
            if (onLine) onLine(utf8Line(pending));
            pending.clear();
        }
    };
    for (;;) {
        if (!eof) {
            pollfd p{pfd[0], POLLIN, 0};
            const int r = ::poll(&p, 1, exited ? 0 : 15);
            if (r > 0) {
                const ssize_t n = ::read(pfd[0], buf, sizeof buf);
                if (n > 0) {
                    pending.append(buf, size_t(n));
                    emit(false);
                    continue;
                }
                if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) eof = true;
            }
        }
        if (exited) break;  // saida do filho drenada; netos que herdaram o pipe nao seguram o Builder
        if (!killed) {
            if (cancel && cancel->load()) res.cancelled = true;
            else if (timeoutMs && std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(timeoutMs)) res.timedOut = true;
            if (res.cancelled || res.timedOut) {
                ::kill(-pid, SIGKILL);  // a arvore inteira (o grupo); ao menos o filho direto
                ::kill(pid, SIGKILL);
                killed = true;
            }
        }
        // a saida e vista sem colher o filho (WNOWAIT): enquanto ele e zumbi o numero do grupo nao pode ser reusado
        siginfo_t si{};
        if (::waitid(P_PID, id_t(pid), &si, WEXITED | WNOHANG | WNOWAIT) == 0 ? si.si_pid == pid : errno != EINTR) exited = true;
        if (eof && !exited) ::usleep(15000);  // saida fechada, filho ainda vivo: o prazo e o cancelamento continuam valendo
    }
    // o que sobrou do grupo (netos criados depois da primeira tentativa, ou deixados em segundo plano por um filho que
    // terminou normalmente) sai sempre, como o job do Windows ao fechar (revisao Codex 20261103-105354), antes de colher o
    // filho; o codigo de saida e o do filho direto
    ::kill(-pid, SIGKILL);
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    ::close(pfd[0]);
    emit(true);
    res.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
    return res;
}

} // namespace bs::builder
