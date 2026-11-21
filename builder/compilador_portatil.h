#pragma once
// Compilador portatil do Windows (mantenedor, 2026-11-20; issue #3: "nao quero instalar o Visual Studio"). Sem o Visual
// Studio, o Builder baixa, so com a confirmacao do usuario, o Zig 0.13.0 (o mesmo do Linux: Clang 18, alvo
// x86_64-windows-gnu com o mingw-w64 e a libc++), o CMake 3.30.5 e o Ninja 1.12.1 oficiais, cada um conferido pelo
// SHA-256 fixado aqui, e monta a pasta do compilador (o mesmo formato do toolchain/ do Linux: bs-toolchain.cmake, o
// CMake, o Ninja e o VERSAO.txt). A pasta fica no cache do usuario (BS_BUILDER_CACHE ou %LOCALAPPDATA%\BlueStingerRecomp;
// nunca no ProgramData, que outros usuarios gravam) e vale para todas as instalacoes. A pasta inteira (cada arquivo,
// inclusive as bibliotecas, os modulos do CMake e as licencas) tem um hash fixado no Builder (kArvoreSha256, revisao Codex
// 20261120-194725): conferido logo depois de montar e a cada uso; diferente = montada de novo a partir dos zips.
#include "filmes_widescreen.h"  // Fetch

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bs::builder::portatil {

inline constexpr const char kVersao[] = "zig-0.13.0";  // nome da pasta e do conjunto (muda com qualquer peca)
// hash_arvore() da pasta montada a partir dos downloads oficiais (muda com qualquer peca, com o bs-toolchain.cmake e com o
// VERSAO.txt gerados aqui): a raiz de confianca da pasta, que vive no cache do usuario
inline constexpr const char kArvoreSha256[] = "c8d78208562b4443c7607be5201ad0df541a51c8af8412cf3a70a094756c410f";

struct Peca {
    const char* nome;     // para as mensagens
    const char* arquivo;  // nome do download
    const char* url;      // so HTTPS
    const char* sha256;   // publicado pelo projeto (Ninja: medido em 2026-11-20; a release nao publica soma)
    uint64_t    bytes;    // tamanho do download (so para a mensagem)
    const char* licenca;
};

inline constexpr Peca kPecas[] = {
    {"Zig 0.13.0", "zig-windows-x86_64-0.13.0.zip", "https://ziglang.org/download/0.13.0/zig-windows-x86_64-0.13.0.zip",
     "d859994725ef9402381e557c60bb57497215682e355204d754ee3df75ee3c158", 79163968, "MIT; LLVM: Apache-2.0 WITH LLVM-exception"},
    {"CMake 3.30.5", "cmake-3.30.5-windows-x86_64.zip",
     "https://github.com/Kitware/CMake/releases/download/v3.30.5/cmake-3.30.5-windows-x86_64.zip",
     "5ab6e1faf20256ee4f04886597e8b6c3b1bd1297b58a68a58511af013710004b", 45417243, "BSD-3-Clause"},
    {"Ninja 1.12.1", "ninja-1.12.1-win.zip", "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip",
     "f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a", 275425, "Apache-2.0"},
    {"Ninja 1.12.1 (COPYING)", "ninja-1.12.1-COPYING", "https://raw.githubusercontent.com/ninja-build/ninja/v1.12.1/COPYING",
     "eb7e9ab9690124c5c9f42bdc81383d886a3dede26345b6ed15bbad7caf81f7ea", 11358, "Apache-2.0"},
};

// O que sera baixado, uma linha por peca, para a confirmacao (instalador e CLI).
std::vector<std::string> resumo();
uint64_t                 bytes_do_download();

// Pasta do compilador no cache do usuario: <raiz>/compilador/<kVersao>, com a primeira raiz que der (BS_BUILDER_CACHE,
// %LOCALAPPDATA%\BlueStingerRecomp). Vazio = nenhuma utilizavel (caminho com caracteres fora da pagina ANSI etc.).
std::filesystem::path pasta_padrao();

// SHA-256 da lista ordenada "sha256  caminho/relativo" de todos os arquivos da pasta (vazio: pasta ausente ou ilegivel).
std::string hash_arvore(const std::filesystem::path& pasta);

// A pasta montada e integra: hash_arvore() igual ao esperado (kArvoreSha256; os testes passam o das pecas falsas).
bool pronta(const std::filesystem::path& pasta, std::string* motivo = nullptr, const std::string& esperado = kArvoreSha256);

struct Pedido {
    std::filesystem::path                          pasta;      // destino (pasta_padrao())
    widescreen::Fetch                              fetch;      // plat::http_download (testes: um falso)
    const std::atomic<bool>*                       cancel = nullptr;
    std::function<void(const std::string&)>        log;
    std::function<void(double, const std::string&)> progresso;  // 0..1 e o que esta fazendo
    std::function<void(const widescreen::Andamento&)> andamento;  // o download por peca (a tela mostra MB e velocidade)
    std::vector<Peca>                                  pecas;      // vazio = kPecas (testes: pecas falsas, mesma ordem e formato)
    std::string                                        arvore = kArvoreSha256;  // o hash esperado da pasta montada
};

// Garante a pasta pronta: se ja estiver integra, nada; senao baixa o que faltar (para <pasta>.downloads, conferido),
// extrai numa pasta temporaria e so entao a poe no lugar. Lanca std::runtime_error com a mensagem para o usuario.
// Devolve a trava da pasta (<pasta>.trava, entre processos): manter viva enquanto o compilador estiver em uso (a compilacao
// inteira); outra instalacao com o compilador portatil espera por ela (revisao Codex 20261120-214554).
std::shared_ptr<void> preparar(const Pedido& p);

} // namespace bs::builder::portatil
