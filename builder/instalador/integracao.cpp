// N9.1: a parte comum da integracao (nomes e chaves); o que toca o sistema fica em integracao_win.cpp e integracao_posix.cpp.
#include "instalador/integracao.h"

#include "builder_platform.h"
#include "child_process.h"
#include "instalador/instalacoes.h"

#include <cstdio>

namespace fs = std::filesystem;

namespace bs::builder::instalador {

fs::path lista_instalacoes() { return pasta_usuario() / kListFile; }

std::wstring nome_exibicao(native::edition::Edition e) {
    return e == native::edition::Edition::Remastered ? L"Blue Stinger Remastered" : L"Blue Stinger";
}

std::string chave_pasta(const fs::path& pasta) {
    std::error_code ec;
    fs::path        a = fs::weakly_canonical(fs::absolute(pasta, ec), ec);
    while (a.has_relative_path() && a.filename().empty()) a = a.parent_path();
    const std::string s = narrow(plat::fold_name(path_wide_generic(a)));
    uint32_t          h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    char b[12];
    std::snprintf(b, sizeof b, "%08x", h);
    return b;
}

} // namespace bs::builder::instalador
