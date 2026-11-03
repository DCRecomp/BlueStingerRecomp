// bs-builder: o fluxo completo do Builder pela linha de comando (automacao e testes).
//   bs-builder --disc <imagem.cdi> --out <pasta> [--kit <pasta>] [--sdl3 <pasta>]
//              [--vcvars <vcvars64.bat> | --vswhere <vswhere.exe>] [--clean] [--verify-vblanks N] [--sem-filmes]
//              [--name <subpasta>]   (padrao BlueStinger; o jogo instalado nao precisa do CDI)
//              [--projeto <pasta>]   exporta tambem o projeto C++ editavel (project_export.h)
//   bs-builder --de-projeto <pasta> --out <pasta> [--disc <cdi>]   compila um projeto editado
//   bs-builder --disc <cdi> --out <recursos> --projeto <pasta> --somente-exportar
//              so exporta (sem compilador, compilacao nem instalacao); [--tipo Release|Debug] no build
//   Linux (N9-L L5): [--compilador <pasta>] (padrao <Builder>/toolchain) ou --compilador-sistema (clang, cmake e ninja
//   do sistema, fora do compilador portatil da D-L2); o projeto C++ editavel so existe no Windows nesta versao.
// Codigo de saida: 0 = BlueStinger.exe instalado e verificado; 1 = falha; 2 = uso;
// 3 = cancelado (Ctrl+C; no Linux tambem SIGTERM e SIGHUP).
#include "builder_core.h"
#include "builder_platform.h"
#include "child_process.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace bs::builder;

namespace {

std::atomic<bool> g_cancel{false};

void out(const std::string& s) {  // UTF-8 no console e em pipes
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

bool parseEdition(const std::wstring& v, Request& req) {
    bs::native::edition::Edition e{};
    if (!bs::native::edition::parse_edition(narrow(v), &e)) return false;
    req.edition = e;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::wstring> args = plat::cli_args(argc, argv);
    auto P = [](const std::wstring& w) { return plat::to_path(w); };  // caminho do usuario sem passar pela localidade

    Request req;
    req.kitDir = plat::exe_dir() / L"kit";
    for (size_t i = 1; i < args.size(); ++i) {
        const std::wstring& a    = args[i];
        const bool          more = i + 1 < args.size();
        if (a == L"--disc" && more) req.disc = P(args[++i]);
        else if (a == L"--out" && more) req.outDir = P(args[++i]);
        else if (a == L"--kit" && more) req.kitDir = P(args[++i]);
        else if (a == L"--sdl3" && more) req.sdl3Dir = P(args[++i]);
        else if (a == L"--vcvars" && more) req.vcvars = P(args[++i]);
        else if (a == L"--vswhere" && more) req.vswhere = P(args[++i]);
        else if (a == L"--verify-vblanks" && more) req.verifyVblanks = unsigned(std::stoul(args[++i]));
        else if (a == L"--name" && more) req.installName = args[++i];
        else if (a == L"--version-label" && more) req.versionLabel = narrow(args[++i]);
        else if (a == L"--projeto" && more) req.exportProject = P(args[++i]);
        else if (a == L"--de-projeto" && more) req.fromProject = P(args[++i]);
        else if (a == L"--clean") req.clean = true;
        else if (a == L"--sem-filmes") req.movies = false;
        else if (a == L"--edicao" && more && parseEdition(args[i + 1], req)) ++i;
        else if (a == L"--somente-exportar") req.exportOnly = true;
        else if (a == L"--tipo" && more) req.buildType = narrow(args[++i]);
        else if (plat::cli_toolchain_options() && a == L"--compilador" && more) req.toolchainDir = P(args[++i]);  // so Linux
        else if (plat::cli_toolchain_options() && a == L"--compilador-sistema") req.systemToolchain = true;
        else {
            std::fprintf(stderr,
                         "uso: bs-builder --disc <imagem.cdi> --out <pasta> [--kit <pasta>] [--sdl3 <pasta>]\n"
                         "                [--vcvars <vcvars64.bat> | --vswhere <vswhere.exe>] [--clean] [--name <subpasta>]\n"
                         "                [--edicao original|remastered]\n"
                         "                [--projeto <pasta>]      exporta o projeto C++ editavel\n"
                         "  bs-builder --de-projeto <pasta> --out <pasta> [--disc <cdi>]   compila o projeto (mods)\n"
                         "                [--verify-vblanks N] [--sem-filmes] [--tipo Release|Debug]\n"
                         "  bs-builder --disc <cdi> --out <recursos> --projeto <pasta> --somente-exportar   so exporta\n"
                         "                (valida, prepara os dados em <recursos>, gera o C++ e exporta; sem compilador)\n%s",
                         plat::cli_usage_extra());
            return 2;
        }
    }
    if ((req.disc.empty() && req.fromProject.empty()) || req.outDir.empty()) {
        std::fprintf(stderr, "uso: bs-builder --disc <imagem.cdi> --out <pasta> (veja --help)\n");
        return 2;
    }
    plat::cli_on_interrupt(&g_cancel);

    Callbacks cb;
    cb.cancel   = &g_cancel;
    int lastPct = -1;
    Step lastStep = Step::Done;
    cb.progress = [&](Step s, double f, const std::string&) {
        const int pct = int(f * 100);
        if (pct != lastPct || s != lastStep) {
            out("[progresso] " + std::to_string(pct) + "% " + step_name(s) + "\n");
            lastPct = pct, lastStep = s;
        }
    };
    const bool verbose = plat::env_present(L"BS_BUILDER_VERBOSE");
    cb.log = [&](const std::string& l) {
        if (verbose || l.rfind("== ", 0) == 0 || l.rfind("ERRO", 0) == 0 || l.rfind("OK:", 0) == 0) out(l + "\n");
    };
    const Result r = build(req, cb);
    if (r.ok && req.exportOnly) {
        out("exportado: " + path_text(r.projectDir) + "\n");
        return 0;
    }
    if (r.ok) {
        out("instalado: " + path_text(r.exe) + "\n");
        if (!r.warning.empty()) out("aviso: " + r.warning + "\n");
        return 0;
    }
    out(std::string("falhou em \"") + step_name(r.failedStep) + "\": " + r.error + "\n");
    out("log completo: " + path_text(r.logFile) + "\n");
    return r.cancelled ? 3 : 1;
}
