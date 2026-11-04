#include "instalador/instalacoes.h"

#include "builder_platform.h"
#include "instalador/integracao.h"
#include "child_process.h"
#include "platform/install_config.h"
#include "util/hash.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <fstream>
#include <optional>
#include <random>
#include <set>
#include <chrono>
#include <thread>
#include <sstream>

namespace fs = std::filesystem;

namespace bs::builder::instalador {

namespace {

std::wstring fold(const fs::path& p) { return plat::fold_name(path_wide_generic(p)); }

// Arquivos da instalacao que nao estao na lista do pacote: o instalacao.toml (o instalador o escreve, N9.0) e o log do jogo
// (bluestinger.log, ao lado do executavel quando a pasta aceita escrita: app/main_sdl.cpp, userDir). Nao sao do jogador:
// ficam fora dos dados pessoais e saem na desinstalacao.
const wchar_t* const kDaInstalacao[] = {native::install::kConfigFile, L"bluestinger.log"};
bool da_instalacao(const fs::path& rel) {
    for (const wchar_t* f : kDaInstalacao)
        if (fold(rel) == plat::fold_name(f)) return true;
    return false;
}
size_t remove_da_instalacao(const fs::path& jogo) {
    size_t          n = 0;
    std::error_code ec;
    for (const wchar_t* f : kDaInstalacao) {
        const fs::path a = jogo / plat::to_path(f);
        const auto     st = fs::symlink_status(a, ec);  // um link sai como link (o alvo nao e tocado)
        if ((fs::is_regular_file(st) || fs::is_symlink(st)) && fs::remove(a, ec)) ++n;
    }
    return n;
}

fs::path absolute_clean(const fs::path& p) {
    std::error_code ec;
    fs::path        a = fs::weakly_canonical(fs::absolute(p, ec), ec);
    if (ec) a = fs::absolute(p, ec).lexically_normal();
    // sem a barra final (C:\Jogos\ e C:\Jogos sao a mesma pasta)
    while (a.has_relative_path() && a.filename().empty()) a = a.parent_path();
    return a;
}

std::string first_line(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    std::string   l;
    std::getline(in, l);
    while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
    return l;
}

uint64_t marker(const fs::path& dir) {
    std::ifstream in(dir / kInstallMarker, std::ios::binary);
    uint64_t      v = 0;
    return (in >> v) ? v : 0;
}

// Lista do pacote (relativos com '/'). Vazia sem o arquivo.
std::vector<std::string> package_list(const fs::path& jogo, bool* existe = nullptr) {
    std::vector<std::string> r;
    std::ifstream            in(jogo / kPackageManifest, std::ios::binary);
    if (existe) *existe = bool(in);
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty()) r.push_back(l);
    }
    return r;
}

// Caminho relativo seguro: sem raiz, sem "..", sem componente vazio (uma lista adulterada nao apaga fora da pasta)
bool safe_relative(const std::string& rel) {
    if (rel.empty() || rel.front() == '/' || rel.front() == '\\' || rel.find(':') != std::string::npos) return false;
    std::stringstream ss(rel);
    for (std::string part; std::getline(ss, part, '/');)
        if (part.empty() || part == "." || part == ".." || part.find('\\') != std::string::npos) return false;
    return true;
}

fs::path rel_path(const std::string& rel) { return plat::to_path(widen(rel)); }

std::string sha256_file(const fs::path& f, bool* ok = nullptr) {
    std::ifstream in(f, std::ios::binary);
    if (ok) *ok = bool(in);
    util::Sha256      sha;
    std::vector<char> buf(1 << 16);
    while (in) {
        in.read(buf.data(), std::streamsize(buf.size()));
        if (in.gcount() > 0) sha.update(buf.data(), size_t(in.gcount()));
    }
    return sha.hexdigest();
}

std::string random_hex() {
    std::random_device rd;
    char               b[24];
    std::snprintf(b, sizeof b, "%08x%08x", rd(), rd());
    return b;
}

struct Entrada {
    fs::path              pasta;
    std::vector<fs::path> atalhos;
};

std::vector<Entrada> read_list(const fs::path& lista, std::string* aviso) {
    std::vector<Entrada> r;
    std::error_code      ec;
    if (!fs::exists(lista, ec)) return r;
    try {
        std::ifstream in(lista, std::ios::binary);
        std::string   text((std::istreambuf_iterator<char>(in)), {});
        const auto    t = toml::parse(text);
        if (const auto* arr = t["instalacao"].as_array())
            for (const auto& n : *arr) {
                const auto* tab = n.as_table();
                if (!tab) continue;
                Entrada e;
                if (const auto p = (*tab)["pasta"].value<std::string>()) e.pasta = rel_path(*p);
                if (e.pasta.empty()) continue;
                if (const auto* at = (*tab)["atalhos"].as_array())
                    for (const auto& a : *at)
                        if (const auto s = a.value<std::string>()) e.atalhos.push_back(rel_path(*s));
                r.push_back(std::move(e));
            }
    } catch (const std::exception& e) {
        if (aviso) *aviso = std::string("lista de instalacoes ilegivel (") + e.what() + "): ignorada";
        r.clear();
    }
    return r;
}

std::string toml_string(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += char(c);
        else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else o += char(c);
    }
    return o + "\"";
}

bool write_list(const fs::path& lista, const std::vector<Entrada>& es, std::string* erro) {
    std::error_code ec;
    fs::create_directories(lista.parent_path(), ec);
    std::ostringstream o;
    o << "# Blue Stinger: instalacoes feitas pelo instalador (N9.1). A verdade e a pasta: uma entrada sem jogo e ignorada.\n";
    for (const auto& e : es) {
        o << "\n[[instalacao]]\npasta = " << toml_string(narrow(path_wide_generic(e.pasta))) << "\natalhos = [";
        for (size_t i = 0; i < e.atalhos.size(); ++i)
            o << (i ? ", " : "") << toml_string(narrow(path_wide_generic(e.atalhos[i])));
        o << "]\n";
    }
    const fs::path tmp = fs::path(lista).concat(".tmp-" + random_hex());
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << o.str();
        out.close();
        if (!out) {
            if (erro) *erro = "nao foi possivel gravar " + path_text(tmp);
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, lista, ec);
    if (ec) {
        if (erro) *erro = "nao foi possivel gravar " + path_text(lista) + " (" + error_text(ec) + ")";
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// Pasta de verdade (nao link simbolico, junction nem outro ponto de reparse: o MSVC informa a junction como file_type::junction)
bool pasta_real(const fs::path& p) {
    std::error_code ec;
    const auto      st = fs::symlink_status(p, ec);
    return !ec && st.type() == fs::file_type::directory;
}

// Todos os componentes intermediarios de raiz/rel sao pastas de verdade (e a raiz tambem): um link ou junction no meio levaria
// a exclusao para fora da instalacao (revisao Codex 20261104-195545). Resta a corrida com quem troca a pasta durante a
// desinstalacao, que exige o proprio usuario mexendo nela ao mesmo tempo.
bool caminho_sem_elos(const fs::path& raiz, const fs::path& rel) {
    if (!pasta_real(raiz)) return false;
    fs::path p = raiz;
    for (auto it = rel.begin(); it != rel.end(); ++it) {
        if (std::next(it) == rel.end()) break;
        p /= *it;
        if (!pasta_real(p)) return false;
    }
    return true;
}

void remove_empty_dirs(const fs::path& root) {
    std::error_code       ec;
    std::vector<fs::path> dirs;
    for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (pasta_real(it->path())) dirs.push_back(it->path());
        else if (it->is_directory(ec)) it.disable_recursion_pending();  // link ou junction: nem entra nem sai
    }
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) { return a.native().size() > b.native().size(); });
    for (const auto& d : dirs) fs::remove(d, ec);  // so remove vazia
}

// Apaga da pasta de um jogo os arquivos da lista; devolve quantos. A lista, a marca e os hashes saem por ultimo. O que nao
// sai (em uso) vai para `pendentes`, relativo a pasta.
size_t remove_package(const fs::path& jogo, std::vector<fs::path>* pendentes = nullptr) {
    bool                           tem = false;
    const std::vector<std::string> lista = package_list(jogo, &tem);
    if (!tem) return 0;
    size_t          n = 0;
    std::error_code ec;
    const std::set<std::wstring> ultimos = {plat::fold_name(kPackageManifest), plat::fold_name(kInstallMarker)};
    for (const auto& rel : lista) {
        if (!safe_relative(rel) || ultimos.count(plat::fold_name(widen(rel)))) continue;
        if (!caminho_sem_elos(jogo, rel_path(rel))) continue;
        const fs::path f  = jogo / rel_path(rel);
        const auto     st = fs::symlink_status(f, ec);  // o proprio arquivo: um link sai como link
        if (st.type() == fs::file_type::regular || st.type() == fs::file_type::symlink) {
            if (fs::remove(f, ec)) ++n;
            else if (pendentes) pendentes->push_back(rel_path(rel));
        }
    }
    for (const wchar_t* f : {kInstallMarker, kPackageManifest})
        if (fs::remove(jogo / f, ec)) ++n;
    remove_empty_dirs(jogo);
    return n;
}

} // namespace

bool mesma_pasta(const fs::path& a, const fs::path& b) { return fold(absolute_clean(a)) == fold(absolute_clean(b)); }

Instalacao ler_instalacao(const fs::path& pasta) {
    Instalacao i;
    i.pasta = absolute_clean(pasta);
    const fs::path jogo = i.pasta / kGameDir;
    i.instante = marker(jogo);
    std::string aviso;
    const auto  cfg = native::install::read_config(jogo, &aviso);
    i.edicao   = cfg.edition;
    i.portatil = cfg.saves == native::install::SavesLocation::Portable;
    i.versao   = first_line(jogo / L"VERSAO.txt");
    return i;
}

std::vector<Instalacao> listar(const fs::path& lista, std::string* aviso) {
    std::vector<Instalacao> r;
    for (const auto& e : read_list(lista, aviso)) {
        Instalacao i = ler_instalacao(e.pasta);
        if (!i.valida()) continue;
        if (std::any_of(r.begin(), r.end(), [&](const Instalacao& o) { return mesma_pasta(o.pasta, i.pasta); })) continue;
        i.atalhos = e.atalhos;
        r.push_back(std::move(i));
    }
    std::stable_sort(r.begin(), r.end(), [](const Instalacao& a, const Instalacao& b) { return a.instante > b.instante; });
    return r;
}

// Trava da lista por usuario durante a leitura, a alteracao e a troca: dois instaladores ou desinstaladores ao mesmo tempo nao
// perdem a alteracao um do outro (revisao Codex 20261104-195545). Espera ate 10 s pela outra execucao.
struct TravaLista {
    std::optional<plat::DirLock> t;
    bool                         ok = false;
    explicit TravaLista(const fs::path& lista) {
        std::error_code ec;
        fs::create_directories(lista.parent_path(), ec);
        const fs::path f = fs::path(lista).concat(".lock");
        for (int i = 0; i < 200 && !ok; ++i) {
            t.emplace();
            ok = t->acquire(f);
            if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
};

bool registrar(const fs::path& lista, const fs::path& pasta, const std::vector<fs::path>& atalhos, std::string* erro) {
    TravaLista trava(lista);
    if (!trava.ok) {
        if (erro) *erro = "outra instalacao esta atualizando " + path_text(lista);
        return false;
    }
    std::string          aviso;
    std::vector<Entrada> es = read_list(lista, &aviso);
    es.erase(std::remove_if(es.begin(), es.end(), [&](const Entrada& e) { return mesma_pasta(e.pasta, pasta); }), es.end());
    es.insert(es.begin(), Entrada{absolute_clean(pasta), atalhos});
    return write_list(lista, es, erro);
}

bool esquecer(const fs::path& lista, const fs::path& pasta, std::string* erro) {
    TravaLista trava(lista);
    if (!trava.ok) {
        if (erro) *erro = "outra instalacao esta atualizando " + path_text(lista);
        return false;
    }
    std::string          aviso;
    std::vector<Entrada> es = read_list(lista, &aviso);
    const size_t         antes = es.size();
    es.erase(std::remove_if(es.begin(), es.end(), [&](const Entrada& e) { return mesma_pasta(e.pasta, pasta); }), es.end());
    return es.size() == antes || write_list(lista, es, erro);
}

std::vector<fs::path> atalhos_registrados(const fs::path& lista, const fs::path& pasta) {
    std::string aviso;
    for (const auto& e : read_list(lista, &aviso))
        if (mesma_pasta(e.pasta, pasta)) return e.atalhos;
    return {};
}

Verificacao verificar_pacote(const fs::path& jogo, const std::function<void(double)>& progresso, const std::atomic<bool>* cancelar) {
    Verificacao              v;
    bool                     tem = false;
    const std::vector<std::string> lista = package_list(jogo, &tem);
    if (!tem) {
        v.faltando.push_back(narrow(kPackageManifest));
        return v;
    }
    std::map<std::wstring, std::string> hashes;  // nome dobrado -> hash
    {
        std::ifstream in(jogo / kPackageHashes, std::ios::binary);
        v.semHashes = !in;
        for (std::string l; std::getline(in, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (l.size() > 66 && l[64] == ' ' && l[65] == ' ') hashes[plat::fold_name(widen(l.substr(66)))] = l.substr(0, 64);
        }
    }
    std::error_code ec;
    for (size_t k = 0; k < lista.size(); ++k) {
        if (cancelar && cancelar->load()) {
            v.cancelado = true;
            return v;
        }
        const std::string& rel = lista[k];
        if (!safe_relative(rel)) {
            v.diferentes.push_back(rel);
            continue;
        }
        const fs::path f = jogo / rel_path(rel);
        if (!fs::is_regular_file(f, ec)) {
            v.faltando.push_back(rel);
        } else if (const auto h = hashes.find(plat::fold_name(widen(rel))); h != hashes.end()) {
            if (sha256_file(f) != h->second) v.diferentes.push_back(rel);
        }
        ++v.conferidos;
        if (progresso) progresso(double(k + 1) / double(lista.size()));
    }
    // um arquivo com hash que sumiu da lista tambem e falta (lista adulterada)
    if (!v.semHashes) {
        std::set<std::wstring> naLista;
        for (const auto& rel : lista) naLista.insert(plat::fold_name(widen(rel)));
        for (const auto& [nome, h] : hashes)
            if (!naLista.count(nome)) v.diferentes.push_back(narrow(nome));
    }
    v.ok = v.diferentes.empty() && v.faltando.empty();
    return v;
}

std::vector<fs::path> arquivos_do_jogador(const fs::path& jogo) {
    bool                   tem = false;
    std::set<std::wstring> pkg;
    for (const auto& rel : package_list(jogo, &tem)) pkg.insert(plat::fold_name(widen(rel)));
    std::vector<fs::path> r;
    std::error_code       ec;
    if (!fs::is_directory(jogo, ec)) return r;
    for (auto it = fs::recursive_directory_iterator(jogo, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_directory(ec)) {
            if (!pasta_real(it->path())) it.disable_recursion_pending();
            continue;
        }
        const fs::path rel = fs::relative(it->path(), jogo, ec);
        if (da_instalacao(rel)) continue;
        if (!pkg.count(plat::fold_name(path_wide_generic(rel)))) r.push_back(rel);
    }
    std::sort(r.begin(), r.end());
    return r;
}

std::map<std::string, std::string> impressao_pessoal(const fs::path& raiz) {
    std::map<std::string, std::string> r;
    for (const auto& rel : arquivos_do_jogador(raiz)) r[narrow(path_wide_generic(rel))] = sha256_file(raiz / rel);
    return r;
}

ResultadoDesinstalar desinstalar(const fs::path& pastaIn, const OpcoesDesinstalar& op, const std::vector<fs::path>& cache) {
    ResultadoDesinstalar r;
    std::error_code      ec;
    const fs::path       pasta = absolute_clean(pastaIn);
    const fs::path       jogo  = pasta / kGameDir;
    if (!marker(jogo)) {
        r.kind = ErrorKind::MissingInput;
        r.erro = "nao ha um jogo instalado pelo Builder em " + path_text(pasta);
        return r;
    }
    // a pasta do jogo e a da versao anterior tem de ser pastas de verdade: um link para outro lugar faria a lista valer la
    if (!pasta_real(jogo) || (fs::exists(pasta / kPreviousDir, ec) && !pasta_real(pasta / kPreviousDir))) {
        r.kind = ErrorKind::Other;
        r.erro = "a pasta do jogo e um link para outro lugar; nada foi alterado";
        return r;
    }
    // uma execucao do Builder nessa pasta (a mesma trava dele)
    std::optional<plat::DirLock> lock;
    lock.emplace();
    if (fs::is_directory(pasta / kWorkDir, ec) && !lock->acquire(pasta / kWorkDir / L".bs-lock")) {
        r.kind = ErrorKind::Busy;
        r.erro = "o Builder esta usando " + path_text(pasta) + "; espere ele terminar";
        return r;
    }
    // o jogo aberto segura o executavel (Windows): nada muda
    if (arquivo_em_uso(jogo / plat::game_binary_installed())) {
        r.kind = ErrorKind::Busy;
        r.erro = "o jogo esta aberto. Feche-o e tente de novo. Nada foi alterado.";
        return r;
    }
    // outro arquivo aberto sem compartilhar a exclusao impede renomear a pasta: tambem nada muda
    const fs::path tmp = pasta / plat::to_path(std::wstring(kGameDir) + L".desinstalando-" + widen(random_hex()));
    fs::rename(jogo, tmp, ec);
    if (ec) {
        r.kind = error_kind_of(ec, ErrorKind::Busy);
        r.erro = "nao foi possivel desinstalar (" + error_text(ec) + "). O jogo esta aberto? Nada foi alterado.";
        return r;
    }
    std::vector<fs::path> pend;
    r.removidos += remove_package(tmp, &pend);
    r.removidos += remove_da_instalacao(tmp);
    // o que ficou e do jogador (ou pendente): volta para o nome de antes
    if (fs::is_empty(tmp, ec)) fs::remove(tmp, ec);
    else {
        fs::rename(tmp, jogo, ec);
        const fs::path onde = ec ? tmp : jogo;
        std::set<std::wstring> pendentes;
        for (const auto& p : pend) r.pendentes.push_back(onde / p), pendentes.insert(fold(p));
        for (const auto& rel : arquivos_do_jogador(onde))
            if (!pendentes.count(fold(rel))) r.restantes.push_back(onde / rel);
    }
    // a versao anterior guardada pelo Builder: o mesmo criterio (e copias de arquivos do jogador de antes ficam)
    const fs::path ant = pasta / kPreviousDir;
    if (marker(ant)) {
        r.removidos += remove_package(ant);
        remove_da_instalacao(ant);
        if (fs::is_empty(ant, ec)) fs::remove(ant, ec);
        else
            for (const auto& rel : arquivos_do_jogador(ant)) r.restantes.push_back(ant / rel);
    }
    if (op.trabalho) {
        // .bs-build: dados preparados do CDI, C++ gerado, log. So a nossa (tem o builder.log ou a trava)
        lock.reset();
        const fs::path work = pasta / kWorkDir;
        if (fs::exists(work / L"builder.log", ec) || fs::exists(work / L".bs-lock", ec)) fs::remove_all(work, ec);
        for (const auto& c : cache)
            if (fs::exists(c / L".bs-out", ec)) fs::remove_all(c, ec);  // marca do cache desta saida
    }
    if (fs::is_directory(pasta, ec) && fs::is_empty(pasta, ec)) r.pastaRemovida = fs::remove(pasta, ec);
    r.ok = true;
    return r;
}

uint64_t espaco_livre(const fs::path& pasta) {
    std::error_code ec;
    fs::path        p = absolute_clean(pasta);
    while (!p.empty() && !fs::exists(p, ec) && p != p.parent_path()) p = p.parent_path();
    const auto s = fs::space(p, ec);
    return ec ? 0 : uint64_t(s.available);
}

uint64_t espaco_necessario(bool filmes) {
    // medido em 2026-11-04: dados de execucao 1,15 GB (no jogo e em .bs-build por hard link), cache de compilacao
    // ate 1 GB, filmes convertidos (Windows) 0,4 GB; com folga
    constexpr uint64_t GB = 1024ull * 1024 * 1024;
    return filmes ? 3 * GB : 5 * GB / 2;
}

} // namespace bs::builder::instalador
