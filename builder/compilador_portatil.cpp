// Compilador portatil do Windows (compilador_portatil.h).
#include "compilador_portatil.h"

#include "builder_platform.h"
#include "child_process.h"
#include "disc/archive.h"
#include "disc/disc_image.h"  // DiscError
#include "util/hash.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace bs::builder::portatil {

namespace fs = std::filesystem;

namespace {

constexpr const char* kZigTopo   = "zig-windows-x86_64-0.13.0/";
constexpr const char* kCmakeTopo = "cmake-3.30.5-windows-x86_64/";

void gravar(const fs::path& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o << s;
    o.close();
    if (!o) throw std::runtime_error("nao foi possivel gravar " + path_text(p));
}

void cancelado(const Pedido& p) {
    if (p.cancel && p.cancel->load()) throw std::runtime_error("cancelado");
}

// baixa para .part, confere o SHA-256 e so entao poe no lugar (ja baixado e conferido: nada)
void baixar(const Pedido& p, const Peca& peca, const fs::path& dst, double f0, double f1, widescreen::Andamento& an) {
    std::error_code ec;
    if (fs::is_regular_file(dst, ec) && widescreen::sha256_file(dst) == peca.sha256) return;
    an.arquivo = peca.nome, an.recebidos = 0, an.tamanho = peca.bytes;
    if (p.andamento) p.andamento(an);
    const uint64_t base = an.sessao;
    fs::create_directories(dst.parent_path(), ec);
    const fs::path part = fs::path(dst).concat(".part");
    std::string    err;
    if (p.log) p.log(std::string("compilador portatil: baixando ") + peca.nome + " (" + peca.url + ")");
    const bool ok = p.fetch && p.fetch(peca.url, part, p.cancel, [&](uint64_t got, uint64_t total) {
        const uint64_t t = total ? total : peca.bytes;
        if (p.progresso && t) p.progresso(f0 + (f1 - f0) * std::min(1.0, double(got) / double(t)), std::string("baixando ") + peca.nome);
        an.recebidos = got, an.tamanho = t, an.sessao = base + got;
        if (p.andamento) p.andamento(an);
    }, &err);
    if (!ok) {
        fs::remove(part, ec);
        cancelado(p);
        throw std::runtime_error(std::string("nao foi possivel baixar ") + peca.nome + " de " + peca.url +
                                 (err.empty() ? "" : " (" + err + ")"));
    }
    const std::string got = widescreen::sha256_file(part);
    if (got != peca.sha256) {
        fs::remove(part, ec);
        throw std::runtime_error(std::string("o download de ") + peca.nome + " nao confere (SHA-256 " + got + ", esperado " +
                                 peca.sha256 + "): nada foi usado");
    }
    fs::remove(dst, ec);
    fs::rename(part, dst, ec);
    if (ec) throw std::runtime_error("nao foi possivel gravar " + path_text(dst));
}

bool comeca(const std::string& s, const char* prefixo) { return s.rfind(prefixo, 0) == 0; }

// O que do Zig nao serve ao alvo x86_64-windows-gnu e nao e extraido (mantenedor, 2026-11-20, item 3): as libc e os
// cabecalhos de outros sistemas (musl, WASI, glibc, Darwin; em lib/libc/include so o any-windows-any fica), o
// ThreadSanitizer e a documentacao. Fica o mingw-w64, os cabecalhos do clang, a std e o compiler_rt (que o Zig compila), a
// libc++, a libc++abi e a libunwind. O mesmo filtro esta em tools/dev/conferir_compilador_portatil.py.
bool podado(const std::string& rel) {
    for (const char* p : {"lib/libc/musl/", "lib/libc/wasi/", "lib/libc/glibc/", "lib/libc/darwin/", "lib/tsan/", "lib/docs/"})
        if (comeca(rel, p)) return true;
    return comeca(rel, "lib/libc/include/") && !comeca(rel, "lib/libc/include/any-windows-any/");
}

void extrair(const Pedido& p, const fs::path& zip, const fs::path& dir, disc::ArchiveSelection sel, double f0, double f1,
             const char* oque) {
    try {
        disc::archive_extract_selected(zip, dir, sel, [&](double f) {
            if (p.progresso) p.progresso(f0 + (f1 - f0) * f, std::string("extraindo ") + oque);
        }, p.cancel);
    } catch (const disc::DiscError& e) {
        cancelado(p);
        throw std::runtime_error(std::string("nao foi possivel extrair ") + oque + ": " + e.what());
    }
}

// move <de>/<topo sem a barra> para <para>
void subir(const fs::path& de, const char* topo, const fs::path& para) {
    std::string t = topo;
    t.pop_back();
    std::error_code ec;
    fs::rename(de / t, para, ec);
    if (ec) throw std::runtime_error("nao foi possivel montar " + path_text(para) + " (" + ec.message() + ")");
    fs::remove_all(de, ec);
}

const char* const kToolchainCmake = R"(# Blue Stinger Builder: compilador portatil do Windows (Zig 0.13.0: Clang/LLD 18, alvo x86_64-windows-gnu com o
# mingw-w64 e a libc++ estatica, CPU base). Montado pelo Builder a partir dos downloads oficiais conferidos
# (builder/compilador_portatil.cpp). As opcoes de fidelidade (-fno-strict-aliasing -fwrapv -ffp-contract=off) vem do
# CMakeLists.txt do kit, as mesmas do Linux.
get_filename_component(_bs_tc "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(_bs_zig "${_bs_tc}/zig/zig.exe")
set(CMAKE_C_COMPILER   "${_bs_zig}")
set(CMAKE_C_COMPILER_ARG1   "cc -target x86_64-windows-gnu -mcpu=baseline -g0")
set(CMAKE_CXX_COMPILER "${_bs_zig}")
set(CMAKE_CXX_COMPILER_ARG1 "c++ -target x86_64-windows-gnu -mcpu=baseline -g0")
set(CMAKE_ASM_COMPILER "${_bs_zig}")
set(CMAKE_ASM_COMPILER_ARG1 "cc -target x86_64-windows-gnu -mcpu=baseline -g0")
set(CMAKE_RC_COMPILER  "${_bs_zig}")
set(CMAKE_AR     "${_bs_tc}/bin/bs-ar.cmd"     CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${_bs_tc}/bin/bs-ranlib.cmd" CACHE FILEPATH "" FORCE)
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${_bs_tc}/bs-regras.cmake")
# so o que esta no compilador e no kit: nada de bibliotecas nem cabecalhos de outras instalacoes do sistema
set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH OFF)
set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH OFF)
set(CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY ON)
)";

const char* const kRegras = R"(# recursos .rc pelo compilador de recursos do Zig (sintaxe do rc.exe; o .res vai direto para o ligador)
set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> rc <DEFINES> <INCLUDES> <FLAGS> /fo <OBJECT> -- <SOURCE>")
set(CMAKE_RC_OUTPUT_EXTENSION .res)
)";

// troca recuperavel (revisao Codex 20261120-214554): a antiga vira <pasta>.antiga; a nova entra e e conferida no lugar;
// so entao a antiga sai. Qualquer falha devolve a antiga (a nova vai para <pasta>.quarentena) e a mensagem diz onde ficou.
void trocar(const fs::path& nova, const fs::path& pasta, const std::string& esperado) {
    std::error_code ec;
    const fs::path  antiga = fs::path(pasta).concat(".antiga"), quarentena = fs::path(pasta).concat(".quarentena");
    fs::remove_all(quarentena, ec);
    // a .antiga ja foi recuperada pelo preparar, sob a trava; uma .antiga ainda valida e a unica copia boa: nunca sai aqui
    if (pronta(antiga, nullptr, esperado))
        throw std::runtime_error("compilador portatil: " + path_text(antiga) + " ainda e a copia boa; nada foi trocado");
    fs::remove_all(antiga, ec);
    const bool tinha = fs::exists(pasta, ec);
    if (tinha) {
        std::error_code e1;
        fs::rename(pasta, antiga, e1);
        if (e1) throw std::runtime_error("nao foi possivel trocar " + path_text(pasta) + " (" + e1.message() + "); a anterior continua no lugar");
    }
    auto devolver = [&](const std::string& oque) -> std::string {
        if (!tinha) return oque;
        std::error_code e3;
        fs::rename(antiga, pasta, e3);
        return e3 ? oque + "; a anterior ficou em " + path_text(antiga) + " e volta na proxima instalacao (" + e3.message() + ")"
                  : oque + "; a anterior foi mantida";
    };
    std::error_code e2;
    fs::rename(nova, pasta, e2);
    if (e2) throw std::runtime_error(devolver("nao foi possivel montar " + path_text(pasta) + " (" + e2.message() + ")"));
    std::string motivo;
    if (!pronta(pasta, &motivo, esperado)) {
        std::error_code e4;
        fs::rename(pasta, quarentena, e4);
        throw std::runtime_error(devolver("o compilador portatil no lugar nao confere (" + motivo + ")"));
    }
    fs::remove_all(antiga, ec);
}

} // namespace

std::string hash_arvore(const fs::path& pasta) {
    std::error_code ec;
    if (!fs::is_directory(pasta, ec)) return {};
    std::vector<std::pair<std::string, fs::path>> arquivos;
    for (fs::recursive_directory_iterator it(pasta, ec), fim; !ec && it != fim; it.increment(ec)) {
        // so arquivos comuns e pastas: ligacao, junction, ponto de reparse ou qualquer outro tipo invalida a pasta
        const fs::file_type t = it->symlink_status(ec).type();
        if (ec) return {};
        if (t == fs::file_type::directory) continue;
        if (t != fs::file_type::regular) return {};
        const auto u = fs::relative(it->path(), pasta, ec).generic_u8string();
        arquivos.push_back({std::string(u.begin(), u.end()), it->path()});
    }
    if (ec) return {};
    std::sort(arquivos.begin(), arquivos.end());
    std::string lista;
    for (const auto& [rel, caminho] : arquivos) {
        const std::string h = widescreen::sha256_file(caminho);
        if (h.empty()) return {};
        lista += h + "  " + rel + "\n";
    }
    return util::sha256Hex(lista.data(), lista.size());
}

std::vector<std::string> resumo() {
    std::vector<std::string> v;
    for (const Peca& p : kPecas)
        v.push_back(std::string(p.nome) + " (" + p.licenca + "), " +
                    (p.bytes >= 1000000 ? std::to_string((p.bytes + 999999) / 1000000) + " MB" : std::to_string((p.bytes + 999) / 1000) + " KB") +
                    ", de " + p.url + ", conferido pelo SHA-256");
    return v;
}

uint64_t bytes_do_download() {
    uint64_t t = 0;
    for (const Peca& p : kPecas) t += p.bytes;
    return t;
}

fs::path pasta_padrao() {
    for (const fs::path& raiz : plat::cache_roots_privadas()) {
        const fs::path p = raiz / "compilador" / kVersao;
        if (plat::build_path_ok(p) && p.native().size() <= plat::max_cache_path()) return p;
    }
    return {};
}

bool pronta(const fs::path& pasta, std::string* motivo, const std::string& esperado) {
    std::error_code ec;
    if (!fs::is_directory(pasta, ec)) {
        if (motivo) *motivo = "ausente";
        return false;
    }
    const std::string h = hash_arvore(pasta);
    if (h != esperado) {
        if (motivo) *motivo = "conteudo diferente do esperado (hash " + (h.empty() ? std::string("ilegivel") : h) + ")";
        return false;
    }
    return true;
}

std::shared_ptr<void> preparar(const Pedido& p) {
    std::string     motivo;
    std::error_code ec;
    fs::create_directories(p.pasta.parent_path(), ec);
    // a trava vale para a montagem E para o uso (revisao Codex 20261120-214554): quem a tem pode montar, trocar e compilar;
    // outra instalacao com o compilador portatil espera aqui ate esta terminar de compilar
    auto           trava        = std::make_shared<plat::DirLock>();
    const fs::path arquivoTrava = fs::path(p.pasta).concat(".trava");
    for (int i = 0; !trava->acquire(arquivoTrava); ++i) {
        cancelado(p);
        if (i == 0 && p.log) p.log("compilador portatil: esperando outra instalacao que o usa ou monta");
        if (i > 7200) throw std::runtime_error("compilador portatil: outra instalacao o usa ha mais de duas horas");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    // uma troca interrompida (processo morto entre os dois renames) deixa a boa em <pasta>.antiga: volta para o lugar
    // (revisao Codex 20261120-220357: cada passo conferido; sem conseguir, para aqui e a .antiga, a unica boa, fica)
    const fs::path antiga = fs::path(p.pasta).concat(".antiga"), quarentena = fs::path(p.pasta).concat(".quarentena");
    if (!pronta(p.pasta, nullptr, p.arvore) && pronta(antiga, nullptr, p.arvore)) {
        std::error_code e1, e2, e3;
        if (fs::exists(p.pasta, e1)) {
            fs::remove_all(quarentena, e2);
            fs::rename(p.pasta, quarentena, e2);
            if (e2)
                throw std::runtime_error("compilador portatil: nao foi possivel afastar " + path_text(p.pasta) + " (" + e2.message() +
                                         "); a copia boa continua em " + path_text(antiga));
        }
        fs::rename(antiga, p.pasta, e3);
        if (e3 || !pronta(p.pasta, nullptr, p.arvore))
            throw std::runtime_error("compilador portatil: nao foi possivel recuperar " + path_text(antiga) +
                                     (e3 ? " (" + e3.message() + ")" : std::string(" (nao confere no lugar)")));
        if (p.log) p.log("compilador portatil: recuperado de uma troca interrompida");
    }
    if (pronta(p.pasta, &motivo, p.arvore)) return trava;
    if (p.log && fs::exists(p.pasta)) p.log("compilador portatil: montando de novo (" + motivo + ")");
    const fs::path  downloads = fs::path(p.pasta).concat(".downloads");
    const fs::path  tmp       = fs::path(p.pasta).concat(".montando");
    // a pasta temporaria sai em qualquer saida sem sucesso (erro, cancelamento, hash diferente)
    struct Limpa {
        fs::path t;
        bool     armada = true;
        ~Limpa() {
            std::error_code e;
            if (armada) fs::remove_all(t, e);
        }
    } limpa{tmp};
    // 1. os downloads conferidos (a soma do tamanho reparte a barra)
    const std::vector<Peca> pecas = p.pecas.empty() ? std::vector<Peca>(std::begin(kPecas), std::end(kPecas)) : p.pecas;
    if (pecas.size() != std::size(kPecas)) throw std::runtime_error("compilador portatil: lista de pecas invalida");
    double total = 0;
    for (const Peca& peca : pecas) total += double(peca.bytes);
    double f = 0;
    widescreen::Andamento an;
    an.quantos = pecas.size();
    {
        struct Fim {  // a linha do download sai da tela em qualquer saida (erro ou cancelamento tambem)
            widescreen::Andamento& a;
            const Pedido&          p;
            ~Fim() {
                a.fim = true;
                if (p.andamento) p.andamento(a);
            }
        } fim{an, p};
        for (const Peca& peca : pecas) {
            cancelado(p);
            ++an.atual;
            const double parte = 0.7 * double(peca.bytes) / total;
            baixar(p, peca, downloads / peca.arquivo, f, f + parte, an);
            f += parte;
        }
    }
    // 2. extrair numa pasta temporaria (uma montagem interrompida nunca fica no lugar da boa)
    fs::remove_all(tmp, ec);
    if (ec) throw std::runtime_error("nao foi possivel limpar " + path_text(tmp));
    disc::ArchiveSelection zig;
    zig.maxEntries = 30000, zig.maxBytes = 1ull << 30;
    zig.keep       = [](const std::string& n) { return comeca(n, kZigTopo) && !podado(n.substr(std::string(kZigTopo).size())); };
    extrair(p, downloads / pecas[0].arquivo, tmp / "zig.x", zig, 0.70, 0.85, "o Zig");
    subir(tmp / "zig.x", kZigTopo, tmp / "zig");
    disc::ArchiveSelection cmake;
    cmake.maxEntries = 30000, cmake.maxBytes = 1ull << 30;
    cmake.keep       = [](const std::string& n) {
        const std::string r = comeca(n, kCmakeTopo) ? n.substr(std::string(kCmakeTopo).size()) : std::string();
        return r == "bin/cmake.exe" || comeca(r, "share/cmake-3.30/") || r == "doc/cmake/Copyright.txt";
    };
    extrair(p, downloads / pecas[1].arquivo, tmp / "cmake.x", cmake, 0.85, 0.95, "o CMake");
    subir(tmp / "cmake.x", kCmakeTopo, tmp / "cmake");
    disc::ArchiveSelection ninja;
    ninja.keep = [](const std::string& n) { return n == "ninja.exe"; };
    extrair(p, downloads / pecas[2].arquivo, tmp / "bin", ninja, 0.95, 0.97, "o Ninja");
    // 3. invocadores, toolchain do CMake, licencas e o VERSAO.txt
    gravar(tmp / "bin" / "bs-ar.cmd", "@\"%~dp0..\\zig\\zig.exe\" ar %*\r\n");
    gravar(tmp / "bin" / "bs-ranlib.cmd", "@\"%~dp0..\\zig\\zig.exe\" ranlib %*\r\n");
    gravar(tmp / "bs-toolchain.cmake", kToolchainCmake);
    gravar(tmp / "bs-regras.cmake", kRegras);
    const std::pair<fs::path, const char*> licencas[] = {
        {tmp / "zig" / "LICENSE", "Zig-LICENSE.txt"},
        {tmp / "zig" / "lib" / "libc" / "mingw" / "COPYING", "mingw-w64-COPYING.txt"},
        {tmp / "zig" / "lib" / "libcxx" / "LICENSE.TXT", "libcxx-LICENSE.txt"},
        {tmp / "cmake" / "doc" / "cmake" / "Copyright.txt", "CMake-Copyright.txt"},
        {downloads / pecas[3].arquivo, "Ninja-COPYING.txt"}};
    for (const auto& [de, nome] : licencas) {
        fs::create_directories(tmp / "licencas", ec);
        fs::copy_file(de, tmp / "licencas" / nome, fs::copy_options::overwrite_existing, ec);
        if (ec) throw std::runtime_error("licenca ausente no download: " + path_text(de));
    }
    std::string versao = "Zig 0.13.0 (Clang/LLD 18), alvo x86_64-windows-gnu (mingw-w64, libc++ estatica), CPU base\n"
                         "CMake 3.30.5 e Ninja 1.12.1 oficiais\n";
    for (const Peca& peca : pecas) versao += std::string(peca.arquivo) + " " + peca.sha256 + "\n";
    gravar(tmp / "VERSAO.txt", versao);
    // 4. a montagem tem de dar exatamente a arvore esperada antes de ir para o lugar
    const std::string h = hash_arvore(tmp);
    if (h != p.arvore)
        throw std::runtime_error("o compilador portatil montado nao confere (hash da pasta " + h + ", esperado " + p.arvore +
                                 "): nada foi usado");
    cancelado(p);
    trocar(tmp, p.pasta, p.arvore);
    limpa.armada = false;
    if (p.log) p.log("compilador portatil pronto em " + path_text(p.pasta));
    if (p.progresso) p.progresso(1.0, "compilador portatil pronto");
    return trava;
}

} // namespace bs::builder::portatil
