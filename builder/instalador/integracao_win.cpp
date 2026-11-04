// N9.1: integracao no Windows, por usuario (integracao.h). Atalhos .lnk pelo IShellLinkW; a entrada de "Aplicativos
// instalados" em HKCU (sem administrador). BS_INSTALADOR_TESTE_RAIZ desvia os dois para os testes.
#include "instalador/integracao.h"

#include "builder_platform.h"
#include "child_process.h"

#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>

namespace fs = std::filesystem;

namespace bs::builder::instalador {

namespace {

fs::path test_root() {
    std::wstring b(32768, L'\0');
    const DWORD  n = GetEnvironmentVariableW(L"BS_INSTALADOR_TESTE_RAIZ", b.data(), DWORD(b.size()));
    if (!n || n >= b.size()) return {};
    b.resize(n);
    return fs::path(b);
}

fs::path known(REFKNOWNFOLDERID id) {
    PWSTR    p = nullptr;
    fs::path r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) r = p;
    CoTaskMemFree(p);
    return r;
}

fs::path menu_dir() {
    const fs::path t = test_root();
    return t.empty() ? known(FOLDERID_Programs) : t / L"menu";
}
fs::path desktop_dir() {
    const fs::path t = test_root();
    return t.empty() ? known(FOLDERID_Desktop) : t / L"desktop";
}

std::wstring uninstall_root() {
    return test_root().empty() ? L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
                               : L"Software\\BlueStingerRecomp\\Teste\\Uninstall";
}
std::wstring uninstall_key(const fs::path& pasta) { return uninstall_root() + L"\\BlueStingerRecomp-" + widen(chave_pasta(pasta)); }

struct Com {
    bool    owns = false;
    Com() { owns = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)); }
    ~Com() {
        if (owns) CoUninitialize();
    }
};

bool make_link(const fs::path& lnk, const fs::path& target, const fs::path& icon, const std::wstring& desc, std::string* err) {
    IShellLinkW* sl = nullptr;
    HRESULT      hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl));
    if (FAILED(hr)) {
        *err = "IShellLink indisponivel";
        return false;
    }
    sl->SetPath(target.c_str());
    sl->SetWorkingDirectory(target.parent_path().c_str());
    sl->SetDescription(desc.c_str());
    if (!icon.empty()) sl->SetIconLocation(icon.c_str(), 0);
    IPersistFile* pf = nullptr;
    hr = sl->QueryInterface(IID_PPV_ARGS(&pf));
    if (SUCCEEDED(hr)) {
        std::error_code ec;
        fs::create_directories(lnk.parent_path(), ec);
        hr = pf->Save(lnk.c_str(), TRUE);
        pf->Release();
    }
    sl->Release();
    if (FAILED(hr)) *err = "nao foi possivel gravar " + path_text(lnk);
    return SUCCEEDED(hr);
}

// nome livre: "Blue Stinger Remastered.lnk"; ocupado por outra coisa (outra instalacao): com o nome da pasta e, se esse tambem
// estiver ocupado, com um numero. Nunca grava sobre um arquivo que nao e desta instalacao (revisao Codex 20261104-195545).
fs::path free_link(const fs::path& dir, const std::wstring& base, const fs::path& pasta,
                   const std::vector<fs::path>& anteriores) {
    std::error_code ec;
    for (int i = 0; i < 100; ++i) {
        const fs::path p = i == 0 ? dir / (base + L".lnk")
                                  : dir / (base + L" (" + pasta.filename().wstring() + (i > 1 ? L" " + std::to_wstring(i) : L"") + L").lnk");
        const bool nosso = std::any_of(anteriores.begin(), anteriores.end(), [&](const fs::path& a) { return fs::equivalent(a, p, ec); });
        if (!fs::exists(p, ec) || nosso) return p;
    }
    return {};
}

bool mesma(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (a.empty() || b.empty()) return false;
    return plat::fold_name(fs::absolute(a, ec).lexically_normal().wstring()) ==
           plat::fold_name(fs::absolute(b, ec).lexically_normal().wstring());
}

// Um atalho que a lista diz ser desta instalacao so sai se for mesmo um atalho nosso: arquivo .lnk "Blue Stinger*" direto no
// Menu Iniciar ou na Area de Trabalho do usuario. Uma lista adulterada nao apaga outra coisa (revisao Codex 20261104-195545).
bool atalho_nosso(const fs::path& p) {
    std::error_code ec;
    if (fs::symlink_status(p, ec).type() != fs::file_type::regular) return false;
    if (plat::fold_name(p.extension().wstring()) != L".lnk") return false;
    if (plat::fold_name(p.filename().wstring()).rfind(L"blue stinger", 0) != 0) return false;
    return mesma(p.parent_path(), menu_dir()) || mesma(p.parent_path(), desktop_dir());
}

void set_str(HKEY k, const wchar_t* name, const std::wstring& v) {
    RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), DWORD((v.size() + 1) * sizeof(wchar_t)));
}
void set_dw(HKEY k, const wchar_t* name, DWORD v) { RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof v); }

} // namespace

fs::path pasta_usuario() {
    const fs::path t = test_root();
    if (!t.empty()) return t / L"usuario";
    const fs::path b = known(FOLDERID_LocalAppData);
    return b.empty() ? fs::path() : b / L"BlueStingerRecomp";
}

std::vector<fs::path> integrar(const DadosIntegracao& d, const Atalhos& a, const std::vector<fs::path>& anteriores, std::string* aviso) {
    Com                   com;
    std::vector<fs::path> feitos;
    std::error_code       ec;
    for (const auto& p : anteriores)
        if (atalho_nosso(p)) fs::remove(p, ec);
    const std::wstring nome = nome_exibicao(d.edicao);
    const fs::path     icon = d.icone.empty() ? d.exe : d.icone;
    auto add = [&](const fs::path& dir) {
        if (dir.empty()) return;
        const fs::path lnk = free_link(dir, nome, d.pasta, anteriores);
        std::string    err = "nenhum nome livre para o atalho em " + path_text(dir);
        if (lnk.empty()) {
            if (aviso) *aviso += err + "\n";
            return;
        }
        if (make_link(lnk, d.exe, icon, nome, &err)) feitos.push_back(lnk);
        else if (aviso) *aviso += err + "\n";
    };
    if (a.menu) add(menu_dir());
    if (a.areaDeTrabalho) add(desktop_dir());

    if (!d.desinstalador.empty()) {
        HKEY       k = nullptr;
        const auto key = uninstall_key(d.pasta);
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            set_str(k, L"DisplayName", nome + L" (" + d.pasta.wstring() + L")");
            set_str(k, L"DisplayVersion", widen(d.versao));
            set_str(k, L"Publisher", L"Blue Stinger Recompiled (projeto comunitario)");
            set_str(k, L"InstallLocation", d.pasta.wstring());
            set_str(k, L"DisplayIcon", icon.wstring());
            set_str(k, L"UninstallString", L"\"" + d.desinstalador.wstring() + L"\" --pasta \"" + d.pasta.wstring() + L"\"");
            set_str(k, L"QuietUninstallString",
                    L"\"" + d.desinstalador.wstring() + L"\" --pasta \"" + d.pasta.wstring() + L"\" --auto");
            set_dw(k, L"NoModify", 1);
            set_dw(k, L"NoRepair", 1);
            if (d.tamanhoKB) set_dw(k, L"EstimatedSize", DWORD(std::min<uint64_t>(d.tamanhoKB, 0xFFFFFFFFu)));
            RegCloseKey(k);
        } else if (aviso) {
            *aviso += "nao foi possivel registrar o desinstalador em Aplicativos instalados\n";
        }
    }
    return feitos;
}

void desintegrar(const fs::path& pasta, const std::vector<fs::path>& atalhos) {
    std::error_code ec;
    for (const auto& p : atalhos)
        if (atalho_nosso(p)) fs::remove(p, ec);
    RegDeleteTreeW(HKEY_CURRENT_USER, uninstall_key(pasta).c_str());
}

bool arquivo_em_uso(const fs::path& f) {
    // sem compartilhar a exclusao: falha com o arquivo mapeado como imagem (o processo rodando) ou aberto sem FILE_SHARE_DELETE
    HANDLE h = CreateFileW(f.c_str(), DELETE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_SHARING_VIOLATION || GetLastError() == ERROR_ACCESS_DENIED;
    CloseHandle(h);
    return false;
}

bool entrada_desinstalacao(const fs::path& pasta, std::wstring* comando) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, uninstall_key(pasta).c_str(), 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    if (comando) {
        wchar_t buf[4096];
        DWORD   sz = sizeof buf, type = 0;
        if (RegQueryValueExW(k, L"UninstallString", nullptr, &type, reinterpret_cast<BYTE*>(buf), &sz) == ERROR_SUCCESS && type == REG_SZ)
            *comando = std::wstring(buf, wcsnlen(buf, sz / sizeof(wchar_t)));
    }
    RegCloseKey(k);
    return true;
}

} // namespace bs::builder::instalador
