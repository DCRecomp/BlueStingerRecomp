// v0.9.7-2: o mod "filmes widescreen" baixado na instalacao (filmes_widescreen.h).
#include "filmes_widescreen.h"

#include "util/hash.h"

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace bs::builder::widescreen {

bool safe_name(const std::string& f) {
    if (f.size() < 5 || f.size() > 40 || f.compare(f.size() - 4, 4, ".mp4") != 0) return false;
    for (size_t i = 0; i + 4 < f.size(); ++i) {
        const char c = f[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

namespace {

bool hex64(const std::string& h) {
    if (h.size() != 64) return false;
    for (char c : h)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

std::vector<std::pair<std::string, std::string>> parse_lines(const std::string& text) {  // (nome, hash)
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream in(text);
    std::string        l;
    while (std::getline(in, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        const size_t sp = l.find("  ");
        if (sp == std::string::npos) continue;
        std::string h = l.substr(0, sp), n = l.substr(sp + 2);
        if (!n.empty() && n[0] == '*') n.erase(0, 1);  // sha256sum em modo binario
        if (hex64(h) && safe_name(n)) out.emplace_back(n, h);
    }
    return out;
}

std::string read_text(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

void write_registry(const fs::path& dir, const std::map<std::string, std::string>& reg) {
    const fs::path tmp = dir / (std::wstring(kRegistry) + L".novo");
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        for (const auto& [n, h] : reg) o << h << "  " << n << "\n";
    }
    std::error_code ec;
    fs::rename(tmp, dir / kRegistry, ec);
}

} // namespace

std::vector<Entry> parse_sums(const std::string& text) {
    std::vector<Entry> out;
    for (auto& [n, h] : parse_lines(text)) {
        bool dup = false;
        for (const auto& e : out) dup |= e.file == n;
        if (!dup) out.push_back({n, h});
    }
    return out;
}

std::string sha256_file(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    if (!in) return {};
    util::Sha256      sha;
    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), std::streamsize(buf.size()));
        if (in.gcount() > 0) sha.update(buf.data(), size_t(in.gcount()));
    }
    return in.bad() ? std::string{} : sha.hexdigest();
}

std::map<std::string, std::string> read_registry(const fs::path& dir) {
    std::map<std::string, std::string> r;
    for (auto& [n, h] : parse_lines(read_text(dir / kRegistry))) r[n] = h;
    return r;
}

Report install(const fs::path& dir, const Fetch& fetch, const std::atomic<bool>* cancel,
               const std::function<void(const std::string&)>& log, const std::function<void(double)>& progress,
               const std::string& baseUrl) {
    Report rep;
    auto   L = [&](const std::string& s) {
        if (log) log("filmes widescreen: " + s);
    };
    auto P = [&](double f) {
        if (progress) progress(f);
    };
    auto stopped = [&] { return cancel && cancel->load(); };
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        rep.aviso = "nao foi possivel criar a pasta mods\\video; o jogo usa os filmes originais";
        ++rep.falhas;
        return rep;
    }
    const fs::path sumsTmp = dir / L".filmes-widescreen-lista.part";
    std::string    err;
    if (!fetch(baseUrl + kSumsFile, sumsTmp, cancel, nullptr, &err)) {
        fs::remove(sumsTmp, ec);
        rep.cancelado = stopped();
        rep.aviso     = rep.cancelado ? "download cancelado; o jogo usa os filmes originais"
                                      : "lista dos filmes indisponivel (" + err + "); o jogo usa os filmes originais";
        ++rep.falhas;
        L(rep.aviso);
        return rep;
    }
    const std::vector<Entry> lista = parse_sums(read_text(sumsTmp));
    fs::remove(sumsTmp, ec);
    if (lista.empty()) {
        rep.aviso = "lista dos filmes vazia ou invalida; o jogo usa os filmes originais";
        ++rep.falhas;
        L(rep.aviso);
        return rep;
    }
    L(std::to_string(lista.size()) + " filmes na lista");
    std::map<std::string, std::string> reg = read_registry(dir);
    for (size_t i = 0; i < lista.size(); ++i) {
        const Entry& e   = lista[i];
        const fs::path dst = dir / fs::path(e.file);  // nomes so ASCII (safe_name)
        const double   f0 = double(i) / double(lista.size()), f1 = double(i + 1) / double(lista.size());
        P(f0);
        if (stopped()) {
            rep.cancelado = true;
            break;
        }
        if (fs::exists(dst, ec)) {
            const std::string atual = sha256_file(dst);
            if (atual == e.sha256) {
                ++rep.presentes, reg[e.file] = e.sha256;
                continue;
            }
            const auto r = reg.find(e.file);
            if (r == reg.end() || r->second != atual) {  // nao fomos nos que pusemos este arquivo: e do jogador
                ++rep.doJogador;
                L(e.file + ": ja existe um filme do jogador com esse nome; mantido");
                continue;
            }
        }
        const fs::path part = dir / fs::path(e.file + ".part");
        L("baixando " + e.file + " (" + std::to_string(i + 1) + "/" + std::to_string(lista.size()) + ")");
        const bool ok = fetch(baseUrl + e.file, part, cancel, [&](uint64_t got, uint64_t total) {
            if (total) P(f0 + (f1 - f0) * double(got) / double(total));
        }, &err);
        if (!ok) {
            fs::remove(part, ec);
            if (stopped()) {
                rep.cancelado = true;
                break;
            }
            ++rep.falhas;
            L(e.file + ": falhou (" + err + ")");
            continue;
        }
        if (sha256_file(part) != e.sha256) {
            fs::remove(part, ec);
            ++rep.falhas;
            L(e.file + ": SHA-256 diferente da lista; descartado");
            continue;
        }
        fs::rename(part, dst, ec);
        if (ec) {
            fs::remove(part, ec);
            ++rep.falhas;
            L(e.file + ": nao foi possivel gravar");
            continue;
        }
        reg[e.file] = e.sha256;
        write_registry(dir, reg);
        ++rep.baixados;
    }
    write_registry(dir, reg);
    P(1);
    if (rep.cancelado) rep.aviso = "download dos filmes widescreen cancelado; os que faltam usam o filme original";
    else if (rep.falhas) rep.aviso = std::to_string(rep.falhas) + " filme(s) widescreen nao baixado(s); esses usam o filme original";
    L("baixados " + std::to_string(rep.baixados) + ", ja presentes " + std::to_string(rep.presentes) + ", falhas " +
      std::to_string(rep.falhas) + (rep.doJogador ? ", do jogador " + std::to_string(rep.doJogador) : ""));
    return rep;
}

} // namespace bs::builder::widescreen
