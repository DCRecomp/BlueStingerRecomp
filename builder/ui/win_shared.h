#pragma once
// Pecas comuns as duas janelas do Builder (BlueStinger-Recompiler e BlueStinger-Developer): a chave do
// catalogo de cada etapa e os idiomas preferenciais do Windows (o idioma inicial segue a mesma regra nas duas).
#include "builder_core.h"
#include "ui/step_keys.h"

#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cwchar>
#include <string>
#include <vector>

namespace bs::builder::ui {

// idiomas preferenciais do usuario no Windows (o primeiro decide)
inline std::vector<std::wstring> windows_languages() {
    ULONG                     n = 0, size = 0;
    std::vector<std::wstring> r;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &n, nullptr, &size) || !size) return r;
    std::wstring buf(size, L'\0');
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &n, buf.data(), &size)) return r;
    for (const wchar_t* p = buf.c_str(); *p; p += std::wcslen(p) + 1) r.emplace_back(p);
    return r;
}

} // namespace bs::builder::ui
