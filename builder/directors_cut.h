#pragma once
// Director's Cut (mantenedor, 2026-11-19; docs/DIRECTORS-CUT-ANALISE.md): a opcao do instalador que aplica o patch
// espouaisso/Blue-Stinger-Directors-Cut-Patcher (GPL-3.0, de terceiros) ao disco japones v1.006 antes de recompilar. O
// patcher NAO vem com o Builder: e baixado do repositorio do autor, num commit fixo, com o SHA-256 de cada arquivo
// conferido, e roda num Python 3 (o do sistema; no Windows sem Python, o "embeddable" oficial de python.org, versao e hash
// fixos, guardado na pasta de trabalho, sem instalar nada). Nada do codigo GPL entra no nosso codigo nem no pacote.
// Um disco que ja tem o Director's Cut (o executavel reconhecido como versao 4 ou 5) nao e patcheado de novo.
#include "child_process.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace bs::builder::dc {

enum class Language { En, Fr };

inline constexpr char kRepository[] = "https://github.com/espouaisso/Blue-Stinger-Directors-Cut-Patcher";
inline constexpr char kCommit[]     = "747b4cc4dbab083638a2f9d3b4f7192eb0bdefcd";
inline constexpr char kLicense[]    = "GPL-3.0";

struct PinnedFile {
    const char* path;    // no repositorio (e na pasta local)
    const char* sha256;  // conferido antes de rodar
};
// Os arquivos que o patcher executa, no commit kCommit (lidos na analise de 2026-11-19). O SHA-256 e o do conteudo do commit
// (o blob do git, com LF), o mesmo que raw.githubusercontent.com entrega; um clone no Windows troca LF por CRLF e da outro
inline constexpr PinnedFile kPatcherFiles[] = {
    {"patch.py", "49878d4dac85a6c71af4432e200c0b0873cd6f744956bc3c993c30139b4f1651"},
    {"lib/bscode.py", "85ff29fa0c816d7694e74499aabcf2b68e16361d809cbcadc9ab0818bcb83103"},
    {"lib/ecc.py", "111793b9bc34d4a75e0f5ce888c4aa390e2f177470e0091414a5c4d5beaa3ca9"},
    {"lib/gdi.py", "794c0bb2d133169380f7ece7451d943ba83d7c62703986f1826e6be19bc33255"},
    {"lib/roomfmt.py", "033fd41fa7d863713f178b229458958e14e8f39a5c72ea134c0c1b2b7c163cc8"},
};
// Python "embeddable" oficial (Windows x64), so quando o sistema nao tem Python 3
inline constexpr char kPythonVersion[] = "3.14.10";
inline constexpr char kPythonUrl[]     = "https://www.python.org/ftp/python/3.14.10/python-3.14.10-embed-amd64.zip";
inline constexpr char kPythonSha256[]  = "90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159";

// O resultado do patch tem de ser uma versao reconhecida (boot_identity: hash do executavel). EN: versao 4; FR: versao 5
// (conferido em 2026-11-20 com o Redump "Blue Stinger (France)"). Fica como ponto unico para uma lingua nova do patcher.
inline bool language_supported(Language l) { return l == Language::En || l == Language::Fr; }

// Versao do jogo (game_version_of) de um disco que ja tem o Director's Cut neste idioma: 4 EN, 5 FR
inline int patched_version(Language l) { return l == Language::En ? 4 : 5; }

// URL de um arquivo do patcher no commit fixo
std::string patcher_url(const char* path);

// O que sera baixado, para a confirmacao do usuario (CLI e interface): uma linha por item
std::vector<std::string> download_summary(bool pythonNeeded);

// Baixa `url` (HTTPS) para `dst` (a assinatura de plat::http_download)
using Fetch = std::function<bool(const std::string& url, const std::filesystem::path& dst, const std::atomic<bool>* cancel,
                                 const std::function<void(uint64_t, uint64_t)>& onBytes, std::string* err)>;
// Roda um programa (a assinatura de run_process_argv), para os testes trocarem
using Run = std::function<ProcessResult(const std::vector<std::wstring>& argv, const std::filesystem::path& cwd,
                                        const std::function<void(const std::string&)>& onLine)>;

// Python 3 do sistema (py -3, python3 ou python, nessa ordem; versao >= 3.8): o argv para rodar, ou vazio
std::vector<std::wstring> system_python(const Run& run);

struct Request {
    std::filesystem::path japaneseDisc;  // imagem do japones v1.006 (GDI, CUE, CHD; pacotes ja extraidos por quem chama)
    std::filesystem::path westernDisc;   // americano Redump (EN) ou PAL frances (FR)
    Language              language = Language::En;
    std::filesystem::path work;          // pasta de trabalho (cache do patcher e do Python; GDIs temporarios)
    Fetch                 fetch;
    Run                   run;
    const std::atomic<bool>*                      cancel = nullptr;
    std::function<void(const std::string&)>       log;
    std::function<void(double, const std::string&)> progress;
};

// O Python que vai rodar o patcher: o do sistema, ou (Windows) o embeddable, com o zip conferido pelo hash e extraido de novo
// a cada uso. Lanca std::runtime_error.
std::vector<std::wstring> prepare_python(const Request& r);

// O fluxo inteiro: confere os discos, exporta os dois como GDI, prepara o patcher e o Python, roda o patch e devolve a
// folha .gdi do disco patcheado (em <work>/saida). Lanca std::runtime_error com a mensagem para o usuario.
std::filesystem::path apply(const Request& r);

// Apaga os GDIs temporarios (entradas e saida); o patcher e o Python ficam para a proxima vez
void cleanup(const std::filesystem::path& work);

} // namespace bs::builder::dc
