#pragma once
// N9-L (L5): o que o Builder pede ao sistema. Windows: builder_platform_win.cpp (o comportamento aprovado, sem mudanca).
// Linux: builder_platform_posix.cpp. O fluxo (builder_core.cpp) e a linha de comando (main_cli.cpp) sao um so.
//
// Regra dos nomes: o que vem do usuario fica em std::filesystem::path ou passa por to_path/path_wide (child_process.h);
// literais so ASCII.
#include "builder_core.h"
#include "child_process.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace bs::builder::plat {

// Instante para a marca de instalacao (.bs-install): so a ordem importa (unidades de 100 ns).
uint64_t now_ticks();

// Nome para comparar entradas de pacote: minusculas no Windows (sistema de arquivos sem caixa), igual no Linux.
std::wstring fold_name(const std::wstring& s);

// Texto (UTF-16 no Windows, UTF-32 no Linux) para caminho, sem passar pela localidade do processo.
std::filesystem::path to_path(const std::wstring& s);

// Gravacao real numa pasta (uma ACL ou um ponto de montagem so leitura nao aparecem nos atributos). Vazio = gravavel.
std::error_code probe_writable(const std::filesystem::path& dir);

// Trava exclusiva de uma pasta de saida durante a execucao (solta no destrutor e na morte do processo).
class DirLock {
public:
    DirLock() = default;
    DirLock(const DirLock&) = delete;
    DirLock& operator=(const DirLock&) = delete;
    ~DirLock();
    bool acquire(const std::filesystem::path& file);  // false: outra execucao tem a trava

private:
    intptr_t h_ = -1;
};

// Cria a pasta so se ela nao existir. true: criada; false com ec vazio: ja existia; false com ec: erro.
bool create_dir_exclusive(const std::filesystem::path& p, std::error_code& ec);

// Codigos de falta de espaco e de permissao do sistema (o resto: `otherwise`).
ErrorKind error_kind_of(const std::error_code& ec, ErrorKind otherwise);

// Cache de compilacao: chave da pasta de saida, pastas candidatas na ordem (a ultima e `work`) e a mensagem sem nenhuma.
uint32_t                           cache_key(const std::filesystem::path& out);
std::vector<std::filesystem::path> cache_roots(const std::filesystem::path& work);
const char*                        cache_hint();
const char*                        example_out_dir();
// O caminho serve ao compilador e ao Ninja (Windows: pagina ANSI e limite de 260; Linux: sempre).
bool   build_path_ok(const std::filesystem::path& p);
size_t max_out_path();    // pasta de saida e kit
size_t max_cache_path();  // pasta do cache
size_t max_direct_kit();  // kit mais longo que isso e copiado para o cache
// Caminho como valor de -D do CMake.
std::wstring cmake_path(const std::filesystem::path& p);
// Caminho que vai para o CMake (kit, SDL3, toml++, compilador) e que ele nao aceita: vazio = aceita. Windows: sempre vazio
// (o kit fora da pagina ANSI e copiado para o cache). Linux: ; \ " e $ (builder_platform_posix.cpp).
std::string build_path_problem(const std::filesystem::path& p);

// Nomes do executavel do jogo (na compilacao e no pacote) e do LEIA-ME do kit.
const wchar_t* game_binary_built();      // bluestinger.exe | bluestinger
const wchar_t* game_binary_installed();  // BlueStinger.exe | BlueStinger
const wchar_t* readme_in_kit();          // tools/dist/LEIA-ME.txt | tools/dist/linux/LEIA-ME-JOGO.txt

// Arquivo do pacote numa instalacao antiga sem a lista (.bs-package).
bool is_package_file(const std::filesystem::path& rel);

// Kit: arquivos obrigatorios desta plataforma, na ordem das mensagens, e o SDL3 (fontes no Windows, pre-compilado no
// Linux). Vazio = completo; senao, o que falta.
std::string check_kit_platform(const std::filesystem::path& kit, const std::filesystem::path& sdl3);
std::filesystem::path default_sdl3(const std::filesystem::path& kit);
// Argumentos do CMake para as dependencias do jogo (SDL3, toml++ e, no Linux, FreeType).
std::vector<std::wstring> dependency_args(const std::filesystem::path& kit, const std::filesystem::path& sdl3,
                                          const std::filesystem::path& toml);
// Licencas que vao para <instalacao>/licencas alem das comuns: (origem, nome no pacote).
std::vector<std::pair<std::filesystem::path, std::wstring>> extra_licenses(const std::filesystem::path& kit,
                                                                           const std::filesystem::path& sdl3);
// Ambiente do compilador para este cache de compilacao (Linux: ZIG_GLOBAL_CACHE_DIR e ZIG_LOCAL_CACHE_DIR em <raiz do
// cache>/zig, provada gravavel, ou dentro do proprio cache, se o usuario nao os definiu; Windows: nada).
void toolchain_env(Env& env, const std::filesystem::path& cache);
// Permissao de execucao no executavel do pacote (Linux; no Windows nada).
void make_executable(const std::filesystem::path& p);
// Projeto C++ editavel (exportar ou compilar a partir dele): so no Windows nesta versao.
bool supports_project();

// Linha de comando: argumentos (com o nome do programa), Ctrl+C/SIGTERM viram cancelamento, pasta do executavel.
std::vector<std::wstring> cli_args(int argc, char** argv);
void                      cli_on_interrupt(std::atomic<bool>* cancel);
std::filesystem::path     exe_dir();
bool                      env_present(const wchar_t* name);
// Opcoes do compilador portatil (--compilador, --compilador-sistema) e a linha de uso delas: so no Linux (o Windows aceita e
// mostra o mesmo de antes; revisao Codex 20261103-105354)
bool        cli_toolchain_options();
const char* cli_usage_extra();

} // namespace bs::builder::plat
