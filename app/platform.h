#pragma once
// N9-L: o que o executavel pede ao sistema (argumentos, pastas, mensagens, log). Windows: platform_win.cpp. Linux: platform_posix.cpp.
// Os argumentos e nomes continuam em wstring nos dois sistemas: o codigo que os interpreta e um so.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bs::platform {

std::vector<std::wstring> command_line();  // sem o nome do programa
std::string               to_utf8(const std::wstring& w);
std::wstring              from_utf8(const std::string& s);

enum class BoxKind : uint8_t { Error, Warning, Info };
void message_box(const std::string& title, const std::string& text, BoxKind kind);  // nativa: funciona antes do SDL

std::filesystem::path exe_dir();
std::filesystem::path user_data_dir();    // arquivos do usuario quando a pasta do executavel nao aceita escrita
std::filesystem::path default_saves_dir();  // vazio: a pasta padrao nao existe nesta maquina
uint32_t              process_id();

std::optional<std::wstring> env(const wchar_t* name);
std::FILE*                  open_for_write(const std::filesystem::path& p);

// stdout e stderr vao para `log`, a nao ser que quem abriu o jogo ja os tenha redirecionado.
void redirect_output(const std::filesystem::path& log);
// Falhas do runtime do C que encerram o processo sem mensagem passam pelo log.
void log_runtime_failures();

} // namespace bs::platform
