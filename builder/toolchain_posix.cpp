// N9-L (L5): o compilador do jogo no Linux.
// - Padrao (D-L2): o compilador portatil do pacote, em <Builder>/toolchain (ou --compilador <pasta>):
//     bs-toolchain.cmake   arquivo de toolchain do CMake (escolhe o compilador e o alvo de glibc antiga)
//     cmake/bin/cmake      CMake do pacote
//     bin/ninja            Ninja do pacote
//     VERSAO.txt           o que e cada peca (vai para o VERSAO.txt do jogo)
//   Todos compilam com o mesmo compilador: a prova de fidelidade (A3) vale para todos.
// - --compilador-sistema: clang, clang++, cmake (>= 3.25) e ninja do PATH, por conta do usuario (fora da prova).
// Cada ferramenta e executada uma vez (--version): pasta sem permissao de execucao, arquitetura errada ou arquivo
// corrompido aparecem aqui, com o nome da peca, e nao no meio da compilacao.
#include "toolchain.h"

#include "builder_platform.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace bs::builder {

namespace fs = std::filesystem;

const char* const kToolchainHelp =
    "O Builder do Linux traz o proprio compilador na pasta toolchain/ (reinstale o pacote com ./instalar.sh se ela "
    "faltar ou estiver incompleta). Para usar o compilador do sistema, rode com --compilador-sistema; ele precisa de "
    "clang, clang++, cmake 3.25 ou mais novo e ninja (Ubuntu: sudo apt install clang lld cmake ninja-build).";

namespace {

bool isExecutable(const fs::path& p) {
    struct stat st {};
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && ::access(p.c_str(), X_OK) == 0;
}

fs::path findOnPath(const Env& env, const char* exe) {
    const auto it = env.find(L"PATH");
    const std::string pathVar = it == env.end() ? std::string("/usr/local/bin:/usr/bin:/bin") : narrow(it->second);
    for (size_t a = 0; a <= pathVar.size();) {
        const size_t b = std::min(pathVar.find(':', a), pathVar.size());
        if (b > a) {
            const fs::path p = fs::path(pathVar.substr(a, b - a)) / exe;
            if (isExecutable(p)) return p;
        }
        a = b + 1;
    }
    return {};
}

// primeira linha de "<ferramenta> --version"; vazio se nao rodou
std::string firstLine(const fs::path& tool, const Env& env) {
    std::string first;
    const auto  r = run_process_argv({path_wide(tool), L"--version"}, {}, env, [&](const std::string& l) {
        if (first.empty() && !l.empty()) first = l;
    }, nullptr, 60000);
    return r.started && r.exitCode == 0 ? first : std::string();
}

// "cmake version 3.28.3" -> 3.28 (0 se nao der para ler)
int cmakeMinor(const std::string& line, int& major) {
    major = 0;
    int minor = 0;
    const size_t p = line.find("version ");
    if (p == std::string::npos || std::sscanf(line.c_str() + p + 8, "%d.%d", &major, &minor) != 2) return 0;
    return minor;
}

std::string readAll(const fs::path& p) {
    std::ifstream      in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

} // namespace

Toolchain find_toolchain(const ToolchainOptions& opt, const std::function<void(const std::string&)>& log) {
    auto say = [&](const std::string& s) {
        if (log) log(s);
    };
    Toolchain       tc;
    std::error_code ec;
    tc.env = current_env();

    if (!opt.systemToolchain) {
        const fs::path dir  = opt.toolchainDir.empty() ? plat::exe_dir() / "toolchain" : fs::absolute(opt.toolchainDir, ec);
        const fs::path file = dir / "bs-toolchain.cmake";
        tc.installRoot      = dir;
        tc.cmake            = dir / "cmake" / "bin" / "cmake";
        tc.ninja            = dir / "bin" / "ninja";
        std::string missing;
        for (const fs::path& p : {file, dir / "VERSAO.txt"})
            if (!fs::is_regular_file(p, ec)) missing += "\n  " + path_text(p);
        for (const fs::path& p : {tc.cmake, tc.ninja})
            if (!isExecutable(p)) missing += "\n  " + path_text(p) + (fs::exists(p, ec) ? " (sem permissao de execucao)" : "");
        if (!missing.empty())
            throw std::runtime_error("compilador portatil incompleto em " + path_text(dir) + "; falta:" + missing + "\n\n" +
                                     kToolchainHelp);
        const std::string cm = firstLine(tc.cmake, tc.env), nj = firstLine(tc.ninja, tc.env);
        if (cm.empty()) throw std::runtime_error("o CMake do pacote nao executa: " + path_text(tc.cmake) + "\n\n" + kToolchainHelp);
        if (nj.empty()) throw std::runtime_error("o Ninja do pacote nao executa: " + path_text(tc.ninja) + "\n\n" + kToolchainHelp);
        const std::string versao = readAll(dir / "VERSAO.txt");
        tc.cmakeArgs   = {L"-DCMAKE_TOOLCHAIN_FILE=" + plat::cmake_path(file)};
        // o build incremental cai se o compilador do pacote mudar (outra versao instalada no mesmo lugar)
        tc.stamp       = path_text(dir) + "\n" + versao;
        tc.description = "Compilador: portatil do Builder (" + path_text(dir) + ")\n" + versao +
                         (versao.empty() || versao.back() == '\n' ? "" : "\n") + "CMake: " + cm + "\nNinja: " + nj + "\n";
        say("compilador portatil: " + path_text(dir));
        say("cmake: " + cm + "; ninja " + nj);
        return tc;
    }

    // compilador do sistema (pedido explicito)
    tc.installRoot = "/";
    tc.cmake       = findOnPath(tc.env, "cmake");
    tc.ninja       = findOnPath(tc.env, "ninja");
    const fs::path cc = findOnPath(tc.env, "clang"), cxx = findOnPath(tc.env, "clang++"), lld = findOnPath(tc.env, "ld.lld");
    std::string    missing;
    if (tc.cmake.empty()) missing += " cmake";
    if (tc.ninja.empty()) missing += " ninja";
    if (cc.empty()) missing += " clang";
    if (cxx.empty()) missing += " clang++";
    if (!missing.empty())
        throw std::runtime_error("--compilador-sistema: faltam no PATH:" + missing + "\n\n" + kToolchainHelp);
    // cada um vai ao CMake pelo proprio caminho (installRoot e "/"): conferidos aqui (revisao Codex 20261103-143212)
    for (const fs::path& p : {tc.cmake, tc.ninja, cc, cxx})
        if (const std::string m = plat::build_path_problem(p); !m.empty()) throw std::runtime_error("--compilador-sistema: " + m);
    const std::string cm = firstLine(tc.cmake, tc.env), nj = firstLine(tc.ninja, tc.env), cv = firstLine(cxx, tc.env);
    int major = 0;
    const int minor = cmakeMinor(cm, major);
    if (cm.empty() || major < 3 || (major == 3 && minor < 25))
        throw std::runtime_error("--compilador-sistema: e preciso o CMake 3.25 ou mais novo (" +
                                 (cm.empty() ? std::string("nao executa") : cm) + ")\n\n" + kToolchainHelp);
    if (nj.empty() || cv.empty()) throw std::runtime_error("--compilador-sistema: ninja ou clang++ nao executa\n\n" + std::string(kToolchainHelp));
    tc.cmakeArgs = {L"-DCMAKE_C_COMPILER=" + plat::cmake_path(cc), L"-DCMAKE_CXX_COMPILER=" + plat::cmake_path(cxx)};
    if (!lld.empty()) tc.cmakeArgs.push_back(L"-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld");
    tc.stamp       = path_text(cxx) + "\n" + cv;
    tc.description = "Compilador: do sistema (--compilador-sistema), " + cv + "\nCMake: " + cm + "\nNinja: " + nj + "\n";
    say("compilador do sistema: " + cv);
    say("cmake: " + cm + "; ninja " + nj);
    return tc;
}

} // namespace bs::builder
