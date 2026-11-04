// N9.1: integracao no Linux, por usuario (integracao.h): um .desktop no menu de aplicativos ($XDG_DATA_HOME/applications) e,
// se pedido, na Area de Trabalho (XDG_DESKTOP_DIR de ~/.config/user-dirs.dirs). Sem entrada de desinstalacao: o Linux nao tem
// um lugar comum para ela; o BlueStinger-Desinstalar fica na pasta do jogo e o instalador o oferece na tela de boas-vindas.
#include "instalador/integracao.h"

#include "child_process.h"

#include <sys/stat.h>

#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;

namespace bs::builder::instalador {

namespace {

fs::path env_path(const char* n) {
    const char* v = std::getenv(n);
    return v && *v ? fs::path(v) : fs::path();
}

fs::path home() { return env_path("HOME"); }

fs::path data_home() {
    const fs::path x = env_path("XDG_DATA_HOME");
    return x.is_absolute() ? x : home() / ".local/share";
}

fs::path apps_dir() {
    const fs::path t = env_path("BS_INSTALADOR_TESTE_RAIZ");
    return t.empty() ? data_home() / "applications" : t / "menu";
}

// XDG_DESKTOP_DIR="$HOME/Desktop" (user-dirs.dirs); sem ele, ~/Desktop se existir
fs::path desktop_dir() {
    const fs::path t = env_path("BS_INSTALADOR_TESTE_RAIZ");
    if (!t.empty()) return t / "desktop";
    fs::path       cfg = env_path("XDG_CONFIG_HOME");
    if (!cfg.is_absolute()) cfg = home() / ".config";
    std::ifstream in(cfg / "user-dirs.dirs");
    for (std::string l; std::getline(in, l);) {
        if (l.rfind("XDG_DESKTOP_DIR=", 0) != 0) continue;
        std::string v = l.substr(16);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        if (v.rfind("$HOME", 0) == 0) return home() / v.substr(v.size() > 5 && v[5] == '/' ? 6 : 5);
        if (!v.empty() && v.front() == '/') return fs::path(v);
    }
    std::error_code ec;
    return fs::is_directory(home() / "Desktop", ec) ? home() / "Desktop" : fs::path();
}

// Exec do Desktop Entry: argumento entre aspas, com \ " ` $ escapados (Desktop Entry Specification 1.5, "Exec key")
std::string exec_quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') o += '\\';
        o += c;
    }
    return o + "\"";
}

std::string escape_value(const std::string& s) {  // valor de string: \n, \t, \\ escapados
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else o += c;
    }
    return o;
}

bool write_desktop(const fs::path& f, const DadosIntegracao& d, std::string* err) {
    std::error_code ec;
    fs::create_directories(f.parent_path(), ec);
    const fs::path tmp = fs::path(f).concat(".tmp");
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        o << "[Desktop Entry]\nType=Application\nVersion=1.5\n";
        o << "Name=" << escape_value(narrow(nome_exibicao(d.edicao))) << "\n";
        o << "Comment=" << escape_value("Blue Stinger recompilado (" + d.versao + ")") << "\n";
        // o Exec escapado e depois o valor como string (as duas regras da especificacao)
        o << "Exec=" << escape_value(exec_quote(path_text(d.exe))) << "\n";
        o << "Path=" << escape_value(path_text(d.exe.parent_path())) << "\n";
        if (!d.icone.empty()) o << "Icon=" << escape_value(path_text(d.icone)) << "\n";
        o << "Terminal=false\nCategories=Game;\nStartupNotify=true\n";
        o << "X-BlueStinger-Pasta=" << escape_value(path_text(d.pasta)) << "\n";
        o.close();
        if (!o) {
            *err = "nao foi possivel gravar " + path_text(tmp);
            return false;
        }
    }
    ::chmod(tmp.c_str(), 0755);  // a Area de Trabalho do GNOME e do KDE so abrem um .desktop executavel
    fs::rename(tmp, f, ec);
    if (ec) *err = "nao foi possivel gravar " + path_text(f) + " (" + error_text(ec) + ")";
    return !ec;
}

bool mesma(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (a.empty() || b.empty()) return false;
    return fs::absolute(a, ec).lexically_normal() == fs::absolute(b, ec).lexically_normal();
}

// Um atalho que a lista diz ser desta instalacao so sai se for mesmo um atalho nosso: arquivo bluestinger-*.desktop direto
// no menu de aplicativos ou na Area de Trabalho do usuario (revisao Codex 20261104-195545).
bool atalho_nosso(const fs::path& p) {
    std::error_code ec;
    if (fs::symlink_status(p, ec).type() != fs::file_type::regular) return false;
    const std::string n = p.filename().string();
    if (n.rfind("bluestinger-", 0) != 0 || p.extension() != ".desktop") return false;
    return mesma(p.parent_path(), apps_dir()) || mesma(p.parent_path(), desktop_dir());
}

} // namespace

fs::path pasta_usuario() {
    const fs::path t = env_path("BS_INSTALADOR_TESTE_RAIZ");
    return t.empty() ? data_home() / "BlueStingerRecomp" : t / "usuario";
}

std::vector<fs::path> integrar(const DadosIntegracao& d, const Atalhos& a, const std::vector<fs::path>& anteriores, std::string* aviso) {
    std::vector<fs::path> feitos;
    std::error_code       ec;
    for (const auto& p : anteriores)
        if (atalho_nosso(p)) fs::remove(p, ec);
    const std::string nome = "bluestinger-" + chave_pasta(d.pasta) + ".desktop";
    auto add = [&](const fs::path& dir) {
        if (dir.empty()) {
            if (aviso) *aviso += "pasta da Area de Trabalho nao encontrada\n";
            return;
        }
        std::string err;
        if (write_desktop(dir / nome, d, &err)) feitos.push_back(dir / nome);
        else if (aviso) *aviso += err + "\n";
    };
    if (a.menu) add(apps_dir());
    if (a.areaDeTrabalho) add(desktop_dir());
    return feitos;
}

void desintegrar(const fs::path&, const std::vector<fs::path>& atalhos) {
    std::error_code ec;
    for (const auto& p : atalhos)
        if (atalho_nosso(p)) fs::remove(p, ec);
}

bool entrada_desinstalacao(const fs::path&, std::wstring*) { return false; }

bool arquivo_em_uso(const fs::path&) { return false; }

} // namespace bs::builder::instalador
