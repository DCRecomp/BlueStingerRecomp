// Director's Cut (builder/directors_cut.h): a procura do Python, a lista do que sera baixado e as recusas antes de baixar ou
// rodar qualquer coisa. Sem rede, sem Python e sem os discos (o fluxo inteiro e provado pelo bs-builder com as imagens;
// STATUS.md, "Director's Cut").
#include "test.h"

#include "directors_cut.h"

#include <atomic>
#include <cstring>
#include <functional>
#include <map>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
namespace dc = bs::builder::dc;

namespace {

// Run falso: a saida de "<programa> --version" por programa; ausente = nao inicia
dc::Run fakeRun(std::map<std::wstring, std::string> versions, std::vector<std::wstring>* calls = nullptr) {
    return [versions, calls](const std::vector<std::wstring>& argv, const fs::path&, const std::function<void(const std::string&)>& on) {
        if (calls) calls->push_back(argv.front());
        bs::builder::ProcessResult r;
        const auto                 it = versions.find(argv.front());
        if (it == versions.end()) return r;  // started = false
        r.started = true, r.exitCode = 0;
        on(it->second);
        return r;
    };
}

} // namespace

BS_TEST(dc_python_do_sistema) {
    // so Python 3.8 ou mais novo; Python 2 e versao ilegivel ficam de fora
    BS_CHECK(dc::system_python(fakeRun({{L"python3", "Python 2.7.18"}, {L"python", "Python 3.11.0"}})) ==
             std::vector<std::wstring>{L"python"});
    BS_CHECK(dc::system_python(fakeRun({{L"python3", "Python 3.7.9"}})).empty());
    BS_CHECK(dc::system_python(fakeRun({{L"python3", "algo"}})).empty());
    BS_CHECK(dc::system_python(fakeRun({})).empty());
#ifdef _WIN32
    // no Windows o lancador "py -3" vem primeiro
    std::vector<std::wstring> calls;
    BS_CHECK(dc::system_python(fakeRun({{L"py", "Python 3.12.1"}, {L"python", "Python 3.11.0"}}, &calls)) ==
             (std::vector<std::wstring>{L"py", L"-3"}));
    BS_CHECK(calls.size() == 1);
#endif
}

BS_TEST(dc_o_que_sera_baixado) {
    // a confirmacao mostra a origem, o commit fixo e a licenca; o Python so quando falta
    const auto um = dc::download_summary(false), dois = dc::download_summary(true);
    BS_CHECK(um.size() == 1 && dois.size() == 2);
    BS_CHECK(um[0].find("espouaisso/Blue-Stinger-Directors-Cut-Patcher") != std::string::npos);
    BS_CHECK(um[0].find("747b4cc") != std::string::npos && um[0].find("GPL-3.0") != std::string::npos);
    BS_CHECK(dois[1].find(dc::kPythonVersion) != std::string::npos);
    BS_CHECK(dc::patcher_url("lib/gdi.py") ==
             "https://raw.githubusercontent.com/espouaisso/Blue-Stinger-Directors-Cut-Patcher/"
             "747b4cc4dbab083638a2f9d3b4f7192eb0bdefcd/lib/gdi.py");
    // todo arquivo executado tem hash fixo (64 hex minusculos), e o Python tambem
    for (const auto& f : dc::kPatcherFiles) {
        BS_CHECK(std::strlen(f.sha256) == 64);
        for (const char* c = f.sha256; *c; ++c) BS_CHECK((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f'));
    }
    BS_CHECK(std::strlen(dc::kPythonSha256) == 64);
}

#ifdef _WIN32
// revisao Codex 2026-11-19 (ALTA): um python.exe e um zip adulterados no cache nunca rodam. O zip e conferido a cada uso; o
// download falso entrega outro conteudo, o hash nao bate e nada e executado (so as sondas de versao do Python do sistema)
BS_TEST(dc_python_do_cache_adulterado_nao_roda) {
    const fs::path dir = fs::temp_directory_path() / "bs unit directors cut python";
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path py = dir / (std::string("python-") + dc::kPythonVersion);
    fs::create_directories(py);
    std::ofstream(py / "python.exe") << "adulterado";
    std::ofstream(dir / (std::string("python-") + dc::kPythonVersion + "-embed-amd64.zip")) << "zip adulterado";
    dc::Request r;
    r.work  = dir;
    int fetches = 0;
    r.fetch = [&](const std::string& url, const fs::path& dst, const std::atomic<bool>*, const std::function<void(uint64_t, uint64_t)>&,
                  std::string*) {
        ++fetches;
        BS_CHECK(url == dc::kPythonUrl);
        std::ofstream(dst) << "outro conteudo";
        return true;
    };
    std::vector<std::wstring> calls;
    r.run = fakeRun({}, &calls);  // sem Python no sistema
    std::string msg;
    try {
        dc::prepare_python(r);
    } catch (const std::runtime_error& e) {
        msg = e.what();
    }
    BS_CHECK(msg.find("nao confere") != std::string::npos && fetches == 1);
    for (const auto& c : calls) BS_CHECK(c == L"py" || c == L"python3" || c == L"python");  // o python.exe do cache nunca
    fs::remove_all(dir, ec);
}
#endif

// revisao Codex 2026-11-20: um disco ja patcheado so vale para o idioma dele (o Builder recusa a combinacao cruzada)
BS_TEST(dc_disco_ja_patcheado_por_idioma) {
    BS_CHECK(dc::patched_version(dc::Language::En) == 4 && dc::patched_version(dc::Language::Fr) == 5);
    BS_CHECK(dc::patched_version(dc::Language::En) != 5 && dc::patched_version(dc::Language::Fr) != 4);
    BS_CHECK(dc::language_supported(dc::Language::En) && dc::language_supported(dc::Language::Fr));
}

BS_TEST(dc_recusa_antes_de_baixar) {
    // um arquivo que nao e o disco japones: recusado antes de qualquer download ou processo
    const fs::path dir = fs::temp_directory_path() / "bs unit directors cut";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    std::ofstream(dir / "nao-e-disco.gdi") << "isto nao e uma folha GDI\n";
    int          fetches = 0;
    dc::Request  r;
    r.japaneseDisc = dir / "nao-e-disco.gdi";
    r.westernDisc  = dir / "nao-e-disco.gdi";
    r.work         = dir / "trabalho";
    r.fetch = [&](const std::string&, const fs::path&, const std::atomic<bool>*, const std::function<void(uint64_t, uint64_t)>&,
                  std::string*) { return ++fetches, false; };
    std::vector<std::wstring> calls;
    r.run = fakeRun({}, &calls);
    std::string msg;
    try {
        dc::apply(r);
    } catch (const std::runtime_error& e) {
        msg = e.what();
    }
    BS_CHECK(msg.find("disco japones") != std::string::npos);
    BS_CHECK(fetches == 0 && calls.empty());
    fs::remove_all(dir, ec);
}
