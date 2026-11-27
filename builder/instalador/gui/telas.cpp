#include "instalador/gui/telas.h"

#include "boot/disc_boot.h"
#include "boot_identity.h"
#include "builder_platform.h"
#include "compilador_portatil.h"
#include "toolchain.h"
#include "disc/archive.h"
#include "child_process.h"
#include "instalador/gui/arte.h"
#include "instalador/gui/tema.h"
#include "instalador/steam.h"
#include "platform/install_config.h"
#include "ui/i18n.h"
#include "ui/step_keys.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <deque>
#include <fstream>

namespace fs = std::filesystem;

namespace bs::builder::instalador::gui {

using native::edition::Edition;
using ui::tr;

const char* nome_tela(Tela t) {
    switch (t) {
    case Tela::BoasVindas: return "boas-vindas";
    case Tela::Edicao: return "edicao";
    case Tela::Cdi: return "cdi";
    case Tela::Componentes: return "componentes";
    case Tela::Pasta: return "pasta";
    case Tela::Progresso: return "progresso";
    case Tela::Conclusao: return "conclusao";
    case Tela::Verificar: return "verificar";
    case Tela::Desinstalar: return "desinstalar";
    case Tela::DesinstalarFim: return "desinstalar-fim";
    }
    return "?";
}

namespace {

std::wstring tamanho(uint64_t b) {
    const double gb = double(b) / (1024.0 * 1024 * 1024);
    wchar_t      s[32];
    if (gb >= 1) std::swprintf(s, 32, L"%.1f", gb);
    else std::swprintf(s, 32, L"%.0f", double(b) / (1024.0 * 1024));
    std::wstring v = s;
    if (std::string(ui::current_language().code) == "pt-BR") std::replace(v.begin(), v.end(), L'.', L',');
    return tr(gb >= 1 ? "size.gb" : "size.mb", {v});
}

std::wstring duracao(double s) {
    const int t = int(s);
    wchar_t   b[32];
    std::swprintf(b, 32, L"%d:%02d", t / 60, t % 60);
    return b;
}

// v0.9.7-4: velocidade de download (KB/s abaixo de 1 MB/s, senao MB/s com uma casa) e tempo restante por extenso
std::wstring velocidade(double bps) {
    wchar_t s[32];
    if (bps < 1024.0 * 1024) {
        std::swprintf(s, 32, L"%.0f", bps / 1024);
        return tr("rate.kb", {s});
    }
    std::swprintf(s, 32, L"%.1f", bps / (1024.0 * 1024));
    std::wstring v = s;
    if (std::string(ui::current_language().code) == "pt-BR") std::replace(v.begin(), v.end(), L'.', L',');
    return tr("rate.mb", {v});
}

std::wstring restante(double s) {
    if (s < 60) return tr("eta.lt1");
    const int min = int(std::ceil(s / 60));
    if (min < 60) return tr("eta.min", {std::to_wstring(min)});
    return tr("eta.hmin", {std::to_wstring(min / 60), std::to_wstring(min % 60)});
}

Estilo est(float tam, Peso p = Peso::Normal, bool quebra = true) {
    Estilo e;
    e.tamanho = tam;
    e.peso    = p;
    e.quebra  = quebra;
    return e;
}

// mod.toml de um pacote local: [mod] id/nome/tipo; o idioma das legendas pelo nome da pasta (legendas-pt-br, legendas-en)
struct Pacote {
    fs::path     dir;
    std::wstring nome;
    std::string  idioma;  // legendas: "pt-BR", "en-US"; vazio: outro tipo
};

std::string idioma_do_pacote(const std::string& id) {
    std::string l = id;
    for (char& ch : l) ch = char(std::tolower((unsigned char)ch));
    if (l.find("legendas") == std::string::npos && l.find("subtitles") == std::string::npos) return {};
    if (l.find("pt-br") != std::string::npos || l.find("pt_br") != std::string::npos) return "pt-BR";
    if (l.size() >= 3 && (l.compare(l.size() - 3, 3, "-en") == 0 || l.find("en-us") != std::string::npos)) return "en-US";
    // 2026-11-24: espanhol, frances, russo, japones e chines (legendas-es, legendas-fr, legendas-ru, legendas-ja, legendas-zh-cn)
    static const std::pair<const char*, const char*> kOutros[] = {{"-es", "es-ES"}, {"-fr", "fr-FR"}, {"-ru", "ru-RU"},
                                                                  {"-ja", "ja-JP"}, {"-zh", "zh-CN"}};
    for (const auto& [suf, lang] : kOutros)
        if (l.find(std::string("legendas") + suf) != std::string::npos) return lang;
    return {};
}

std::vector<Pacote> pacotes_em(const fs::path& raiz) {
    std::vector<Pacote> r;
    std::error_code     ec;
    if (raiz.empty() || !fs::is_directory(raiz, ec)) return r;
    for (const auto& e : fs::directory_iterator(raiz, ec)) {
        if (!e.is_directory(ec) || !fs::is_regular_file(e.path() / "mod.toml", ec)) continue;
        Pacote p;
        p.dir = e.path();
        const std::string id = narrow(e.path().filename().wstring());
        p.nome = e.path().filename().wstring();
        try {
            const auto t = toml::parse_file(path_text(e.path() / "mod.toml"));
            if (auto n = t["mod"]["nome"].value<std::string>()) p.nome = widen(*n);
            else if (auto n2 = t["nome"].value<std::string>()) p.nome = widen(*n2);
        } catch (...) {
        }
        p.idioma = idioma_do_pacote(id);
        r.push_back(std::move(p));
    }
    std::sort(r.begin(), r.end(), [](const Pacote& a, const Pacote& b) { return a.dir < b.dir; });
    return r;
}

fs::path prefs_file() { return pasta_usuario() / "recompiler.ini"; }

} // namespace

struct Instalador::Impl {
    // escolhas
    Edition                   edicao = Edition::Original;
    bool                      edicaoEscolhida = false;
    std::wstring              cdi, pasta;
    bool                      filmes = true, portatil = false, aceitaAlheia = false, logTecnico = false;
    bool                      filmesWidescreen = false;
    // Director's Cut (directors_cut.h): so com o disco japones v1.006; o segundo disco e a confirmacao do download
    int                       cdiVersao = -1;  // game_version_of do disco conferido (-1: desconhecida, ex. dentro de .zip)
    bool                      cdiPacote = false;  // .zip/.7z: so a lista foi lida
    bool                      dc = false, dcConfirmado = false;
    dc::Language              dcIdioma = dc::Language::En;
    std::wstring              ocidental;
    std::string               legendas = "auto";
    Atalhos                   atalhos;
    std::optional<Instalacao> atualizando;
    std::vector<Instalacao>   encontradas;
    std::wstring              pastaPacotes;
    std::vector<Pacote>       pacotes, doKit;
    std::string               idiomaPrefs;
    // CDI
    enum class Cdi { Vazio, Conferindo, Ok, Erro, Falta } cdiEstado = Cdi::Vazio;
    std::wstring              cdiInfo, cdiConferido;
    unsigned                  cdiGeracao = 0;
    std::thread               cdiThread;
    // pasta
    InstallInfo               info;
    native::install::InstallConfig cfgAtual;
    uint64_t                  livre = 0, precisa = 0;
    std::wstring              pastaAvaliada;
    // progresso
    std::mutex                m;
    Progresso                 prog;
    std::deque<std::string>   log;
    double                    inicio = 0, fimEm = 0;
    bool                      confirmarCancelar = false, cancelando = false;
    // v0.9.7-4: o download dos filmes widescreen (o motivo da demora): filme, MB, velocidade e tempo restante
    struct Download {
        bool                                   ativo = false;
        bool                                   compilador = false;  // v0.9.7-5: o compilador portatil, nao os filmes
        size_t                                 i = 0, n = 0;
        std::string                            arquivo;
        uint64_t                               recebidos = 0, tamanho = 0;
        std::deque<std::pair<double, uint64_t>> amostras;  // (instante, bytes da sessao): a velocidade dos ultimos segundos
        double                                 velocidadeFixa = 0;  // so a captura (--captura)
    } dl;
    std::optional<Edition> edicaoSim;  // so as capturas (simular)
    // v0.9.7-5: o Visual Studio existe? (sondado ao abrir; sem ele, a tela da pasta oferece o compilador portatil)
    enum class Msvc { Procurando, Sim, Nao } msvc = Msvc::Procurando;
    bool                      instalarPendente = false;  // "Instalar" durante a sondagem: segue quando ela terminar
    // as callbacks das threads (sondagens) so tocam no instalador se ele ainda existe (revisao Codex 20261120-194725)
    std::shared_ptr<int>      vida = std::make_shared<int>(0);
    std::thread               msvcThread;
    bool                      compiladorPortatil = false;  // a confirmacao do download (ou --compilador-portatil)
    std::atomic<bool>         cancelar{false};
    // resultado
    std::optional<Result>     res;
    std::string               avisoIntegracao;
    std::vector<fs::path>     atalhosFeitos;
    int                       steamFeitos = -1;  // v0.9.7-1: contas da Steam com o jogo (-1 = nao pedido)
    size_t                    pacotesCopiados = 0;
    // verificacao
    Verificacao               ver;
    int                       dadosRc = -2;  // -2 nao rodou; -1 nao iniciou
    double                    verFrac = 0;
    fs::path                  alvo;          // pasta da verificacao ou da desinstalacao
    // desinstalacao
    bool                      desTrabalho = true;
    ResultadoDesinstalar      des;
    bool                      confirmarSair = false;
    float                     rolagem = 0;
};

Instalador::Instalador(Hospedeiro& h, Config c) : h_(h), c_(std::move(c)), s_(std::make_unique<Impl>()) {
    Impl& s = *s_;
    // preferencias do instalador: pasta_usuario()/recompiler.ini (cdi=, out=, idioma=; o mesmo arquivo de antes do N9.1)
    std::wstring cdiPref, outPref;
    {
        std::ifstream f(prefs_file(), std::ios::binary);
        for (std::string l; std::getline(f, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (l.rfind("cdi=", 0) == 0) cdiPref = widen(l.substr(4));
            else if (l.rfind("out=", 0) == 0) outPref = widen(l.substr(4));
            else if (l.rfind("idioma=", 0) == 0) s.idiomaPrefs = l.substr(7);
        }
    }
    ui::set_language(ui::initial_language(c_.idioma.empty() ? s.idiomaPrefs : c_.idioma, c_.idiomasSistema));
    std::error_code ec;
    s.cdi = c_.disc ? c_.disc->wstring() : cdiPref;
    if (!c_.disc && (s.cdi.empty() || !fs::is_regular_file(s.cdi, ec)))  // uma imagem (.gdi, .cue, .cdi) ao lado do instalador
        for (const auto& e : fs::directory_iterator(c_.exeDir, ec)) {
            std::wstring ext = e.path().extension().wstring();
            for (auto& ch : ext) ch = wchar_t(std::towlower(ch));
            if (e.is_regular_file(ec) && (ext == L".cdi" || ext == L".gdi" || ext == L".cue" || ext == L".chd")) {
                s.cdi = e.path().wstring();
                break;
            }
        }
    s.pasta = c_.out ? c_.out->wstring() : !outPref.empty() ? outPref : c_.pastaPadrao.wstring();
    if (c_.portatil) s.portatil = *c_.portatil;
    if (c_.edicao) s.edicao = *c_.edicao, s.edicaoEscolhida = true;
    s.filmes = !c_.semFilmes;
    s.doKit = pacotes_em(c_.kit / "mods-padrao");
    std::string aviso;
    s.encontradas = listar(lista_instalacoes(), &aviso);
    if (c_.modo == Config::Modo::Desinstalar) {
        s.alvo = c_.pastaDesinstalar;
        tela_ = Tela::Desinstalar;
    }
}

Instalador::~Instalador() {
    s_->cancelar = true;
    if (thread_.joinable()) thread_.join();
    if (s_->cdiThread.joinable()) s_->cdiThread.join();
    if (s_->msvcThread.joinable()) s_->msvcThread.join();
}

void Instalador::juntar() {
    if (thread_.joinable() && terminou_) thread_.join();
}

void Instalador::iniciar() {
    h_.titulo(tr(c_.modo == Config::Modo::Desinstalar ? "des.window" : "ins.window"));
    s_->compiladorPortatil = c_.compiladorPortatil;
    if (c_.modo == Config::Modo::Instalar && !c_.automatico && !c_.linux_) {
        // o vswhere e o vcvars levam alguns segundos: fora da tela, o resultado vem pela fila
        s_->msvcThread = std::thread([this, vida = std::weak_ptr<int>(s_->vida)] {
            bool tem = false;
            // so testes (test_instalador I9): a sondagem demora, para o clique em "Instalar" durante ela ser exercitado
            if (const char* v = std::getenv("BS_INSTALADOR_TESTE_SONDA_MS"))
                std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(std::atoi(v), 0, 60000)));
            try {
                ToolchainOptions o;
                o.vswhere = c_.vswhere;
                find_toolchain(o, nullptr);
                tem = true;
            } catch (const std::exception&) {
            }
            h_.postar([this, tem, vida] {
                if (vida.expired()) return;
                s_->msvc = tem ? Impl::Msvc::Sim : Impl::Msvc::Nao;
                if (s_->instalarPendente) {  // o "Instalar" ja foi apertado: com o Visual Studio segue; sem, a tela mostra a opcao
                    s_->instalarPendente = false;
                    if (tem && tela_ == Tela::Pasta) instalar();
                    else if (!tem) ui_.focar(id("tc.confirm"));  // a caixa que falta marcar
                }
                h_.redesenhar();
            });
        });
    } else {
        s_->msvc = Impl::Msvc::Sim;  // --auto: a linha de comando decide (--compilador-portatil); Linux: o do pacote
    }
    if (!c_.automatico) return;
    if (c_.modo == Config::Modo::Desinstalar) {
        comecar_desinstalacao();
        return;
    }
    // --auto: sem telas, direto ao progresso (as escolhas da linha de comando; o resto, a instalacao anterior)
    if (s_->cdi.empty() || s_->pasta.empty()) {
        codigo_ = 2;
        h_.fechar(2);
        return;
    }
    instalar();
}

void Instalador::ir(Tela t) {
    tela_ = t;
    entrouEm_ = ui_.agora();
    s_->rolagem = 0;
    s_->confirmarCancelar = false;
    s_->instalarPendente  = false;
    ui_.focar(id("nav.next"));
    if (t == Tela::Cdi) conferir_cdi();
    if (t == Tela::Pasta) avaliar_pasta();
    if (t == Tela::Verificar) comecar_verificacao();
    h_.redesenhar();
}

bool Instalador::pedir_fechar() {
    if (!trabalhando()) return true;
    if (c_.automatico) {
        s_->cancelar = true;
        return false;
    }
    s_->confirmarSair = true;
    h_.redesenhar();
    return false;
}

void Instalador::salvar_prefs() {
    if (!c_.idioma.empty()) return;  // --idioma vale so para esta execucao (como antes do N9.1)
    std::error_code ec;
    fs::create_directories(prefs_file().parent_path(), ec);
    std::ofstream o(prefs_file(), std::ios::binary | std::ios::trunc);
    o << "# Blue Stinger Recompiler\ncdi=" << narrow(s_->cdi) << "\nout=" << narrow(s_->pasta)
      << "\nidioma=" << ui::current_language().code << "\n";
}

// ---------------------------------------------------------------------------------------------------------------------
// acoes

void Instalador::conferir_cdi() {
    Impl& s = *s_;
    if (s.cdi == s.cdiConferido && s.cdiEstado != Impl::Cdi::Vazio) return;
    s.cdiConferido = s.cdi;
    std::error_code ec;
    if (s.cdi.empty()) {
        s.cdiEstado = Impl::Cdi::Vazio;
        return;
    }
    if (!fs::is_regular_file(fs::path(s.cdi), ec)) {
        s.cdiEstado = Impl::Cdi::Falta;
        return;
    }
    s.cdiEstado = Impl::Cdi::Conferindo;
    const unsigned g = ++s.cdiGeracao;
    if (s.cdiThread.joinable()) s.cdiThread.join();
    const fs::path arq = s.cdi;
    s.cdiThread = std::thread([this, g, arq, vida = std::weak_ptr<int>(s.vida)] {
        std::wstring info;
        bool         ok = false;
        int          versao = -1;
        try {
            if (disc::archive_supported(arq)) {
                // .zip/.7z (I8): so a lista aqui (extrair ~1 GB so para conferir seria lento); a instalacao extrai e
                // confere o executavel como em qualquer imagem
                const std::string n = disc::archive_image_name(disc::archive_list(arq));
                info = widen("pacote com " + n + " (conferido na instalacao)");
            } else {
                const auto b = boot_read_disc(arq);
                info   = widen(b.product + " " + b.version + " (" + b.versionId + ")");
                versao = game_version_of(b.versionId);
            }
            ok = true;
        } catch (const std::exception& e) {
            info = widen(e.what());
        }
        const bool pacote = disc::archive_supported(arq);
        h_.postar([this, g, ok, info, versao, pacote, vida] {
            if (vida.expired() || g != s_->cdiGeracao) return;
            s_->cdiEstado = ok ? Impl::Cdi::Ok : Impl::Cdi::Erro;
            s_->cdiInfo   = info;
            s_->cdiVersao = versao;
            s_->cdiPacote = pacote;
            if (versao != 3 && !(versao < 0 && pacote)) s_->dc = false;  // a opcao so existe para o japones
            h_.redesenhar();
        });
    });
}

void Instalador::avaliar_pasta() {
    Impl& s = *s_;
    s.pastaAvaliada = s.pasta;
    s.info = {};
    s.livre = s.precisa = 0;
    if (s.pasta.empty()) return;
    std::error_code ec;
    s.info    = inspect_install(fs::path(s.pasta));
    s.cfgAtual = native::install::read_config(fs::path(s.pasta) / kGameDir);
    s.livre   = espaco_livre(s.pasta);
    s.precisa = s.info.ours ? espaco_necessario(s.filmes) / 3 : espaco_necessario(s.filmes);
}

void Instalador::instalar() {
    Impl& s = *s_;
    Request req;
    req.disc   = fs::path(s.cdi);
    req.outDir = fs::path(s.pasta);
    req.kitDir = c_.kit;
    req.sdl3Dir = c_.sdl3;
    req.movies = s.filmes;
    req.vswhere = c_.vswhere;
    // v0.9.7-5: o compilador portatil so com a confirmacao (a caixa da tela da pasta, sem o Visual Studio, ou a linha de comando)
    req.portableCompiler = c_.compiladorPortatil || (s.compiladorPortatil && s.msvc == Impl::Msvc::Nao);
    if (c_.automatico) {
        // as escolhas da linha de comando; sem elas a instalacao anterior fica (contrato do N9.0)
        if (c_.portatil) req.portableSaves = *c_.portatil;
        req.edition = c_.edicao;
        req.widescreenMovies = c_.filmesWidescreen;
        req.directorsCut     = c_.directorsCut;
        if (c_.discoOcidental) req.westernDisc = *c_.discoOcidental;
        req.directorsCutConfirmed = c_.confirmarDownload;
    } else {
        req.portableSaves = s.portatil;
        req.edition       = s.edicao;
        req.subtitles     = s.legendas;
        req.widescreenMovies = s.filmesWidescreen && s.edicao == Edition::Remastered;  // v0.9.7-2
        if (s.dc && (s.cdiVersao == 3 || s.cdiPacote) && dc::language_supported(s.dcIdioma)) {  // Director's Cut: confirmado na tela do disco
            req.directorsCut          = s.dcIdioma;
            req.westernDisc           = fs::path(s.ocidental);
            req.directorsCutConfirmed = s.dcConfirmado;
        }
    }
    std::error_code ec;
    if (!c_.desinstalador.empty() && fs::is_regular_file(c_.desinstalador, ec))
        req.extraPackageFiles.push_back({c_.desinstalador, c_.desinstalador.filename().wstring()});
    if (!c_.automatico) salvar_prefs();
    s.cancelar = false;
    s.cancelando = false;
    s.res.reset();
    s.prog = {};
    s.dl   = {};
    {
        std::lock_guard l(s.m);
        s.log.clear();
    }
    s.inicio = ui_.agora();
    ir(Tela::Progresso);
    if (thread_.joinable()) thread_.join();
    terminou_ = false;
    thread_ = std::thread([this, req] {
        Callbacks cb;
        cb.cancel   = &s_->cancelar;
        cb.progress = [this](Step st, double f, const std::string&) {
            if (c_.cancelarNaFase && fase_de(st) == *c_.cancelarNaFase) s_->cancelar = true;  // testes: cancelar em cada fase
            h_.postar([this, st, f] {
                s_->prog.avancar(st, f);
                h_.redesenhar();
            });
        };
        // no maximo 4 por segundo (a cada 64 KB numa rede rapida seriam milhares), mais a troca de filme e o fim
        cb.download = [this, ultimo = std::chrono::steady_clock::time_point{}, ultimoFilme = size_t(0)](
                          const DownloadProgress& d) mutable {
            const auto agora = std::chrono::steady_clock::now();
            if (!d.done && d.index == ultimoFilme && agora - ultimo < std::chrono::milliseconds(250)) return;
            ultimo = agora, ultimoFilme = d.index;
            h_.postar([this, d] {
                auto& dl = s_->dl;
                if (d.done) {
                    dl = {};
                } else {
                    dl.ativo = true, dl.i = d.index, dl.n = d.count, dl.arquivo = d.file;
                    dl.compilador = d.kind == DownloadProgress::Kind::PortableCompiler;
                    dl.recebidos = d.received, dl.tamanho = d.size;
                    const double t = ui_.agora();
                    dl.amostras.emplace_back(t, d.session);
                    while (dl.amostras.size() > 2 && t - dl.amostras.front().first > 20) dl.amostras.pop_front();
                }
                h_.redesenhar();
            });
        };
        cb.log = [this](const std::string& l) {
            std::lock_guard g(s_->m);
            s_->log.push_back(l);
            while (s_->log.size() > 400) s_->log.pop_front();
        };
        Result r = build(req, cb);
        h_.postar([this, r] { ao_terminar(r); });
    });
}

void Instalador::ao_terminar(Result r) {
    Impl& s = *s_;
    terminou_ = true;
    juntar();
    s.fimEm = ui_.agora();
    s.dl    = {};
    if (r.ok) {
        s.prog.concluir();
        // pacotes locais escolhidos: viram arquivos do jogador em mods/<id> (preservados e nunca apagados pelo instalador)
        s.pacotesCopiados = 0;
        for (const auto& p : s.pacotes) {
            std::error_code ec;
            const fs::path  to = r.installDir / "mods" / p.dir.filename();
            fs::create_directories(to, ec);
            fs::copy(p.dir, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (fs::is_directory(p.dir / "video", ec))
                fs::copy(p.dir / "video", r.installDir / "mods" / "video",
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (!ec) ++s.pacotesCopiados;
        }
        // integracao e lista: so na janela, ou no --auto com --integrar (os testes desviam a raiz)
        if (!c_.automatico || c_.integrar) {
            const fs::path lista = lista_instalacoes();
            const auto     antes = atalhos_registrados(lista, s.pasta);
            const Instalacao inst = ler_instalacao(s.pasta);
            DadosIntegracao  d;
            d.pasta  = inst.pasta;
            d.exe    = r.exe;
            d.edicao = inst.edicao;
            d.versao = inst.versao;
            if (!c_.desinstalador.empty()) d.desinstalador = r.installDir / c_.desinstalador.filename();
            d.icone = c_.linux_ ? h_.icone_atalho() : r.exe;
            std::error_code ec;
            uint64_t        kb = 0;
            for (auto it = fs::recursive_directory_iterator(r.installDir, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
                if (it->is_regular_file(ec)) kb += it->file_size(ec) / 1024;
            d.tamanhoKB = kb;
            s.avisoIntegracao.clear();
            Atalhos pedidos = c_.automatico ? Atalhos{} : s.atalhos;
            if (c_.automatico) pedidos.steam = c_.steam;
            s.atalhosFeitos = integrar(d, pedidos, antes, &s.avisoIntegracao);
            // v0.9.7-1: a Steam (atalho de jogo nao-Steam), so se marcado
            s.steamFeitos = -1;
            if (pedidos.steam) {
                EntradaSteam e;
                const std::u8string nome = fs::path(nome_exibicao(inst.edicao)).u8string();
                e.nome = std::string(nome.begin(), nome.end()), e.exe = r.exe, e.pasta = r.installDir, e.icone = d.icone;
                s.steamFeitos = adicionar_steam(e, &s.avisoIntegracao);
            }
            std::string err;
            if (!registrar(lista, inst.pasta, s.atalhosFeitos, &err)) s.avisoIntegracao += err;
            s.encontradas = listar(lista);
        }
    }
    codigo_ = r.ok ? 0 : r.cancelled ? 3 : 1;
    s.res = std::move(r);
    if (c_.automatico) {
        h_.fechar(codigo_);
        return;
    }
    if (s.confirmarSair) {
        h_.fechar(codigo_);
        return;
    }
    ir(Tela::Conclusao);
    ui_.focar(id(s.res->ok ? "fim.play" : "nav.next"));
}

void Instalador::comecar_verificacao() {
    Impl& s = *s_;
    s.ver = {};
    s.dadosRc = -2;
    s.verFrac = 0;
    s.cancelar = false;
    if (thread_.joinable()) thread_.join();
    terminou_ = false;
    const fs::path jogo = s.alvo / kGameDir;
    thread_ = std::thread([this, jogo] {
        Verificacao v = verificar_pacote(jogo, [this](double f) {
            h_.postar([this, f] {
                s_->verFrac = f * 0.5;
                h_.redesenhar();
            });
        }, &s_->cancelar);
        int rc = -1;
        if (!v.cancelado) {
            const fs::path exe = jogo / plat::game_binary_installed();
            const auto     pr  = run_process_argv({path_wide(exe), L"--verify-data"}, jogo, {}, [](const std::string&) {},
                                                  &s_->cancelar, 600000);
            rc = pr.started ? pr.exitCode : -1;
        }
        h_.postar([this, v, rc] {
            terminou_ = true;
            juntar();
            s_->ver = v;
            s_->dadosRc = rc;
            s_->verFrac = 1;
            h_.redesenhar();
        });
    });
}

void Instalador::comecar_desinstalacao() {
    Impl& s = *s_;
    if (thread_.joinable()) thread_.join();
    terminou_ = false;
    const fs::path pasta = s.alvo;
    const bool     trab  = s.desTrabalho;
    thread_ = std::thread([this, pasta, trab] {
        OpcoesDesinstalar op;
        op.trabalho = trab;
        const fs::path lista   = lista_instalacoes();
        const auto     atalhos = atalhos_registrados(lista, pasta);
        ResultadoDesinstalar r = instalador::desinstalar(pasta, op, trab ? cache_dirs(pasta) : std::vector<fs::path>{});
        if (r.ok) {
            desintegrar(pasta, atalhos);
            std::string ignorado;
            remover_steam(pasta, &ignorado);  // v0.9.7-1: as entradas da Steam que apontam para a pasta (se houver)
            esquecer(lista, pasta);
            if (!r.pendentes.empty()) {  // o proprio desinstalador: sai depois do processo, com as pastas que esvaziarem
                std::vector<fs::path> pastas;
                for (const auto& f : r.pendentes)
                    for (fs::path d = f.parent_path(); !d.empty() && d != pasta && d.native().size() > pasta.native().size();
                         d = d.parent_path())
                        if (std::find(pastas.begin(), pastas.end(), d) == pastas.end()) pastas.push_back(d);
                std::sort(pastas.begin(), pastas.end(), [](const fs::path& a, const fs::path& b) { return a.native().size() > b.native().size(); });
                pastas.push_back(pasta);
                h_.apagar_ao_sair(r.pendentes, pastas);
            }
        }
        h_.postar([this, r] {
            terminou_ = true;
            juntar();
            s_->des = r;
            codigo_ = r.ok ? 0 : 1;
            std::string aviso;
            s_->encontradas = listar(lista_instalacoes(), &aviso);
            if (c_.automatico) {
                h_.fechar(codigo_);
                return;
            }
            ir(Tela::DesinstalarFim);
        });
    });
}

// ---------------------------------------------------------------------------------------------------------------------
// desenho

void Instalador::quadro(Pintor& p, const Entrada& e, double agora, bool menosAnimacao, float largura, float altura) {
    ui_.comecar(p, e, agora, menosAnimacao);
    ui_.contexto = std::string(nome_tela(tela_)) + "/" + ui::current_language().code;
    if (entrouEm_ == 0) entrouEm_ = agora;
    if (s_->inicio < 0) s_->inicio += agora;
    p.limpar(tema::kFundo);
    const Ret lat{0, 0, tema::kLateralLargura, altura};
    lateral(lat);
    const Ret conteudo{tema::kLateralLargura + tema::kMargem, tema::kMargem, largura - tema::kLateralLargura - 2 * tema::kMargem,
                       altura - 2 * tema::kMargem};
    // transicao discreta: o conteudo aparece subindo 10 DIP
    const float ent = menosAnimacao ? 1.f : float(std::clamp((agora - entrouEm_) / tema::kTransicao, 0.0, 1.0));
    Ret r = conteudo;
    r.y += (1 - ent) * 10;
    desenhar_tela(r);
    if (ent < 1) {
        p.retangulo({lat.dir(), 0, largura - lat.w, altura}, tema::kFundo.com_alfa(1 - ent));
        ui_.suave(id("anim"), 0.f, 0);  // mantem os quadros vindo
    }
    // confirmacao de sair durante a instalacao (por cima)
    if (s_->confirmarSair) {
        p.retangulo({0, 0, largura, altura}, rgb(0x000000, 0.55f));
        const Ret caixa{largura / 2 - 230, altura / 2 - 90, 460, 180};
        p.arredondado(caixa, 12, tema::kFundoAlto);
        p.contorno(caixa, 12, 1, tema::kBorda);
        ui_.rotulo(tr("msg.close_running"), est(tema::kSubtitulo, Peso::SemiNegrito), caixa.recuo(24, 22), tema::kTexto);
        const Ret b1{caixa.dir() - 24 - 170, caixa.base() - 24 - tema::kBotaoAltura, 170, tema::kBotaoAltura};
        const Ret b2{b1.x - 12 - 150, b1.y, 150, tema::kBotaoAltura};
        if (ui_.botao(id("sair.sim"), tr("exit.yes"), b1, TipoBotao::Perigo)) s_->cancelar = true, s_->cancelando = true;
        if (ui_.botao(id("sair.nao"), tr("exit.no"), b2, TipoBotao::Secundario, true, true) || ui_.escape())
            s_->confirmarSair = false;
    }
    ui_.terminar();
}

void Instalador::lateral(const Ret& r) {
    Pintor& p = ui_.pintor();
    arte::fundo_lateral(p, r);
    arte::logotipo(p, r.x + 24, r.y + 28, r.w - 40);
    const bool manut = tela_ == Tela::Verificar || tela_ == Tela::Desinstalar || tela_ == Tela::DesinstalarFim;
    float      y = r.y + 120;
    if (manut) {
        ui_.rotulo(tr("side.maint"), est(tema::kCorpo, Peso::SemiNegrito), {r.x + 28, y, r.w - 48, 22}, tema::kAmbar);
    } else {
        const char* passos[] = {"side.welcome", "side.edition", "side.disc", "side.components", "side.folder", "side.progress", "side.done"};
        const int   atual = std::min(int(tela_), 6);
        for (int i = 0; i < 7; ++i) {
            const bool feito = i < atual, este = i == atual;
            const Cor  c = este ? tema::kTexto : feito ? tema::kTextoSuave : tema::kTextoApagado;
            const Ret  marca{r.x + 26, y + 4, 14, 14};
            if (este) {
                p.arredondado({r.x + 12, y - 5, r.w - 24, 32}, 8, tema::kCartao);
                p.arredondado(marca, 7, tema::kAmbar);
            } else if (feito) {
                p.arredondado(marca, 7, tema::kAgua.com_alfa(0.25f));
                arte::simbolo_ok(p, marca.x + 1, marca.y + 1, 12, tema::kAgua);
            } else {
                p.contorno(marca, 7, 1.25f, tema::kBorda);
            }
            ui_.rotulo(tr(passos[i]), est(tema::kCorpo, este ? Peso::SemiNegrito : Peso::Normal, false), {r.x + 50, y + 1, r.w - 66, 22}, c);
            y += 38;
        }
    }
    const float fh = ui_.altura_texto(tr("side.footer"), est(tema::kPequeno - 1), r.w - 40);  // de baixo para cima
    ui_.rotulo(tr("side.footer"), est(tema::kPequeno - 1), {r.x + 24, r.base() - 18 - fh, r.w - 40, fh}, tema::kTextoApagado);
}

float Instalador::titulo(const Ret& r, const std::wstring& t, const std::wstring& sub) {
    const Medida a = ui_.rotulo(t, est(tema::kTitulo, Peso::SemiNegrito), {r.x, r.y, r.w, 36}, tema::kTexto);
    float        y = r.y + std::max(36.f, a.h) + 6;
    if (!sub.empty()) {
        const float h = ui_.altura_texto(sub, est(tema::kCorpo), r.w);
        ui_.rotulo(sub, est(tema::kCorpo), {r.x, y, r.w, h}, tema::kTextoSuave);
        y += h;
    }
    return y + 22 - r.y;
}

void Instalador::desenhar_tela(const Ret& r) {
    switch (tela_) {
    case Tela::BoasVindas: boas_vindas(r); break;
    case Tela::Edicao: edicao(r); break;
    case Tela::Cdi: cdi(r); break;
    case Tela::Componentes: componentes(r); break;
    case Tela::Pasta: pasta(r); break;
    case Tela::Progresso: progresso(r); break;
    case Tela::Conclusao: conclusao(r); break;
    case Tela::Verificar: verificar(r); break;
    case Tela::Desinstalar: desinstalar(r); break;
    case Tela::DesinstalarFim: desinstalar_fim(r); break;
    }
}

void Instalador::boas_vindas(const Ret& r) {
    Impl& s = *s_;
    const float y0 = titulo(r, tr("wel.title"), tr("wel.sub", {c_.linux_ ? L"Linux" : L"Windows"}));
    float       y = r.y + y0;
    // idioma do instalador
    ui_.rotulo(tr("wel.lang"), est(tema::kPequeno, Peso::SemiNegrito), {r.x, y, r.w, 18}, tema::kTextoSuave);
    y += 22;
    {
        float x = r.x;
        int   k = 0;
        for (const auto* l : ui::languages()) {
            const float w = 190;
            const bool  este = l == &ui::current_language();
            if (ui_.opcao(id("idioma", k), 1, l->name, {}, {x, y, w, 24}, este) && !este) {
                ui::set_language(l->code);
                h_.titulo(tr("ins.window"));
            }
            x += w + 16;
            ++k;
        }
    }
    y += 44;
    ui_.rotulo(tr("wel.found"), est(tema::kSubtitulo, Peso::SemiNegrito), {r.x, y, r.w, 24}, tema::kTexto);
    y += 32;
    const float rodapeY = r.base() - tema::kBotaoAltura;
    if (s.encontradas.empty()) {
        ui_.rotulo(tr("wel.none"), est(tema::kCorpo), {r.x, y, r.w, 22}, tema::kTextoSuave);
        y += 32;
    }
    for (size_t i = 0; i < s.encontradas.size() && i < 3; ++i) {
        const Instalacao& in = s.encontradas[i];
        const Ret         linha{r.x, y, r.w, 76};
        ui_.pintor().arredondado(linha, 10, tema::kCartao);
        ui_.pintor().retangulo({linha.x, linha.y + 14, 3, linha.h - 28}, in.edicao == Edition::Remastered ? tema::kAgua : tema::kAmbar);
        const float bw = 104, bx = linha.dir() - 3 * (bw + 8) - 6, tw = bx - linha.x - 28;
        const Estilo nome = est(tema::kCorpo, Peso::SemiNegrito, false), peq = est(tema::kPequeno, Peso::Normal, false);
        ui_.rotulo(nome_exibicao(in.edicao), nome, {linha.x + 18, linha.y + 9, tw, 20}, tema::kTexto);
        ui_.rotulo(ui_.elidir(widen(in.versao), peq, tw), peq, {linha.x + 18, linha.y + 31, tw, 18}, tema::kTextoSuave);
        ui_.rotulo(ui_.elidir(in.pasta.wstring(), peq, tw), peq, {linha.x + 18, linha.y + 50, tw, 18}, tema::kTextoApagado);
        const Ret b{bx, linha.y + (linha.h - tema::kBotaoAltura) / 2, bw, tema::kBotaoAltura};
        if (ui_.botao(id("wel.update", int(i)), tr("wel.update"), b)) {
            s.atualizando = in;
            s.pasta = in.pasta.wstring();
            s.edicao = in.edicao, s.edicaoEscolhida = true;
            s.portatil = in.portatil;
            ir(Tela::Edicao);
        }
        if (ui_.botao(id("wel.verify", int(i)), tr("wel.verify"), {b.x + bw + 8, b.y, bw, b.h})) {
            s.alvo = in.pasta;
            ir(Tela::Verificar);
        }
        if (ui_.botao(id("wel.uninstall", int(i)), tr("wel.uninstall"), {b.x + 2 * (bw + 8), b.y, bw, b.h})) {
            s.alvo = in.pasta;
            ir(Tela::Desinstalar);
        }
        y += 84;
    }
    const float nh = ui_.altura_texto(tr("wel.note"), est(tema::kPequeno), r.w);
    ui_.rotulo(tr("wel.note"), est(tema::kPequeno), {r.x, rodapeY - nh - 18, r.w, nh}, tema::kTextoApagado);
    if (ui_.botao(id("nav.next"), tr("wel.new"), {r.dir() - 200, rodapeY, 200, tema::kBotaoAltura}, TipoBotao::Primario, true, true)) {
        s.atualizando.reset();
        if (!c_.edicao) s.edicaoEscolhida = false;
        ir(Tela::Edicao);
    }
}

void Instalador::edicao(const Ret& r) {
    Impl& s = *s_;
    const float y0 = titulo(r, tr("ed.title"), tr("ed.sub"));
    const float y = r.y + y0, rodapeY = r.base() - tema::kBotaoAltura;
    const float gap = 20, w = (r.w - gap) / 2, h = rodapeY - 20 - y;
    const Edition eds[] = {Edition::Original, Edition::Remastered};
    for (int k = 0; k < 2; ++k) {
        const Edition ed = eds[k];
        const Ret     c{r.x + k * (w + gap), y, w, h};
        const Cor     destaque = ed == Edition::Original ? tema::kAmbar : tema::kAgua;
        const bool    marcado = s.edicao == ed;
        if (ui_.cartao(id("ed", k), 2, c, marcado, destaque, [&](const Ret& cr) {
                Pintor&   p = ui_.pintor();
                const Ret img{cr.x + 16, cr.y + 16, cr.w - 32, std::min(cr.h * 0.45f, (cr.w - 32) * 0.56f)};
                if (ed == Edition::Original) arte::ilustracao_original(p, img);
                else arte::ilustracao_remastered(p, img);
                float yy = img.base() + 16;
                ui_.rotulo(tr(ed == Edition::Original ? "ed.original" : "ed.remastered"), est(tema::kSubtitulo, Peso::SemiNegrito, false),
                           {cr.x + 20, yy, cr.w - 40, 24}, destaque);
                yy += 30;
                if (s.atualizando && s.atualizando->edicao == ed) {
                    ui_.rotulo(tr("ed.current"), est(tema::kPequeno, Peso::SemiNegrito, false), {cr.x + 20, yy, cr.w - 40, 18}, tema::kTextoSuave);
                    yy += 22;
                }
                const std::wstring d = tr(ed == Edition::Original ? "ed.original.desc" : "ed.remastered.desc");
                ui_.rotulo(d, est(tema::kCorpo), {cr.x + 20, yy, cr.w - 40, cr.base() - 16 - yy}, tema::kTextoSuave);
            }))
            s.edicao = ed, s.edicaoEscolhida = true;
    }
    if (ui_.botao(id("nav.back"), tr("nav.back"), {r.x, rodapeY, 120, tema::kBotaoAltura}) || ui_.escape()) ir(Tela::BoasVindas);
    if (ui_.botao(id("nav.next"), tr("nav.next"), {r.dir() - 160, rodapeY, 160, tema::kBotaoAltura}, TipoBotao::Primario, true, true)) {
        if (!s.edicaoEscolhida) s.edicaoEscolhida = true;
        if (s.edicao == Edition::Remastered && s.legendas == "auto" && !s.atualizando) s.legendas = "pt-BR";  // N9 3.2
        ir(Tela::Cdi);
    }
}

void Instalador::cdi(const Ret& r) {
    Impl& s = *s_;
    const float y0 = titulo(r, tr("cdi.title"), tr("cdi.sub"));
    float       y = r.y + y0;
    const float rodapeY = r.base() - tema::kBotaoAltura, bw = 130;
    if (ui_.campo(id("cdi.campo"), s.cdi, {r.x, y, r.w - bw - 12, 40}, tr("cdi.empty"))) {
        s.cdiConferido.clear();
        conferir_cdi();
    }
    if (ui_.botao(id("cdi.browse"), tr("browse"), {r.dir() - bw, y + 1, bw, tema::kBotaoAltura})) {
        h_.escolher_arquivo(tr("dlg.disc_title"), fs::path(s.cdi), [this](fs::path f) {
            if (f.empty()) return;
            s_->cdi = f.wstring();
            s_->cdiConferido.clear();
            conferir_cdi();
            h_.redesenhar();
        });
    }
    y += 56;
    const Ret st{r.x, y, r.w, 60};
    Pintor&   p = ui_.pintor();
    switch (s.cdiEstado) {
    case Impl::Cdi::Vazio: break;
    case Impl::Cdi::Conferindo:
        ui_.barra({r.x, y + 8, 160, 6}, 0.35, tema::kAmbar, true);
        ui_.rotulo(tr("cdi.checking"), est(tema::kCorpo), {r.x + 176, y, r.w - 176, 22}, tema::kTextoSuave);
        break;
    case Impl::Cdi::Ok:
        arte::simbolo_ok(p, st.x, st.y, 20, tema::kAgua);
        ui_.rotulo(tr("cdi.ok", {s.cdiInfo}), est(tema::kCorpo), {r.x + 30, y, r.w - 30, 40}, tema::kTexto);
        break;
    case Impl::Cdi::Falta:
        arte::simbolo_erro(p, st.x, st.y, 20, tema::kCoral);
        ui_.rotulo(tr("cdi.missing"), est(tema::kCorpo), {r.x + 30, y, r.w - 30, 22}, tema::kTexto);
        break;
    case Impl::Cdi::Erro:
        arte::simbolo_erro(p, st.x, st.y, 20, tema::kCoral);
        ui_.rotulo(tr("cdi.invalid"), est(tema::kCorpo), {r.x + 30, y, r.w - 30, 44}, tema::kTexto);
    {   // o motivo pode ter varias linhas (o tamanho e o SHA-256 do executavel, para o jogador relatar: issue #1)
        const float dh = ui_.altura_texto(s.cdiInfo, est(tema::kPequeno), r.w - 30);
        ui_.rotulo(s.cdiInfo, est(tema::kPequeno), {r.x + 30, y + 46, r.w - 30, std::max(36.0f, dh)}, tema::kTextoSuave);
    }
        break;
    }
    // outras versoes (docs/DIRECTORS-CUT-ANALISE.md): a Remastered so no americano por ora; o Director's Cut so no japones
    bool podeSeguir = s.cdiEstado == Impl::Cdi::Ok;
    if (s.cdiEstado == Impl::Cdi::Ok) {
        float yv = y + 48;
        auto  nota = [&](const std::wstring& t, Cor cor) {
            const float h = ui_.altura_texto(t, est(tema::kPequeno), r.w);
            ui_.rotulo(t, est(tema::kPequeno), {r.x, yv, r.w, h}, cor);
            yv += h + 8;
        };
        if (s.cdiVersao > 0 && s.edicao == Edition::Remastered) {
            nota(tr("cdi.remaster_na"), tema::kCoral);
            podeSeguir = false;
        }
        if (s.cdiVersao == 4 || s.cdiVersao == 5) nota(tr("dc.already"), tema::kTextoSuave);
        // dentro de .zip/.7z a versao so e conhecida na instalacao (extrair ~1 GB so para conferir seria lento): a opcao
        // aparece e o Builder recusa se o disco nao for o japones v1.006 (revisao Codex 2026-11-19)
        if (s.cdiVersao == 3 || (s.cdiVersao < 0 && s.cdiPacote)) {
            const std::wstring t = tr("dc.check"), d = tr(s.cdiVersao == 3 ? "dc.check.hint" : "dc.check.hint.pkg");
            const float        h = ui_.altura_texto(t, est(tema::kCorpo), r.w - 30) + 2 + ui_.altura_texto(d, est(tema::kPequeno), r.w - 30) + 10;
            ui_.caixa(id("dc.check"), t, d, {r.x, yv, r.w, h}, s.dc);
            yv += h + 6;
            if (s.dc) {
                const float meia = (r.w - 16) / 2;
                if (ui_.opcao(id("dc.lang", 0), 2, tr("dc.lang.en"), {}, {r.x, yv, meia, 26}, s.dcIdioma == dc::Language::En))
                    s.dcIdioma = dc::Language::En;
                // o frances ainda nao: o resultado do patch com o disco PAL frances nao foi conferido (revisao Codex 2026-11-19)
                const bool fr = dc::language_supported(dc::Language::Fr);
                if (ui_.opcao(id("dc.lang", 1), 2, tr(fr ? "dc.lang.fr" : "dc.lang.fr.na"), {}, {r.x + meia + 16, yv, meia, 26},
                              s.dcIdioma == dc::Language::Fr, fr))
                    s.dcIdioma = dc::Language::Fr;
                yv += 34;
                ui_.campo(id("dc.western"), s.ocidental, {r.x, yv, r.w - bw - 12, 36},
                          tr(s.dcIdioma == dc::Language::En ? "dc.western.en" : "dc.western.fr"));
                if (ui_.botao(id("dc.browse"), tr("browse"), {r.dir() - bw, yv, bw, tema::kBotaoAltura})) {
                    h_.escolher_arquivo(tr("dlg.disc_title"), fs::path(s.ocidental), [this](fs::path f) {
                        if (f.empty()) return;
                        s_->ocidental = f.wstring();
                        h_.redesenhar();
                    });
                }
                yv += 44;
                // o que sera baixado e a confirmacao explicita (sem ela, nao segue)
                const std::wstring lista =
                    L"• " +
                    tr("dc.patcher", {widen(dc::kRepository), widen(std::string(dc::kCommit).substr(0, 7)),
                                      std::to_wstring(std::size(dc::kPatcherFiles))}) +
                    L"\n• " + tr("dc.python");
                nota(tr("dc.download") + L"\n" + lista, tema::kTextoSuave);
                ui_.caixa(id("dc.confirm"), tr("dc.confirm"), {}, {r.x, yv, r.w, 26}, s.dcConfirmado);
                yv += 32;
                std::error_code ec;
                if (!s.dcConfirmado || s.ocidental.empty() || !fs::is_regular_file(fs::path(s.ocidental), ec)) podeSeguir = false;
            }
        }
    }
    const float nh = ui_.altura_texto(tr("cdi.privacy"), est(tema::kPequeno), r.w);
    ui_.rotulo(tr("cdi.privacy"), est(tema::kPequeno), {r.x, rodapeY - nh - 18, r.w, nh}, tema::kTextoApagado);
    if (ui_.botao(id("nav.back"), tr("nav.back"), {r.x, rodapeY, 120, tema::kBotaoAltura}) || ui_.escape()) ir(Tela::Edicao);
    if (ui_.botao(id("nav.next"), tr("nav.next"), {r.dir() - 160, rodapeY, 160, tema::kBotaoAltura}, TipoBotao::Primario,
                  podeSeguir, true))
        ir(Tela::Componentes);
}

void Instalador::componentes(const Ret& r) {
    Impl& s = *s_;
    const float y0 = titulo(r, tr("comp.title"), {});
    float       y = r.y + y0 - 8;
    const float rodapeY = r.base() - tema::kBotaoAltura;
    const float col = (r.w - 28) / 2;
    // coluna 1: legendas
    const float hs = ui_.altura_texto(tr("comp.subs"), est(tema::kCorpo, Peso::SemiNegrito), col);
    ui_.rotulo(tr("comp.subs"), est(tema::kCorpo, Peso::SemiNegrito), {r.x, y, col, hs}, tema::kTexto);
    float y1 = y + hs + 10;
    auto  tem = [&](const char* lang) {
        for (const auto& v : {s.doKit, s.pacotes})
            for (const auto& pk : v)
                if (pk.idioma == lang) return true;
        return false;
    };
    struct Op {
        const char* valor;
        const char* chave;
        const char* dica;
    };
    // 2026-11-24: os idiomas novos (nome no proprio idioma) so aparecem com o pacote no kit ou na pasta de pacotes
    struct Extra {
        const char*    valor;
        const wchar_t* nome;
    };
    static const Extra kExtras[] = {{"es-ES", L"Español"}, {"fr-FR", L"Français"}, {"ru-RU", L"Русский"},
                                    {"ja-JP", L"日本語"}, {"zh-CN", L"中文（简体）"}};
    std::vector<Op> ops = {{"auto", "comp.subs.auto", "comp.subs.auto.hint"}, {"pt-BR", "comp.subs.pt", nullptr},
                           {"en-US", "comp.subs.en", nullptr}};
    for (const auto& e : kExtras)
        if (tem(e.valor)) ops.push_back({e.valor, nullptr, nullptr});
    ops.push_back({"", "comp.subs.off", nullptr});
    int k = 0;
    for (const auto& o : ops) {
        std::wstring nome = o.chave ? tr(o.chave) : L"";
        for (const auto& e : kExtras)
            if (!o.chave && std::string(o.valor) == e.valor) nome = e.nome;
        std::wstring dica = o.dica ? tr(o.dica) : L"";
        if ((std::string(o.valor) == "pt-BR" || std::string(o.valor) == "en-US") && !tem(o.valor)) dica = tr("comp.pkg.missing");
        const float h = ui_.altura_texto(nome, est(tema::kCorpo), col - 30) + 6 +
                        (dica.empty() ? 0.f : 2 + ui_.altura_texto(dica, est(tema::kPequeno), col - 30));
        if (ui_.opcao(id("leg", k), 3, nome, dica, {r.x, y1, col, h}, s.legendas == o.valor)) s.legendas = o.valor;
        y1 += h + 6;
        ++k;
    }
    // coluna 2: filmes, saves, atalhos
    const float x2 = r.x + col + 28;
    float       y2 = y;
    ui_.rotulo(tr("comp.options"), est(tema::kCorpo, Peso::SemiNegrito), {x2, y2, col, 22}, tema::kTexto);
    y2 += 30;
    auto hcaixa = [&](const std::wstring& t, const std::wstring& d) {
        return ui_.altura_texto(t, est(tema::kCorpo), col - 30) + (d.empty() ? 0 : 2 + ui_.altura_texto(d, est(tema::kPequeno), col - 30)) + 10;
    };
    {
        const std::wstring t = tr("comp.movies"), d = tr("comp.movies.hint");
        const float        h = hcaixa(t, d);
        ui_.caixa(id("comp.movies"), t, d, {x2, y2, col, h}, s.filmes);
        y2 += h + 6;
    }
    if (s.edicao == Edition::Remastered) {  // v0.9.7-2: o mod dos filmes widescreen (baixado do Internet Archive)
        const std::wstring t = tr("comp.widescreen"), d = tr("comp.widescreen.hint");
        const float        h = hcaixa(t, d);
        ui_.caixa(id("comp.widescreen"), t, d, {x2, y2, col, h}, s.filmesWidescreen);
        y2 += h + 6;
    }
    {
        const std::wstring t = tr("portable.check"), d = tr(s.portatil ? "portable.on" : "portable.off");
        const float        h = std::max(hcaixa(t, tr("portable.on")), hcaixa(t, tr("portable.off")));
        ui_.caixa(id("comp.portable"), t, d, {x2, y2, col, h}, s.portatil);
        y2 += h + 6;
    }
    {
        const std::wstring t = tr(c_.linux_ ? "comp.menu.linux" : "comp.menu");
        ui_.caixa(id("comp.menu"), t, {}, {x2, y2, col, 26}, s.atalhos.menu);
        y2 += 32;
        ui_.caixa(id("comp.desktop"), tr("comp.desktop"), {}, {x2, y2, col, 26}, s.atalhos.areaDeTrabalho);
        y2 += 32;
        ui_.caixa(id("comp.steam"), tr("comp.steam"), {}, {x2, y2, col, 26}, s.atalhos.steam);  // v0.9.7-1
        y2 += 32;
    }
    // pacotes locais (toda a largura)
    float y3 = std::max(y1, y2) + 10;
    ui_.rotulo(tr("comp.local"), est(tema::kCorpo, Peso::SemiNegrito), {r.x, y3, r.w - 190, 22}, tema::kTexto);
    if (ui_.botao(id("comp.local.add"), tr("comp.local.add"), {r.dir() - 180, y3 - 6, 180, tema::kBotaoAltura})) {
        h_.escolher_pasta(tr("comp.local.dlg"), fs::path(s.pastaPacotes), [this](fs::path d) {
            if (d.empty()) return;
            s_->pastaPacotes = d.wstring();
            s_->pacotes = pacotes_em(d);
            if (s_->pacotes.empty())  // a propria pasta pode ser um pacote
                if (std::error_code ec; fs::is_regular_file(d / "mod.toml", ec)) s_->pacotes = pacotes_em(d.parent_path()),
                    s_->pacotes.erase(std::remove_if(s_->pacotes.begin(), s_->pacotes.end(), [&](const Pacote& p) { return p.dir != d; }),
                                      s_->pacotes.end());
            h_.redesenhar();
        });
    }
    y3 += 30;
    std::wstring lista;
    for (const auto& pk : s.pacotes) lista += (lista.empty() ? L"" : L", ") + pk.nome;
    const std::wstring linha = s.pacotes.empty() ? tr(s.pastaPacotes.empty() ? "comp.local.none" : "comp.local.empty")
                                                 : tr("comp.local.found", {std::to_wstring(s.pacotes.size()), lista});
    const float lh = std::min(ui_.altura_texto(linha, est(tema::kPequeno), r.w), 36.f);
    ui_.rotulo(linha, est(tema::kPequeno), {r.x, y3, r.w, std::max(lh, 18.f)}, tema::kTextoSuave);
    y3 += std::max(lh, 18.f) + 2;
    const float dh = ui_.altura_texto(tr("comp.local.hint"), est(tema::kPequeno), r.w);
    if (y3 + dh < rodapeY - 8) ui_.rotulo(tr("comp.local.hint"), est(tema::kPequeno), {r.x, y3, r.w, dh}, tema::kTextoApagado);

    if (ui_.botao(id("nav.back"), tr("nav.back"), {r.x, rodapeY, 120, tema::kBotaoAltura}) || ui_.escape()) ir(Tela::Cdi);
    if (ui_.botao(id("nav.next"), tr("nav.next"), {r.dir() - 160, rodapeY, 160, tema::kBotaoAltura}, TipoBotao::Primario, true, true))
        ir(Tela::Pasta);
}

void Instalador::pasta(const Ret& r) {
    Impl& s = *s_;
    const float y0 = titulo(r, tr("pasta.title"), tr("pasta.sub"));
    float       y = r.y + y0;
    const float rodapeY = r.base() - tema::kBotaoAltura, bw = 130;
    if (ui_.campo(id("pasta.campo"), s.pasta, {r.x, y, r.w - bw - 12, 40}, tr("pasta.empty"))) avaliar_pasta(), s.aceitaAlheia = false;
    if (ui_.botao(id("pasta.browse"), tr("browse"), {r.dir() - bw, y + 1, bw, tema::kBotaoAltura})) {
        h_.escolher_pasta(tr("dlg.out_title"), fs::path(s.pasta), [this](fs::path d) {
            if (d.empty()) return;
            s_->pasta = d.wstring();
            s_->aceitaAlheia = false;
            avaliar_pasta();
            h_.redesenhar();
        });
    }
    y += 54;
    Pintor& p = ui_.pintor();
    bool    pode = !s.pasta.empty();
    auto    aviso = [&](bool erro, const std::wstring& t) {
        const float h = ui_.altura_texto(t, est(tema::kCorpo), r.w - 34);
        if (erro) arte::simbolo_erro(p, r.x, y, 20, tema::kCoral);
        else arte::simbolo_alerta(p, r.x, y, 20, tema::kAmbar);
        ui_.rotulo(t, est(tema::kCorpo), {r.x + 32, y, r.w - 34, h}, tema::kTexto);
        y += h + 12;
    };
    const fs::path pp(s.pasta);
    if (!s.pasta.empty() && !pp.is_absolute()) aviso(true, tr("pasta.relative")), pode = false;
    else if (pp.native().size() > plat::max_out_path()) aviso(true, tr("pasta.long", {std::to_wstring(plat::max_out_path())})), pode = false;
    else if (!s.pasta.empty()) {
        // espaco
        if (s.livre) {
            ui_.rotulo(tr("pasta.free", {tamanho(s.livre), tamanho(s.precisa)}), est(tema::kCorpo), {r.x, y, r.w, 22}, tema::kTextoSuave);
            y += 30;
            if (s.livre < s.precisa) aviso(true, tr("pasta.nospace")), pode = false;
        }
        if (s.info.exists && s.info.ours) {
            aviso(false, tr("pasta.existing"));
            const bool agora = s.cfgAtual.saves == native::install::SavesLocation::Portable;
            if (agora != s.portatil)
                aviso(false, tr("pasta.mode", {tr(agora ? "loc.portable" : "loc.documents"), tr(s.portatil ? "loc.portable" : "loc.documents")}));
        } else if (s.info.exists) {
            aviso(false, tr("pasta.foreign"));
            const std::wstring t = tr("pasta.foreign.accept");
            ui_.caixa(id("pasta.aceita"), t, {}, {r.x + 32, y, r.w - 32, 26}, s.aceitaAlheia);
            y += 34;
            pode = pode && s.aceitaAlheia;
        }
        ui_.rotulo(tr("pasta.saves", {tr(s.portatil ? "loc.portable" : "loc.documents")}), est(tema::kPequeno), {r.x, y, r.w, 20},
                   tema::kTextoSuave);
        y += 32;
    }
    // v0.9.7-5: sem o Visual Studio, o compilador portatil (Zig, CMake e Ninja oficiais), so com a confirmacao
    if (s.msvc == Impl::Msvc::Procurando && !c_.compiladorPortatil) {
        ui_.rotulo(tr(s.instalarPendente ? "tc.checking.wait" : "tc.checking"), est(tema::kPequeno), {r.x, y, r.w, 20}, tema::kTextoSuave);
    } else if (s.msvc == Impl::Msvc::Nao && !c_.compiladorPortatil) {
        ui_.rotulo(tr("tc.title"), est(tema::kCorpo, Peso::SemiNegrito), {r.x, y, r.w, 22}, tema::kTexto);
        y += 28;
        std::wstring lista = tr("tc.text");
        for (const portatil::Peca& pc : portatil::kPecas) {
            const std::wstring tam = pc.bytes >= 1000000 ? tr("size.mb", {std::to_wstring((pc.bytes + 999999) / 1000000)})
                                                         : std::to_wstring((pc.bytes + 999) / 1000) + L" KB";
            lista += L"\n• " + tr("tc.item", {widen(pc.nome), widen(pc.licenca), tam, widen(pc.url)});
        }
        const float lh = ui_.altura_texto(lista, est(tema::kPequeno), r.w);
        ui_.rotulo(lista, est(tema::kPequeno), {r.x, y, r.w, lh}, tema::kTextoSuave);
        y += lh + 8;
        ui_.caixa(id("tc.confirm"), tr("tc.confirm"), {}, {r.x, y, r.w, 26}, s.compiladorPortatil);
        y += 34;
        pode = pode && s.compiladorPortatil;
    }
    if (ui_.botao(id("nav.back"), tr("nav.back"), {r.x, rodapeY, 120, tema::kBotaoAltura}) || ui_.escape()) ir(Tela::Componentes);
    const bool atualiza = s.info.exists && s.info.ours;
    if (ui_.botao(id("nav.next"), tr(atualiza ? "nav.update" : "nav.install"), {r.dir() - 180, rodapeY, 180, tema::kBotaoAltura},
                  TipoBotao::Primario, pode, true))
    {
        // durante a sondagem do Visual Studio o clique fica pendente (revisao Codex 20261120-194725): com ele, a instalacao
        // segue quando a sondagem terminar; sem ele, a tela mostra o compilador portatil para confirmar
        if (s.msvc == Impl::Msvc::Procurando && !c_.compiladorPortatil) s.instalarPendente = true;
        else instalar();
    }
}

void Instalador::progresso(const Ret& r) {
    Impl& s = *s_;
    const bool  atualiza = s.info.exists && s.info.ours;
    const float y0 = titulo(r, tr(atualiza ? "prog.title.update" : "prog.title"), tr("prog.sub"));
    float       y = r.y + y0;
    const auto& e = s.prog.estado();
    Pintor&     p = ui_.pintor();
    // as tres fases
    const char* fases[3][2] = {{"fase.extracao", "fase.extracao.d"}, {"fase.preparacao", "fase.preparacao.d"},
                               {"fase.instalacao", "fase.instalacao.d"}};
    const float w = (r.w - 2 * 14) / 3;
    for (int i = 0; i < 3; ++i) {
        const Ret  c{r.x + i * (w + 14), y, w, 78};
        const bool feito = int(e.fase) > i, atual = int(e.fase) == i;
        p.arredondado(c, 10, atual ? tema::kCartaoHover : tema::kCartao);
        if (atual) p.contorno(c, 10, 1.5f, tema::kAmbar);
        if (feito) arte::simbolo_ok(p, c.dir() - 30, c.y + 12, 18, tema::kAgua);
        ui_.rotulo(tr(fases[i][0]), est(tema::kCorpo, Peso::SemiNegrito, false), {c.x + 14, c.y + 12, c.w - 50, 22},
                   feito || atual ? tema::kTexto : tema::kTextoApagado);
        ui_.rotulo(tr(fases[i][1]), est(tema::kPequeno), {c.x + 14, c.y + 36, c.w - 28, 36}, tema::kTextoSuave);
    }
    y += 98;
    ui_.barra({r.x, y, r.w, 10}, e.total, tema::kAmbar, !s.res && !s.cancelando);
    y += 20;
    const int pct = int(std::floor(e.total * 100 + 1e-9));
    ui_.rotulo(tr("prog.step", {std::to_wstring(pct), tr(ui::step_key(e.etapa))}), est(tema::kCorpo, Peso::Normal, false),
               {r.x, y, r.w * 0.7f, 22}, tema::kTexto);
    Estilo dir = est(tema::kPequeno, Peso::Normal, false);
    dir.h = Alinha::Fim;
    ui_.rotulo(tr("prog.elapsed", {duracao(ui_.agora() - s.inicio)}), dir, {r.x + r.w * 0.7f, y + 2, r.w * 0.3f, 20}, tema::kTextoSuave);
    y += 34;
    if (s.dl.ativo) {  // v0.9.7-4: o motivo da demora (o download dos filmes widescreen), com velocidade e tempo restante
        const auto& d = s.dl;
        ui_.rotulo(tr(d.compilador ? "dl.file.compiler" : "dl.file", {widen(d.arquivo), std::to_wstring(d.i), std::to_wstring(d.n)}),
                   est(tema::kCorpo, Peso::SemiNegrito, false), {r.x, y, r.w, 22}, tema::kTexto);
        y += 26;
        double vel = d.velocidadeFixa;
        // revisao Codex 20261120-133930 (MEDIA): sem dados ha mais de 20 s, a velocidade e o tempo antigos nao valem mais
        const double parado = d.amostras.empty() || d.velocidadeFixa > 0 ? 0 : ui_.agora() - d.amostras.back().first;
        if (parado > 20) {
            vel = -1;
        } else if (vel <= 0 && d.amostras.size() >= 2) {
            const double dt = d.amostras.back().first - d.amostras.front().first;
            if (dt >= 2) vel = double(d.amostras.back().second - d.amostras.front().second) / dt;
        }
        auto         mb    = [&](uint64_t b) { return tr("size.mb", {std::to_wstring(b / (1024 * 1024))}); };  // anda a cada MB
        std::wstring linha = d.tamanho ? tr("dl.bytes", {mb(d.recebidos), mb(d.tamanho)}) : tr("dl.bytes.nosize", {mb(d.recebidos)});
        if (vel < 0) {
            linha += L" · " + tr("dl.stalled", {std::to_wstring(int(parado))});
        } else if (vel > 0) {
            linha += L" · " + velocidade(vel);
            if (d.tamanho > d.recebidos)
                linha += L" · " + tr(d.compilador ? "dl.eta.compiler" : "dl.eta", {restante(double(d.tamanho - d.recebidos) / vel)});
        } else {
            linha += L" · " + tr("dl.eta.calc");
        }
        ui_.rotulo(linha, est(tema::kCorpo, Peso::Normal, false), {r.x, y, r.w, 22}, tema::kTextoSuave);
        y += 28;
        const std::wstring nota = tr(d.compilador ? "dl.note.compiler" : "dl.note");
        const float        nh   = ui_.altura_texto(nota, est(tema::kPequeno), r.w);
        ui_.rotulo(nota, est(tema::kPequeno), {r.x, y, r.w, nh}, tema::kAmbar);
        y += nh + 14;
    }
    const float rodapeY = r.base() - tema::kBotaoAltura;
    // detalhes tecnicos (o mesmo texto do builder.log)
    const std::wstring det = tr(s.logTecnico ? "prog.hide" : "prog.details");
    const float        dw = ui_.pintor().medir(det, est(tema::kCorpo, Peso::Normal, false), 0).w + 28;
    if (ui_.botao(id("prog.details"), det, {r.x - 12, y, dw, 30}, TipoBotao::Texto))
        s.logTecnico = !s.logTecnico;
    y += 38;
    if (s.logTecnico) {
        const Ret caixa{r.x, y, r.w, rodapeY - 16 - y};
        p.arredondado(caixa, 8, rgb(0x070B12));
        p.recortar(caixa.recuo(10, 8));
        std::lock_guard l(s.m);
        const float     lh = 16;
        const int       n = int((caixa.h - 16) / lh);
        const size_t    de = s.log.size() > size_t(n) ? s.log.size() - size_t(n) : 0;
        float           yy = caixa.y + 8;
        for (size_t k = de; k < s.log.size(); ++k, yy += lh)
            p.texto(widen(s.log[k]), est(11.5f, Peso::Normal, false), {caixa.x + 10, yy, caixa.w * 4, lh}, tema::kTextoSuave);
        p.soltar_recorte();
    }
    if (s.confirmarCancelar) {
        const std::wstring t = tr(s.dl.ativo && !s.dl.compilador ? "prog.cancel.ask.dl" : "prog.cancel.ask");
        ui_.rotulo(t, est(tema::kCorpo), {r.x, rodapeY - 34, r.w, 24}, tema::kTexto);
        if (ui_.botao(id("prog.cancel.yes"), tr("prog.cancel.yes"), {r.dir() - 180, rodapeY, 180, tema::kBotaoAltura}, TipoBotao::Perigo))
            s.cancelar = true, s.cancelando = true, s.confirmarCancelar = false;
        if (ui_.botao(id("prog.cancel.no"), tr("prog.cancel.no"), {r.dir() - 180 - 12 - 200, rodapeY, 200, tema::kBotaoAltura},
                      TipoBotao::Secundario, true, true) ||
            ui_.escape())
            s.confirmarCancelar = false;
    } else if (ui_.botao(id("nav.cancel"), tr(s.cancelando ? "prog.cancelling" : "nav.cancel"),
                         {r.dir() - 160, rodapeY, 160, tema::kBotaoAltura}, TipoBotao::Secundario, !s.cancelando) ||
               (ui_.escape() && !s.cancelando)) {
        s.confirmarCancelar = true;
        ui_.focar(id("prog.cancel.no"));
    }
}

void Instalador::conclusao(const Ret& r) {
    Impl& s = *s_;
    if (!s.res) return;
    const Result& res = *s.res;
    Pintor&       p = ui_.pintor();
    const float   rodapeY = r.base() - tema::kBotaoAltura;
    if (res.ok) {
        arte::simbolo_ok(p, r.x, r.y + 6, 30, tema::kAgua);
        const Instalacao in = ler_instalacao(s.pasta);
        Ret               t = r;
        t.x += 44, t.w -= 44;
        float y = r.y + titulo(t, tr("fim.title.ok"), {});
        auto  par = [&](const std::wstring& txt, const Cor& c, float tam = tema::kCorpo) {
            const float h = ui_.altura_texto(txt, est(tam), r.w);
            ui_.rotulo(txt, est(tam), {r.x, y, r.w, h}, c);
            y += h + 12;
        };
        par(tr("fim.ok", {nome_exibicao(in.edicao), res.installDir.wstring()}), tema::kTexto);
        if (!s.atalhosFeitos.empty()) {
            std::wstring l;
            for (const auto& a : s.atalhosFeitos) l += (l.empty() ? L"" : L"; ") + a.parent_path().filename().wstring();
            par(tr("fim.shortcuts", {std::to_wstring(s.atalhosFeitos.size())}), tema::kTextoSuave);
        }
        if (s.steamFeitos > 0) par(tr("fim.steam", {std::to_wstring(s.steamFeitos)}), tema::kTextoSuave);  // v0.9.7-1
        if (res.widescreenDownloaded >= 0)  // v0.9.7-2
            par(tr("fim.widescreen", {std::to_wstring(res.widescreenDownloaded + res.widescreenPresent)}), tema::kTextoSuave);
        if (s.pacotesCopiados) par(tr("fim.packs", {std::to_wstring(s.pacotesCopiados)}), tema::kTextoSuave);
        if (!res.warning.empty()) par(tr("fim.warn", {widen(res.warning)}), tema::kAmbar, tema::kPequeno);
        if (!s.avisoIntegracao.empty()) par(tr("fim.warn", {widen(s.avisoIntegracao)}), tema::kAmbar, tema::kPequeno);
        par(tr("fim.legal"), tema::kTextoApagado, tema::kPequeno);
        if (ui_.botao(id("fim.play"), tr("fim.play"), {r.dir() - 180, rodapeY, 180, tema::kBotaoAltura}, TipoBotao::Primario, true, true))
            if (h_.executar(res.exe)) h_.fechar(0);
        if (ui_.botao(id("fim.open"), tr("fim.open"), {r.dir() - 180 - 12 - 160, rodapeY, 160, tema::kBotaoAltura}))
            h_.abrir(res.installDir);
        if (ui_.botao(id("nav.close"), tr("nav.close"), {r.x, rodapeY, 120, tema::kBotaoAltura}) || ui_.escape()) h_.fechar(0);
        return;
    }
    const bool canc = res.cancelled;
    if (canc) arte::simbolo_alerta(p, r.x, r.y + 6, 30, tema::kAmbar);
    else arte::simbolo_erro(p, r.x, r.y + 6, 30, tema::kCoral);
    Ret t = r;
    t.x += 44, t.w -= 44;
    float y = r.y + titulo(t, tr(canc ? "fim.title.cancel" : "fim.title.err"), {});
    auto  par = [&](const std::wstring& txt, const Cor& c, float tam = tema::kCorpo) {
        const float h = ui_.altura_texto(txt, est(tam), r.w);
        ui_.rotulo(txt, est(tam), {r.x, y, r.w, h}, c);
        y += h + 12;
    };
    if (canc) par(tr("fim.cancel"), tema::kTexto);
    else {
        std::string k = error_key(res.kind);
        if (c_.linux_ && res.kind == ErrorKind::Toolchain) k = "err.toolchain.linux";
        par(tr(k), tema::kTexto);
        par(tr("fim.err.kept"), tema::kTextoSuave);
        par(tr("fim.err.log", {res.logFile.wstring()}), tema::kTextoApagado, tema::kPequeno);
        if (ui_.botao(id("fim.openlog"), tr("fim.openlog"), {r.dir() - 180 - 12 - 160, rodapeY, 160, tema::kBotaoAltura}))
            h_.abrir(res.logFile);
    }
    if (ui_.botao(id("nav.next"), tr("nav.retry"), {r.dir() - 180, rodapeY, 180, tema::kBotaoAltura}, TipoBotao::Primario, true, true))
        ir(Tela::Pasta);
    if (ui_.botao(id("nav.close"), tr("nav.close"), {r.x, rodapeY, 120, tema::kBotaoAltura}) || ui_.escape()) h_.fechar(codigo_);
}

void Instalador::verificar(const Ret& r) {
    Impl& s = *s_;
    const float y0 = titulo(r, tr("ver.title"), tr("ver.sub", {s.alvo.wstring()}));
    float       y = r.y + y0;
    const float rodapeY = r.base() - tema::kBotaoAltura;
    Pintor&     p = ui_.pintor();
    const bool  pronto = s.dadosRc != -2 && !trabalhando();
    ui_.barra({r.x, y, r.w, 10}, pronto ? 1.0 : std::max(0.05, s.verFrac), pronto ? (s.ver.ok && s.dadosRc == 0 ? tema::kAgua : tema::kCoral)
                                                                                 : tema::kAmbar, !pronto);
    y += 28;
    auto par = [&](int tipo, const std::wstring& txt) {
        const float h = ui_.altura_texto(txt, est(tema::kCorpo), r.w - 32);
        if (tipo == 1) arte::simbolo_ok(p, r.x, y, 20, tema::kAgua);
        else if (tipo == 2) arte::simbolo_erro(p, r.x, y, 20, tema::kCoral);
        else if (tipo == 3) arte::simbolo_alerta(p, r.x, y, 20, tema::kAmbar);
        ui_.rotulo(txt, est(tema::kCorpo), {r.x + 32, y, r.w - 32, h}, tema::kTexto);
        y += h + 12;
    };
    if (!pronto) par(0, tr("ver.running"));
    else {
        if (s.ver.ok) par(1, tr("ver.ok", {std::to_wstring(s.ver.conferidos)}));
        else par(2, tr("ver.bad", {std::to_wstring(s.ver.diferentes.size()), std::to_wstring(s.ver.faltando.size())}));
        if (s.ver.semHashes) par(3, tr("ver.old"));
        par(s.dadosRc == 0 ? 1 : 2, tr(s.dadosRc == 0 ? "ver.data.ok" : "ver.data.bad"));
        std::wstring lista;
        for (const auto& v : {s.ver.diferentes, s.ver.faltando})
            for (size_t k = 0; k < v.size() && k < 6; ++k) lista += widen(v[k]) + L"\n";
        if (!lista.empty()) {
            const float h = std::min(ui_.altura_texto(lista, est(tema::kPequeno), r.w - 32), rodapeY - 20 - y);
            p.recortar({r.x + 32, y, r.w - 32, h});
            p.texto(lista, est(tema::kPequeno), {r.x + 32, y, r.w - 32, h + 200}, tema::kTextoSuave);
            p.soltar_recorte();
        }
    }
    if (ui_.botao(id("nav.back"), tr("nav.back"), {r.x, rodapeY, 120, tema::kBotaoAltura}, TipoBotao::Secundario, pronto) ||
        (ui_.escape() && pronto))
        ir(Tela::BoasVindas);
    const bool ruim = pronto && (!s.ver.ok || s.dadosRc != 0);
    if (ruim && !c_.kit.empty() &&
        ui_.botao(id("nav.next"), tr("ver.repair"), {r.dir() - 160, rodapeY, 160, tema::kBotaoAltura}, TipoBotao::Primario, true, true)) {
        const Instalacao in = ler_instalacao(s.alvo);
        s.atualizando = in;
        s.pasta = in.pasta.wstring();
        s.edicao = in.edicao, s.edicaoEscolhida = true;
        s.portatil = in.portatil;
        ir(Tela::Cdi);
    }
}

void Instalador::desinstalar(const Ret& r) {
    Impl& s = *s_;
    const Instalacao in = ler_instalacao(s.alvo);
    const float y0 = titulo(r, tr("des.title"), tr("des.sub", {nome_exibicao(in.edicao), s.alvo.wstring()}));
    float       y = r.y + y0;
    const float rodapeY = r.base() - tema::kBotaoAltura;
    Pintor&     p = ui_.pintor();
    {
        const std::wstring t = tr("des.keep");
        const float        h = ui_.altura_texto(t, est(tema::kCorpo), r.w - 32);
        arte::simbolo_ok(p, r.x, y, 20, tema::kAgua);
        ui_.rotulo(t, est(tema::kCorpo), {r.x + 32, y, r.w - 32, h}, tema::kTexto);
        y += h + 18;
    }
    {
        const std::wstring t = tr("des.work"), d = tr("des.work.hint");
        const float h = ui_.altura_texto(t, est(tema::kCorpo), r.w - 30) + 2 + ui_.altura_texto(d, est(tema::kPequeno), r.w - 30) + 10;
        ui_.caixa(id("des.work"), t, d, {r.x, y, r.w, h}, s.desTrabalho, !trabalhando());
        y += h + 12;
    }
    if (!in.valida()) {
        arte::simbolo_erro(p, r.x, y, 20, tema::kCoral);
        ui_.rotulo(tr("des.none"), est(tema::kCorpo), {r.x + 32, y, r.w - 32, 44}, tema::kTexto);
    }
    if (trabalhando()) ui_.barra({r.x, rodapeY - 30, r.w, 8}, 0.5, tema::kCoral, true);
    const bool modoDes = c_.modo == Config::Modo::Desinstalar;
    if (ui_.botao(id("nav.back"), tr(modoDes ? "nav.cancel" : "nav.back"), {r.x, rodapeY, 120, tema::kBotaoAltura}, TipoBotao::Secundario,
                  !trabalhando()) ||
        (ui_.escape() && !trabalhando())) {
        if (modoDes) h_.fechar(3);
        else ir(Tela::BoasVindas);
    }
    if (ui_.botao(id("nav.next"), tr("des.run"), {r.dir() - 180, rodapeY, 180, tema::kBotaoAltura}, TipoBotao::Perigo,
                  in.valida() && !trabalhando(), false))
        comecar_desinstalacao();
}

void Instalador::desinstalar_fim(const Ret& r) {
    Impl&       s = *s_;
    Pintor&     p = ui_.pintor();
    const float rodapeY = r.base() - tema::kBotaoAltura;
    if (s.des.ok) arte::simbolo_ok(p, r.x, r.y + 6, 30, tema::kAgua);
    else arte::simbolo_erro(p, r.x, r.y + 6, 30, tema::kCoral);
    Ret t = r;
    t.x += 44, t.w -= 44;
    float y = r.y + titulo(t, tr(s.des.ok ? "des.done" : "des.err"), {});
    auto  par = [&](const std::wstring& txt, const Cor& c, float tam = tema::kCorpo) {
        const float h = ui_.altura_texto(txt, est(tam), r.w);
        ui_.rotulo(txt, est(tam), {r.x, y, r.w, h}, c);
        y += h + 12;
    };
    if (s.des.ok) {
        par(tr("des.removed", {std::to_wstring(s.des.removidos)}), tema::kTexto);
        if (s.des.pastaRemovida) par(tr("des.allgone"), tema::kTextoSuave);
        else if (s.des.restantes.empty() && !s.des.pendentes.empty()) par(tr("des.later"), tema::kTextoSuave);
        else if (!s.des.restantes.empty())
            par(tr("des.left", {std::to_wstring(s.des.restantes.size()), s.alvo.wstring()}), tema::kTextoSuave);
    } else {
        par(widen(s.des.erro), tema::kTexto);
    }
    const bool modoDes = c_.modo == Config::Modo::Desinstalar;
    if (!modoDes && ui_.botao(id("nav.back"), tr("nav.back"), {r.x, rodapeY, 120, tema::kBotaoAltura})) ir(Tela::BoasVindas);
    if (!s.des.restantes.empty() && fs::exists(s.alvo) &&
        ui_.botao(id("fim.open"), tr("fim.open"), {r.dir() - 160 - 12 - 160, rodapeY, 160, tema::kBotaoAltura}))
        h_.abrir(s.alvo);
    if (ui_.botao(id("nav.next"), tr("nav.close"), {r.dir() - 160, rodapeY, 160, tema::kBotaoAltura}, TipoBotao::Primario, true, true) ||
        ui_.escape())
        h_.fechar(codigo_);
}

std::string Instalador::estado(const std::string& k) const {
    const Impl& s = *s_;
    if (k == "res.ok") return s.res ? (s.res->ok ? "1" : "0") : "-";
    if (k == "res.cancelado") return s.res ? (s.res->cancelled ? "1" : "0") : "-";
    if (k == "ver.ok") return s.dadosRc == -2 ? "-" : s.ver.ok ? "1" : "0";
    if (k == "ver.dados") return std::to_string(s.dadosRc);
    if (k == "des.ok") return s.des.ok ? "1" : "0";
    if (k == "des.restantes") return std::to_string(s.des.restantes.size());
    if (k == "edicao") return native::edition::edition_name(s.edicao);
    if (k == "atalhos") return std::to_string(s.atalhosFeitos.size());
    if (k == "steam") return std::to_string(s.steamFeitos);  // v0.9.7-1
    if (k == "encontradas") return std::to_string(s.encontradas.size());
    if (k == "msvc") return s.msvc == Impl::Msvc::Procurando ? "procurando" : s.msvc == Impl::Msvc::Sim ? "sim" : "nao";  // v0.9.7-5
    if (k == "compilador") return s.compiladorPortatil ? "1" : "0";
    if (k == "pendente") return s.instalarPendente ? "1" : "0";
    if (k == "recuos") return std::to_string(s.prog.recuos()) + (s.prog.recuos() ? " (" + s.prog.primeiro_recuo() + ")" : "");
    if (k == "cdi") {
        switch (s.cdiEstado) {
        case Impl::Cdi::Vazio: return "vazio";
        case Impl::Cdi::Conferindo: return "conferindo";
        case Impl::Cdi::Ok: return "ok";
        case Impl::Cdi::Erro: return "erro";
        case Impl::Cdi::Falta: return "falta";
        }
    }
    return "?";
}

// ---------------------------------------------------------------------------------------------------------------------
// simulacoes (capturas e testes de layout; nada roda de verdade)

void Instalador::simular(Tela t, Simulacao sim) {
    Impl& s = *s_;
    tela_ = t;
    entrouEm_ = -100;  // sem a transicao
    s.cdi = L"D:\\Jogos\\Blue Stinger (USA).cdi";
    if (c_.linux_) s.cdi = L"/run/media/jogador/USB/Blue Stinger (USA).cdi";
    s.pasta = c_.linux_ ? L"/home/jogador/Jogos/BlueStinger" : L"C:\\Users\\jogador\\Documents\\Blue Stinger Recompilado";
    s.cdiEstado = Impl::Cdi::Ok;
    s.cdiInfo = L"T13001N V1.000 (NTSC-U)";
    s.cdiConferido = s.cdi;
    s.livre = 120ull << 30;
    s.precisa = espaco_necessario(true);
    s.pastaAvaliada = s.pasta;
    s.alvo = s.pasta;
    s.info = {};
    if (sim == Simulacao::AvisoPasta) s.info.exists = true, s.info.ours = true, s.portatil = true;
    if (!s.edicaoSim) s.edicaoSim = s.edicao;  // a edicao da captura: a Remastered so na variante "remastered"
    s.edicao = sim == Simulacao::Remastered ? Edition::Remastered : *s.edicaoSim;
    s.cdiVersao = 0, s.dc = false;
    if (sim == Simulacao::DirectorsCut) {  // o disco japones com a opcao marcada, o segundo disco e a confirmacao
        s.cdi       = c_.linux_ ? L"/run/media/jogador/USB/Blue Stinger (Japan).chd" : L"D:\\Jogos\\Blue Stinger (Japan).chd";
        s.cdiInfo   = L"HDR-0003 V1.006 (bluestinger_jp_v1006)";
        s.cdiVersao = 3, s.dc = true, s.dcConfirmado = true;
        s.ocidental = c_.linux_ ? L"/run/media/jogador/USB/Blue Stinger (USA).chd" : L"D:\\Jogos\\Blue Stinger (USA).chd";
    }
    if (t == Tela::BoasVindas) s.encontradas.clear();
    s.logTecnico = sim == Simulacao::Progresso;
    if (t == Tela::BoasVindas && sim == Simulacao::Sucesso) {
        Instalacao a, b;
        a.pasta = s.pasta, a.edicao = Edition::Remastered, a.versao = "Blue Stinger v0.9.0 (0123456789)", a.instante = 2;
        b.pasta = c_.linux_ ? fs::path(L"/mnt/jogos/Blue Stinger Original") : fs::path(L"E:\\Jogos\\Blue Stinger Original");
        b.edicao = Edition::Original, b.versao = "Blue Stinger v0.8.0 (22396c1d5a)", b.instante = 1;
        s.encontradas = {a, b};
    }
    s.msvc = sim == Simulacao::SemCompilador ? Impl::Msvc::Nao : Impl::Msvc::Sim;
    s.compiladorPortatil = sim == Simulacao::SemCompilador;
    s.dl = {};
    if (sim == Simulacao::CompiladorDownload) {  // v0.9.7-5: o compilador portatil sendo baixado
        s.prog = {};
        s.prog.avancar(Step::Toolchain, 0.5);
        s.inicio            = -(1 * 60 + 12);
        s.dl.ativo          = true, s.dl.compilador = true, s.dl.i = 1, s.dl.n = 4, s.dl.arquivo = "Zig 0.13.0";
        s.dl.recebidos      = uint64_t(41) << 20, s.dl.tamanho = 79163968;
        s.dl.velocidadeFixa = 2.4 * 1024 * 1024;
    }
    if (sim == Simulacao::Download) {  // v0.9.7-4: o download dos filmes widescreen, ja com a velocidade medida
        s.prog = {};
        s.prog.avancar(Step::Install, 0.92);
        s.inicio            = -(41 * 60 + 17);
        s.dl.ativo          = true, s.dl.i = 13, s.dl.n = 13, s.dl.arquivo = "NEWGAME.mp4";
        s.dl.recebidos      = uint64_t(1077) << 20, s.dl.tamanho = 1528972724;
        s.dl.velocidadeFixa = 256.0 * 1024;
    }
    if (sim == Simulacao::Progresso) {
        s.prog = {};
        s.prog.avancar(Step::Compile, 0.57);
        s.inicio = -143;  // relativo ao primeiro quadro
        std::lock_guard l(s.m);
        for (int k = 0; k < 30; ++k) s.log.push_back("[" + std::to_string(400 + k) + "/1874] Building CXX object generated/func_8c0" + std::to_string(1000 + k) + ".cpp.obj");
    }
    if (t == Tela::Conclusao) {
        Result r;
        r.installDir = fs::path(s.pasta) / kGameDir;
        r.exe = r.installDir / plat::game_binary_installed();
        r.logFile = fs::path(s.pasta) / kWorkDir / "builder.log";
        r.ok = sim == Simulacao::Sucesso;
        r.cancelled = sim == Simulacao::Cancelado;
        r.kind = sim == Simulacao::Erro ? ErrorKind::Toolchain : ErrorKind::None;
        s.res = r;
        s.atalhosFeitos = {fs::path("menu") / "Blue Stinger.lnk"};
        s.pacotesCopiados = 2;
    }
    if (t == Tela::Verificar) {
        s.ver = {};
        s.ver.ok = sim == Simulacao::Verificado;
        s.ver.conferidos = 37;
        if (!s.ver.ok) s.ver.diferentes = {"BlueStinger.exe"}, s.ver.faltando = {"licencas/SDL3-zlib.txt"};
        s.dadosRc = s.ver.ok ? 0 : 1;
        s.verFrac = 1;
    }
    if (t == Tela::DesinstalarFim) {
        s.des = {};
        s.des.ok = true;
        s.des.removidos = 1874;
        s.des.restantes = {fs::path("saves/BLUESTIN.000"), fs::path("controles.toml")};
    }
    if (t == Tela::Componentes) {
        s.pastaPacotes = L"D:\\pacotes";
        s.pacotes = {{fs::path("legendas-pt-br"), L"Legendas pt-BR 0.2.1", "pt-BR"}, {fs::path("legendas-en"), L"Subtitles en-US 0.3.1", "en-US"},
                     {fs::path("legendas-es"), L"Subtítulos es-ES 0.1.0", "es-ES"}, {fs::path("legendas-fr"), L"Sous-titres fr-FR 0.1.0", "fr-FR"},
                     {fs::path("legendas-ru"), L"Субтитры ru-RU 0.1.0", "ru-RU"}, {fs::path("legendas-ja"), L"字幕 ja-JP 0.1.0", "ja-JP"},
                     {fs::path("legendas-zh-cn"), L"字幕 zh-CN 0.1.0", "zh-CN"}};
    }
}

} // namespace bs::builder::instalador::gui
