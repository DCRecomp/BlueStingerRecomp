#pragma once
// v0.9.7-1 (pedido do mantenedor, 2026-11-12): "adicionar o jogo a Steam" na instalacao, uma opcao desmarcada por padrao.
// O jogo entra como atalho de jogo nao-Steam no shortcuts.vdf de cada usuario da Steam desta maquina
// (<Steam>/userdata/<id>/config/shortcuts.vdf), no formato binario de chave e valor da propria Steam, sem dependencia nova.
//   - A Steam: Windows, HKCU\Software\Valve\Steam\SteamPath; Linux, ~/.steam/steam, $XDG_DATA_HOME/Steam (~/.local/share/Steam)
//     e o Flatpak (~/.var/app/com.valvesoftware.Steam/.local/share/Steam). BS_INSTALADOR_TESTE_RAIZ=<pasta> desvia para
//     <pasta>/steam (os testes nunca tocam na Steam real).
//   - Um arquivo que nao se le no formato esperado nunca e reescrito (o aviso diz qual); cada gravacao guarda a versao
//     anterior em shortcuts.vdf.anterior-bluestinger e troca o arquivo de uma vez (temporario + rename).
//   - A entrada do mesmo executavel e atualizada, nunca duplicada; a desinstalacao tira as entradas que apontam para a pasta.
//   - A Steam le o arquivo ao abrir: com ela aberta, o jogo aparece depois de reinicia-la (o aviso diz isso).
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bs::builder::instalador {

// Um no do formato binario (KeyValues): 0x00 tabela, 0x01 texto, 0x02 inteiro de 32 bits, 0x07 inteiro de 64 bits.
struct NoVdf {
    uint8_t             tipo = 0;
    std::string         nome;
    std::string         texto;
    uint64_t            numero = 0;
    std::vector<NoVdf>  filhos;
    const NoVdf*        filho(const std::string& n) const;  // pelo nome, sem diferenciar maiusculas
};

// O arquivo inteiro: uma raiz sem nome cujos filhos sao as tabelas do topo ("shortcuts"). false = formato desconhecido.
bool        ler_vdf(const std::string& bytes, NoVdf& raiz);
std::string gravar_vdf(const NoVdf& raiz);

// O appid que a propria Steam calcula para um atalho nao-Steam: CRC-32 de exe + nome, com o bit alto ligado.
uint32_t appid_atalho(const std::string& exe, const std::string& nome);

struct EntradaSteam {
    std::string           nome;  // o nome na biblioteca (UTF-8)
    std::filesystem::path exe, pasta, icone;
};

// Acrescenta (ou atualiza, pelo executavel) a entrada no conteudo de um shortcuts.vdf (vazio = arquivo novo). false = o
// conteudo nao e do formato esperado (nada muda).
bool incluir_entrada(const std::string& atual, const EntradaSteam& e, std::string& novo);
// Tira as entradas cujo executavel esta dentro de `pasta`; *tiradas recebe quantas. false = formato desconhecido.
bool tirar_entradas(const std::string& atual, const std::filesystem::path& pasta, std::string& novo, int* tiradas);

// A pasta da Steam desta maquina (vazio = nao encontrada) e os shortcuts.vdf dos usuarios dela (existentes ou a criar).
std::filesystem::path              pasta_steam();
std::vector<std::filesystem::path> arquivos_atalhos_steam(const std::filesystem::path& steam);
bool                               steam_aberta();

// Na Steam de verdade (ou na desviada): quantos usuarios receberam / perderam a entrada; os problemas em *aviso.
int adicionar_steam(const EntradaSteam& e, std::string* aviso);
int remover_steam(const std::filesystem::path& pasta, std::string* aviso);

} // namespace bs::builder::instalador
