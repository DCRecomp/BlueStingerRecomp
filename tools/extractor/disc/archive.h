#pragma once
// Imagens dentro de arquivos compactados (.zip, .7z): decisao D1 do mantenedor (docs/IMAGENS-DISCO-PLANO.md, I8).
// So nas ferramentas. O pacote e extraido para uma pasta de trabalho (a da instalacao), apagada por quem chamou.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace bs::disc {

struct ArchiveEntry {
    std::string name;  // UTF-8, separador '/'
    uint64_t    size = 0;
    bool        dir  = false;
};

bool archive_supported(const std::filesystem::path& p);  // .zip ou .7z

// Lista o conteudo (sem extrair). Lanca DiscError se o arquivo nao abrir.
std::vector<ArchiveEntry> archive_list(const std::filesystem::path& archive);

// A imagem de disco dentro do pacote: a primeira .gdi, senao .cue, .chd, .cdi (nessa ordem). Lanca DiscError se nao
// houver nenhuma ou se um nome for inseguro (absoluto ou com ..).
std::string archive_image_name(const std::vector<ArchiveEntry>& entries);

// Extrai TODOS os arquivos do pacote (as faixas que a folha cita) para `dir` e devolve o caminho da imagem. `progress`
// recebe a fracao (0..1). Lanca DiscError (ou para no cancelamento, com DiscError "cancelado"). No .zip o cancelamento
// vale durante a extracao; no .7z, entre um arquivo e outro: um bloco solido e decodificado inteiro na memoria (o LZMA
// SDK nao tem chamada de progresso), ~30 s para um GD-ROM. Limites: 256 entradas e 8 GB extraidos.
std::filesystem::path archive_extract_image(const std::filesystem::path& archive, const std::filesystem::path& dir,
                                            const std::function<void(double)>& progress = {},
                                            const std::atomic<bool>* cancel = nullptr);

// Extrai todos os arquivos (os mesmos limites e nomes seguros), sem exigir imagem de disco. Director's Cut: o Python
// "embeddable" oficial baixado pelo Builder quando o sistema nao tem Python 3.
void archive_extract_all(const std::filesystem::path& archive, const std::filesystem::path& dir,
                         const std::function<void(double)>& progress = {}, const std::atomic<bool>* cancel = nullptr);

} // namespace bs::disc
