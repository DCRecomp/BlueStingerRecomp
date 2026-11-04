// N9.1, Linux: a janela do instalador (decisao do mantenedor, 2026-11-04): SDL3 (X11 e Wayland, DPI alto) mostrando o buffer
// do pintor em software (pintor_sw.cpp, FreeType + Noto Sans). Dialogos de arquivo pelo SDL3 (portal do desktop ou zenity).
// Sem Win32, Wine nem Proton. Entradas dos dois executaveis:
//   BlueStinger-Recompiler   o instalador (a mesma linha de comando do Windows)
//   BlueStinger-Desinstalar  vai no pacote do jogo (--pasta <instalacao>); no Linux nao precisa de copia temporaria
#include "instalador/gui/arte.h"
#include "instalador/gui/hospedeiro.h"
#include "instalador/gui/pintor_sw.h"
#include "instalador/gui/tema.h"
#include "instalador/integracao.h"

#include "builder_platform.h"
#include "child_process.h"
#include "ui/i18n.h"

#include <SDL3/SDL.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>

namespace fs = std::filesystem;

namespace bs::builder::instalador::gui {

namespace {

double relogio() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

class QuadroSW final : public QuadroOffscreen {
public:
    QuadroSW(float w, float h, float e) : p_(uint32_t(w * e + 0.5f), uint32_t(h * e + 0.5f), e) {}
    Pintor& comecar() override { return p_; }
    void    terminar() override {}
    bool    salvar(const fs::path& png) override { return p_.salvar_png(png); }

private:
    PintorSW p_;
};

FabricaQuadro fabrica() {
    return [](float w, float h, float e) -> std::unique_ptr<QuadroOffscreen> { return std::make_unique<QuadroSW>(w, h, e); };
}

// idiomas do sistema: LANGUAGE (lista com ':'), LC_ALL, LC_MESSAGES, LANG ("pt_BR.UTF-8" -> "pt-BR")
std::vector<std::wstring> idiomas_sistema() {
    std::vector<std::wstring> r;
    auto add = [&](std::string v) {
        if (v.empty() || v == "C" || v == "POSIX") return;
        if (const auto p = v.find_first_of(".@"); p != std::string::npos) v.resize(p);
        for (char& c : v)
            if (c == '_') c = '-';
        r.push_back(widen(v));
    };
    if (const char* l = std::getenv("LANGUAGE"); l && *l) {
        std::string s = l;
        for (size_t i = 0, j; i <= s.size(); i = j + 1) {
            j = s.find(':', i);
            if (j == std::string::npos) j = s.size();
            add(s.substr(i, j - i));
        }
    }
    for (const char* n : {"LC_ALL", "LC_MESSAGES", "LANG"})
        if (const char* v = std::getenv(n); v && *v) add(v);
    return r;
}

class Janela final : public Hospedeiro {
public:
    explicit Janela(Config c) : app_(*this, std::move(c)) {}
    ~Janela() override {
        if (tex_) SDL_DestroyTexture(tex_);
        if (ren_) SDL_DestroyRenderer(ren_);
        if (win_) SDL_DestroyWindow(win_);
    }

    int rodar(bool minimizada) {
        evento_ = SDL_RegisterEvents(1);
        win_ = SDL_CreateWindow("Blue Stinger", int(tema::kLarguraJanela), int(tema::kAlturaJanela),
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
        if (!win_) {
            std::fprintf(stderr, "janela: %s\n", SDL_GetError());
            return 1;
        }
        ren_ = SDL_CreateRenderer(win_, nullptr);
        if (!ren_) ren_ = SDL_CreateRenderer(win_, "software");
        if (!ren_) {
            std::fprintf(stderr, "renderizador: %s\n", SDL_GetError());
            return 1;
        }
        // a janela comeca no tamanho em DIP da escala do monitor
        const float k = std::max(1.f, SDL_GetWindowDisplayScale(win_));
        const float dens = std::max(1.f, SDL_GetWindowPixelDensity(win_));
        SDL_SetWindowSize(win_, int(tema::kLarguraJanela * k / dens), int(tema::kAlturaJanela * k / dens));
        SDL_SetWindowMinimumSize(win_, int(tema::kLarguraMin * k / dens), int(tema::kAlturaMin * k / dens));
        icone();
        if (minimizada) SDL_MinimizeWindow(win_);
        SDL_ShowWindow(win_);
        SDL_StartTextInput(win_);
        app_.iniciar();
        bool sujo = true;
        while (!fechou_) {
            SDL_Event ev;
            const bool tem = sujo || app_.animando() ? SDL_WaitEventTimeout(&ev, 16) : SDL_WaitEvent(&ev);
            if (tem) {
                do sujo |= tratar(ev);
                while (!fechou_ && SDL_PollEvent(&ev));
            }
            if (fechou_) break;
            if (sujo || app_.animando()) {
                pintar();
                sujo = false;
            }
        }
        return codigo_;
    }

    void redesenhar() override { acordar(); }
    void postar(std::function<void()> f) override {
        {
            std::lock_guard l(m_);
            fila_.push_back(std::move(f));
        }
        acordar();
    }
    void escolher_arquivo(const std::wstring& t, const fs::path& ini, std::function<void(fs::path)> cb) override {
        static const SDL_DialogFileFilter filtros[] = {{"CDI", "cdi;CDI"}, {"*", "*"}};
        auto* pedido = new Pedido{this, std::move(cb)};
        (void)t;
        const std::string inicial = ini.empty() ? std::string() : path_text(ini);
        SDL_ShowOpenFileDialog(&Janela::resposta, pedido, win_, filtros, 2, inicial.empty() ? nullptr : inicial.c_str(), false);
    }
    void escolher_pasta(const std::wstring& t, const fs::path& ini, std::function<void(fs::path)> cb) override {
        auto* pedido = new Pedido{this, std::move(cb)};
        (void)t;
        std::error_code   ec;
        const std::string inicial = ini.empty() || !fs::is_directory(ini, ec) ? std::string() : path_text(ini);
        SDL_ShowOpenFolderDialog(&Janela::resposta, pedido, win_, inicial.empty() ? nullptr : inicial.c_str(), false);
    }
    void abrir(const fs::path& p) override {
        const std::string url = "file://" + path_text(fs::absolute(p));
        SDL_OpenURL(url.c_str());
    }
    bool executar(const fs::path& exe) override {
        const std::string e = path_text(exe), d = path_text(exe.parent_path());
        const pid_t       pid = fork();
        if (pid < 0) return false;
        if (pid == 0) {  // neto desligado do instalador (o filho sai na hora, sem zumbi)
            if (fork() != 0) _exit(0);
            setsid();
            if (chdir(d.c_str()) != 0) _exit(127);
            execl(e.c_str(), e.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        return WIFEXITED(st) && WEXITSTATUS(st) == 0;
    }
    void fechar(int codigo) override {
        codigo_ = codigo;
        fechou_ = true;
    }
    void titulo(const std::wstring& t) override {
        if (win_) SDL_SetWindowTitle(win_, narrow(t).c_str());
    }

    fs::path icone_atalho() override { return icone_atalho_png(fabrica()); }

private:
    struct Pedido {
        Janela*                         j;
        std::function<void(fs::path)>   cb;
    };
    // o SDL pode chamar de outra thread: a resposta vai para a fila da janela
    static void SDLCALL resposta(void* u, const char* const* lista, int) {
        auto*    p = static_cast<Pedido*>(u);
        fs::path r;
        if (lista && lista[0]) r = fs::path(lista[0]);
        p->j->postar([p, r] {
            p->cb(r);
            delete p;
        });
    }

    void acordar() {
        SDL_Event e{};
        e.type = evento_;
        SDL_PushEvent(&e);
    }

    void icone() {
        PintorSW p(64, 64, 1);
        p.limpar({0, 0, 0, 0});
        arte::emblema(p, 0, 0, 64, true);
        if (SDL_Surface* s = SDL_CreateSurfaceFrom(64, 64, SDL_PIXELFORMAT_ARGB8888, const_cast<uint32_t*>(p.pixels().data()), 64 * 4)) {
            SDL_SetWindowIcon(win_, s);
            SDL_DestroySurface(s);
        }
    }

    float dip_por_ponto() const {
        const float k = std::max(0.5f, SDL_GetWindowDisplayScale(win_)), d = std::max(0.5f, SDL_GetWindowPixelDensity(win_));
        return d / k;
    }

    bool tratar(const SDL_Event& ev) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (app_.pedir_fechar()) fechar(app_.codigo_saida());
            return true;
        case SDL_EVENT_MOUSE_MOTION:
            entrada_.mx = ev.motion.x * dip_por_ponto(), entrada_.my = ev.motion.y * dip_por_ponto();
            return true;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE: entrada_.mx = entrada_.my = -1; return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (ev.button.button != SDL_BUTTON_LEFT) return false;
            entrada_.mx = ev.button.x * dip_por_ponto(), entrada_.my = ev.button.y * dip_por_ponto();
            (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? entrada_.apertou : entrada_.soltou) = true;
            return true;
        case SDL_EVENT_MOUSE_WHEEL: entrada_.roda += ev.wheel.y; return true;
        case SDL_EVENT_KEY_DOWN: {
            const bool ctrl = (ev.key.mod & SDL_KMOD_CTRL) != 0, shift = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
            switch (ev.key.key) {
            case SDLK_TAB: entrada_.teclas.push_back(shift ? Tecla::ShiftTab : Tecla::Tab); break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER: entrada_.teclas.push_back(Tecla::Enter); break;
            case SDLK_ESCAPE: entrada_.teclas.push_back(Tecla::Esc); break;
            case SDLK_SPACE: entrada_.teclas.push_back(Tecla::Espaco); break;
            case SDLK_LEFT: entrada_.teclas.push_back(Tecla::Esquerda); break;
            case SDLK_RIGHT: entrada_.teclas.push_back(Tecla::Direita); break;
            case SDLK_UP: entrada_.teclas.push_back(Tecla::Cima); break;
            case SDLK_DOWN: entrada_.teclas.push_back(Tecla::Baixo); break;
            case SDLK_HOME: entrada_.teclas.push_back(Tecla::Inicio); break;
            case SDLK_END: entrada_.teclas.push_back(Tecla::Fim); break;
            case SDLK_BACKSPACE: entrada_.teclas.push_back(Tecla::Backspace); break;
            case SDLK_DELETE: entrada_.teclas.push_back(Tecla::Delete); break;
            case SDLK_F1: entrada_.teclas.push_back(Tecla::F1); break;
            case SDLK_A:
                if (ctrl) entrada_.teclas.push_back(Tecla::SelecionarTudo);
                break;
            case SDLK_V:
                if (ctrl) {
                    if (char* t = SDL_GetClipboardText()) {
                        entrada_.colado = widen(t);
                        SDL_free(t);
                    }
                    entrada_.teclas.push_back(Tecla::Colar);
                }
                break;
            default: return false;
            }
            return true;
        }
        case SDL_EVENT_TEXT_INPUT: {
            for (wchar_t ch : widen(ev.text.text))
                if (ch != L' ') entrada_.texto += ch;  // o espaco ja veio como tecla (o campo o insere)
            return true;
        }
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_EXPOSED: return true;
        default:
            if (ev.type == evento_) {
                std::deque<std::function<void()>> f;
                {
                    std::lock_guard l(m_);
                    f.swap(fila_);
                }
                for (auto& x : f) x();
                return true;
            }
            return false;
        }
    }

    void pintar() {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(win_, &w, &h);
        if (w <= 0 || h <= 0) return;
        const float k = std::max(0.5f, SDL_GetWindowDisplayScale(win_));
        if (!p_ || p_->largura() != uint32_t(w) || p_->altura() != uint32_t(h) || p_->escala() != k) {
            if (!p_) p_ = std::make_unique<PintorSW>(uint32_t(w), uint32_t(h), k);
            else p_->redimensionar(uint32_t(w), uint32_t(h), k);
            if (tex_) SDL_DestroyTexture(tex_);
            tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        }
        Entrada e = std::move(entrada_);
        entrada_ = {};
        entrada_.mx = e.mx, entrada_.my = e.my;
        const char* ma = std::getenv("BS_MENOS_ANIMACAO");
        app_.quadro(*p_, e, relogio(), ma && *ma == '1', float(w) / k, float(h) / k);
        SDL_UpdateTexture(tex_, nullptr, p_->pixels().data(), w * 4);
        SDL_RenderClear(ren_);
        SDL_RenderTexture(ren_, tex_, nullptr, nullptr);
        SDL_RenderPresent(ren_);
    }

    Instalador                        app_;
    SDL_Window*                       win_ = nullptr;
    SDL_Renderer*                     ren_ = nullptr;
    SDL_Texture*                      tex_ = nullptr;
    std::unique_ptr<PintorSW>         p_;
    Uint32                            evento_ = 0;
    Entrada                           entrada_;
    bool                              fechou_ = false;
    int                               codigo_ = 0;
    std::mutex                        m_;
    std::deque<std::function<void()>> fila_;
};

int iniciar_processo(Linha& l) {
    l.c.exeDir = plat::exe_dir();
    l.c.idiomasSistema = idiomas_sistema();
    l.c.linux_ = true;
    if (!l.teclas.empty()) return roteiro_teclas(l.c, l.teclas, l.captura, fabrica());
    if (!l.captura.empty()) return capturar(l.c, l.captura, l.escalas, fabrica());
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    int rc;
    {
        Janela j(l.c);
        rc = j.rodar(l.c.automatico);
    }
    SDL_Quit();
    return rc;
}

std::vector<std::wstring> argumentos(int argc, char** argv) { return plat::cli_args(argc, argv); }

} // namespace

int principal_instalador(int argc, char** argv) {
    Linha l;
    if (!ler_linha(argumentos(argc, argv), l)) return 2;
    if (l.c.kit.empty()) l.c.kit = plat::exe_dir() / "kit";
    std::error_code ec;
    if (fs::is_regular_file(plat::exe_dir() / "BlueStinger-Desinstalar", ec)) l.c.desinstalador = plat::exe_dir() / "BlueStinger-Desinstalar";
    if (const char* h = std::getenv("HOME"); h && *h) l.c.pastaPadrao = fs::path(h) / "Jogos" / "BlueStinger";
    if (l.captura.empty() && l.teclas.empty())
        if (const std::string k = check_kit(l.c.kit, l.c.sdl3.empty() ? plat::default_sdl3(l.c.kit) : l.c.sdl3); !k.empty()) {
            std::fprintf(stderr, "%s\n", k.c_str());
            if (!l.c.automatico) {
                ui::set_language(ui::initial_language(l.c.idioma, idiomas_sistema()));
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, narrow(ui::tr("ins.window")).c_str(), narrow(ui::tr("msg.kit_missing")).c_str(), nullptr);
            }
            return 1;
        }
    return iniciar_processo(l);
}

int principal_desinstalador(int argc, char** argv) {
    Linha l;
    if (!ler_linha(argumentos(argc, argv), l)) return 2;
    l.c.modo = Config::Modo::Desinstalar;
    if (l.c.pastaDesinstalar.empty()) l.c.pastaDesinstalar = plat::exe_dir().parent_path();
    return iniciar_processo(l);
}

} // namespace bs::builder::instalador::gui

int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "");
#ifdef BS_INSTALADOR_DESINSTALAR
    return bs::builder::instalador::gui::principal_desinstalador(argc, argv);
#else
    return bs::builder::instalador::gui::principal_instalador(argc, argv);
#endif
}
