// v0.9.7-1: o atalho de jogo nao-Steam (steam.h). O formato binario de chave e valor (KeyValues) da Steam: cada item e um
// byte de tipo, o nome terminado em 0 e o valor (0x00 tabela ate 0x08; 0x01 texto terminado em 0; 0x02 inteiro de 32 bits;
// 0x07 de 64 bits, ambos little-endian); 0x08 fecha a tabela. O arquivo: a tabela "shortcuts" e mais um 0x08.
#include "instalador/steam.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <signal.h>
#endif

namespace fs = std::filesystem;

namespace bs::builder::instalador {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string utf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

fs::path from_utf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

struct Leitor {
    const std::string& b;
    size_t             i = 0;
    bool               ok = true;
    bool cstr(std::string& out) {
        const size_t z = b.find('\0', i);
        if (z == std::string::npos) return ok = false;
        out.assign(b, i, z - i), i = z + 1;
        return true;
    }
    bool bytes(size_t n, uint64_t& v) {
        if (i + n > b.size()) return ok = false;
        v = 0;
        for (size_t k = 0; k < n; ++k) v |= uint64_t(static_cast<unsigned char>(b[i + k])) << (8 * k);
        i += n;
        return true;
    }
    // os filhos de uma tabela, ate o 0x08 que a fecha (ou o fim do arquivo, so na raiz)
    bool tabela(std::vector<NoVdf>& filhos, int prof) {
        if (prof > 16) return ok = false;
        while (i < b.size()) {
            const uint8_t t = static_cast<uint8_t>(b[i++]);
            if (t == 0x08) return true;
            NoVdf n;
            n.tipo = t;
            if (!cstr(n.nome)) return false;
            if (t == 0x00) {
                if (!tabela(n.filhos, prof + 1)) return false;
            } else if (t == 0x01) {
                if (!cstr(n.texto)) return false;
            } else if (t == 0x02) {
                if (!bytes(4, n.numero)) return false;
            } else if (t == 0x07) {
                if (!bytes(8, n.numero)) return false;
            } else {
                return ok = false;  // tipo que este leitor nao conhece: o arquivo nao e tocado
            }
            filhos.push_back(std::move(n));
        }
        return ok = prof == 0;  // so a raiz pode acabar no fim do arquivo
    }
};

void gravar(std::string& o, const std::vector<NoVdf>& filhos) {
    for (const NoVdf& n : filhos) {
        o.push_back(char(n.tipo)), o += n.nome, o.push_back('\0');
        if (n.tipo == 0x00) gravar(o, n.filhos), o.push_back(char(0x08));
        else if (n.tipo == 0x01) o += n.texto, o.push_back('\0');
        else
            for (int k = 0; k < (n.tipo == 0x07 ? 8 : 4); ++k) o.push_back(char((n.numero >> (8 * k)) & 0xFF));
    }
}

NoVdf texto(const char* nome, const std::string& v) {
    NoVdf n;
    n.tipo = 0x01, n.nome = nome, n.texto = v;
    return n;
}
NoVdf inteiro(const char* nome, uint32_t v) {
    NoVdf n;
    n.tipo = 0x02, n.nome = nome, n.numero = v;
    return n;
}

std::string aspas(const fs::path& p) { return "\"" + utf8(p) + "\""; }

// o caminho do Exe de uma entrada (sem as aspas), normalizado para comparar
fs::path exe_de(const NoVdf& e) {
    const NoVdf* x = e.filho("exe");
    if (!x || x->tipo != 0x01) return {};
    std::string s = x->texto;
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    std::error_code ec;
    return fs::weakly_canonical(from_utf8(s), ec);
}

bool mesmo(const fs::path& a, const fs::path& b) {
#ifdef _WIN32
    return lower(utf8(a)) == lower(utf8(b));
#else
    return a == b;
#endif
}

bool dentro(const fs::path& f, const fs::path& pasta) {
    const std::string a = utf8(f), p = utf8(pasta);
#ifdef _WIN32
    const std::string la = lower(a), lp = lower(p);
#else
    const std::string &la = a, &lp = p;
#endif
    return la.size() > lp.size() && la.compare(0, lp.size(), lp) == 0 && (la[lp.size()] == '\\' || la[lp.size()] == '/');
}

// a tabela "shortcuts" do arquivo (criada se vazio); false = formato desconhecido
bool abrir(const std::string& atual, NoVdf& raiz, NoVdf*& lista) {
    raiz = {};
    if (!atual.empty() && !ler_vdf(atual, raiz)) return false;
    for (NoVdf& n : raiz.filhos)
        if (n.tipo == 0x00 && lower(n.nome) == "shortcuts") {
            lista = &n;
            return true;
        }
    if (!raiz.filhos.empty()) return false;  // um arquivo com outra coisa no topo: nao e o que esperamos
    NoVdf s;
    s.tipo = 0x00, s.nome = "shortcuts";
    raiz.filhos.push_back(s);
    lista = &raiz.filhos.back();
    return true;
}

void renumerar(NoVdf& lista) {  // as entradas sao "0", "1", ... na ordem
    for (size_t k = 0; k < lista.filhos.size(); ++k) lista.filhos[k].nome = std::to_string(k);
}

fs::path test_root() {
#ifdef _WIN32
    const wchar_t* v = _wgetenv(L"BS_INSTALADOR_TESTE_RAIZ");
#else
    const char* v = std::getenv("BS_INSTALADOR_TESTE_RAIZ");
#endif
    return v && *v ? fs::path(v) : fs::path();
}

bool ler_arquivo(const fs::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// a versao anterior guardada e o arquivo novo trocado de uma vez
bool gravar_arquivo(const fs::path& p, const std::string& bytes, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (fs::exists(p, ec)) fs::copy_file(p, fs::path(p).concat(".anterior-bluestinger"), fs::copy_options::overwrite_existing, ec);
    const fs::path tmp = fs::path(p).concat(".tmp-bluestinger");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), std::streamsize(bytes.size()));
        out.close();
        if (!out) {
            if (err) *err = "nao foi possivel gravar " + utf8(tmp);
            return false;
        }
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        if (err) *err = "nao foi possivel trocar " + utf8(p);
        return false;
    }
    return true;
}

} // namespace

const NoVdf* NoVdf::filho(const std::string& n) const {
    const std::string l = lower(n);
    for (const NoVdf& f : filhos)
        if (lower(f.nome) == l) return &f;
    return nullptr;
}

bool ler_vdf(const std::string& bytes, NoVdf& raiz) {
    raiz = {};
    Leitor l{bytes};
    if (!l.tabela(raiz.filhos, 0) || !l.ok) return false;
    while (l.i < bytes.size() && bytes[l.i] == char(0x08)) ++l.i;  // o 0x08 final do arquivo
    return l.i == bytes.size();
}

std::string gravar_vdf(const NoVdf& raiz) {
    std::string o;
    gravar(o, raiz.filhos);
    o.push_back(char(0x08));
    return o;
}

uint32_t appid_atalho(const std::string& exe, const std::string& nome) {
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : exe + nome) {
        c ^= b;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c | 0x80000000u;
}

bool incluir_entrada(const std::string& atual, const EntradaSteam& e, std::string& novo) {
    NoVdf  raiz;
    NoVdf* lista = nullptr;
    if (!abrir(atual, raiz, lista)) return false;
    std::error_code ec;
    const fs::path  alvo = fs::weakly_canonical(e.exe, ec);
    const std::string exe = aspas(e.exe);
    NoVdf ent;
    ent.tipo = 0x00;
    NoVdf tags;
    tags.tipo = 0x00, tags.nome = "tags";
    ent.filhos = {inteiro("appid", appid_atalho(exe, e.nome)),
                  texto("AppName", e.nome),
                  texto("Exe", exe),
                  texto("StartDir", aspas(e.pasta)),
                  texto("icon", e.icone.empty() ? std::string() : utf8(e.icone)),
                  texto("ShortcutPath", ""),
                  texto("LaunchOptions", ""),
                  inteiro("IsHidden", 0),
                  inteiro("AllowDesktopConfig", 1),
                  inteiro("AllowOverlay", 1),
                  inteiro("OpenVR", 0),
                  inteiro("Devkit", 0),
                  texto("DevkitGameID", ""),
                  inteiro("DevkitOverrideAppID", 0),
                  inteiro("LastPlayTime", 0),
                  texto("FlatpakAppID", ""),
                  tags};
    bool trocou = false;
    for (NoVdf& n : lista->filhos)
        if (n.tipo == 0x00 && mesmo(exe_de(n), alvo)) {
            // a mesma instalacao: os campos dela sao refeitos; o resto que a Steam guardou (tags, tempo de jogo) fica
            for (const NoVdf& f : ent.filhos) {
                if (f.nome == "tags" || f.nome == "LastPlayTime") continue;
                auto it = std::find_if(n.filhos.begin(), n.filhos.end(), [&](const NoVdf& g) { return lower(g.nome) == lower(f.nome); });
                if (it != n.filhos.end()) *it = f;
                else n.filhos.push_back(f);
            }
            trocou = true;
            break;
        }
    if (!trocou) lista->filhos.push_back(ent);
    renumerar(*lista);
    novo = gravar_vdf(raiz);
    return true;
}

bool tirar_entradas(const std::string& atual, const fs::path& pasta, std::string& novo, int* tiradas) {
    NoVdf  raiz;
    NoVdf* lista = nullptr;
    if (tiradas) *tiradas = 0;
    if (atual.empty()) {
        novo.clear();
        return true;
    }
    if (!abrir(atual, raiz, lista)) return false;
    std::error_code ec;
    const fs::path  p = fs::weakly_canonical(pasta, ec);
    const size_t    antes = lista->filhos.size();
    lista->filhos.erase(std::remove_if(lista->filhos.begin(), lista->filhos.end(),
                                       [&](const NoVdf& n) { return n.tipo == 0x00 && dentro(exe_de(n), p); }),
                        lista->filhos.end());
    if (tiradas) *tiradas = int(antes - lista->filhos.size());
    renumerar(*lista);
    novo = gravar_vdf(raiz);
    return true;
}

fs::path pasta_steam() {
    std::error_code ec;
    if (const fs::path t = test_root(); !t.empty()) return fs::is_directory(t / "steam", ec) ? t / "steam" : fs::path();
#ifdef _WIN32
    wchar_t buf[2048];
    DWORD   sz = sizeof buf;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS) {
        const fs::path p = fs::path(buf).make_preferred();
        if (fs::is_directory(p / "userdata", ec)) return p;
    }
    return {};
#else
    const char* home = std::getenv("HOME");
    const char* xdg  = std::getenv("XDG_DATA_HOME");
    std::vector<fs::path> c;
    if (home) c.push_back(fs::path(home) / ".steam" / "steam");
    if (xdg && *xdg) c.push_back(fs::path(xdg) / "Steam");
    if (home) {
        c.push_back(fs::path(home) / ".local" / "share" / "Steam");
        c.push_back(fs::path(home) / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam");
    }
    for (const fs::path& p : c)
        if (fs::is_directory(p / "userdata", ec)) return fs::weakly_canonical(p, ec);
    return {};
#endif
}

std::vector<fs::path> arquivos_atalhos_steam(const fs::path& steam) {
    std::vector<fs::path> r;
    std::error_code       ec;
    if (steam.empty()) return r;
    for (const auto& d : fs::directory_iterator(steam / "userdata", ec)) {
        const std::string n = utf8(d.path().filename());
        // so as contas (o numero da conta; "0" e a pasta anonima da Steam)
        if (!d.is_directory(ec) || n.empty() || n == "0" || !std::all_of(n.begin(), n.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
            continue;
        r.push_back(d.path() / "config" / "shortcuts.vdf");
    }
    std::sort(r.begin(), r.end());
    return r;
}

bool steam_aberta() {
    if (!test_root().empty()) return false;
#ifdef _WIN32
    DWORD pid = 0, sz = sizeof pid;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid", RRF_RT_REG_DWORD, nullptr, &pid, &sz) != ERROR_SUCCESS || !pid)
        return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD code = 0;
    const bool vivo = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return vivo;
#else
    const char* home = std::getenv("HOME");
    if (!home) return false;
    std::ifstream in(fs::path(home) / ".steam" / "steam.pid");
    long pid = 0;
    return in >> pid && pid > 0 && kill(pid_t(pid), 0) == 0;
#endif
}

int adicionar_steam(const EntradaSteam& e, std::string* aviso) {
    const fs::path steam = pasta_steam();
    if (steam.empty()) {
        if (aviso) *aviso += "Steam nao encontrada: o jogo nao foi adicionado a ela\n";
        return 0;
    }
    const auto arquivos = arquivos_atalhos_steam(steam);
    if (arquivos.empty() && aviso) *aviso += "a Steam nao tem nenhuma conta nesta maquina: o jogo nao foi adicionado a ela\n";
    int feitos = 0;
    for (const fs::path& f : arquivos) {
        std::string atual, novo, err;
        std::error_code ec;
        if (fs::exists(f, ec) && !ler_arquivo(f, atual)) {
            if (aviso) *aviso += "nao foi possivel ler " + utf8(f) + "\n";
            continue;
        }
        if (!incluir_entrada(atual, e, novo)) {
            if (aviso) *aviso += utf8(f) + " num formato desconhecido: nao foi alterado\n";
            continue;
        }
        if (gravar_arquivo(f, novo, &err)) ++feitos;
        else if (aviso) *aviso += err + "\n";
    }
    if (feitos && steam_aberta() && aviso) *aviso += "a Steam esta aberta: o jogo aparece na biblioteca depois de reinicia-la\n";
    return feitos;
}

int remover_steam(const fs::path& pasta, std::string* aviso) {
    int total = 0;
    for (const fs::path& f : arquivos_atalhos_steam(pasta_steam())) {
        std::string     atual, novo, err;
        std::error_code ec;
        if (!fs::exists(f, ec) || !ler_arquivo(f, atual)) continue;
        int n = 0;
        if (!tirar_entradas(atual, pasta, novo, &n)) {
            if (aviso) *aviso += utf8(f) + " num formato desconhecido: nao foi alterado\n";
            continue;
        }
        if (!n) continue;
        if (gravar_arquivo(f, novo, &err)) total += n;
        else if (aviso) *aviso += err + "\n";
    }
    return total;
}

} // namespace bs::builder::instalador
