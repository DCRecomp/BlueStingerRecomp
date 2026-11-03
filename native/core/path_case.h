#pragma once
// N9-L (L4, A11): caminhos de mods sem diferenciar maiusculas, como no Windows. Um mod feito no Windows chama "E001.mp4",
// "PT-BR" ou "Manifest.toml" do jeito que quiser; num sistema de arquivos que diferencia maiusculas (Linux) o nome exato falha.
// resolve_case() tenta primeiro o caminho exato (no Windows sempre basta: o resultado e o mesmo de antes) e so entao procura
// cada parte do caminho na pasta sem diferenciar maiusculas (ASCII). Varios candidatos: o primeiro em ordem de nome, para o
// resultado nao depender da ordem do diretorio.
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

namespace bs::native {

// UTF-8 do nome (sem a pagina de codigo do Windows, que nao representa todo nome), com as letras ASCII em maiusculas
inline std::string ascii_upper(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    std::string         s(u.begin(), u.end());
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return s;
}

// `base` / `relative`, com cada parte de `relative` achada sem diferenciar maiusculas. Se alguma parte nao existe de jeito
// nenhum, devolve o caminho como pedido (quem chama ve que nao existe). `base` em si nao e procurado.
inline std::filesystem::path resolve_case(const std::filesystem::path& base, const std::filesystem::path& relative) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path  exact = base / relative;
    if (relative.empty() || fs::exists(exact, ec)) return exact;
    fs::path cur = base;
    for (const fs::path& part : relative) {
        if (part.empty() || part == ".") continue;
        if (part == ".." || fs::exists(cur / part, ec)) {
            cur /= part;
            continue;
        }
        const std::string want = ascii_upper(part);
        fs::path          found;
        for (const auto& e : fs::directory_iterator(cur, ec)) {
            const fs::path name = e.path().filename();
            if (ascii_upper(name) == want && (found.empty() || name < found)) found = name;
        }
        if (found.empty()) return exact;
        cur /= found;
    }
    return cur;
}

} // namespace bs::native
