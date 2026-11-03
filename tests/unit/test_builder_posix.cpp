// N9-L (L5): o Builder no Linux, sem o jogo. O que o Windows prova em test_builder.cpp, com os meios do Linux:
// - processos pela lista de argumentos (sem shell: nada e interpretado), PATH, ambiente, pasta com espaco e acento,
//   saida em UTF-8, prazo e cancelamento matando a arvore (inclusive o neto), filho que fecha a saida e segue vivo,
//   descritores do Builder fora do filho;
// - troca da instalacao (arquivos do usuario ficam, rollback, troca interrompida, pasta sem permissao);
// - trava da saida, pasta sem gravacao, criacao exclusiva, nomes com acento;
// - localizador do compilador (portatil incompleto, sem permissao de execucao, falso completo, sistema sem cmake);
// - entradas ruins (CDI ausente ou invalido, projeto C++ no Linux, kit incompleto, saida ocupada) sem efeitos colaterais.
#include "test.h"

#include "builder_core.h"
#include "builder_platform.h"
#include "child_process.h"
#include "platform/install_config.h"
#include "toolchain.h"

#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace bs::builder;

namespace {

struct TempDir {
    fs::path p;
    TempDir() {
        p = fs::temp_directory_path() / ("bs-builder-linux pasta ç " + std::to_string(std::random_device{}()));
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~TempDir() {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(p, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
            fs::permissions(it->path(), fs::perms::owner_all, fs::perm_options::add, ec);
        fs::permissions(p, fs::perms::owner_all, fs::perm_options::add, ec);
        fs::remove_all(p, ec);
    }
};

void writeFile(const fs::path& p, const std::string& s, bool exec = false) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << s;
    if (exec) fs::permissions(p, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec, fs::perm_options::replace);
}
std::string readFile(const fs::path& p) {
    std::ifstream      in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

struct Run {
    ProcessResult            r;
    std::vector<std::string> lines;
    double                   seconds = 0;
};
Run run(const std::vector<std::wstring>& argv, const fs::path& cwd = {}, const Env& env = {}, unsigned timeoutMs = 0,
        const std::atomic<bool>* cancel = nullptr) {
    Run        o;
    const auto t0 = std::chrono::steady_clock::now();
    o.r           = run_process_argv(argv, cwd, env, [&](const std::string& l) { o.lines.push_back(l); }, cancel, timeoutMs);
    o.seconds     = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return o;
}
bool alive(pid_t p) { return p > 0 && ::kill(p, 0) == 0; }
bool isPkg(const fs::path& rel) { return plat::is_package_file(rel); }

} // namespace

BS_TEST(builder_posix_process_exit_code_output_and_no_shell) {
    // codigo de saida e as duas saidas, em UTF-8
    Run a = run({L"/bin/sh", L"-c", L"echo ok; echo 'ação' >&2; exit 7"});
    BS_CHECK(a.r.started && a.r.exitCode == 7);
    BS_CHECK(a.lines.size() == 2 && a.lines[0] == "ok" && a.lines[1] == "ação");
    // os argumentos chegam como estao: nada de expansao, ; ou aspas
    Run b = run({L"/bin/echo", L"$HOME; rm -rf /x", L"a'b \"c\"", L"`id`"});
    BS_CHECK(b.r.exitCode == 0 && b.lines.size() == 1 && b.lines[0] == "$HOME; rm -rf /x a'b \"c\" `id`");
    // byte invalido vira '?'
    Run c = run({L"/usr/bin/printf", L"x\\377y\\n"});
    BS_CHECK(c.lines.size() == 1 && c.lines[0] == "x?y");
    // morto por sinal: 128 + sinal
    Run d = run({L"/bin/sh", L"-c", L"kill -9 $$"});
    BS_CHECK(d.r.started && d.r.exitCode == 128 + 9);
}

BS_TEST(builder_posix_process_path_env_cwd_and_missing) {
    TempDir t;
    const fs::path bin = t.p / "bin dir";
    writeFile(bin / "ferramenta", "#!/bin/sh\necho \"var=$BS_TESTE_VAR\"; pwd\n", true);
    writeFile(bin / "sem-exec", "#!/bin/sh\necho nao\n", false);
    const fs::path cwd = t.p / "pasta com espaço";
    fs::create_directories(cwd);
    Env env = current_env();
    env[L"PATH"]         = path_wide(bin) + L":/usr/bin:/bin";
    env[L"BS_TESTE_VAR"] = L"valor ç";
    Run a = run({L"ferramenta"}, cwd, env);
    BS_CHECK(a.r.started && a.r.exitCode == 0);
    BS_CHECK(a.lines.size() == 2 && a.lines[0] == "var=valor ç" && a.lines[1] == cwd.string());
    // programa ausente, sem permissao de execucao, ou pasta de trabalho ausente: nao inicia (o Builder diz qual)
    BS_CHECK(!run({L"nao-existe-bs"}, {}, env).r.started);
    BS_CHECK(!run({path_wide(bin / "sem-exec")}).r.started);
    BS_CHECK(!run({L"/bin/true"}, t.p / "nao existe").r.started);
    BS_CHECK(!run({}).r.started);
}

BS_TEST(builder_posix_process_timeout_and_cancel_kill_tree) {
    TempDir t;
    const fs::path pidf = t.p / "neto.pid";
    // o neto (sleep em segundo plano) segura o pipe: o prazo mata a arvore e o Builder nao fica preso
    const std::wstring script = L"sleep 30 & echo $! > '" + path_wide(pidf) + L"'; sleep 30";
    Run a = run({L"/bin/sh", L"-c", script}, {}, {}, 500);
    BS_CHECK(a.r.started && a.r.timedOut && !a.r.cancelled);
    BS_CHECK(a.seconds < 10);
    pid_t neto = 0;
    std::ifstream(pidf) >> neto;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    BS_CHECK(neto > 0 && !alive(neto));

    // cancelamento vindo de outra thread (Ctrl+C no Builder)
    fs::remove(pidf);
    std::atomic<bool> cancel{false};
    std::thread       th([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        cancel = true;
    });
    Run b = run({L"/bin/sh", L"-c", script}, {}, {}, 0, &cancel);
    th.join();
    BS_CHECK(b.r.cancelled && b.seconds < 10);
    neto = 0;
    std::ifstream(pidf) >> neto;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    BS_CHECK(neto > 0 && !alive(neto));

    // filho que termina bem e deixa um neto em segundo plano: o neto sai junto (como o job do Windows)
    fs::remove(pidf);
    Run e = run({L"/bin/sh", L"-c", L"sleep 30 > /dev/null 2>&1 & echo $! > '" + path_wide(pidf) + L"'; exit 0"});
    BS_CHECK(e.r.exitCode == 0 && e.seconds < 10);
    neto = 0;
    std::ifstream(pidf) >> neto;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    BS_CHECK(neto > 0 && !alive(neto));

    // filho que fecha a saida e continua vivo: o prazo ainda vale
    Run c = run({L"/bin/sh", L"-c", L"exec >/dev/null 2>&1; sleep 30"}, {}, {}, 500);
    BS_CHECK(c.r.timedOut && c.seconds < 10);
}

BS_TEST(builder_posix_process_does_not_leak_descriptors) {
    TempDir t;
    // um arquivo aberto sem O_CLOEXEC (como o builder.log de um ofstream) e a trava da saida
    std::FILE* f = std::fopen((t.p / "log.txt").c_str(), "w");
    plat::DirLock lock;
    BS_CHECK(lock.acquire(t.p / ".bs-lock"));
    // e um descritor de numero alto (acima de 65535, se o limite deixar)
    int high = -1;
    rlimit rl{};
    if (::getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_max > 70001) {
        if (rl.rlim_cur < 70001) rl.rlim_cur = 70001, ::setrlimit(RLIMIT_NOFILE, &rl);
        high = ::dup2(::fileno(f), 70000);
    }
    Run a = run({L"/bin/ls", L"/proc/self/fd"});
    if (high >= 0) ::close(high);
    else std::fprintf(stderr, "  (limite de descritores baixo: sem o teste do descritor 70000)\n");
    std::fclose(f);
    // 0, 1, 2 e o do proprio ls lendo /proc/self/fd
    BS_CHECK(a.r.exitCode == 0 && a.lines.size() <= 4);
    for (const auto& l : a.lines) BS_CHECK(l == "0" || l == "1" || l == "2" || l == "3");
}

BS_TEST(builder_posix_lock_probe_and_exclusive_dir) {
    TempDir t;
    {
        plat::DirLock a, b;
        BS_CHECK(a.acquire(t.p / ".bs-lock"));
        BS_CHECK(!b.acquire(t.p / ".bs-lock"));  // outra execucao na mesma saida
    }
    plat::DirLock c;
    BS_CHECK(c.acquire(t.p / ".bs-lock"));  // solta no fim da anterior

    BS_CHECK(!plat::probe_writable(t.p));
    std::error_code ec;
    BS_CHECK(plat::create_dir_exclusive(t.p / "nova ç", ec) && !ec);
    BS_CHECK(!plat::create_dir_exclusive(t.p / "nova ç", ec) && !ec);  // ja existe: sem erro
    BS_CHECK(!plat::create_dir_exclusive(t.p / "falta" / "x", ec) && ec);
    if (::geteuid() != 0) {  // root grava em qualquer pasta
        const fs::path ro = t.p / "so leitura";
        fs::create_directories(ro);
        fs::permissions(ro, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
        const std::error_code e = plat::probe_writable(ro);
        BS_CHECK(e && plat::error_kind_of(e, ErrorKind::Other) == ErrorKind::NoPermission);
    }
    // ';' separa listas no CMake: caminho do kit ou do compilador com ele e recusado (o resto passa)
    BS_CHECK(!plat::build_path_problem("/home/x/a;b/kit").empty() && !plat::build_path_ok("/home/x/a;b"));
    BS_CHECK(!plat::build_path_problem("/home/x/a\\b/kit").empty());  // '\' vira '/' dentro do CMake
    BS_CHECK(!plat::build_path_problem("/home/x/a \"b\"/kit").empty() && !plat::build_path_problem("/home/x/$b/kit").empty());
    BS_CHECK(plat::build_path_problem("/home/x/a 'b' (e) & # `c` ç/kit").empty());
    // nomes com acento sem passar pela localidade
    BS_CHECK(plat::to_path(L"Instalação ç") == fs::path("Instalação ç"));
    BS_CHECK(path_wide(fs::path("Instalação ç")) == L"Instalação ç");
    BS_CHECK(narrow(widen("ç\xff")) == "ç\xEF\xBF\xBD");  // byte invalido vira U+FFFD
}

BS_TEST(builder_posix_install_swap_keeps_user_files_and_recovers) {
    TempDir        tmp;
    const fs::path dest = tmp.p / "BlueStinger";
    auto           stage = [&](const std::string& ver) {
        const fs::path s = tmp.p / ("BlueStinger.new-" + ver);
        writeFile(s / "BlueStinger", ver, true);
        writeFile(s / "licencas" / "MIT.txt", "mit " + ver);
        if (ver == "v1") writeFile(s / "licencas" / "so_v1.txt", "pacote v1");
        return s;
    };
    install_swap(stage("v1"), dest, isPkg);
    BS_CHECK(readFile(dest / "BlueStinger") == "v1");
    BS_CHECK(fs::exists(dest / kPackageManifest) && fs::exists(dest / kInstallMarker));
    // arquivos do usuario: saves portateis, mods, configuracao, um arquivo dele em licencas
    writeFile(dest / "saves" / "card0" / "BLUESTIN.000", "save");
    writeFile(dest / "mods" / "video" / "NEWGAME.mp4", "mod");
    writeFile(dest / "controles.toml", "[teclado]");
    writeFile(dest / "licencas" / "minha.txt", "fica");
    bs::native::install::write_config(dest, {bs::native::install::SavesLocation::Portable});
    install_swap(stage("v2"), dest, isPkg);
    BS_CHECK(readFile(dest / "BlueStinger") == "v2");
    BS_CHECK(readFile(dest / "saves" / "card0" / "BLUESTIN.000") == "save" && readFile(dest / "mods" / "video" / "NEWGAME.mp4") == "mod");
    BS_CHECK(readFile(dest / "controles.toml") == "[teclado]" && readFile(dest / "licencas" / "minha.txt") == "fica");
    BS_CHECK(bs::native::install::read_config(dest).saves == bs::native::install::SavesLocation::Portable);
    BS_CHECK(!fs::exists(dest / "licencas" / "so_v1.txt"));
    BS_CHECK(readFile(tmp.p / "BlueStinger.anterior" / "BlueStinger") == "v1");
    // a permissao de execucao do pacote passa para a instalacao
    BS_CHECK((fs::status(dest / "BlueStinger").permissions() & fs::perms::owner_exec) != fs::perms::none);

    // pasta da instalacao sem permissao de gravacao: a troca falha antes de entrar e a v2 fica
    if (::geteuid() != 0) {
        const fs::path s3 = stage("v3");
        fs::permissions(tmp.p, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
        bool threw = false;
        try {
            install_swap(s3, dest, isPkg);
        } catch (const std::exception&) {
            threw = true;
        }
        fs::permissions(tmp.p, fs::perms::owner_all, fs::perm_options::replace);
        BS_CHECK(threw && readFile(dest / "BlueStinger") == "v2" && readFile(dest / "controles.toml") == "[teclado]");
        fs::remove_all(s3);
    }

    // troca interrompida (sem dest, so o backup marcado): o backup volta antes da nova entrar
    fs::rename(dest, tmp.p / "BlueStinger.old-deadbeef");
    install_swap(stage("v4"), dest, isPkg);
    BS_CHECK(readFile(dest / "BlueStinger") == "v4" && readFile(dest / "saves" / "card0" / "BLUESTIN.000") == "save");
    BS_CHECK(!fs::exists(tmp.p / "BlueStinger.old-deadbeef"));
    // no Linux a caixa conta: "blustinger" nao e o executavel do pacote
    BS_CHECK(isPkg("BlueStinger") && !isPkg("bluestinger") && isPkg("licencas/x.txt") && !isPkg("saves/a"));
}

BS_TEST(builder_posix_find_toolchain) {
    TempDir t;
    ToolchainOptions o;
    o.toolchainDir = t.p / "toolchain";
    auto expectThrow = [&](const ToolchainOptions& opt, const char* contains) {
        try {
            find_toolchain(opt, nullptr);
        } catch (const std::runtime_error& e) {
            const std::string m = e.what();
            if (m.find(contains) == std::string::npos) std::fprintf(stderr, "  mensagem: %s\n", m.c_str());
            return m.find(contains) != std::string::npos;
        }
        return false;
    };
    // vazio: diz o que falta
    BS_CHECK(expectThrow(o, "bs-toolchain.cmake"));
    // pecas presentes, mas o cmake sem permissao de execucao
    writeFile(o.toolchainDir / "bs-toolchain.cmake", "# falso\n");
    writeFile(o.toolchainDir / "VERSAO.txt", "Compilador falso 1.0\n");
    writeFile(o.toolchainDir / "cmake" / "bin" / "cmake", "#!/bin/sh\necho 'cmake version 3.30.0'\n", false);
    writeFile(o.toolchainDir / "bin" / "ninja", "#!/bin/sh\necho 1.12.1\n", true);
    BS_CHECK(expectThrow(o, "sem permissao de execucao"));
    // o cmake executa mas falha
    writeFile(o.toolchainDir / "cmake" / "bin" / "cmake", "#!/bin/sh\nexit 1\n", true);
    BS_CHECK(expectThrow(o, "nao executa"));
    // completo
    writeFile(o.toolchainDir / "cmake" / "bin" / "cmake", "#!/bin/sh\necho 'cmake version 3.30.0'\n", true);
    const Toolchain tc = find_toolchain(o, nullptr);
    BS_CHECK(tc.cmake == o.toolchainDir / "cmake" / "bin" / "cmake" && tc.ninja == o.toolchainDir / "bin" / "ninja");
    BS_CHECK(tc.cmakeArgs.size() == 1 && tc.cmakeArgs[0] == L"-DCMAKE_TOOLCHAIN_FILE=" + path_wide(o.toolchainDir / "bs-toolchain.cmake"));
    BS_CHECK(tc.crtDir.empty() && tc.description.find("Compilador falso 1.0") != std::string::npos);
    BS_CHECK(tc.stamp.find("Compilador falso 1.0") != std::string::npos);
    // do sistema, sem cmake no PATH: a mensagem lista o que falta
    const char* old = std::getenv("PATH");
    const std::string keep = old ? old : "";
    const fs::path onlyClang = t.p / "so clang";
    writeFile(onlyClang / "clang", "#!/bin/sh\necho clang 1\n", true);
    writeFile(onlyClang / "clang++", "#!/bin/sh\necho clang 1\n", true);
    ::setenv("PATH", onlyClang.c_str(), 1);
    ToolchainOptions s;
    s.systemToolchain = true;
    BS_CHECK(expectThrow(s, "cmake ninja"));
    // do sistema, completo, mas numa pasta que o CMake nao aceita: recusado antes de ir ao CMake (Codex 20261103-143212)
    const fs::path semi = t.p / "a;b";
    writeFile(semi / "clang", "#!/bin/sh\necho clang 1\n", true);
    writeFile(semi / "clang++", "#!/bin/sh\necho clang 1\n", true);
    writeFile(semi / "cmake", "#!/bin/sh\necho 'cmake version 3.30.0'\n", true);
    writeFile(semi / "ninja", "#!/bin/sh\necho 1.12.1\n", true);
    ::setenv("PATH", semi.c_str(), 1);
    BS_CHECK(expectThrow(s, "que o CMake nao aceita"));
    // a mesma pasta sem o ';' passa
    const fs::path ok = t.p / "sistema ok";
    fs::rename(semi, ok);
    ::setenv("PATH", ok.c_str(), 1);
    const Toolchain sys = find_toolchain(s, nullptr);
    BS_CHECK(sys.cmake == ok / "cmake" && sys.cmakeArgs.size() >= 2);
    ::setenv("PATH", keep.c_str(), 1);
}

// Cache do Zig: <raiz>/zig criada e gravavel; sem gravacao na raiz, dentro do cache de compilacao; ZIG_* do usuario ficam
BS_TEST(builder_posix_zig_cache_env) {
    TempDir        t;
    const fs::path cache = t.p / "raiz" / "build" / "0000abcd";
    fs::create_directories(cache);
    Env e;
    plat::toolchain_env(e, cache);
    BS_CHECK(e[L"ZIG_GLOBAL_CACHE_DIR"] == path_wide(t.p / "raiz" / "zig") && fs::is_directory(t.p / "raiz" / "zig"));
    BS_CHECK(e[L"ZIG_LOCAL_CACHE_DIR"] == e[L"ZIG_GLOBAL_CACHE_DIR"]);
    if (::geteuid() != 0) {
        const fs::path cache2 = t.p / "ro" / "build" / "0000abcd";
        fs::create_directories(cache2);
        fs::permissions(t.p / "ro", fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
        Env e2;
        plat::toolchain_env(e2, cache2);
        fs::permissions(t.p / "ro", fs::perms::owner_all, fs::perm_options::replace);
        BS_CHECK(e2[L"ZIG_GLOBAL_CACHE_DIR"] == path_wide(cache2 / "zig") && !fs::exists(t.p / "ro" / "zig"));
    }
    Env e3;
    e3[L"ZIG_GLOBAL_CACHE_DIR"] = L"/escolha/do/usuario";
    plat::toolchain_env(e3, cache);
    BS_CHECK(e3[L"ZIG_GLOBAL_CACHE_DIR"] == L"/escolha/do/usuario" && e3[L"ZIG_LOCAL_CACHE_DIR"] == L"/escolha/do/usuario");
}

// Entradas ruins: nada e criado alem da pasta de saida e do trabalho, e a mensagem diz o que fazer
BS_TEST(builder_posix_rejects_bad_inputs_without_side_effects) {
    TempDir t;
    // kit falso completo (so os arquivos que o check_kit exige), SDL3 pre-compilado falso e compilador falso
    const fs::path kit = t.p / "kit";
    for (const char* f : {"CMakeLists.txt", "LICENSE", "config/bluestinger_us.toml", "app/CMakeLists.txt", "runtime/CMakeLists.txt",
                          "tools/extractor/CMakeLists.txt", "tools/dist/linux/LEIA-ME-JOGO.txt", "tools/dcmap/CMakeLists.txt",
                          "native/CMakeLists.txt", "legacy/CMakeLists.txt", "transitional/CMakeLists.txt",
                          "native/text/fonts_embed.S.in", "third_party/fonts/noto-sans/NotoSans-Regular.ttf",
                          "third_party/fonts/noto-sans/NotoSans-SemiBold.ttf", "third_party/fonts/noto-sans/OFL.txt",
                          "third_party/pl_mpeg/pl_mpeg.h", "third_party/freetype/CMakeLists.txt", "third_party/freetype/LICENSE.TXT",
                          "third_party/tomlplusplus/CMakeLists.txt", "third_party/SDL3-linux/lib/cmake/SDL3/SDL3Config.cmake",
                          "third_party/SDL3-linux/lib/libSDL3.a", "third_party/SDL3-linux/LICENSE.txt"})
        writeFile(kit / f, "x");
    const fs::path tcd = t.p / "toolchain";
    writeFile(tcd / "bs-toolchain.cmake", "#\n");
    writeFile(tcd / "VERSAO.txt", "falso\n");
    writeFile(tcd / "cmake" / "bin" / "cmake", "#!/bin/sh\necho 'cmake version 3.30.0'\n", true);
    writeFile(tcd / "bin" / "ninja", "#!/bin/sh\necho 1.12.1\n", true);
    BS_CHECK(check_kit(kit, kit / "third_party" / "SDL3-linux").empty());

    auto req = [&](const fs::path& out) {
        Request r;
        r.kitDir = kit, r.toolchainDir = tcd, r.outDir = out, r.movies = false;
        return r;
    };
    // CDI ausente
    {
        Request r = req(t.p / "saida1");
        r.disc    = t.p / "nao existe.cdi";
        const Result res = build(r, {});
        BS_CHECK(!res.ok && res.kind == ErrorKind::DiscNotFound && !fs::exists(t.p / "saida1"));
    }
    // CDI invalido (bytes quaisquer): recusado na leitura, sem instalacao
    {
        writeFile(t.p / "falso.cdi", std::string(1 << 16, '\x5A'));
        Request r = req(t.p / "saida 2 ç");
        r.disc    = t.p / "falso.cdi";
        const Result res = build(r, {});
        BS_CHECK(!res.ok && res.kind == ErrorKind::InvalidDisc && res.failedStep == Step::Extract);
        BS_CHECK(res.error.find("NTSC-U") != std::string::npos);
        BS_CHECK(!fs::exists(t.p / "saida 2 ç" / "BlueStinger"));
        BS_CHECK(fs::exists(t.p / "saida 2 ç" / ".bs-build" / "builder.log"));
    }
    // projeto C++ editavel: so no Windows nesta versao
    {
        Request r = req(t.p / "saida3");
        r.disc    = t.p / "falso.cdi";
        r.exportProject = t.p / "projeto";
        const Result res = build(r, {});
        BS_CHECK(!res.ok && res.error.find("Windows") != std::string::npos && !fs::exists(t.p / "projeto"));
    }
    // kit incompleto
    {
        fs::rename(kit / "third_party" / "freetype" / "CMakeLists.txt", t.p / "ft.txt");
        Request r = req(t.p / "saida4");
        r.disc    = t.p / "falso.cdi";
        const Result res = build(r, {});
        BS_CHECK(!res.ok && res.error.find("kit de fontes incompleto") != std::string::npos);
        BS_CHECK(res.error.find("freetype") != std::string::npos);
        fs::rename(t.p / "ft.txt", kit / "third_party" / "freetype" / "CMakeLists.txt");
    }
    // compilador portatil ausente
    {
        Request r      = req(t.p / "saida5");
        r.disc         = t.p / "falso.cdi";
        r.toolchainDir = t.p / "sem compilador";
        const Result res = build(r, {});
        BS_CHECK(!res.ok && res.kind == ErrorKind::Toolchain && res.error.find("compilador portatil incompleto") != std::string::npos);
    }
    // outra execucao na mesma saida
    {
        fs::create_directories(t.p / "saida6" / ".bs-build");
        plat::DirLock other;
        BS_CHECK(other.acquire(t.p / "saida6" / ".bs-build" / ".bs-lock"));
        Request r = req(t.p / "saida6");
        r.disc    = t.p / "falso.cdi";
        const Result res = build(r, {});
        BS_CHECK(!res.ok && res.kind == ErrorKind::Busy);
    }
    // saida sem permissao de gravacao
    if (::geteuid() != 0) {
        const fs::path ro = t.p / "somente leitura";
        fs::create_directories(ro);
        fs::permissions(ro, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
        Request r = req(ro);
        r.disc    = t.p / "falso.cdi";
        const Result res = build(r, {});
        BS_CHECK(!res.ok && res.kind == ErrorKind::NoPermission);
    }
}
