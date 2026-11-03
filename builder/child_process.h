#pragma once
// Processos filhos do Builder (vcvars, cmake, ninja/cl, o jogo na verificacao): linha de
// comando e ambiente em UTF-16, saida linha a linha por callback, cancelamento que mata a
// arvore inteira (job object).
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace bs::builder {

// Ambiente completo (nome -> valor). Vazio = herda o do Builder.
using Env = std::map<std::wstring, std::wstring>;

struct ProcessResult {
    int  exitCode  = -1;
    bool started   = false;
    bool cancelled = false;
    bool timedOut  = false;
};

// Cada linha de saida (stdout e stderr juntos) vai para onLine em UTF-8. A saida das
// ferramentas pode vir em UTF-8 (cmake, ninja) ou na pagina OEM (cl, link, cmd): linha
// que nao e UTF-8 valido e convertida da pagina OEM. utf16Output: o filho escreve UTF-16
// (cmd /u). timeoutMs = 0: sem limite.
ProcessResult run_process(const std::wstring& commandLine, const std::filesystem::path& cwd, const Env& env,
                          const std::function<void(const std::string&)>& onLine,
                          const std::atomic<bool>* cancel = nullptr, bool utf16Output = false,
                          unsigned timeoutMs = 0);

// N9-L (L5): o processo pela lista de argumentos (argv[0] = programa). No Windows vira a linha de comando com quote_arg
// (a mesma que o Builder montava); no Linux vai direto ao execve, sem shell: nada que venha do usuario e interpretado.
// Programa sem barra: procurado no PATH do ambiente passado (ou do Builder).
ProcessResult run_process_argv(const std::vector<std::wstring>& argv, const std::filesystem::path& cwd, const Env& env,
                               const std::function<void(const std::string&)>& onLine,
                               const std::atomic<bool>* cancel = nullptr, unsigned timeoutMs = 0);
// A mesma lista como texto, para o log.
std::wstring display_command(const std::vector<std::wstring>& argv);

// Argumento entre aspas para CreateProcess/CommandLineToArgvW (Linux: so para exibir).
std::wstring quote_arg(const std::wstring& a);

// Caminho em texto largo sem passar pela localidade (Linux: UTF-8 -> UTF-32), nativo e com '/'.
std::wstring path_wide(const std::filesystem::path& p);
std::wstring path_wide_generic(const std::filesystem::path& p);

std::wstring widen(const std::string& utf8);
std::string  narrow(const std::wstring& w);
std::string  path_text(const std::filesystem::path& p);  // UTF-8, sem excecao

// O caminho e representavel na pagina ANSI do sistema? O Ninja do Visual Studio usa as
// APIs "A" do Windows: fora disso, os caracteres viram '?'.
bool ansi_safe(const std::filesystem::path& p);

// Mensagem de um error_code em UTF-8 (a do MSVC vem na pagina ANSI do sistema).
std::string error_text(const std::error_code& ec);

// Ambiente do processo atual.
Env current_env();

} // namespace bs::builder
