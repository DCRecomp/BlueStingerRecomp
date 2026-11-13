#include "instalador/gui/hospedeiro.h"

#include "child_process.h"
#include "instalador/gui/arte.h"
#include "instalador/gui/tema.h"
#include "instalador/integracao.h"
#include "ui/i18n.h"

#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace bs::builder::instalador::gui {

namespace {

// Hospedeiro sem janela: as tarefas postadas ficam numa fila que o laco do modo processa entre os quadros.
class SemJanela final : public Hospedeiro {
public:
    explicit SemJanela(const FabricaQuadro* fab = nullptr) : fab_(fab) {}
    fs::path icone_atalho() override { return fab_ ? icone_atalho_png(*fab_) : fs::path(); }
    void redesenhar() override {}
    void postar(std::function<void()> f) override {
        std::lock_guard l(m_);
        fila_.push_back(std::move(f));
    }
    void escolher_arquivo(const std::wstring&, const fs::path&, std::function<void(fs::path)>) override {}
    void escolher_pasta(const std::wstring&, const fs::path&, std::function<void(fs::path)>) override {}
    void abrir(const fs::path&) override {}
    bool executar(const fs::path&) override { return false; }
    void fechar(int codigo) override { fechado = true, codigo_ = codigo; }
    void titulo(const std::wstring&) override {}
    void processar() {
        std::deque<std::function<void()>> f;
        {
            std::lock_guard l(m_);
            f.swap(fila_);
        }
        for (auto& x : f) x();
    }
    bool fechado = false;
    int  codigo_ = 0;

private:
    const FabricaQuadro*               fab_;
    std::mutex                         m_;
    std::deque<std::function<void()>>  fila_;
};

double relogio() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void um_quadro(Instalador& app, QuadroOffscreen& q, const Entrada& e, double t, float w, float h) {
    Pintor& p = q.comecar();
    app.quadro(p, e, t, true, w, h);
    q.terminar();
}

} // namespace

fs::path icone_atalho_png(const FabricaQuadro& fab) {
    const fs::path  f = pasta_usuario() / "bluestinger.png";
    std::error_code ec;
    fs::create_directories(f.parent_path(), ec);
    auto q = fab(256, 256, 1);
    if (!q) return {};
    Pintor& p = q->comecar();
    p.limpar({0, 0, 0, 0});
    arte::emblema(p, 0, 0, 256, true);
    q->terminar();
    return q->salvar(f) ? f : fs::path();
}

int capturar(const Config& base, const fs::path& pasta, const std::vector<int>& escalas, const FabricaQuadro& fab) {
    using S = Instalador::Simulacao;
    const std::pair<Tela, S> casos[] = {
        {Tela::BoasVindas, S::Nada},     {Tela::BoasVindas, S::Sucesso},  {Tela::Edicao, S::Nada},
        {Tela::Cdi, S::Nada},            {Tela::Componentes, S::Nada},    {Tela::Pasta, S::Nada},
        {Tela::Pasta, S::AvisoPasta},    {Tela::Progresso, S::Progresso}, {Tela::Conclusao, S::Sucesso},
        {Tela::Conclusao, S::Erro},      {Tela::Conclusao, S::Cancelado}, {Tela::Verificar, S::Verificado},
        {Tela::Verificar, S::Erro},      {Tela::Desinstalar, S::Nada},    {Tela::DesinstalarFim, S::Desinstalado}};
    const char* nomesSim[] = {"", "progresso", "sucesso", "erro", "cancelado", "verificado", "desinstalado", "aviso"};
    std::error_code ec;
    fs::create_directories(pasta, ec);
    std::ofstream rel(pasta / "cortes.txt", std::ios::binary | std::ios::trunc);
    size_t        total = 0, imagens = 0;
    for (const auto* lang : ui::languages()) {
        Config c = base;
        c.idioma = lang->code;
        for (const int pct : escalas) {
            const float esc = float(pct) / 100.f;
            // o tamanho inicial (a imagem) e o minimo da janela (o pior caso para caber)
            const std::pair<float, float> tamanhos[] = {{tema::kLarguraJanela, tema::kAlturaJanela}, {tema::kLarguraMin, tema::kAlturaMin}};
            for (int k = 0; k < 2; ++k) {
                const auto [w, h] = tamanhos[k];
                for (const auto& [tela, sim] : casos) {
                    SemJanela  host;
                    Instalador app(host, c);
                    app.simular(tela, sim);
                    auto q = fab(w, h, esc);
                    if (!q) return 1;
                    Entrada e;
                    um_quadro(app, *q, e, 1000.0, w, h);  // o primeiro fixa as animacoes no alvo
                    app.cortes().clear();
                    um_quadro(app, *q, e, 1001.0, w, h);
                    for (const auto& ct : app.cortes()) {
                        rel << ct.contexto << " " << pct << "% " << int(w) << "x" << int(h) << ": \"" << narrow(ct.texto) << "\" precisa "
                            << ct.precisa << ", cabe " << ct.cabe << "\n";
                        ++total;
                    }
                    if (k == 0) {
                        std::string nome = std::string(nome_tela(tela)) + (sim == S::Nada ? "" : std::string("-") + nomesSim[int(sim)]) +
                                           "-" + lang->code + "-" + std::to_string(pct) + ".png";
                        if (!q->salvar(pasta / nome)) return 1;
                        ++imagens;
                    }
                }
            }
        }
    }
    rel << total << " cortes em " << imagens << " imagens\n";
    std::printf("capturas: %zu imagens, %zu cortes de texto (%s)\n", imagens, total, path_text(pasta / "cortes.txt").c_str());
    return total ? 4 : 0;
}

int roteiro_teclas(const Config& c, const fs::path& roteiro, const fs::path& capturas, const FabricaQuadro& fab) {
    std::ifstream in(roteiro, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "roteiro nao encontrado: %s\n", path_text(roteiro).c_str());
        return 5;
    }
    SemJanela  host(&fab);  // --teclas pode instalar de verdade: o atalho do Linux precisa do icone
    Instalador app(host, c);
    app.iniciar();
    const float w = tema::kLarguraJanela, h = tema::kAlturaJanela;
    auto        q = fab(w, h, 1.f);
    if (!q) return 1;
    double t = 1000;
    auto   passo = [&](const Entrada& e) {
        host.processar();
        t += 0.05;
        um_quadro(app, *q, e, t, w, h);
    };
    for (int k = 0; k < 3; ++k) passo({});
    const std::pair<const char*, Tecla> nomes[] = {
        {"Tab", Tecla::Tab},         {"ShiftTab", Tecla::ShiftTab}, {"Enter", Tecla::Enter},
        {"Espaco", Tecla::Espaco},   {"Esc", Tecla::Esc},           {"Cima", Tecla::Cima},
        {"Baixo", Tecla::Baixo},     {"Esquerda", Tecla::Esquerda}, {"Direita", Tecla::Direita},
        {"SelecionarTudo", Tecla::SelecionarTudo}, {"Backspace", Tecla::Backspace}};
    int n = 0;
    for (std::string l; std::getline(in, l);) {
        ++n;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.empty() || l[0] == '#') continue;
        std::istringstream ss(l);
        std::string        cmd;
        ss >> cmd;
        auto falha = [&](const std::string& m) {
            std::fprintf(stderr, "roteiro linha %d (%s): %s; tela atual: %s\n", n, l.c_str(), m.c_str(), nome_tela(app.tela()));
            if (!capturas.empty()) q->salvar(capturas / ("falha-linha-" + std::to_string(n) + ".png"));
            return 5;
        };
        if (cmd == "tecla") {
            std::string nome;
            int         rep = 1;
            ss >> nome >> rep;
            if (rep < 1) rep = 1;
            Tecla tk{};
            bool  achou = false;
            for (const auto& [nm, tc] : nomes)
                if (nome == nm) tk = tc, achou = true;
            if (!achou) return falha("tecla desconhecida");
            for (int i = 0; i < rep; ++i) {
                Entrada e;
                e.teclas.push_back(tk);
                passo(e);
                passo({});
            }
        } else if (cmd == "texto") {
            std::string resto;
            std::getline(ss, resto);
            if (!resto.empty() && resto[0] == ' ') resto.erase(0, 1);
            Entrada e;
            e.texto = widen(resto);
            passo(e);
            passo({});
        } else if (cmd == "espera" || cmd == "confere") {
            std::string tela;
            double      seg = 1800;
            ss >> tela >> seg;
            const double fim = relogio() + (cmd == "confere" ? 0 : seg);
            for (;;) {
                passo({});
                if (nome_tela(app.tela()) == tela) break;
                if (relogio() > fim) return falha("a tela esperada nao apareceu");
                if (cmd == "espera") std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            std::printf("roteiro linha %d: tela %s\n", n, tela.c_str());
        } else if (cmd == "repete") {  // repete <tecla> ate <tela> [segundos]: aperta a tecla ate a tela aparecer
            std::string nome, ate, tela;
            double      seg = 120;
            ss >> nome >> ate >> tela >> seg;
            Tecla tk{};
            bool  achou = false;
            for (const auto& [nm, tc] : nomes)
                if (nome == nm) tk = tc, achou = true;
            if (!achou || ate != "ate") return falha("uso: repete <tecla> ate <tela> [segundos]");
            const double fim = relogio() + seg;
            while (nome_tela(app.tela()) != tela) {
                if (relogio() > fim) return falha("a tela esperada nao apareceu");
                Entrada e;
                e.teclas.push_back(tk);
                passo(e);
                for (int i = 0; i < 4; ++i) passo({});
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            std::printf("roteiro linha %d: tela %s\n", n, tela.c_str());
        } else if (cmd == "aguarda") {  // aguarda <chave> <valor> [segundos]: espera o estado (trabalho em outra thread)
            std::string chave, valor;
            double      seg = 600;
            ss >> chave >> valor >> seg;
            const double fim = relogio() + seg;
            while (app.estado(chave) != valor) {
                const std::string v = app.estado(chave);  // valor final diferente: falha ja (so "-" e "conferindo" esperam)
                if (v != "-" && v != "conferindo" && !app.trabalhando()) return falha("estado " + chave + " = " + v + ", esperado " + valor);
                if (relogio() > fim) return falha("estado " + chave + " = " + app.estado(chave) + ", esperado " + valor);
                passo({});
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            std::printf("roteiro linha %d: %s = %s\n", n, chave.c_str(), valor.c_str());
        } else if (cmd == "estado") {  // estado <chave> <valor esperado>
            std::string chave, valor;
            ss >> chave >> valor;
            const std::string v = app.estado(chave);
            if (v != valor) return falha("estado " + chave + " = " + v + ", esperado " + valor);
            std::printf("roteiro linha %d: %s = %s\n", n, chave.c_str(), v.c_str());
        } else if (cmd == "captura") {
            std::string nome;
            ss >> nome;
            if (!capturas.empty()) {
                std::error_code ec;
                fs::create_directories(capturas, ec);
                passo({});
                q->salvar(capturas / (nome + ".png"));
            }
        } else {
            return falha("comando desconhecido");
        }
        if (host.fechado) break;
    }
    // deixa terminar o que estiver rodando (o --auto dos testes nao vem por aqui)
    while (app.trabalhando()) passo({}), std::this_thread::sleep_for(std::chrono::milliseconds(50));
    host.processar();
    std::printf("roteiro: fim na tela %s, codigo %d\n", nome_tela(app.tela()), host.fechado ? host.codigo_ : app.codigo_saida());
    return host.fechado ? host.codigo_ : app.codigo_saida();
}

bool ler_linha(const std::vector<std::wstring>& args, Linha& l) {
    for (size_t i = 1; i < args.size(); ++i) {
        const bool    more = i + 1 < args.size();
        const auto&   a = args[i];
        if (a == L"--disc" && more) l.c.disc = fs::path(args[++i]);
        else if (a == L"--out" && more) l.c.out = fs::path(args[++i]);
        else if (a == L"--kit" && more) l.c.kit = fs::path(args[++i]);
        else if (a == L"--sdl3" && more) l.c.sdl3 = fs::path(args[++i]);
        else if (a == L"--portatil") l.c.portatil = true;
        else if (a == L"--idioma" && more) l.c.idioma = narrow(args[++i]);
        else if (a == L"--auto") l.c.automatico = true;
        else if (a == L"--sem-filmes") l.c.semFilmes = true;
        else if (a == L"--integrar") l.c.integrar = true;
        else if (a == L"--steam") l.c.steam = true;
        else if (a == L"--pasta" && more) l.c.pastaDesinstalar = fs::path(args[++i]);
        else if (a == L"--captura" && more) l.captura = fs::path(args[++i]);
        else if (a == L"--teclas" && more) l.teclas = fs::path(args[++i]);
        else if (a == L"--escalas" && more) {
            l.escalas.clear();
            std::wstringstream ss(args[++i]);
            for (std::wstring v; std::getline(ss, v, L',');)
                if (const int p = std::wcstol(v.c_str(), nullptr, 10); p >= 50 && p <= 400) l.escalas.push_back(p);
            if (l.escalas.empty()) return false;
        } else if (a == L"--edicao" && more) {
            native::edition::Edition e{};
            if (!native::edition::parse_edition(narrow(args[++i]), &e)) return false;
            l.c.edicao = e;
        } else if (a == L"--cancelar-na-fase" && more) {
            const std::wstring f = args[++i];
            if (f == L"extracao") l.c.cancelarNaFase = Fase::Extracao;
            else if (f == L"preparacao") l.c.cancelarNaFase = Fase::Preparacao;
            else if (f == L"instalacao") l.c.cancelarNaFase = Fase::Instalacao;
            else return false;
        }
    }
    return true;
}

} // namespace bs::builder::instalador::gui
