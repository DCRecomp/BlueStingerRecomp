#include "builder_core.h"

#include "app/recomp.h"
#include "boot/disc_boot.h"
#include "boot/native_boot.h"
#include "builder_platform.h"
#include "child_process.h"
#include "project_export.h"
#include "disc/cdi_image.h"
#include "runtime_data.h"
#include "toolchain.h"
#include "util/hash.h"
#include "video/movie_build.h"
#include "platform/install_config.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace bs::builder {

const char* step_name(Step s) {
    switch (s) {
    case Step::Validate: return "conferindo as entradas";
    case Step::Toolchain: return "localizando o compilador";
    case Step::Extract: return "lendo o disco";
    case Step::Recompile: return "analisando e gerando o C++";
    case Step::Configure: return "configurando a compilacao";
    case Step::Compile: return "compilando";
    case Step::Package: return "montando o pacote";
    case Step::Verify: return "verificando o executavel";
    case Step::Export: return "exportando o projeto C++";
    case Step::Install: return "instalando";
    case Step::Done: return "concluido";
    }
    return "?";
}

namespace {

// faixa de progresso total de cada etapa
struct Range {
    double a, b;
};
Range rangeOf(Step s, bool exportOnly = false) {
    if (exportOnly) {
        switch (s) {
        case Step::Validate: return {0.00, 0.02};
        case Step::Extract: return {0.02, 0.70};
        case Step::Recompile: return {0.70, 0.90};
        case Step::Export: return {0.90, 1.00};
        default: return {1.00, 1.00};
        }
    }
    switch (s) {
    case Step::Validate: return {0.00, 0.02};
    case Step::Toolchain: return {0.02, 0.05};
    case Step::Extract: return {0.05, 0.20};
    case Step::Recompile: return {0.20, 0.30};
    case Step::Configure: return {0.30, 0.34};
    case Step::Compile: return {0.34, 0.88};
    case Step::Package: return {0.88, 0.90};
    case Step::Verify: return {0.90, 0.96};
    case Step::Export: return {0.96, 0.98};
    case Step::Install: return {0.98, 1.00};
    case Step::Done: return {1.00, 1.00};
    }
    return {0, 0};
}

// erro de uma etapa: mensagem tecnica (log, Developer) e o tipo (mensagem traduzida do Recompiler)
struct StepError : std::runtime_error {
    ErrorKind kind = ErrorKind::Other;
    explicit StepError(const std::string& m) : std::runtime_error(m) {}
    StepError(ErrorKind k, const std::string& m) : std::runtime_error(m), kind(k) {}
};

// falta de espaco e de permissao pelo codigo do sistema (inclusive dentro de filesystem_error)
ErrorKind kindOf(const std::error_code& ec, ErrorKind otherwise) { return error_kind_of(ec, otherwise); }
struct Cancelled {};

// linhas de log do processo: tambem para o resumo do erro
class LogTail {
public:
    void push(const std::string& l) {
        tail_.push_back(l);
        if (tail_.size() > 400) tail_.pop_front();
    }
    void clear() { tail_.clear(); }
    // linhas com "error"/"erro" (ate 12); senao as ultimas linhas
    std::string summary() const {
        std::string out;
        int         n = 0;
        for (const auto& l : tail_) {
            std::string low = l;
            for (char& c : low) c = char(tolower((unsigned char)c));
            if ((low.find("error") != std::string::npos || low.find("erro") != std::string::npos) && n < 12) {
                out += l + "\n";
                ++n;
            }
        }
        if (out.empty())
            for (size_t i = tail_.size() > 12 ? tail_.size() - 12 : 0; i < tail_.size(); ++i) out += tail_[i] + "\n";
        return out;
    }

private:
    std::deque<std::string> tail_;
};

std::string randomSuffix() {  // 64 bits
    std::random_device rd;
    char               b[24];
    std::snprintf(b, sizeof b, "%08x%08x", rd(), rd());
    return b;
}

uint64_t nowTicks() { return plat::now_ticks(); }

// marca de instalacao valida: o instante (FILETIME) gravado por install_swap; 0 = nao e nossa
uint64_t installMarker(const fs::path& dir) {
    std::ifstream in(dir / kInstallMarker, std::ios::binary);
    uint64_t      v = 0;
    return (in >> v) ? v : 0;
}

std::wstring lower(const std::wstring& s) { return plat::fold_name(s); }

// `p` com um sufixo ASCII no nome (".tmp", ".anterior"...), sem converter o resto do caminho
fs::path withSuffix(const fs::path& p, const wchar_t* suffix) {
    fs::path r = p;
    r += suffix;
    return r;
}

void writeFileAtomic(const fs::path& p, const std::vector<uint8_t>& data) {
    {
        std::ifstream in(p, std::ios::binary);
        if (in) {
            const std::vector<uint8_t> old((std::istreambuf_iterator<char>(in)), {});
            if (old == data) return;  // mesmo conteudo: preserva a data (build incremental)
        }
    }
    const fs::path tmp = withSuffix(p, L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        out.close();
        if (!out) throw StepError("nao foi possivel gravar " + path_text(tmp));
    }
    fs::rename(tmp, p);
}

std::string readText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::string   s((std::istreambuf_iterator<char>(in)), {});
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

std::wstring cmakePath(const fs::path& p) { return plat::cmake_path(p); }

bool isPackage(const fs::path& rel) { return plat::is_package_file(rel); }

// teste de gravacao real (ACL de negacao nao aparece nos atributos)
void probeWritable(const fs::path& dir) {
    if (const std::error_code e = plat::probe_writable(dir))
        throw StepError(kindOf(e, ErrorKind::NoPermission),
                        "sem permissao de gravacao em " + path_text(dir) + " (erro " + std::to_string(e.value()) +
                        "). Escolha outra pasta de saida.");
}

constexpr wchar_t kStageMarker[] = L".bs-stage";  // pasta de pacote temporario criada por nos
// Windows: o jogo instalado e o trabalho (sem compilacao) ficam na saida (200); objetos do SDL3 somam ~160 caracteres ao
// cache (90); kit mais longo que isso e copiado para o cache (90). Linux: sem limite pratico (builder_platform_posix.cpp)
const size_t kMaxOutPath   = plat::max_out_path();
const size_t kMaxCachePath = plat::max_cache_path();
const size_t kMaxDirectKit = plat::max_direct_kit();

// Cache de compilacao, um por pasta de saida: <raiz>\build\<hash da saida>. Tudo o que o
// Ninja ve fica ali (build do CMake, C++ gerado e, se preciso, uma copia do kit): caminho
// curto (limite de 260 do MSVC) e representavel na pagina ANSI (o Ninja usa as APIs "A").
fs::path cacheDir(const fs::path& out, const fs::path& work) {
    char name[16];
    std::snprintf(name, sizeof name, "%08x", plat::cache_key(out));
    for (const auto& r : plat::cache_roots(work)) {
        const fs::path c = r / L"build" / name;
        if (!plat::build_path_ok(c) || c.native().size() > kMaxCachePath) continue;
        std::error_code ec;
        fs::create_directories(c, ec);
        if (ec) continue;
        if (!plat::probe_writable(c)) return c;
    }
    throw StepError(plat::cache_hint());
}

// Parte do kit que a compilacao usa: kKitEntries (project_export.cpp).

// Copia `from` para `to` so com o que mudou (data mais nova); a data e preservada.
void syncTree(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (fs::is_directory(from, ec)) {
        fs::create_directories(to);
        fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::update_existing);
    } else if (fs::is_regular_file(from, ec)) {
        fs::create_directories(to.parent_path());
        fs::copy_file(from, to, fs::copy_options::update_existing);
    }
}

} // namespace

// N9.1: as pastas de cache que ja existem para esta saida (a marca .bs-out diz de quem sao), sem criar nada
std::vector<fs::path> cache_dirs(const fs::path& outDir) {
    std::error_code ec;
    const fs::path  out = fs::absolute(outDir, ec);
    char            name[16];
    std::snprintf(name, sizeof name, "%08x", plat::cache_key(out));
    std::vector<fs::path> r;
    for (const auto& root : plat::cache_roots(out / L".bs-build")) {
        const fs::path c = root / L"build" / name;
        if (readText(c / L".bs-out") == path_text(out)) r.push_back(c);
    }
    return r;
}

// arquivos obrigatorios e icones (Windows), SDL3 em fontes (Windows) ou pre-compilado (Linux): builder_platform_*.cpp
std::string check_kit(const fs::path& kit, const fs::path& sdl3) {
    std::error_code ec;
    if (std::string p = plat::check_kit_platform(kit, sdl3); !p.empty()) return p;
    if (const fs::path toml = toml_dir(kit, sdl3); !fs::is_regular_file(toml / L"CMakeLists.txt", ec))
        return "fontes do toml++ nao encontradas em " + path_text(toml);
    return {};
}

ErrorKind error_kind_of(const std::error_code& ec, ErrorKind otherwise) { return plat::error_kind_of(ec, otherwise); }

const char* error_key(ErrorKind k) {
    switch (k) {
    case ErrorKind::None: return "";
    case ErrorKind::Cancelled: return "status.cancelled";
    case ErrorKind::MissingInput: return "err.missing_input";
    case ErrorKind::DiscNotFound: return "err.disc_not_found";
    case ErrorKind::InvalidDisc: return "err.invalid_disc";
    case ErrorKind::NoSpace: return "err.no_space";
    case ErrorKind::NoPermission: return "err.no_permission";
    case ErrorKind::PathTooLong: return "err.path_too_long";
    case ErrorKind::Busy: return "err.busy";
    case ErrorKind::Toolchain: return "err.toolchain";
    case ErrorKind::Data: return "err.data";
    case ErrorKind::Build: return "err.build";
    case ErrorKind::Verify: return "err.verify";
    case ErrorKind::Install: return "err.install";
    case ErrorKind::Other: return "err.other";
    }
    return "err.other";
}

InstallInfo inspect_install(const fs::path& outDir, const std::wstring& installName) {
    InstallInfo     i;
    std::error_code ec;
    i.dir      = outDir / plat::to_path(installName);
    i.previous = outDir / plat::to_path(installName + L".anterior");
    i.exists   = fs::is_directory(i.dir, ec) && fs::directory_iterator(i.dir, ec) != fs::directory_iterator();
    if (!i.exists) return i;
    i.ours     = installMarker(i.dir) != 0;
    i.portable = native::install::read_config(i.dir).saves == native::install::SavesLocation::Portable;
    return i;
}

fs::path toml_dir(const fs::path& kit, const fs::path& sdl3) {
    std::error_code ec;
    const fs::path  inKit = kit / L"third_party" / L"tomlplusplus";
    if (fs::is_regular_file(inKit / L"CMakeLists.txt", ec)) return inKit;
    return sdl3.parent_path() / L"tomlplusplus-src";
}

namespace {

// <dest>.anterior-<data>-<aleatorio>: nunca colide com uma pasta existente
fs::path uniqueArchiveName(const fs::path& parent, const fs::path& dest) {
    char              ts[32];
    const std::time_t t = std::time(nullptr);
    std::strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", std::localtime(&t));
    std::error_code ec;
    for (;;) {
        const fs::path p = parent / withSuffix(dest.filename(), (L".anterior-" + widen(ts) + L"-" +
                                                                 widen(randomSuffix().substr(0, 6))).c_str());
        if (!fs::exists(p, ec)) return p;
    }
}

// Depois da troca: o backup (versao anterior) vira <dest>.anterior. Uma .anterior nossa e
// trocada so depois que a nova entra (vai para um nome temporario e volta se der errado); uma
// pasta alheia nesse nome nao e tocada (a anterior vai para .anterior-<data>-<x>). Falha lanca
// dizendo onde a versao anterior ficou (revisao Codex 2026-10-15: o erro era ignorado).
void archivePrevious(const fs::path& parent, const fs::path& dest, const fs::path& backup) {
    std::error_code ec;
    fs::path        prev = parent / withSuffix(dest.filename(), L".anterior");
    fs::path        aside;
    if (fs::exists(prev, ec)) {
        if (installMarker(prev)) {
            aside = parent / withSuffix(dest.filename(), (L".anterior-descartar-" + widen(randomSuffix())).c_str());
            fs::rename(prev, aside, ec);
            if (ec) prev = uniqueArchiveName(parent, dest), aside.clear();  // nao deu para tirar: outro nome
        } else {
            prev = uniqueArchiveName(parent, dest);
        }
    }
    fs::rename(backup, prev, ec);
    if (ec) {
        std::error_code ec2;
        if (!aside.empty()) fs::rename(aside, parent / withSuffix(dest.filename(), L".anterior"), ec2);
        throw ArchiveError("a versao nova foi instalada, mas a anterior nao pode ser guardada em " +
                                 path_text(prev) + " (" + error_text(ec) + "); ela continua em " + path_text(backup));
    }
    if (!aside.empty()) fs::remove_all(aside, ec);  // a .anterior antiga (nossa) so sai agora
}

} // namespace

native::install::InstallConfig install_config_for(const Request& req, const fs::path& installDir, std::string* warning) {
    native::install::InstallConfig ic = native::install::read_config(installDir, warning);
    if (req.edition) ic.edition = *req.edition;
    if (req.portableSaves)
        ic.saves = *req.portableSaves ? native::install::SavesLocation::Portable : native::install::SavesLocation::Documents;
    if (req.subtitles) ic.subtitles = *req.subtitles;
    return ic;
}

void install_swap(const fs::path& stage, const fs::path& dest, const std::function<bool(const fs::path&)>& isPkg,
                  const std::atomic<bool>* cancel) {
    std::error_code    ec;
    const fs::path     parent = dest.parent_path();
    const auto         base   = withSuffix(dest.filename(), L".old-").native();
    // backups de instalacoes anteriores: so pastas com a nossa marca (revisao Codex 2026-10-14)
    std::vector<std::pair<uint64_t, fs::path>> backups;
    for (const auto& e : fs::directory_iterator(parent, ec))
        if (e.is_directory() && e.path().filename().native().rfind(base, 0) == 0)
            if (const uint64_t m = installMarker(e.path())) backups.push_back({m, e.path()});
    if (!fs::exists(dest, ec)) {
        // troca interrompida: o backup mais recente (pela marca) e a ultima copia valida
        if (!backups.empty()) {
            const auto newest = std::max_element(backups.begin(), backups.end());
            fs::rename(newest->second, dest);
            backups.erase(newest);
        }
    }
    // sobras marcadas de trocas concluidas (o arquivamento falhou naquela vez): guardadas com
    // nome unico, nunca apagadas (revisao Codex 2026-10-15)
    if (fs::exists(dest, ec))
        for (const auto& b : backups) fs::rename(b.second, uniqueArchiveName(parent, dest), ec);

    // lista do pacote novo e marca de instalacao (gravadas antes de entrar os arquivos do usuario)
    {
        std::vector<std::wstring> files;
        for (const auto& e : fs::recursive_directory_iterator(stage))
            // instalacao.toml fica fora da lista: e da instalacao (preservado numa troca sem escolha de modo)
            if (e.is_regular_file() && lower(path_wide_generic(fs::relative(e.path(), stage))) != lower(native::install::kConfigFile))
                files.push_back(path_wide_generic(fs::relative(e.path(), stage)));
        {  // N9.1: hashes para a verificacao do instalador (data\ fora: o jogo confere os dados com o manifesto proprio)
            std::ofstream     h(stage / kPackageHashes, std::ios::binary | std::ios::trunc);
            std::vector<char> buf(1 << 16);
            for (const auto& f : files) {
                if (lower(f).rfind(L"data/", 0) == 0) continue;
                std::ifstream in(stage / plat::to_path(f), std::ios::binary);
                util::Sha256  sha;
                while (in) {
                    in.read(buf.data(), std::streamsize(buf.size()));
                    if (in.gcount() > 0) sha.update(buf.data(), size_t(in.gcount()));
                }
                h << sha.hexdigest() << "  " << narrow(f) << "\n";
            }
            if (!h) throw std::runtime_error("nao foi possivel gravar os hashes do pacote");
        }
        std::ofstream m(stage / kPackageManifest, std::ios::binary | std::ios::trunc);
        for (const auto& f : files) m << narrow(f) << "\n";
        m << narrow(kPackageManifest) << "\n" << narrow(kInstallMarker) << "\n" << narrow(kPackageHashes) << "\n";
        std::ofstream(stage / kInstallMarker, std::ios::binary | std::ios::trunc) << nowTicks() << "\n";
        if (!m) throw std::runtime_error("nao foi possivel gravar a lista do pacote");
    }

    // arquivos do usuario (ini, log, saves, o que ele pos na pasta) vao junto: e do pacote
    // anterior so o que a lista dele diz; sem lista (instalacao antiga), o criterio isPkg
    if (fs::exists(dest, ec)) {
        std::set<std::wstring> oldPkg;
        bool                   hasList = false;
        {
            std::ifstream in(dest / kPackageManifest, std::ios::binary);
            for (std::string l; std::getline(in, l);) {
                if (!l.empty() && l.back() == '\r') l.pop_back();
                if (!l.empty()) oldPkg.insert(lower(widen(l))), hasList = true;
            }
        }
        for (auto it = fs::recursive_directory_iterator(dest); it != fs::recursive_directory_iterator(); ++it) {
            const fs::path rel = fs::relative(it->path(), dest);
            if (it->is_directory()) {
                if (!hasList && isPkg && isPkg(rel)) it.disable_recursion_pending();
                continue;
            }
            if (hasList ? oldPkg.count(lower(path_wide_generic(rel))) > 0 : (isPkg && isPkg(rel))) continue;
            const fs::path to = stage / rel;
            fs::create_directories(to.parent_path());
            if (!fs::exists(to)) fs::copy_file(it->path(), to);
        }
    }

    // ponto sem volta: cancelamento so vale ate aqui
    if (cancel && cancel->load()) throw SwapCancelled("cancelado antes da troca");
    const fs::path backup = parent / withSuffix(fs::path(base), widen(randomSuffix()).c_str());
    const bool     hadOld = fs::exists(dest, ec);
    try {
        if (hadOld) fs::rename(dest, backup);
        fs::rename(stage, dest);
    } catch (const fs::filesystem_error& e) {
        const std::string why = error_text(e.code());
        const std::error_code code = e.code();
        if (hadOld) {
            std::error_code ec2;
            if (!fs::exists(dest, ec2) && fs::exists(backup, ec2)) fs::rename(backup, dest, ec2);
            if (!fs::exists(dest / plat::game_binary_installed(), ec2))  // a restauracao tambem falhou: dizer onde esta
                throw SwapError("nao foi possivel trocar a versao instalada (" + why +
                                         ") nem restaurar a anterior. A versao anterior esta em " + path_text(backup) +
                                         (installMarker(backup) ? " e volta sozinha na proxima execucao do Builder."
                                                                : "; renomeie-a de volta para " + path_text(dest) + "."),
                                code);
        }
        throw SwapError("nao foi possivel trocar a versao instalada (" + why +
                        "). O jogo esta aberto? A versao anterior foi mantida.", code);
    }
    // a nova esta no lugar: a anterior fica como <dest>.anterior para rollback (falha aqui nao
    // perde nada: o backup marcado sai na proxima execucao)
    if (hadOld) archivePrevious(parent, dest, backup);
}

Result build(const Request& req, const Callbacks& cb) {
    Result res;
    std::error_code ec;
    const fs::path out   = fs::absolute(req.outDir, ec);
    // de um projeto editavel: o proprio projeto e o kit (fontes, SDL3, C++ gerado e mods)
    const bool     fromProject = !req.fromProject.empty();
    const fs::path kit   = fs::absolute(fromProject ? req.fromProject : req.kitDir, ec);
    const fs::path sdl3  = req.sdl3Dir.empty() || fromProject ? plat::default_sdl3(kit) : fs::absolute(req.sdl3Dir, ec);
    const fs::path work  = out / L".bs-build";
    res.installDir       = out / plat::to_path(req.installName);
    res.exe              = res.installDir / plat::game_binary_installed();
    res.logFile          = work / L"builder.log";

    plat::DirLock lock;  // trava da pasta de saida durante a execucao
    std::mutex    logMutex;
    std::ofstream logFile;
    LogTail       tail;
    auto log = [&](const std::string& l) {
        std::lock_guard lock(logMutex);
        if (logFile) logFile << l << "\n" << std::flush;
        tail.push(l);
        if (cb.log) cb.log(l);
    };
    Step step = Step::Validate;
    auto progress = [&](double within, const std::string& msg) {
        const Range r = rangeOf(step, req.exportOnly);
        if (cb.progress) cb.progress(step, r.a + (r.b - r.a) * within, msg);
    };
    auto enter = [&](Step s) {
        if (cb.cancel && cb.cancel->load()) throw Cancelled{};
        step = s;
        tail.clear();
        log(std::string("== ") + step_name(s));
        progress(0, step_name(s));
    };
    // N9-L (L5): pela lista de argumentos (Windows: a mesma linha de comando de antes; Linux: execve, sem shell)
    auto run = [&](const std::vector<std::wstring>& argv, const fs::path& cwd, const Env& env,
                   const std::function<void(const std::string&)>& extra, unsigned timeoutMs = 0) {
        const std::wstring cmd = display_command(argv);
        log("> " + narrow(cmd));
        const auto r = run_process_argv(argv, cwd, env, [&](const std::string& l) {
            log(l);
            if (extra) extra(l);
        }, cb.cancel, timeoutMs);
        if (r.cancelled) throw Cancelled{};
        if (!r.started) throw StepError("nao foi possivel iniciar: " + narrow(cmd));
        if (r.timedOut) throw StepError("tempo esgotado: " + narrow(cmd));
        return r.exitCode;
    };

    try {
        // 1. entradas
        enter(Step::Validate);
        if (req.disc.empty() && !fromProject) throw StepError(ErrorKind::MissingInput, "escolha a imagem CDI do jogo");
        if (!req.disc.empty() && !fs::is_regular_file(req.disc, ec))
            throw StepError(ErrorKind::DiscNotFound, "imagem nao encontrada: " + path_text(req.disc));
        // nenhum projeto (o exportado nem o de origem) pode cair dentro do que o Builder troca ou apaga: a
        // instalacao (install_swap) e o trabalho (.bs-build: os dados preparados sao apagados e refeitos). fs::relative
        // passa por weakly_canonical (resolve juncoes e links da parte que existe); igual a pasta da "."
        // (revisao Codex 2026-10-20, ALTA: o de origem nao era conferido)
        if (!req.outDir.empty()) {
            for (const fs::path& pr : {fromProject ? kit : fs::path(), req.exportProject.empty() ? fs::path() : fs::absolute(req.exportProject, ec)}) {
                if (pr.empty()) continue;
                for (const fs::path& own : {out / plat::to_path(req.installName), out / L".bs-build"}) {
                    std::error_code rec;
                    const auto      rel = fs::relative(pr, own, rec);
                    if (!rec && !rel.empty() && path_wide(rel).rfind(L"..", 0) != 0)
                        throw StepError("a pasta do projeto nao pode ficar dentro de " + path_text(own) +
                                        " (o Builder troca ou apaga essa pasta)");
                }
            }
        }
        if ((fromProject || !req.exportProject.empty() || req.exportOnly) && !plat::supports_project())
            throw StepError(ErrorKind::MissingInput, "o projeto C++ editavel (exportar ou compilar a partir dele) so existe no "
                            "Builder do Windows nesta versao");
        if (fromProject)
            if (const std::string p = check_project(kit); !p.empty()) throw StepError(p);
        if (fromProject && !req.exportProject.empty())
            throw StepError("escolha entre exportar um projeto e compilar a partir de um projeto");
        if (req.exportOnly && (req.exportProject.empty() || fromProject))
            throw StepError(ErrorKind::MissingInput, "so exportar: escolha a pasta do projeto (e o CDI de origem)");
        if (req.buildType != "Release" && req.buildType != "Debug")
            throw StepError("tipo de build desconhecido: " + req.buildType + " (Release ou Debug)");
        if (req.outDir.empty()) throw StepError(ErrorKind::MissingInput, "escolha a pasta de saida");
        if (req.installName.empty() || req.installName.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos)
            throw StepError("nome de instalacao invalido");
        if (std::max(kit.native().size(), sdl3.native().size()) > kMaxOutPath)
            throw StepError("o Builder esta numa pasta com caminho longo demais (maximo " + std::to_string(kMaxOutPath) +
                            " caracteres). Mova a pasta do Builder para um caminho mais curto.");
        if (const std::string k = check_kit(kit, sdl3); !k.empty()) throw StepError(k + " (reinstale o Builder)");
        for (const fs::path& p : {kit, sdl3})  // Linux: caminhos que o CMake nao aceita (no Windows o kit e copiado)
            if (const std::string m = plat::build_path_problem(p); !m.empty()) throw StepError(ErrorKind::PathTooLong, m);
        // a compilacao acontece no cache (caminho curto); na saida ficam o jogo e o trabalho
        if (out.native().size() > kMaxOutPath)
            throw StepError(ErrorKind::PathTooLong, "o caminho da pasta de saida e longo demais (" + std::to_string(out.native().size()) +
                            " caracteres; maximo " + std::to_string(kMaxOutPath) + "). Escolha uma pasta mais curta, "
                            "por exemplo " + std::string(plat::example_out_dir()) + ".");
        fs::create_directories(out, ec);
        if (ec || !fs::is_directory(out, ec))
            throw StepError(kindOf(ec, ErrorKind::NoPermission), "nao foi possivel criar a pasta de saida " + path_text(out) + " (" + error_text(ec) + ")");
        probeWritable(out);
        fs::create_directories(work, ec);
        if (ec) throw StepError(kindOf(ec, ErrorKind::NoPermission), "nao foi possivel criar " + path_text(work) + " (" + error_text(ec) + ")");
        // uma execucao por pasta de saida (o cache, o pacote e a troca sao dela)
        if (!lock.acquire(work / L".bs-lock"))
            throw StepError(ErrorKind::Busy, "outra execucao do Builder esta usando " + path_text(out) + "; espere ela terminar");
        // pacotes temporarios de uma execucao interrompida (so os marcados por nos)
        for (const auto& e : fs::directory_iterator(out, ec))
            if (e.is_directory() && e.path().filename().native().rfind(plat::to_path(req.installName + L".new-").native(), 0) == 0 &&
                fs::exists(e.path() / kStageMarker))
                fs::remove_all(e.path(), ec);
        logFile.open(res.logFile, std::ios::trunc);
        {
            const std::time_t t = std::time(nullptr);
            char              ts[64];
            std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
            log(std::string("Blue Stinger Builder, ") + ts);
        }
        log("imagem: " + path_text(req.disc));
        log("saida: " + path_text(out));
        log("kit: " + path_text(kit));
        const fs::path cache = cacheDir(out, work);
        log("cache de compilacao: " + path_text(cache));
        std::ofstream(cache / L".bs-out", std::ios::binary | std::ios::trunc) << path_text(out) << "\n";  // de quem e

        // 2. compilador
        if (!req.exportOnly) enter(Step::Toolchain);
        Toolchain tc = req.exportOnly ? Toolchain{} : [&] {
            try {
                return find_toolchain({req.vswhere, req.vcvars, req.toolchainDir, req.systemToolchain}, log);
            } catch (const std::runtime_error& e) {
                throw StepError(ErrorKind::Toolchain, e.what());
            }
        }();
        if (!req.exportOnly)
            if (const std::string m = plat::build_path_problem(tc.installRoot); !m.empty()) throw StepError(ErrorKind::PathTooLong, m);
        if (!req.exportOnly) plat::toolchain_env(tc.env, cache);  // Linux: o cache do Zig ao lado do cache de compilacao

        // 3. disco: hash do executavel e extracao dos arquivos de boot
        enter(Step::Extract);
        BootFiles      boot;
        const fs::path gameData = work / L"game-data";
        if (!req.disc.empty()) {
            try {
                boot = boot_read_disc(req.disc);
            } catch (const std::exception& e) {
                throw StepError(ErrorKind::InvalidDisc, std::string("esta imagem nao pode ser usada: ") + e.what() +
                                "\nE preciso a imagem CDI do Blue Stinger NTSC-U (T13001N V1.000).");
            }
            log("disco: " + boot.product + " " + boot.version + " (" + boot.versionId + ")");
            fs::create_directories(gameData);
            writeFileAtomic(gameData / L"IP.BIN", boot.ipBin);
            writeFileAtomic(gameData / L"1ST_READ.BIN", boot.firstRead);
        }

        // dados de execucao (o jogo nao le o CDI): preparados uma vez em .bs-build\data e
        // conferidos setor a setor contra a imagem. Reaproveitados se a marca e o conteudo
        // (SHA-256 de cada arquivo) baterem. Sem o CDI (compilacao a partir de um projeto), so
        // da para reaproveitar.
        const fs::path dataDir   = work / L"data";
        const fs::path dataStamp = work / L"data.ok";
        const std::string fmtPrefix = std::to_string(disc::kRuntimeDataFormat) + " ";
        std::string       stampText = fmtPrefix + boot.versionId;
        if (req.disc.empty()) {
            const std::string s = readText(dataStamp);
            if (s.rfind(fmtPrefix, 0) != 0)
                throw StepError(ErrorKind::Data, "os dados do jogo ainda nao foram preparados nesta pasta de saida: informe o CDI "
                                "uma vez (eles ficam guardados para as proximas compilacoes)");
            stampText = s;
        }
        bool reuse = readText(dataStamp) == stampText;
        if (reuse) {
            progress(0.1, "conferindo os dados ja preparados");
            try {
                const std::string bad = disc::check_runtime_files(dataDir, [&](double f, const std::string&) {
                    progress(0.1 + 0.8 * f, "conferindo os dados ja preparados");
                });
                if (!bad.empty()) {
                    log("dados preparados alterados (" + bad + "): preparando de novo");
                    reuse = false;
                }
            } catch (const std::exception& e) {
                log(std::string("dados preparados invalidos (") + e.what() + "): preparando de novo");
                reuse = false;
            }
        }
        if (!reuse && req.disc.empty())
            throw StepError(ErrorKind::Data, "os dados preparados nesta saida estao alterados ou incompletos: informe o CDI para "
                            "prepara-los de novo");
        if (!reuse) {
            fs::remove(dataStamp, ec);
            fs::remove_all(dataDir, ec);  // pasta do proprio Builder (.bs-build\data)
            if (ec) throw StepError("nao foi possivel limpar " + path_text(dataDir) + " (" + error_text(ec) + ")");
            try {
                disc::CdiImage img(req.disc);
                const auto     r = disc::prepare_runtime_data(img, dataDir, [&](double f, const std::string& w) {
                    progress(0.6 * f, "preparando os dados: " + w);
                }, cb.cancel);
                log("dados: " + std::to_string(r.files) + " arquivos, " + std::to_string(r.bytes >> 20) + " MB, " +
                    std::to_string(r.metaSectors) + " setores de sistema");
                const std::string diff = disc::verify_runtime_data(img, dataDir, [&](double f, const std::string&) {
                    progress(0.6 + 0.4 * f, "conferindo os dados setor a setor");
                }, cb.cancel);
                if (!diff.empty()) throw StepError(ErrorKind::InvalidDisc, "os dados preparados nao conferem com a imagem: " + diff);
                log("dados conferidos setor a setor com a imagem");
            } catch (const disc::RuntimeDataError& e) {
                if (cb.cancel && cb.cancel->load()) throw Cancelled{};
                throw StepError(std::string("falha ao preparar os dados do jogo: ") + e.what());
            } catch (const disc::DiscError& e) {
                throw StepError(std::string("falha ao ler o disco: ") + e.what());
            }
            // a marca libera futuras compilacoes sem CDI: gravada por troca e conferida
            // (revisao Codex 2026-10-15: falha na gravacao passava despercebida)
            {
                const fs::path tmp = withSuffix(dataStamp, L".tmp");
                std::ofstream  st(tmp, std::ios::binary | std::ios::trunc);
                st << stampText;
                st.close();
                if (!st) throw StepError("nao foi possivel gravar " + path_text(tmp));
                fs::rename(tmp, dataStamp, ec);
                if (ec) throw StepError("nao foi possivel gravar " + path_text(dataStamp) + " (" + error_text(ec) + ")");
            }
        } else {
            log("dados ja preparados e conferidos: reaproveitados");
        }
        // N5: filmes do disco -> data\video (MP4 + manifesto). Reaproveita por SHA-256 da fonte, do
        // conversor e do MP4; sem o codificador do Windows os filmes ficam ausentes e a compilacao segue.
        if (req.movies) {
            video::MovieBuildOptions mo;
            mo.progress = [&](double f, const std::string& w) { progress(f, "filmes: " + w); };
            mo.log      = log;
            mo.cancel   = cb.cancel;
            video::MovieBuildResult mr;
            std::string             merr;
            // Windows: MP4 pelo Media Foundation; Linux (N9-L L5): so o manifesto, os SFD tocam direto do disco
            if (!video::prepare_movies(dataDir / L"disc" / L"BSDATA", dataDir / L"video", mo, mr, &merr)) {
                if (cb.cancel && cb.cancel->load()) throw Cancelled{};
                throw StepError(std::string(video::movie_failure_prefix()) + merr);
            }
            log(video::movie_summary(mr));
        } else {
            // os de uma compilacao anterior nesta saida tambem saem (o pacote copia data\ inteiro)
            std::string merr;
            if (!video::clear_movies(dataDir / L"video", &merr)) throw StepError(merr);
            log("filmes: conversao desligada (--sem-filmes); o jogo os trata como ausentes");
        }

        // 4. analise + C++ (so os arquivos que mudaram sao regravados)
        enter(Step::Recompile);
        {
            RecompOptions ro;
            ro.config      = kit / L"config" / L"bluestinger_us.toml";
            ro.executable  = gameData / L"1ST_READ.BIN";
            ro.out         = cache / L"generated";
            ro.analysisDir = work / L"analysis";
            std::ostringstream s;
            if (fromProject) {
                // o C++ do projeto (talvez editado) vai para o cache, espelhado (arquivos que
                // sairam do projeto tambem saem do cache)
                const fs::path src = kit / L"generated", dst = cache / L"generated";
                fs::create_directories(dst);
                for (const auto& e : fs::directory_iterator(dst))
                    if (!fs::exists(src / e.path().filename(), ec)) fs::remove_all(e.path(), ec);
                syncTree(src, dst);
                log("usando o C++ do projeto: " + path_text(src));
            } else try {
                const RecompResult r = recomp_run(ro, s);
                if (!r.files) throw std::runtime_error("nenhum arquivo gerado");
            } catch (const std::exception& e) {
                std::istringstream in(s.str());
                for (std::string l; std::getline(in, l);) log(l);
                throw StepError(std::string("falha na recompilacao: ") + e.what());
            }
            std::istringstream in(s.str());
            for (std::string l; std::getline(in, l);) log(l);
        }

        const fs::path toml = toml_dir(kit, sdl3);
        std::string    version = req.versionLabel.empty() ? readText(kit / L"VERSION") : req.versionLabel;
        if (version.empty()) version = "dev";
        auto exportNow = [&] {
            enter(Step::Export);
            try {
                export_project({kit, sdl3, toml, cache / L"generated", work / L"analysis" / L"functions.tsv", version, out},
                               fs::absolute(req.exportProject), log, cb.cancel);
            } catch (const ExportCancelled&) {
                throw Cancelled{};
            } catch (const std::exception& e) {
                throw StepError(std::string("falha ao exportar o projeto: ") + e.what());
            }
            // cancelado durante a ultima copia: nunca vira "exportado" (revisao Codex 2026-10-20)
            if (cb.cancel && cb.cancel->load()) throw Cancelled{};
            res.projectDir = fs::absolute(req.exportProject);
        };
        if (req.exportOnly) {  // so exportar: sem configurar, compilar, verificar nem instalar
            exportNow();
            res.installDir.clear(), res.exe.clear();
            step = Step::Done;
            log("OK (so exportar): projeto em " + path_text(res.projectDir) + "; recursos em " + path_text(dataDir));
            progress(1, step_name(Step::Done));
            res.ok = true;
            return res;
        }

        // 5. CMake, no cache. Kit com caminho fora da pagina ANSI ou longo: copia (so o que
        // mudou) para o cache. Build incremental recriado se o kit ou o compilador mudaram.
        enter(Step::Configure);
        fs::path       srcKit = kit, srcSdl3 = sdl3, srcToml = toml;
        if (!ansi_safe(kit) || kit.native().size() > kMaxDirectKit) {
            srcKit = cache / L"kit";
            log("copiando o kit para " + path_text(srcKit));
            for (const wchar_t* e : kKitEntries) syncTree(kit / e, srcKit / e);
        }
        if (!ansi_safe(sdl3) || sdl3.native().size() > kMaxDirectKit) {
            srcSdl3 = cache / L"sdl3";
            log("copiando as fontes do SDL3 para " + path_text(srcSdl3));
            syncTree(sdl3, srcSdl3);
        }
        if (!ansi_safe(toml) || toml.native().size() > kMaxDirectKit) {
            srcToml = cache / L"tomlplusplus";
            log("copiando as fontes do toml++ para " + path_text(srcToml));
            syncTree(toml, srcToml);
        }
        // mods do projeto (so com --de-projeto): espelhados no cache (o Ninja precisa de caminho
        // curto e ANSI); os .cpp de mods/ entram no executavel com BS_MODS=ON
        const fs::path modsSrc = cache / L"mods";
        fs::remove_all(modsSrc, ec);
        if (fromProject && fs::is_directory(kit / L"mods", ec)) {
            fs::create_directories(modsSrc);
            for (const auto& e : fs::directory_iterator(kit / L"mods"))
                if (e.is_regular_file()) fs::copy_file(e.path(), modsSrc / e.path().filename());
            size_t n = 0;
            for (const auto& e : fs::directory_iterator(modsSrc)) n += e.path().extension() == L".cpp";
            log("mods em C++: " + std::to_string(n));
        }
        const fs::path    buildDir = cache / (req.buildType == "Debug" ? L"cmake-debug" : L"cmake");
        const std::string stamp    = path_text(kit) + "\n" + path_text(sdl3) + "\n" + path_text(toml) + "\n" + tc.stamp;
        if (req.clean || readText(buildDir / L".bs-kit") != stamp) {
            if (fs::exists(buildDir, ec)) log("build incremental descartado (kit, compilador ou pedido)");
            fs::remove_all(buildDir, ec);
            if (ec) throw StepError("nao foi possivel limpar " + path_text(buildDir) + " (" + error_text(ec) + ")");
        }
        fs::create_directories(buildDir);
        {
            // (no Windows, a mesma linha de comando de antes: escolha do compilador pelo toolchain e dependencias pela
            // plataforma, nas mesmas posicoes)
            std::vector<std::wstring> cmd = {path_wide(tc.cmake), L"-S", path_wide(srcKit), L"-B", path_wide(buildDir),
                                             L"-G", L"Ninja", L"-DCMAKE_MAKE_PROGRAM=" + cmakePath(tc.ninja),
                                             L"-DCMAKE_BUILD_TYPE=" + widen(req.buildType)};
            cmd.insert(cmd.end(), tc.cmakeArgs.begin(), tc.cmakeArgs.end());
            for (const wchar_t* a : {L"-DBS_BUILD_TOOLS=OFF", L"-DBS_BUILD_TESTS=OFF", L"-DBS_BUILD_GAME=ON"}) cmd.push_back(a);
            cmd.push_back(L"-DBS_GENERATED_DIR=" + cmakePath(cache / L"generated"));
            for (auto& a : plat::dependency_args(srcKit, srcSdl3, srcToml)) cmd.push_back(std::move(a));
            cmd.push_back(L"-DFETCHCONTENT_FULLY_DISCONNECTED=ON");
            cmd.push_back(L"-DBS_VERSION_OVERRIDE=" + widen(version));
            if (fromProject) cmd.push_back(L"-DBS_MODS=ON"), cmd.push_back(L"-DBS_MODS_DIR=" + cmakePath(modsSrc));
            else cmd.push_back(L"-DBS_MODS=OFF"), cmd.push_back(L"-DBS_MODS_DIR=");
            if (run(cmd, buildDir, tc.env, nullptr) != 0)
                throw StepError(ErrorKind::Build, "a configuracao do CMake falhou:\n" + tail.summary());
            std::ofstream(buildDir / L".bs-kit", std::ios::binary | std::ios::trunc) << stamp;  // binario: sem \r
        }

        // 6. compilacao: progresso pelas linhas "[n/m]" do ninja
        enter(Step::Compile);
        {
            const std::vector<std::wstring> cmd = {path_wide(tc.cmake), L"--build", path_wide(buildDir), L"--target", L"bluestinger"};
            const int rc = run(cmd, buildDir, tc.env, [&](const std::string& l) {
                unsigned n = 0, m = 0;
                if (l.size() > 2 && l[0] == '[' && std::sscanf(l.c_str(), "[%u/%u]", &n, &m) == 2 && m)
                    progress(double(n) / m, std::string(step_name(Step::Compile)) + " " + std::to_string(n) + "/" +
                                                std::to_string(m));
            });
            if (rc != 0) throw StepError(ErrorKind::Build, "a compilacao falhou:\n" + tail.summary());
        }
        const fs::path built = buildDir / L"app" / plat::game_binary_built();
        if (!fs::is_regular_file(built, ec)) throw StepError("a compilacao nao produziu " + path_text(built));

        // 7. pacote numa pasta temporaria ao lado do destino (mesmo volume: troca por rename)
        enter(Step::Package);
        fs::path stage;
        for (int i = 0; i < 16 && stage.empty(); ++i) {  // criacao exclusiva: nunca reaproveita uma pasta
            const fs::path  s = out / plat::to_path(req.installName + L".new-" + widen(randomSuffix()));
            std::error_code c;
            if (plat::create_dir_exclusive(s, c)) stage = s;
            else if (c)
                throw StepError(kindOf(c, ErrorKind::Other), "nao foi possivel criar o pacote em " + path_text(out) + " (" + error_text(c) + ")");
        }
        if (stage.empty()) throw StepError("nao foi possivel criar uma pasta de pacote exclusiva");
        std::ofstream(stage / kStageMarker, std::ios::binary) << "pacote temporario do Blue Stinger Builder\n";
        struct StageGuard {  // pacote que nao foi instalado sai no fim, com sucesso ou nao (so se e nosso)
            fs::path p;
            ~StageGuard() {
                std::error_code e;
                if (!p.empty() && fs::exists(p / kStageMarker, e)) fs::remove_all(p, e);
            }
        } guard{stage};
        fs::create_directories(stage / L"licencas");
        fs::copy_file(built, stage / plat::game_binary_installed());
        plat::make_executable(stage / plat::game_binary_installed());
        if (!tc.crtDir.empty())  // runtime do MSVC ao lado do jogo (Linux: nada, o C++ vai ligado no executavel)
            for (const auto& e : fs::directory_iterator(tc.crtDir))
                if (e.path().extension() == L".dll") fs::copy_file(e.path(), stage / e.path().filename());
        fs::copy_file(kit / plat::readme_in_kit(), stage / L"LEIA-ME.txt");
        fs::copy_file(kit / L"LICENSE", stage / L"licencas" / L"BlueStingerRecompiled-MIT.txt");
        if (fs::is_regular_file(sdl3 / L"LICENSE.txt", ec))
            fs::copy_file(sdl3 / L"LICENSE.txt", stage / L"licencas" / L"SDL3-zlib.txt");
        if (fs::is_regular_file(toml / L"LICENSE", ec))  // N3-1: o jogo le controles.toml com o toml++
            fs::copy_file(toml / L"LICENSE", stage / L"licencas" / L"tomlplusplus-MIT.txt");
        for (const auto& [from, name] : plat::extra_licenses(kit, sdl3))  // Linux: FreeType, Noto Sans, pl_mpeg...
            if (fs::is_regular_file(from, ec)) fs::copy_file(from, stage / L"licencas" / name);
        // edicao e local dos saves escolhidos (sem escolha: o instalacao.toml anterior fica, como
        // arquivo do usuario). Nao faz parte da lista do pacote: nunca e apagado por uma troca.
        for (const auto& [from, name] : req.extraPackageFiles) {  // N9.1: o desinstalador do instalador
            const fs::path to = stage / plat::to_path(name);
            fs::create_directories(to.parent_path());
            fs::copy_file(from, to, fs::copy_options::overwrite_existing);
            plat::make_executable(to);
            log("no pacote: " + path_text(to.filename()));
        }
        if (req.portableSaves || req.edition || req.subtitles) {
            std::string     iwarn;
            const native::install::InstallConfig ic = install_config_for(req, res.installDir, &iwarn);
            if (!iwarn.empty()) log(iwarn);
            std::string     ierr;
            std::error_code icode;
            if (!native::install::write_config(stage, ic, &ierr, &icode)) throw StepError(kindOf(icode, ErrorKind::Other), ierr);
            log(std::string("edicao: ") + native::edition::edition_name(ic.edition) + "; saves: " +
                (ic.saves == native::install::SavesLocation::Portable ? "portatil (saves\\ na instalacao)" : "Documentos"));
        }
        // dados do jogo: hard links dos preparados (sem copiar 700 MB; copia se o volume nao
        // aceitar links). Os arquivos nunca sao reescritos no lugar: o Builder prepara de novo
        // numa pasta nova, entao os links nao misturam versoes.
        {
            size_t copied = 0, linked = 0;
            for (const auto& e : fs::recursive_directory_iterator(dataDir)) {
                const fs::path to = stage / L"data" / fs::relative(e.path(), dataDir);
                if (e.is_directory()) {
                    fs::create_directories(to);
                    continue;
                }
                // a ordem da listagem e a do sistema de arquivos (no ext4, a do hash: um arquivo da raiz pode vir antes
                // de qualquer pasta); a pasta do arquivo e criada antes dele (N9-L L5, B10)
                fs::create_directories(to.parent_path());
                std::error_code le;
                fs::create_hard_link(e.path(), to, le);
                if (le) fs::copy_file(e.path(), to), ++copied;
                else ++linked;
            }
            log("dados no pacote: " + std::to_string(linked) + " links, " + std::to_string(copied) + " copias");
        }
        // mods de arquivos do projeto: <instalacao>\mods\assets (o jogo le no lugar dos originais)
        if (fromProject && fs::is_directory(kit / L"mods" / L"assets", ec)) {
            fs::create_directories(stage / L"mods" / L"assets");
            fs::copy(kit / L"mods" / L"assets", stage / L"mods" / L"assets", fs::copy_options::recursive);
            log("mods de arquivos copiados para o pacote");
        }
        // N5: filmes e legendas de mods do projeto: <instalacao>\mods\video (substituem os convertidos)
        if (fromProject && fs::is_directory(kit / L"mods" / L"video", ec)) {
            fs::create_directories(stage / L"mods" / L"video");
            fs::copy(kit / L"mods" / L"video", stage / L"mods" / L"video", fs::copy_options::recursive);
            log("mods de video copiados para o pacote");
        }
        // mods padrao do projeto (kit\mods-padrao\<id>\mod.toml, so texto: as legendas), em toda instalacao: vao para
        // <instalacao>\mods\<id>; as legendas dos filmes (video\ do pacote) tambem para mods\video, onde o player as
        // procura, sem substituir os mods de video do projeto copiados acima
        if (fs::is_directory(kit / L"mods-padrao", ec)) {
            size_t n = 0;
            for (const auto& e : fs::directory_iterator(kit / L"mods-padrao")) {
                if (!e.is_directory() || !fs::is_regular_file(e.path() / L"mod.toml", ec)) continue;
                fs::create_directories(stage / L"mods" / e.path().filename());
                fs::copy(e.path(), stage / L"mods" / e.path().filename(), fs::copy_options::recursive);
                if (fs::is_directory(e.path() / L"video", ec)) {
                    fs::create_directories(stage / L"mods" / L"video");
                    fs::copy(e.path() / L"video", stage / L"mods" / L"video",
                             fs::copy_options::recursive | fs::copy_options::skip_existing);
                }
                log("mod padrao: " + path_text(e.path().filename()));
                ++n;
            }
            if (n == 0) log("mods-padrao sem nenhum pacote (mod.toml)");
        }

        // 8. verificacao do executavel montado, SEM o CDI: responde, da boot com os dados do
        // pacote (data\ ao lado do exe) e confere o SHA-256 de todos os arquivos de dados
        enter(Step::Verify);
        std::string verLine;
        {
            const std::vector<std::wstring> cmd = {path_wide(stage / plat::game_binary_installed()), L"--version"};
            int rc = 0;
            // codigo 0 sem a linha de versao: era a stdout perdida no fim de um processo curto,
            // corrigida no main do jogo (N8.8, test_builder confere 400 execucoes seguidas).
            // A nova tentativa fica, registrada no log, para um projeto editado pelo usuario
            for (int attempt = 0; attempt < 2; ++attempt) {
                verLine.clear();
                rc = run(cmd, stage, {}, [&](const std::string& l) {
                    if (verLine.empty()) verLine = l;
                }, 60000);
                if (rc == 0 && verLine.find("Blue Stinger") != std::string::npos) break;
                log("--version sem a linha esperada (codigo " + std::to_string(rc) + ", primeira linha \"" + verLine +
                    "\")" + (attempt == 0 ? ": tentando de novo" : ""));
            }
            if (rc != 0 || verLine.find("Blue Stinger") == std::string::npos)
                throw StepError("o executavel compilado nao respondeu a --version (codigo " + std::to_string(rc) +
                                ", saida \"" + verLine + "\")");
        }
        progress(0.1, "conferindo os dados do pacote");
        {
            const std::vector<std::wstring> cmd = {path_wide(stage / plat::game_binary_installed()), L"--verify-data"};
            const int          rc  = run(cmd, stage, {}, nullptr, 600000);
            if (rc != 0) throw StepError(ErrorKind::Verify, "os dados do pacote nao conferem:\n" + tail.summary());
        }
        progress(0.4, "boot de teste (sem o CDI)");
        {
            const std::wstring n   = std::to_wstring(req.verifyVblanks);
            const std::vector<std::wstring> cmd = {path_wide(stage / plat::game_binary_installed()), L"--headless", n};
            bool       reached = false;
            const int  rc      = run(cmd, stage, {}, [&](const std::string& l) {
                if (l.rfind("VBlanks: " + std::to_string(req.verifyVblanks) + ";", 0) == 0) reached = true;
            }, 600000);
            if (rc != 0 || !reached)
                throw StepError(ErrorKind::Verify, "o executavel compilado falhou no boot de teste (codigo " + std::to_string(rc) + "):\n" +
                                tail.summary());
        }
        {
            const std::time_t t = std::time(nullptr);
            char              ts[64];
            std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M", std::localtime(&t));
            std::ofstream v(stage / L"VERSAO.txt", std::ios::trunc);
            v << verLine << "\nDistribuicao: compilada pelo Blue Stinger Builder (uso local; nao publicar)\n"
              << "Montada em: " << ts << "\n" << tc.description;
        }

        // 9. projeto C++ editavel (opcional), antes da troca: uma falha aqui nao instala nada
        if (!req.exportProject.empty()) exportNow();

        // 10. troca (a anterior so sai depois que a nova esta no lugar)
        enter(Step::Install);
        fs::remove(stage / kStageMarker, ec);  // nao vai para o jogo instalado
        try {
            install_swap(stage, res.installDir, isPackage, cb.cancel);
        } catch (const ArchiveError& e) {  // a nova ja esta instalada: sucesso com aviso
            res.warning = e.what();
            log(std::string("AVISO: ") + e.what());
        } catch (const SwapCancelled&) {
            std::ofstream(stage / kStageMarker, std::ios::binary) << "x";  // o guard volta a poder apagar
            throw Cancelled{};
        } catch (const std::exception& e) {
            std::ofstream(stage / kStageMarker, std::ios::binary) << "x";
            const auto* se = dynamic_cast<const SwapError*>(&e);
            throw StepError(se ? kindOf(se->code, ErrorKind::Install) : ErrorKind::Install, e.what());
        }
        guard.p.clear();
        step = Step::Done;
        log("OK: " + path_text(res.exe));
        log(verLine);
        progress(1, step_name(Step::Done));
        res.ok = true;
    } catch (const Cancelled&) {
        res.cancelled  = true;
        res.kind       = ErrorKind::Cancelled;
        res.failedStep = step;
        res.error      = "cancelado pelo usuario; a versao instalada anterior foi mantida";
        log("cancelado");
    } catch (const StepError& e) {
        res.kind       = e.kind;
        res.failedStep = step;
        res.error      = e.what();
        log(std::string("ERRO (") + step_name(step) + "): " + e.what());
    } catch (const fs::filesystem_error& e) {
        res.kind       = kindOf(e.code(), step == Step::Install ? ErrorKind::Install : ErrorKind::Other);
        res.failedStep = step;
        res.error      = std::string(step_name(step)) + ": " + error_text(e.code()) + " (" + path_text(e.path1()) + ")";
        log("ERRO: " + res.error);
    } catch (const std::exception& e) {
        res.kind       = ErrorKind::Other;
        res.failedStep = step;
        res.error      = std::string(step_name(step)) + ": " + e.what();
        log("ERRO: " + res.error);
    }
    return res;
}

} // namespace bs::builder
