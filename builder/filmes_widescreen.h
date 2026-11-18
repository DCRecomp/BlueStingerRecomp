#pragma once
// v0.9.7-2 (mantenedor, 2026-11-18): o mod opcional "filmes widescreen" (N9.13), publicado a parte no Internet Archive.
// Depois da instalacao da Remastered, com a opcao marcada, baixa a lista SHA256SUMS.txt do item e cada filme <ID>.mp4 para
// <instalacao>\mods\video (onde o jogo ja procura os filmes de mods, docs/MODS-VIDEO.md). Nada do jogo vem no Builder:
// os filmes sao um mod de terceiros, baixado so a pedido.
// Regras: so HTTPS; cada arquivo conferido pelo SHA-256 da lista antes de entrar no lugar (baixado como .part); um arquivo do
// jogador com o mesmo nome (outro mod) nunca e trocado: so os registrados em .filmes-widescreen (instalados por nos) sao
// atualizados; falha de rede ou de hash nao desfaz a instalacao (o jogo usa o filme original 4:3) e vira aviso.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace bs::builder::widescreen {

inline constexpr const char kBaseUrl[]   = "https://archive.org/download/bluestinger-recompiled-widescreen-movies/";
inline constexpr const char kSumsFile[]  = "SHA256SUMS.txt";
inline constexpr wchar_t    kRegistry[]  = L".filmes-widescreen";  // em mods\video: "sha256  nome" dos que instalamos

struct Entry {
    std::string file;    // <ID>.mp4 (so letras, digitos e _)
    std::string sha256;  // 64 hex minusculos
};

// Lista no formato do sha256sum ("hash  nome", LF ou CRLF). Linhas fora do formato ou com nome inseguro sao ignoradas.
std::vector<Entry> parse_sums(const std::string& text);
bool               safe_name(const std::string& file);

// SHA-256 de um arquivo em hexadecimal minusculo; vazio se nao der para ler.
std::string sha256_file(const std::filesystem::path& f);

// Baixa `url` (HTTPS) para `dst`. onBytes(recebidos, total ou 0). Falso com a mensagem em *err.
using Fetch = std::function<bool(const std::string& url, const std::filesystem::path& dst, const std::atomic<bool>* cancel,
                                 const std::function<void(uint64_t, uint64_t)>& onBytes, std::string* err)>;

struct Report {
    int         baixados = 0, presentes = 0, falhas = 0, doJogador = 0;
    bool        cancelado = false;
    std::string aviso;  // vazio = tudo certo
};

// O fluxo inteiro (testavel com um Fetch falso). progress(0..1) e log(linha) podem ser nulos.
Report install(const std::filesystem::path& modsVideo, const Fetch& fetch, const std::atomic<bool>* cancel,
               const std::function<void(const std::string&)>& log, const std::function<void(double)>& progress,
               const std::string& baseUrl = kBaseUrl);

std::map<std::string, std::string> read_registry(const std::filesystem::path& modsVideo);

} // namespace bs::builder::widescreen
