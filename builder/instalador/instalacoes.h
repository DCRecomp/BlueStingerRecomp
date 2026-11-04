#pragma once
// N9.1 (docs/N9-1-INSTALADOR.md): as instalacoes do jogador, sem janela e sem nada do sistema alem de arquivos (os atalhos e o
// registro do desinstalador ficam em integracao.h). Portavel: o mesmo codigo no Windows e no Linux.
//
// Uma instalacao e a pasta de saida que o jogador escolheu (<pasta>): o jogo em <pasta>/BlueStinger, a versao anterior em
// <pasta>/BlueStinger.anterior e o trabalho do Builder em <pasta>/.bs-build. A lista das instalacoes fica na pasta do usuario
// (instalacoes.toml) e serve para a tela de boas-vindas achar as instalacoes; a verdade continua sendo a pasta (a marca
// .bs-install), entao uma entrada de uma pasta apagada some sozinha da lista.
//
// Dados pessoais (saves, preferencias, log, o que o jogador pos na pasta) nunca estao na lista do pacote (.bs-package): a
// verificacao nao os confere e a desinstalacao nao os apaga.
#include "builder_core.h"
#include "platform/edition.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace bs::builder::instalador {

inline constexpr const wchar_t* kGameDir     = L"BlueStinger";
inline constexpr const wchar_t* kPreviousDir = L"BlueStinger.anterior";
inline constexpr const wchar_t* kWorkDir     = L".bs-build";
inline constexpr const char*    kListFile    = "instalacoes.toml";

struct Instalacao {
    std::filesystem::path         pasta;        // pasta de saida (absoluta)
    native::edition::Edition      edicao = native::edition::Edition::Original;
    bool                          portatil = false;  // saves em <jogo>/saves
    std::string                   versao;        // 1a linha do VERSAO.txt do jogo (vazio: sem o arquivo)
    uint64_t                      instante = 0;  // marca .bs-install (0 = a pasta nao tem um jogo instalado por nos)
    std::vector<std::filesystem::path> atalhos;  // atalhos criados pelo instalador (da lista do usuario)
    bool valida() const { return instante != 0; }
};

// Le o estado de uma pasta de saida (sem a lista do usuario: atalhos vazios).
Instalacao ler_instalacao(const std::filesystem::path& pasta);

// Lista do usuario: so as instalacoes validas, sem repeticao, da mais recente para a mais antiga. Arquivo ausente ou
// ilegivel: vazia (o motivo em *aviso).
std::vector<Instalacao> listar(const std::filesystem::path& lista, std::string* aviso = nullptr);
// Insere ou atualiza a entrada da pasta (gravacao atomica). Falso com o motivo em *erro.
bool registrar(const std::filesystem::path& lista, const std::filesystem::path& pasta,
               const std::vector<std::filesystem::path>& atalhos, std::string* erro = nullptr);
bool esquecer(const std::filesystem::path& lista, const std::filesystem::path& pasta, std::string* erro = nullptr);
// Atalhos registrados para a pasta (vazio se nao houver entrada).
std::vector<std::filesystem::path> atalhos_registrados(const std::filesystem::path& lista, const std::filesystem::path& pasta);

// Mesma pasta? (canonica quando existe; sem caixa no Windows)
bool mesma_pasta(const std::filesystem::path& a, const std::filesystem::path& b);

// --- verificacao ---
struct Verificacao {
    bool                     ok = false;
    bool                     semHashes = false;  // instalacao de antes do N9.1 (sem .bs-package.sha256): so a lista e conferida
    size_t                   conferidos = 0;
    std::vector<std::string> diferentes, faltando;  // caminhos relativos ao jogo
    bool                     cancelado = false;
};
// Confere os arquivos do pacote (lista e SHA-256; data/ fica com o --verify-data do jogo). `progresso` de 0 a 1.
Verificacao verificar_pacote(const std::filesystem::path& jogo, const std::function<void(double)>& progresso = {},
                             const std::atomic<bool>* cancelar = nullptr);

// --- dados pessoais ---
// SHA-256 de cada arquivo fora da lista do pacote, por caminho relativo (os testes comparam antes e depois; a tela de
// desinstalacao mostra quantos ficam).
std::map<std::string, std::string> impressao_pessoal(const std::filesystem::path& raiz);
// Arquivos de uma pasta de jogo que nao sao do pacote nem o instalacao.toml (que e da instalacao), relativos. Sem a lista, a
// pasta nao e nossa: todos.
std::vector<std::filesystem::path> arquivos_do_jogador(const std::filesystem::path& jogo);

// --- desinstalacao ---
struct OpcoesDesinstalar {
    bool trabalho = true;  // apaga tambem <pasta>/.bs-build e o cache de compilacao (nada pessoal: refeito do CDI)
};
struct ResultadoDesinstalar {
    bool                               ok = false;
    ErrorKind                          kind = ErrorKind::None;
    std::string                        erro;
    size_t                             removidos = 0;   // arquivos do pacote apagados
    std::vector<std::filesystem::path> restantes;       // arquivos do jogador que ficaram (absolutos)
    // arquivos do pacote que nao puderam sair agora (o proprio desinstalador e as DLLs que ele carregou da pasta): o
    // chamador os apaga depois de sair (Windows) e remove as pastas que esvaziarem
    std::vector<std::filesystem::path> pendentes;
    bool                               pastaRemovida = false;
};
// O jogo nao pode estar rodando (o executavel em uso, integracao.h arquivo_em_uso, ou a pasta que nao renomeia: nada muda
// e o erro e Busy). O desinstalador pode rodar de dentro da pasta: o que estiver em uso vai para `pendentes`.
// `cache` sao as pastas de cache de compilacao desta saida (o chamador as acha; vazio: nenhuma).
ResultadoDesinstalar desinstalar(const std::filesystem::path& pasta, const OpcoesDesinstalar& op,
                                 const std::vector<std::filesystem::path>& cache = {});

// Espaco livre no volume da pasta (ou da primeira pasta acima dela que existe). 0 se desconhecido.
uint64_t espaco_livre(const std::filesystem::path& pasta);
// Espaco que uma instalacao nova ocupa, aproximado (dados, filmes convertidos, cache de compilacao).
uint64_t espaco_necessario(bool filmes);

} // namespace bs::builder::instalador
