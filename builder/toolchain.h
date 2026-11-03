#pragma once
// Localiza o compilador na maquina do usuario.
// - Windows (toolchain_win.cpp): Visual Studio (ou Build Tools) com o componente C++ x64, pelo vswhere; ambiente do
//   vcvars64; CMake e Ninja que vem com ele; runtime do MSVC para copiar ao lado do jogo.
// - Linux (toolchain_posix.cpp, N9-L L5): o compilador portatil do pacote (D-L2: <Builder>/toolchain, com o arquivo de
//   toolchain do CMake, o CMake e o Ninja) ou, so a pedido (--compilador-sistema), clang, cmake e ninja do sistema.
#include "child_process.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace bs::builder {

struct Toolchain {
    std::filesystem::path installRoot;  // Windows: raiz do Visual Studio / Build Tools; Linux: pasta do compilador
    std::filesystem::path vcvars;       // VC\Auxiliary\Build\vcvars64.bat
    std::filesystem::path cmake, ninja;
    std::filesystem::path crtDir;       // ...\redist\MSVC\<v>\x64\Microsoft.VC*.CRT (Linux: vazio, nada a copiar)
    std::string           msvcVersion;  // VCToolsVersion
    std::string           sdkVersion;   // WindowsSDKVersion
    Env                   env;          // ambiente completo depois do vcvars
    // N9-L (L5): o que o fluxo comum usa
    std::vector<std::wstring> cmakeArgs;    // escolha do compilador para o CMake
    std::string               stamp;        // muda quando o compilador muda (descarta o build incremental)
    std::string               description;  // linhas "Compilador: ..." do VERSAO.txt do jogo
};

struct ToolchainOptions {
    std::filesystem::path vswhere;  // vazio = local padrao do instalador
    std::filesystem::path vcvars;   // definido: pula o vswhere (instalacao fora do padrao)
    // Linux: pasta do compilador portatil (vazio = <Builder>/toolchain) e o recurso ao do sistema
    std::filesystem::path toolchainDir;
    bool                  systemToolchain = false;
};

// Lanca std::runtime_error com mensagem para o usuario (o que falta e como instalar).
Toolchain find_toolchain(const ToolchainOptions& opt, const std::function<void(const std::string&)>& log);

// Texto de ajuda para quando o compilador nao e encontrado.
extern const char* const kToolchainHelp;

} // namespace bs::builder
