// N9.1, Windows: a janela do instalador (Win32 + Direct2D, decisao 12.2) e as entradas dos dois executaveis:
//   BlueStinger-Recompiler.exe   o instalador (o mesmo contrato de linha de comando de antes: --disc --out --kit --sdl3
//                                --portatil --edicao --idioma --auto --sem-filmes; novos: --integrar, --captura, --teclas)
//   BlueStinger-Desinstalar.exe  vai no pacote do jogo; "Aplicativos instalados" o chama com --pasta <instalacao>
#include "instalador/gui/hospedeiro.h"
#include "instalador/gui/pintor_d2d.h"
#include "instalador/gui/tema.h"

#include "child_process.h"
#include "ui/i18n.h"
#include "ui/win_shared.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <deque>
#include <exception>
#include <iterator>
#include <mutex>

namespace fs = std::filesystem;

namespace bs::builder::instalador::gui {

namespace {

enum : UINT { WM_POSTADO = WM_APP + 1 };
constexpr UINT_PTR kTimer = 1;

double relogio() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

fs::path exe_dir() {
    std::wstring b(32768, L'\0');
    b.resize(GetModuleFileNameW(nullptr, b.data(), DWORD(b.size())));
    return fs::path(b).parent_path();
}

fs::path known(REFKNOWNFOLDERID id) {
    PWSTR    p = nullptr;
    fs::path r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
    CoTaskMemFree(p);
    return r;
}

// v0.9.7-4 (issue #3, a janela sumia sem mensagem): uma falha fatal grava BlueStinger-Recompiler-erro.txt (ou
// BlueStinger-Desinstalar-erro.txt) ao lado do executavel, ou na pasta temporaria se ali nao der, com o codigo, o endereco e
// o modulo (o .exe ou a DLL de terceiros), e avisa onde esta. Nada de dados do jogo nem caminhos alem do proprio arquivo.
bool g_semJanelaNoErro = false;  // --auto, --teclas, --captura: so o arquivo

// Caminhos de ate 32767 caracteres (revisao Codex 20261120-133930, MEDIA): buffers estaticos (a pilha pode estar no fim,
// num estouro de pilha) e retornos conferidos (truncado = sem o caminho)
constexpr DWORD kCaminhoMax = 32768;
wchar_t         g_modulo[kCaminhoMax], g_exe[kCaminhoMax], g_arq[kCaminhoMax], g_tmp[kCaminhoMax];
wchar_t         g_texto[3 * kCaminhoMax];
char            g_u8[9 * kCaminhoMax];

bool nome_do_modulo(HMODULE h, wchar_t* b) {
    const DWORD n = GetModuleFileNameW(h, b, kCaminhoMax);
    if (n == 0 || n >= kCaminhoMax) {
        b[0] = 0;
        return false;
    }
    return true;
}

LONG WINAPI ao_falhar(EXCEPTION_POINTERS* e) {
    static LONG uma = 0;
    if (InterlockedExchange(&uma, 1)) return EXCEPTION_CONTINUE_SEARCH;
    const EXCEPTION_RECORD* r = e ? e->ExceptionRecord : nullptr;
    HMODULE                 h   = nullptr;
    void*                   end = r ? r->ExceptionAddress : nullptr;
    uintptr_t               off = 0;
    std::wcscpy(g_modulo, L"?");
    if (end && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  static_cast<LPCWSTR>(end), &h)) {
        if (!nome_do_modulo(h, g_modulo)) std::wcscpy(g_modulo, L"?");
        off = uintptr_t(end) - uintptr_t(h);
    }
    const bool temExe = nome_do_modulo(nullptr, g_exe);
    int        n      = std::swprintf(g_texto, std::size(g_texto),
                                      L"Blue Stinger Recompiled: falha fatal / fatal error\r\nexecutavel: %ls\r\ncodigo: 0x%08lX\r\n"
                                      L"endereco: %p\r\nmodulo: %ls +0x%llX\r\n",
                                      temExe ? g_exe : L"?", r ? static_cast<unsigned long>(r->ExceptionCode) : 0ul, end, g_modulo,
                                      static_cast<unsigned long long>(off));
    if (n < 0) n = 0;
    // o nome do arquivo: <exe sem .exe>-erro.txt ao lado dele; senao, o mesmo nome na pasta temporaria do usuario
    const wchar_t* barra = temExe ? std::wcsrchr(g_exe, L'\\') : nullptr;
    const wchar_t* base  = barra ? barra + 1 : L"BlueStinger-Recompiler.exe";
    const wchar_t* ponto = std::wcsrchr(base, L'.');
    const size_t   tam   = std::min<size_t>(250, ponto ? size_t(ponto - base) : std::wcslen(base));
    wchar_t        nome[300];
    std::swprintf(nome, 300, L"%.*ls-erro.txt", int(tam), base);
    HANDLE f = INVALID_HANDLE_VALUE;
    g_arq[0] = 0;
    if (barra && size_t(barra - g_exe) + 1 + std::wcslen(nome) < kCaminhoMax) {
        std::swprintf(g_arq, kCaminhoMax, L"%.*ls\\%ls", int(barra - g_exe), g_exe, nome);
        f = CreateFileW(g_arq, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (f == INVALID_HANDLE_VALUE) {
        const DWORD t = GetTempPathW(kCaminhoMax, g_tmp);
        if (t > 0 && t < kCaminhoMax && t + std::wcslen(nome) < kCaminhoMax) {
            std::swprintf(g_arq, kCaminhoMax, L"%ls%ls", g_tmp, nome);
            f = CreateFileW(g_arq, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
    }
    if (f != INVALID_HANDLE_VALUE) {
        const int b = WideCharToMultiByte(CP_UTF8, 0, g_texto, n, g_u8, int(sizeof g_u8), nullptr, nullptr);
        DWORD     w = 0;
        if (b > 0) WriteFile(f, g_u8, DWORD(b), &w, nullptr);
        CloseHandle(f);
    } else {
        g_arq[0] = 0;
    }
    if (!g_semJanelaNoErro) {
        const std::wstring m = ui::tr("crash.msg", {g_arq[0] ? std::wstring(g_arq) : std::wstring(L"-"), g_texto});
        MessageBoxW(nullptr, m.c_str(), ui::tr("ins.window").c_str(), MB_ICONERROR | MB_TOPMOST);
    }
    return EXCEPTION_EXECUTE_HANDLER;  // fecha sem a caixa do Windows (o codigo de saida e o da excecao)
}

// O seletor de arquivos/pastas do Windows (roda no processo auxiliar --seletor, nunca no instalador)
fs::path dialogo_shell(HWND dono, bool pasta, const std::wstring& titulo, const fs::path& inicial) {
    fs::path         r;
    IFileOpenDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return r;
    DWORD o = 0;
    d->GetOptions(&o);
    d->SetOptions(o | FOS_FORCEFILESYSTEM | (pasta ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    d->SetTitle(titulo.c_str());
    const std::wstring cdi = ui::tr("dlg.cdi_filter"), todos = ui::tr("dlg.all_files");
    if (!pasta) {
        const COMDLG_FILTERSPEC spec[] = {{cdi.c_str(), L"*.gdi;*.cue;*.chd;*.cdi;*.zip;*.7z"}, {todos.c_str(), L"*.*"}};
        d->SetFileTypes(2, spec);
    }
    std::error_code ec;
    fs::path        ini = inicial.empty() ? exe_dir() : pasta ? inicial : inicial.parent_path();
    while (!ini.empty() && !fs::is_directory(ini, ec) && ini != ini.parent_path()) ini = ini.parent_path();
    IShellItem* it = nullptr;
    if (!ini.empty() && SUCCEEDED(SHCreateItemFromParsingName(ini.c_str(), nullptr, IID_PPV_ARGS(&it)))) {
        d->SetFolder(it);
        it->Release();
    }
    if (SUCCEEDED(d->Show(dono)) && SUCCEEDED(d->GetResult(&it))) {
        PWSTR p = nullptr;
        if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) r = p;
        CoTaskMemFree(p);
        it->Release();
    }
    d->Release();
    return r;
}

// Instalador: abre o processo auxiliar (este .exe com --seletor) e espera por ele processando as mensagens (a janela
// continua se desenhando, desabilitada como num dialogo modal). Saida 0 = caminho na saida padrao (UTF-8); 1 = cancelado;
// qualquer outra = falhou (o codigo da excecao, quando uma extensao derruba o auxiliar).
struct SeletorResultado {
    fs::path      caminho;
    bool          falhou = false;
    unsigned long codigo = 0;
};

SeletorResultado seletor_externo(HWND dono, bool pasta, const std::wstring& titulo, const fs::path& inicial) {
    SeletorResultado res;
    std::wstring     exe(32768, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size())));
    std::wstring cmd = quote_arg(exe) + L" --seletor " + (pasta ? L"pasta" : L"arquivo") + L" --titulo " + quote_arg(titulo) +
                       L" --inicial " + quote_arg(inicial.wstring()) + L" --dono " +
                       std::to_wstring(reinterpret_cast<uintptr_t>(dono)) + L" --idioma " +
                       widen(ui::current_language().code);
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE              ler = nullptr, escrever = nullptr;
    if (!CreatePipe(&ler, &escrever, &sa, 1 << 16)) return res.falhou = true, res.codigo = GetLastError(), res;
    SetHandleInformation(ler, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof si};
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = escrever;
    si.hStdError  = escrever;
    si.hStdInput  = nullptr;
    PROCESS_INFORMATION pi{};
    const BOOL          ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
    CloseHandle(escrever);
    if (!ok) {
        res.falhou = true, res.codigo = GetLastError();
        CloseHandle(ler);
        return res;
    }
    CloseHandle(pi.hThread);
    // a saida inteira (um caminho) cabe no buffer do pipe: o auxiliar nunca bloqueia ao escrever
    if (dono) EnableWindow(dono, FALSE);
    for (bool fim = false; !fim;) {
        const DWORD w = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, INFINITE, QS_ALLINPUT);
        if (w != WAIT_OBJECT_0 + 1) break;  // o auxiliar terminou (ou a espera falhou)
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) {
                PostQuitMessage(int(m.wParam));
                fim = true;
                break;
            }
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (dono) {
        EnableWindow(dono, TRUE);
        SetForegroundWindow(dono);
    }
    DWORD saida = 0;
    GetExitCodeProcess(pi.hProcess, &saida);
    CloseHandle(pi.hProcess);
    std::string texto;
    char        b[4096];
    DWORD       n = 0;
    while (ReadFile(ler, b, sizeof b, &n, nullptr) && n) texto.append(b, n);
    CloseHandle(ler);
    while (!texto.empty() && (texto.back() == '\n' || texto.back() == '\r')) texto.pop_back();
    if (saida == 0 && !texto.empty()) res.caminho = fs::path(widen(texto));
    else if (saida != 1) res.falhou = true, res.codigo = saida;
    return res;
}

// O processo auxiliar: --seletor arquivo|pasta --titulo <t> --inicial <caminho> --dono <hwnd> --idioma <cod>. Sem a caixa
// de erro do Windows (quem avisa e o instalador). So testes: BS_INSTALADOR_TESTE_SELETOR=<caminho> responde sem o dialogo;
// BS_INSTALADOR_TESTE_FALHA=seletor cai com acesso invalido, como uma extensao com defeito.
int seletor_auxiliar(const std::vector<std::wstring>& a) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    bool         pasta = false;
    std::wstring titulo, inicial, idioma;
    HWND         dono = nullptr;
    for (size_t i = 1; i + 1 < a.size(); i += 2) {
        if (a[i] == L"--seletor") pasta = a[i + 1] == L"pasta";
        else if (a[i] == L"--titulo") titulo = a[i + 1];
        else if (a[i] == L"--inicial") inicial = a[i + 1];
        else if (a[i] == L"--dono") dono = reinterpret_cast<HWND>(uintptr_t(std::wcstoull(a[i + 1].c_str(), nullptr, 10)));
        else if (a[i] == L"--idioma") idioma = a[i + 1];
    }
    if (dono && !IsWindow(dono)) dono = nullptr;
    ui::set_language(ui::initial_language(narrow(idioma), ui::windows_languages()));
    wchar_t v[4096];
    if (GetEnvironmentVariableW(L"BS_INSTALADOR_TESTE_FALHA", v, 4096) && std::wstring(v) == L"seletor") {
        volatile int* nulo = nullptr;
        *nulo              = 1;
    }
    fs::path r;
    if (const DWORD n = GetEnvironmentVariableW(L"BS_INSTALADOR_TESTE_SELETOR", v, 4096); n && n < 4096) {
        r = fs::path(v);
    } else {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        r = dialogo_shell(dono, pasta, titulo, fs::path(inicial));
        CoUninitialize();
    }
    if (r.empty()) return 1;
    const std::string s = narrow(r.wstring()) + "\n";
    DWORD             w = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s.data(), DWORD(s.size()), &w, nullptr);
    return 0;
}

class QuadroD2D final : public QuadroOffscreen {
public:
    QuadroD2D(float w, float h, float e) : q_(w, h, e), e_(e) {}
    Pintor& comecar() override {
        q_.rt()->BeginDraw();
        p_ = std::make_unique<PintorD2D>(q_.rt(), e_);
        return *p_;
    }
    void terminar() override {
        p_.reset();
        q_.rt()->EndDraw();
    }
    bool salvar(const fs::path& png) override { return q_.salvar(png); }
    bool ok() const { return q_.ok(); }

private:
    Quadro                     q_;
    float                      e_;
    std::unique_ptr<PintorD2D> p_;
};

FabricaQuadro fabrica() {
    return [](float w, float h, float e) -> std::unique_ptr<QuadroOffscreen> {
        auto q = std::make_unique<QuadroD2D>(w, h, e);
        if (!q->ok()) return nullptr;
        return q;
    };
}

struct ApagarAoSair {
    std::vector<fs::path> arquivos, pastas;
} g_apagar;

class Janela final : public Hospedeiro {
public:
    explicit Janela(Config c) : app_(*this, std::move(c)) {}

    int rodar(HINSTANCE inst, int show, bool minimizada) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc   = &Janela::proc;
        wc.hInstance     = inst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"BlueStingerInstalador";
        wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(1));
        if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassExW(&wc);
        const UINT dpi = GetDpiForSystem();
        escala_ = float(dpi) / 96.f;
        // a area util (nao a janela com a moldura) tem o tamanho do desenho, 960 x 620 DIP (tema.h)
        RECT r{0, 0, LONG(tema::kLarguraJanela * escala_), LONG(tema::kAlturaJanela * escala_)};
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                                r.bottom - r.top, nullptr, nullptr, inst, this);
        if (!hwnd_) return 1;
        BOOL escuro = TRUE;  // barra de titulo escura (Windows 10 2004+ e 11; sem efeito nos outros)
        DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &escuro, sizeof escuro);
        ShowWindow(hwnd_, minimizada ? SW_SHOWMINNOACTIVE : show);
        app_.iniciar();
        MSG m;
        while (!fechou_ && GetMessageW(&m, nullptr, 0, 0) > 0) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        return codigo_;
    }

    // Hospedeiro
    void redesenhar() override {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void postar(std::function<void()> f) override {
        {
            std::lock_guard l(m_);
            fila_.push_back(std::move(f));
        }
        if (hwnd_) PostMessageW(hwnd_, WM_POSTADO, 0, 0);
    }
    // v0.9.7-4 (issue #3, a janela fechava ao escolher a imagem): o dialogo modal nao abre mais de dentro do quadro (o clique
    // e tratado durante o desenho); vai pela fila, depois do quadro, e o resultado volta pelo mesmo caminho
    void escolher_arquivo(const std::wstring& t, const fs::path& ini, std::function<void(fs::path)> cb) override {
        postar([this, t, ini, cb] {
            cb(seletor(false, t, ini));
            redesenhar();
        });
    }
    void escolher_pasta(const std::wstring& t, const fs::path& ini, std::function<void(fs::path)> cb) override {
        postar([this, t, ini, cb] {
            cb(seletor(true, t, ini));
            redesenhar();
        });
    }
    void abrir(const fs::path& p) override { ShellExecuteW(hwnd_, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
    bool executar(const fs::path& exe) override {
        return reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_, L"open", exe.c_str(), nullptr, exe.parent_path().c_str(), SW_SHOWNORMAL)) > 32;
    }
    void fechar(int codigo) override {
        codigo_ = codigo;
        fechou_ = true;
        if (hwnd_) DestroyWindow(hwnd_);
    }
    void titulo(const std::wstring& t) override {
        if (hwnd_) SetWindowTextW(hwnd_, t.c_str());
    }
    void apagar_ao_sair(const std::vector<fs::path>& a, const std::vector<fs::path>& p) override { g_apagar = {a, p}; }

private:
    // v0.9.7-4 (issue #3; revisao Codex 20261120-133930, ALTA): o dialogo do Windows carrega extensoes do Explorer de
    // terceiros (no video, o "OneCommander Connector") no processo que o abre. Ele roda agora num processo auxiliar (este
    // mesmo .exe com --seletor): se uma extensao o derrubar, o instalador segue intacto, avisa e pede o caminho no campo.
    fs::path seletor(bool pasta, const std::wstring& titulo, const fs::path& inicial) {
        const SeletorResultado r = seletor_externo(hwnd_, pasta, titulo, inicial);
        teclas_ = {};  // o dialogo modal come as teclas
        if (r.falhou) {
            wchar_t c[16];
            std::swprintf(c, 16, L"0x%08lX", r.codigo);
            MessageBoxW(hwnd_, ui::tr("dlg.failed", {c}).c_str(), ui::tr("ins.window").c_str(), MB_ICONWARNING);
        }
        return r.caminho;
    }

    void pintar() {
        // v0.9.7-4: um WM_PAINT dentro do proprio desenho (o laco modal de um dialogo ou de uma caixa de mensagem aberta
        // durante o quadro) nao desenha por cima do quadro em andamento; o quadro seguinte cobre
        if (pintando_) {
            pendente_ = true;
            return;
        }
        struct Marca {
            bool& b;
            explicit Marca(bool& x) : b(x) { b = true; }
            ~Marca() { b = false; }
        } marca(pintando_);
        RECT rc;
        GetClientRect(hwnd_, &rc);
        const UINT w = UINT(rc.right - rc.left), h = UINT(rc.bottom - rc.top);
        if (!rt_) {
            auto& f = Fabricas::get();
            if (!f.ok) return;
            f.d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd_, {w, h}), rt_.put());
            if (!rt_) return;
        } else {
            rt_->Resize({w, h});
        }
        Entrada e = std::move(entrada_);
        entrada_ = {};
        entrada_.mx = e.mx, entrada_.my = e.my;
        e.teclas.insert(e.teclas.end(), teclas_.begin(), teclas_.end());
        teclas_.clear();
        BOOL anim = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
        rt_->BeginDraw();
        {
            PintorD2D p(rt_.p, escala_);
            app_.quadro(p, e, relogio(), !anim, float(w) / escala_, float(h) / escala_);
        }
        if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) rt_.reset();
        // animacoes discretas (e o progresso): mais quadros enquanto houver movimento
        if (app_.animando()) SetTimer(hwnd_, kTimer, 16, nullptr);
        else KillTimer(hwnd_, kTimer);
        if (pendente_) {
            pendente_ = false;
            redesenhar();
        }
    }

    void tecla(Tecla t) {
        teclas_.push_back(t);
        redesenhar();
    }

    LRESULT msg(UINT m, WPARAM wp, LPARAM lp) {
        switch (m) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            pintar();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: redesenhar(); return 0;
        case WM_TIMER: redesenhar(); return 0;
        case WM_DPICHANGED: {
            escala_ = float(HIWORD(wp)) / 96.f;
            const RECT* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            redesenhar();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            RECT  r{0, 0, LONG(tema::kLarguraMin * escala_), LONG(tema::kAlturaMin * escala_)};
            AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, UINT(escala_ * 96));
            mm->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
            return 0;
        }
        case WM_MOUSEMOVE: {
            entrada_.mx = float(short(LOWORD(lp))) / escala_, entrada_.my = float(short(HIWORD(lp))) / escala_;
            if (!rastreando_) {
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
                rastreando_ = TrackMouseEvent(&t);
            }
            redesenhar();
            return 0;
        }
        case WM_MOUSELEAVE: rastreando_ = false, entrada_.mx = entrada_.my = -1, redesenhar(); return 0;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd_);
            entrada_.apertou = true;
            entrada_.mx = float(short(LOWORD(lp))) / escala_, entrada_.my = float(short(HIWORD(lp))) / escala_;
            redesenhar();
            return 0;
        case WM_LBUTTONUP:
            ReleaseCapture();
            entrada_.soltou = true;
            entrada_.mx = float(short(LOWORD(lp))) / escala_, entrada_.my = float(short(HIWORD(lp))) / escala_;
            redesenhar();
            return 0;
        case WM_MOUSEWHEEL: entrada_.roda += float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA, redesenhar(); return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
            switch (wp) {
            case VK_TAB: tecla(shift ? Tecla::ShiftTab : Tecla::Tab); return 0;
            case VK_RETURN: tecla(Tecla::Enter); return 0;
            case VK_ESCAPE: tecla(Tecla::Esc); return 0;
            case VK_SPACE: tecla(Tecla::Espaco); return 0;
            case VK_LEFT: tecla(Tecla::Esquerda); return 0;
            case VK_RIGHT: tecla(Tecla::Direita); return 0;
            case VK_UP: tecla(Tecla::Cima); return 0;
            case VK_DOWN: tecla(Tecla::Baixo); return 0;
            case VK_HOME: tecla(Tecla::Inicio); return 0;
            case VK_END: tecla(Tecla::Fim); return 0;
            case VK_BACK: tecla(Tecla::Backspace); return 0;
            case VK_DELETE: tecla(Tecla::Delete); return 0;
            case VK_F1: tecla(Tecla::F1); return 0;
            case 'A':
                if (ctrl) tecla(Tecla::SelecionarTudo);
                break;
            case 'V':
                if (ctrl) {
                    entrada_.colado = area_transferencia();
                    tecla(Tecla::Colar);
                }
                break;
            }
            break;
        }
        case WM_CHAR:
            if (wp >= 0x20 && wp != 0x7F && GetKeyState(VK_CONTROL) >= 0) {
                if (wp == L' ') return 0;  // o espaco ja veio como tecla; o campo o recebe pelo texto abaixo
                entrada_.texto += wchar_t(wp);
                redesenhar();
            }
            return 0;
        case WM_POSTADO: {
            std::deque<std::function<void()>> f;
            {
                std::lock_guard l(m_);
                f.swap(fila_);
            }
            for (auto& x : f) x();
            return 0;
        }
        case WM_CLOSE:
            if (app_.pedir_fechar()) fechar(app_.codigo_saida());
            return 0;
        case WM_DESTROY: fechou_ = true; PostQuitMessage(codigo_); return 0;
        }
        return DefWindowProcW(hwnd_, m, wp, lp);
    }

    static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
        Janela* j = reinterpret_cast<Janela*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            j = static_cast<Janela*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(j));
            j->hwnd_ = h;
        }
        return j ? j->msg(m, wp, lp) : DefWindowProcW(h, m, wp, lp);
    }

    static std::wstring area_transferencia() {
        std::wstring r;
        if (!OpenClipboard(nullptr)) return r;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) r = p, GlobalUnlock(h);
        }
        CloseClipboard();
        return r;
    }

    Instalador                        app_;
    HWND                              hwnd_ = nullptr;
    Com<ID2D1HwndRenderTarget>        rt_;
    float                             escala_ = 1;
    Entrada                           entrada_;
    std::vector<Tecla>                teclas_;
    bool                              rastreando_ = false, fechou_ = false;
    bool                              pintando_ = false, pendente_ = false;  // v0.9.7-4: sem desenho reentrante
    int                               codigo_ = 0;
    std::mutex                        m_;
    std::deque<std::function<void()>> fila_;
};

std::vector<std::wstring> argumentos() {
    int       n = 0;
    wchar_t** v = CommandLineToArgvW(GetCommandLineW(), &n);
    std::vector<std::wstring> r(v, v + n);
    LocalFree(v);
    return r;
}

int iniciar_processo(HINSTANCE inst, int show, Linha& l) {
    g_semJanelaNoErro = l.c.automatico || !l.teclas.empty() || !l.captura.empty();
    SetUnhandledExceptionFilter(&ao_falhar);  // v0.9.7-4: falha fatal com arquivo e mensagem (issue #3)
    std::set_terminate([] {                   // uma excecao C++ sem tratamento chega ao mesmo filtro
        RaiseException(0xE0425301u, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        std::abort();
    });
    // so testes (test_instalador.ps1, I8): uma falha de verdade logo aqui prova o arquivo e o codigo de saida
    if (wchar_t v[8]; GetEnvironmentVariableW(L"BS_INSTALADOR_TESTE_FALHA", v, 8) && v[0] == L'1') {
        volatile int* nulo = nullptr;
        *nulo              = 1;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    l.c.exeDir = exe_dir();
    l.c.idiomasSistema = ui::windows_languages();
    int rc;
    if (!l.teclas.empty()) rc = roteiro_teclas(l.c, l.teclas, l.captura, fabrica());  // --captura: onde guardar as imagens dele
    else if (!l.captura.empty()) rc = capturar(l.c, l.captura, l.escalas, fabrica());
    else {
        Janela j(l.c);
        rc = j.rodar(inst, show, l.c.automatico);
    }
    CoUninitialize();
    return rc;
}

} // namespace

int principal_instalador(HINSTANCE inst, int show) {
    const std::vector<std::wstring> args = argumentos();
    if (args.size() > 2 && args[1] == L"--seletor") return seletor_auxiliar(args);  // v0.9.7-4: o dialogo fora do instalador
    Linha l;
    if (!ler_linha(args, l)) return 2;
    l.c.kit = l.c.kit.empty() ? exe_dir() / L"kit" : l.c.kit;
    {
        std::error_code ec;
        const fs::path  d = exe_dir() / L"BlueStinger-Desinstalar.exe";
        if (fs::is_regular_file(d, ec)) l.c.desinstalador = d;
    }
    const fs::path docs = known(FOLDERID_Documents);
    if (!docs.empty()) l.c.pastaPadrao = docs / L"Blue Stinger Recompilado";
    // kit incompleto: a mensagem de antes, sem a janela (no --auto, so o codigo)
    if (l.captura.empty() && l.teclas.empty()) {
        if (const std::string k = check_kit(l.c.kit, l.c.sdl3.empty() ? l.c.kit / L"third_party" / L"SDL3" : l.c.sdl3); !k.empty()) {
            if (!l.c.automatico) {
                std::string sv;
                ui::set_language(ui::initial_language(l.c.idioma, ui::windows_languages()));
                MessageBoxW(nullptr, ui::tr("msg.kit_missing").c_str(), ui::tr("ins.window").c_str(), MB_ICONERROR);
            }
            return 1;
        }
    }
    return iniciar_processo(inst, show, l);
}

// O desinstalador roda de dentro da pasta do jogo (o "Aplicativos instalados" o chama la): o que ele mesmo segura (o .exe e
// as DLLs do runtime que carregou dali) sai depois que ele termina, por um cmd que espera e apaga so esses arquivos e as
// pastas que ficarem vazias (rd sem /s nunca apaga nada do jogador).
int principal_desinstalador(HINSTANCE inst, int show) {
    Linha l;
    if (!ler_linha(argumentos(), l)) return 2;
    l.c.modo = Config::Modo::Desinstalar;
    if (l.c.pastaDesinstalar.empty()) l.c.pastaDesinstalar = exe_dir().parent_path();
    const int rc = iniciar_processo(inst, show, l);
    if (!g_apagar.arquivos.empty()) {
        // Os caminhos nunca entram na linha do cmd (um nome de pasta com & | ^ % viraria comando: revisao Codex
        // 20261104-195545): cada um vai numa variavel de ambiente herdada e a linha so tem "%BS_DES_n%" entre aspas. O cmd
        // expande a variavel uma vez e nao reinterpreta o valor dentro das aspas; os caminhos do Windows nao tem aspas.
        std::wstring cmd = L"cmd.exe /d /v:off /s /c \"ping -n 3 127.0.0.1 >nul";
        int          n   = 0;
        auto         var = [&](const fs::path& p) {
            const std::wstring nome = L"BS_DES_" + std::to_wstring(n++);
            SetEnvironmentVariableW(nome.c_str(), p.wstring().c_str());
            return L"\"%" + nome + L"%\"";
        };
        for (const auto& f : g_apagar.arquivos) cmd += L" & del /f /q " + var(f);
        for (const auto& d : g_apagar.pastas) cmd += L" & rd " + var(d);
        cmd += L"\"";
        STARTUPINFOW        si{sizeof si};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr,
                           nullptr, &si, &pi))
            CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    }
    return rc;
}

} // namespace bs::builder::instalador::gui
