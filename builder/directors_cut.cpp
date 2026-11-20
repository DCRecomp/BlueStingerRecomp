#include "directors_cut.h"

#include "boot/disc_boot.h"
#include "boot_identity.h"
#include "disc/archive.h"
#include "disc/disc_open.h"
#include "disc/gdi_export.h"
#include "filmes_widescreen.h"  // sha256_file

#include <cstdio>
#include <stdexcept>

namespace bs::builder::dc {

namespace fs = std::filesystem;

namespace {

std::string pathText(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

void say(const Request& r, const std::string& m) {
    if (r.log) r.log("Director's Cut: " + m);
}

void step(const Request& r, double f, const std::string& m) {
    if (r.progress) r.progress(f, m);
}

void checkCancel(const Request& r) {
    if (r.cancel && r.cancel->load()) throw std::runtime_error("cancelado");
}

// baixa para .part, confere o hash e so entao poe no lugar
void fetchVerified(const Request& r, const std::string& url, const fs::path& dst, const std::string& sha) {
    std::error_code ec;
    if (fs::is_regular_file(dst, ec) && widescreen::sha256_file(dst) == sha) return;  // ja baixado e conferido
    fs::create_directories(dst.parent_path(), ec);
    const fs::path part = dst.string() + ".part";
    std::string    err;
    if (!r.fetch || !r.fetch(url, part, r.cancel, {}, &err)) {
        fs::remove(part, ec);
        checkCancel(r);
        throw std::runtime_error("nao foi possivel baixar " + url + (err.empty() ? "" : " (" + err + ")"));
    }
    const std::string got = widescreen::sha256_file(part);
    if (got != sha) {
        fs::remove(part, ec);
        throw std::runtime_error("o arquivo baixado de " + url + " nao confere (SHA-256 " + got + ", esperado " + sha +
                                 "): nada foi executado");
    }
    fs::rename(part, dst, ec);
    if (ec) {
        fs::remove(dst, ec);
        fs::rename(part, dst, ec);
        if (ec) throw std::runtime_error("nao foi possivel gravar " + pathText(dst));
    }
}

bool pythonVersionOk(const std::string& line) {
    unsigned major = 0, minor = 0;
    if (std::sscanf(line.c_str(), "Python %u.%u", &major, &minor) != 2) return false;
    return major == 3 && minor >= 8;
}

} // namespace

std::string patcher_url(const char* path) {
    return "https://raw.githubusercontent.com/espouaisso/Blue-Stinger-Directors-Cut-Patcher/" + std::string(kCommit) + "/" + path;
}

std::vector<std::string> download_summary(bool pythonNeeded) {
    std::vector<std::string> v;
    v.push_back(std::string("o patcher do Director's Cut (") + kRepository + ", commit " + std::string(kCommit).substr(0, 7) +
                ", licenca " + kLicense + ", de terceiros): " + std::to_string(std::size(kPatcherFiles)) +
                " arquivos .py, conferidos pelo SHA-256");
    if (pythonNeeded)
        v.push_back(std::string("o Python ") + kPythonVersion + " \"embeddable\" oficial (python.org, 12 MB), so na pasta de trabalho: nada e instalado no sistema");
    return v;
}

std::vector<std::wstring> system_python(const Run& run) {
    const std::vector<std::vector<std::wstring>> candidates = {
#ifdef _WIN32
        {L"py", L"-3"},
#endif
        {L"python3"}, {L"python"}};
    for (const auto& c : candidates) {
        std::string first;
        auto        argv = c;
        argv.push_back(L"--version");
        const ProcessResult pr = run(argv, {}, [&](const std::string& l) {
            if (first.empty()) first = l;
        });
        if (pr.started && pr.exitCode == 0 && pythonVersionOk(first)) return c;
    }
    return {};
}

std::vector<std::wstring> prepare_python(const Request& r) {
    std::error_code           ec;
    std::vector<std::wstring> python = system_python(r.run);
    if (python.empty()) {
#ifdef _WIN32
        step(r, 0.6, "baixando o Python");
        const fs::path zip = r.work / (std::string("python-") + kPythonVersion + "-embed-amd64.zip");
        const fs::path dir = r.work / (std::string("python-") + kPythonVersion);
        // a cada uso: o zip guardado e conferido pelo hash (baixado de novo se mudou) e a pasta extraida de novo, para
        // nunca rodar um python.exe que alguem tenha trocado no cache (revisao Codex 2026-11-19)
        fetchVerified(r, kPythonUrl, zip, kPythonSha256);
        try {
            fs::remove_all(dir, ec);
            if (ec) throw disc::DiscError("nao foi possivel limpar " + pathText(dir));
            disc::archive_extract_all(zip, dir, {}, r.cancel);
        } catch (const disc::DiscError& e) {
            checkCancel(r);
            throw std::runtime_error(std::string("nao foi possivel abrir o Python baixado: ") + e.what());
        }
        python = {path_wide(dir / "python.exe")};
        say(r, std::string("Python ") + kPythonVersion + " embeddable em " + pathText(dir));
#else
        throw std::runtime_error("o Director's Cut precisa do Python 3 (3.8 ou mais novo): instale o pacote python3 do sistema");
#endif
    }
    return python;
}

fs::path apply(const Request& r) {
    std::error_code ec;
    if (!language_supported(r.language))
        throw std::runtime_error("a versao francesa do Director's Cut ainda nao tem suporte: o executavel que o patch gera com o disco "
                                 "PAL frances ainda nao foi conferido (falta um dump do disco)");
    fs::create_directories(r.work, ec);
    if (ec) throw std::runtime_error("nao foi possivel criar " + pathText(r.work));

    // 1. os discos: o japones v1.006 e o ocidental certo para o idioma (o patcher confere de novo pelo MD5)
    step(r, 0.0, "conferindo os discos");
    BootFiles jp;
    try {
        jp = boot_read_disc(r.japaneseDisc);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("o disco japones nao pode ser lido: ") + e.what());
    }
    if (game_version_of(jp.versionId) != 3)
        throw std::runtime_error("o Director's Cut parte do disco japones v1.006 (HDR-0003); esta imagem e " + jp.product + " " +
                                 jp.version + " (" + jp.versionId + ")");
    {
        const bool en = r.language == Language::En;
        BootFiles  oc;
        try {
            oc = boot_read_disc(r.westernDisc);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("o disco ") + (en ? "americano" : "PAL frances") + " nao pode ser lido: " + e.what());
        }
        // o patcher confere o executavel do GD original (Redump): o do CDI americano tem 7 bytes diferentes
        if (en && oc.versionId != "bluestinger_us_v1000_gd")
            throw std::runtime_error("a versao EN do Director's Cut precisa do disco americano original (GDI, CUE/BIN ou CHD como o "
                                     "Redump), nao de um CDI; esta imagem e " + oc.versionId);
        if (!en && oc.versionId != "bluestinger_pal_fr_v1000")
            throw std::runtime_error("a versao FR do Director's Cut precisa do disco PAL frances (T13001D 09); esta imagem e " + oc.versionId);
    }
    say(r, "discos conferidos: " + jp.versionId + " e o " + (r.language == Language::En ? "americano" : "PAL frances"));
    checkCancel(r);

    // 2. os dois como GDI (o patcher so le GDI)
    const fs::path gdiJp = r.work / "gdi-jp", gdiOc = r.work / "gdi-ocidental", out = r.work / "saida";
    for (const auto& d : {gdiJp, gdiOc, out}) fs::remove_all(d, ec);
    fs::path jpSheet, ocSheet;
    try {
        auto a = disc::disc_open(r.japaneseDisc);
        jpSheet = disc::export_gdi(*a, gdiJp, "japones", [&](double f) { step(r, 0.05 + 0.25 * f, "preparando o disco japones"); }, r.cancel);
        auto b = disc::disc_open(r.westernDisc);
        ocSheet = disc::export_gdi(*b, gdiOc, "ocidental", [&](double f) { step(r, 0.30 + 0.25 * f, "preparando o disco ocidental"); }, r.cancel);
    } catch (const disc::DiscError& e) {
        checkCancel(r);
        throw std::runtime_error(std::string("nao foi possivel preparar os discos para o patch: ") + e.what());
    }
    checkCancel(r);

    // 3. o patcher, no commit fixo e conferido arquivo a arquivo
    step(r, 0.55, "baixando o patcher");
    const fs::path patcher = r.work / (std::string("patcher-") + std::string(kCommit).substr(0, 12));
    for (const auto& f : kPatcherFiles) fetchVerified(r, patcher_url(f.path), patcher / f.path, f.sha256);
    say(r, std::string("patcher ") + kRepository + " @ " + kCommit + " (" + kLicense + ") conferido");

    // 4. o Python
    const std::vector<std::wstring> python = prepare_python(r);
    checkCancel(r);

    // 5. o patch (-I: o Python isolado, sem o diretorio atual nem variaveis do usuario)
    step(r, 0.65, "aplicando o patch");
    std::vector<std::wstring> argv = python;
    for (const std::wstring& a : {std::wstring(L"-I"), path_wide(patcher / "patch.py"),
                                  std::wstring(r.language == Language::En ? L"en" : L"fr"), path_wide(jpSheet), path_wide(ocSheet),
                                  path_wide(out)})
        argv.push_back(a);
    std::string last;
    const ProcessResult pr = r.run(argv, r.work, [&](const std::string& l) {
        last = l;
        say(r, "patcher: " + l);
    });
    checkCancel(r);
    if (!pr.started) throw std::runtime_error("nao foi possivel rodar o Python");
    if (pr.exitCode != 0) throw std::runtime_error("o patcher do Director's Cut falhou: " + last);
    for (const auto& e : fs::directory_iterator(out, ec))
        if (e.path().extension() == ".gdi") {
            step(r, 0.95, "patch aplicado");
            return e.path();
        }
    throw std::runtime_error("o patcher terminou sem gerar o GDI do Director's Cut");
}

void cleanup(const fs::path& work) {
    std::error_code ec;
    for (const char* d : {"gdi-jp", "gdi-ocidental", "saida"}) fs::remove_all(work / d, ec);
}

} // namespace bs::builder::dc
