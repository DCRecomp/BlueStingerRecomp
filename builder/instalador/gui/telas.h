#pragma once
// N9.1 (docs/N9-1-INSTALADOR.md, secao 3): o instalador inteiro sem o sistema. As 7 telas da instalacao (boas-vindas, edicao,
// CDI, componentes, pasta, progresso, conclusao), a verificacao e a desinstalacao. O hospedeiro (janela_win.cpp, janela_sdl.cpp)
// so entrega a entrada, o pintor e os servicos abaixo; tudo o mais e este codigo, igual nos dois sistemas.
#include "builder_core.h"
#include "instalador/fases.h"
#include "instalador/gui/ui.h"
#include "instalador/instalacoes.h"
#include "instalador/integracao.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace bs::builder::instalador::gui {

class Hospedeiro {
public:
    virtual ~Hospedeiro() = default;
    virtual void redesenhar() = 0;
    virtual void postar(std::function<void()> f) = 0;  // roda na thread da janela
    virtual void escolher_arquivo(const std::wstring& titulo, const std::filesystem::path& inicial,
                                  std::function<void(std::filesystem::path)> cb) = 0;
    virtual void escolher_pasta(const std::wstring& titulo, const std::filesystem::path& inicial,
                                std::function<void(std::filesystem::path)> cb) = 0;
    virtual void abrir(const std::filesystem::path& p) = 0;    // pasta ou arquivo, pelo sistema
    virtual bool executar(const std::filesystem::path& exe) = 0;  // o jogo, desligado do instalador
    virtual void fechar(int codigo) = 0;
    virtual void titulo(const std::wstring& t) = 0;
    // Icone para os atalhos (Linux: um PNG do emblema na pasta do usuario; Windows: vazio, o .lnk usa o do jogo).
    virtual std::filesystem::path icone_atalho() { return {}; }
    // Arquivos que so saem depois de o processo terminar (o desinstalador rodando de dentro da pasta) e as pastas a remover
    // se esvaziarem, da mais funda para a mais rasa.
    virtual void apagar_ao_sair(const std::vector<std::filesystem::path>& arquivos, const std::vector<std::filesystem::path>& pastas) {
        (void)arquivos, (void)pastas;
    }
};

struct Config {
    enum class Modo { Instalar, Desinstalar } modo = Modo::Instalar;
    std::filesystem::path kit, sdl3;          // kit de fontes (vazio no desinstalador)
    std::filesystem::path desinstalador;      // o BlueStinger-Desinstalar que vai no pacote (vazio: nenhum)
    std::filesystem::path exeDir;             // pasta do instalador (o CDI ao lado e achado)
    std::filesystem::path pastaPadrao;        // sugestao de pasta de instalacao
    std::filesystem::path pastaDesinstalar;   // --pasta (desinstalador)
    std::optional<std::filesystem::path> disc, out;
    std::optional<bool>   portatil;           // --portatil
    std::optional<native::edition::Edition> edicao;  // --edicao
    std::string           idioma;             // --idioma (so esta execucao)
    bool                  automatico = false; // --auto: sem perguntas, fecha no fim (0/1/2/3)
    bool                  semFilmes = false;  // --sem-filmes
    bool                  integrar = false;   // --integrar: no --auto tambem cria atalhos e a entrada (testes com a raiz desviada)
    bool                  steam = false;      // --steam: no --auto com --integrar, tambem adiciona o jogo a Steam (v0.9.7-1)
    std::optional<Fase>   cancelarNaFase;     // --cancelar-na-fase (testes): cancela quando o Builder entra nela
    std::vector<std::wstring> idiomasSistema; // preferencias do sistema, para o idioma inicial
    bool                  linux_ = false;     // textos e opcoes do Linux (compilador portatil, sem entrada de desinstalacao)
};

enum class Tela {
    BoasVindas, Edicao, Cdi, Componentes, Pasta, Progresso, Conclusao, Verificar, Desinstalar, DesinstalarFim
};
const char* nome_tela(Tela t);

class Instalador {
public:
    Instalador(Hospedeiro& h, Config c);
    ~Instalador();

    // Um quadro: desenha na area (DIP) e responde a entrada.
    void quadro(Pintor& p, const Entrada& e, double agora, bool menosAnimacao, float largura, float altura);
    bool animando() const { return ui_.animando() || trabalhando(); }
    bool trabalhando() const { return thread_.joinable() && !terminou_; }
    // Fechar a janela: durante uma instalacao, pergunta (e cancela); devolve true se pode fechar agora.
    bool pedir_fechar();
    void iniciar();  // depois de a janela existir (o --auto comeca aqui)

    // Capturas e testes: tela e estados simulados, cortes de texto acumulados.
    enum class Simulacao { Nada, Progresso, Sucesso, Erro, Cancelado, Verificado, Desinstalado, AvisoPasta };
    void simular(Tela t, Simulacao s);
    std::vector<Corte>& cortes() { return ui_.cortes; }
    Tela tela() const { return tela_; }
    // Estado para os roteiros de teste: "res.ok", "res.cancelado", "ver.ok", "ver.dados", "des.ok", "des.restantes",
    // "edicao", "cdi" (vazio, conferindo, ok, erro, falta), "atalhos", "encontradas". Desconhecida: "?".
    std::string estado(const std::string& chave) const;
    int  codigo_saida() const { return codigo_; }

private:
    struct Impl;
    friend struct Impl;
    void ir(Tela t);
    void lateral(const Ret& r);
    void desenhar_tela(const Ret& r);
    // telas
    void boas_vindas(const Ret& r);
    void edicao(const Ret& r);
    void cdi(const Ret& r);
    void componentes(const Ret& r);
    void pasta(const Ret& r);
    void progresso(const Ret& r);
    void conclusao(const Ret& r);
    void verificar(const Ret& r);
    void desinstalar(const Ret& r);
    void desinstalar_fim(const Ret& r);
    // acoes
    void conferir_cdi();
    void avaliar_pasta();
    void instalar();
    void ao_terminar(Result r);
    void comecar_verificacao();
    void comecar_desinstalacao();
    void salvar_prefs();
    void juntar();
    float titulo(const Ret& r, const std::wstring& t, const std::wstring& sub);

    Hospedeiro& h_;
    Config      c_;
    Ui          ui_;
    Tela        tela_ = Tela::BoasVindas;
    double      entrouEm_ = 0;
    int         codigo_ = 0;
    std::unique_ptr<Impl> s_;
    std::thread       thread_;
    std::atomic<bool> terminou_{true};
};

} // namespace bs::builder::instalador::gui
