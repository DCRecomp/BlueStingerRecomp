#pragma once
// N9.2 (docs/N9-2-INTERFACE.md): o MODELO da interface nativa da Remastered. Estado, navegacao e textos, sem desenho, janela
// nem jogo: quem desenha le vista() (app/remaster/desenho.h), quem le a entrada traduz teclado, controle e mouse em MenuAction,
// e quem integra (app/main_sdl.cpp) executa o que tomar() devolve (pontos de entrada do titulo, reinicio, configuracoes,
// sair). A edicao Original nao usa nada disto.
//   Menu inicial: Novo jogo, Continuar (o perfil gravado mais recente), Carregar, Configuracoes, Extras, Sair.
//   Pausa:        Continuar, Salvar (no N9.3; ate la desativado), Carregar, Configuracoes, Menu principal, Sair.
//   Perfis:       os saves de PC como perfis (numero, data, valido), sem VMU, soquete nem bloco (decisao do mantenedor).
//   Acoes que perdem o progresso da partida (Carregar, Menu principal e Sair, na pausa) pedem confirmacao.
#include "settings_menu.h"  // MenuAction

#include <cstdint>
#include <optional>
#include <utility>
#include <string>
#include <vector>

namespace bs::remaster {

enum class Tela : uint8_t { Fechada, Inicial, Pausa, Perfis, Confirmar, Extras, Aguarde, Erro };

// Um save de PC (arquivo BLUESTIN.00n do cartao nativo) visto como perfil.
struct Perfil {
    int     arquivo = 0;        // 0..5
    int64_t gravado = 0;        // data da gravacao (Unix, do envelope)
    bool    valido  = true;     // imagem integra (envelope e VMS conferidos)
};

enum class Pedido : uint8_t { Nenhum, NovoJogo, Carregar, Retomar, Configuracoes, MenuPrincipal, Sair };
struct Saida {
    Pedido pedido  = Pedido::Nenhum;
    int    arquivo = 0;  // Carregar
};

struct ItemVista {
    std::string rotulo, detalhe;
    bool        ativo      = true;
    bool        destrutivo = false;  // a cor de alerta (sair, perder o progresso)
};

struct Vista {
    Tela                   tela = Tela::Fechada;
    // a sessao comecou no menu inicial: a tela de titulo original continua por baixo, animada (o logotipo e a personagem);
    // a interface cobre so a faixa das opcoes originais (NEW GAME, LOAD GAME, OPTIONS) e o rodape (pedido do mantenedor)
    bool                   sobreTitulo = false;
    std::string            titulo, subtitulo;
    std::vector<ItemVista> itens;
    int                    selecionado = 0;
    std::string            texto;      // paragrafo (Extras, Aguarde, Erro, a pergunta da confirmacao)
    std::string            dica;       // a linha de ajuda do rodape
};

class Interface {
public:
    explicit Interface(bool ingles = false) : ingles_(ingles) {}
    void set_ingles(bool on) { ingles_ = on; }
    bool ingles() const { return ingles_; }
    void set_versao(std::string v) { versao_ = std::move(v); }

    void abrir_inicial(std::vector<Perfil> perfis);
    void abrir_pausa(std::vector<Perfil> perfis);
    void aguardar(const std::string& texto);       // uma acao em andamento (cobre o jogo)
    void erro(const std::string& texto);           // volta a tela de origem com Confirmar/Voltar
    void fechar();
    bool aberta() const { return tela_ != Tela::Fechada; }
    Tela tela() const { return tela_; }
    bool na_pausa() const { return origem_ == Tela::Pausa; }  // a sessao da interface comecou pela pausa

    void input(MenuAction a);
    void selecionar(int indice);  // mouse: so muda a selecao (sem efeito com a tela de espera)
    std::optional<Saida> tomar();  // o que o jogador escolheu (uma vez)
    Vista vista() const;

    // O perfil de "Continuar": o valido gravado mais recente (empate: o arquivo menor)
    static std::optional<Perfil> mais_recente(const std::vector<Perfil>& perfis);
    // Texto da data de um perfil (dd/mm/aaaa hh:mm, horario local) e o rotulo dele ("Perfil 1")
    std::string rotulo_perfil(const Perfil& p) const;
    static std::string data_texto(int64_t unix, bool ingles);

private:
    enum class Item : uint8_t { NovoJogo, Continuar, Carregar, Configuracoes, Extras, Sair, Retomar, Salvar, MenuPrincipal,
                                Perfil, Sim, Nao, Voltar };
    struct Entrada {
        Item        item;
        std::string rotulo, detalhe;
        bool        ativo = true, destrutivo = false;
        int         arquivo = 0;
    };
    std::vector<Entrada> entradas() const;
    void                 confirmar_entrada(const Entrada& e);
    void                 ir(Tela t);
    const char*          L(const char* pt, const char* en) const { return ingles_ ? en : pt; }
    bool                 tem_perfil() const;

    bool                  ingles_ = false;
    std::string           versao_;
    Tela                  tela_ = Tela::Fechada, origem_ = Tela::Fechada;
    std::vector<Perfil>   perfis_;
    int                   sel_ = 0;
    std::vector<std::pair<Tela, int>> pilha_;  // as telas de onde se veio e a selecao de cada uma (Voltar as restaura)
    std::optional<Saida>  saida_;
    Saida                 aConfirmar_{};  // a acao destrutiva que a confirmacao executa
    std::string           pergunta_, aviso_;
};

} // namespace bs::remaster
