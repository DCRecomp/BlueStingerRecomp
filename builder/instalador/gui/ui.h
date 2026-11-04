#pragma once
// N9.1: os componentes do instalador em modo imediato: a tela os chama a cada quadro, eles desenham e respondem a entrada do
// quadro (mouse, teclado, texto). Foco visivel e navegacao completa pelo teclado: Tab/Shift+Tab percorrem os componentes na
// ordem do desenho; setas andam dentro de um grupo (opcoes, cartoes) e escolhem; Enter/Espaco ativam; Enter fora de um botao
// aciona o botao padrao da tela; Esc e da tela (voltar/cancelar).
// Todo texto passa por `rotulo`, que mede antes de desenhar e registra o que nao cabe (a prova "sem cortes" das capturas).
#include "instalador/gui/pintor.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace bs::builder::instalador::gui {

enum class Tecla : uint8_t {
    Tab, ShiftTab, Esquerda, Direita, Cima, Baixo, Enter, Espaco, Esc, Backspace, Delete, Inicio, Fim, Colar, SelecionarTudo,
    F1, PageUp, PageDown
};

struct Entrada {
    float              mx = -1, my = -1;
    bool               apertou = false, soltou = false;  // botao esquerdo neste quadro
    float              roda = 0;                         // linhas (positivo = para cima)
    std::vector<Tecla> teclas;
    std::wstring       texto;  // caracteres digitados
    std::wstring       colado; // conteudo da area de transferencia quando ha Tecla::Colar
};

using Id = uint32_t;
Id id(std::string_view nome, int indice = 0);

struct Corte {  // texto que nao coube
    std::string  contexto;
    std::wstring texto;
    float        precisa = 0, cabe = 0;  // altura (ou largura, sem quebra)
};

enum class TipoBotao : uint8_t { Primario, Secundario, Perigo, Texto };

class Ui {
public:
    void comecar(Pintor& p, const Entrada& e, double agora, bool menosAnimacao);
    void terminar();
    bool animando() const { return animando_; }
    Pintor& pintor() { return *p_; }

    // contexto dos cortes (o nome da tela)
    std::string contexto;
    std::vector<Corte> cortes;  // acumulados ate o chamador limpar

    // --- texto ---
    // Desenha o texto na caixa; registra corte se nao couber. Devolve a medida.
    Medida rotulo(std::wstring_view t, const Estilo& e, const Ret& r, const Cor& c);
    float  altura_texto(std::wstring_view t, const Estilo& e, float largura);  // sem desenhar
    // Uma linha que cabe na largura: corta no meio com "..." (caminhos: o comeco e o fim sao o que importa)
    std::wstring elidir(std::wstring_view t, const Estilo& e, float largura);

    // --- componentes (devolvem true no quadro em que sao ativados) ---
    bool botao(Id i, std::wstring_view t, const Ret& r, TipoBotao tipo = TipoBotao::Secundario, bool habilitado = true,
               bool padrao = false);
    bool caixa(Id i, std::wstring_view t, std::wstring_view dica, const Ret& r, bool& valor, bool habilitado = true);
    bool opcao(Id i, int grupo, std::wstring_view t, std::wstring_view dica, const Ret& r, bool marcado, bool habilitado = true);
    // cartao clicavel (o conteudo e desenhado por `conteudo` dentro dele)
    bool cartao(Id i, int grupo, const Ret& r, bool marcado, const Cor& destaque, const std::function<void(const Ret&)>& conteudo);
    // campo de texto de uma linha; devolve true quando o texto mudou
    bool campo(Id i, std::wstring& valor, const Ret& r, std::wstring_view vazio = {});
    void barra(const Ret& r, double fracao, const Cor& c, bool ocupada = false);

    // --- foco ---
    bool focado(Id i) const { return foco_ == i; }
    void focar(Id i) { foco_ = i, focoPedido_ = true; }
    bool tem_foco() const { return foco_ != 0; }
    // teclas da tela que nenhum componente consumiu neste quadro
    bool escape() const { return esc_; }
    bool f1() const { return f1_; }
    float roda() const { return entrada_.roda; }
    double agora() const { return agora_; }
    bool menos_animacao() const { return menosAnim_; }
    // valor que se aproxima de `alvo` com o tempo (animacoes discretas; instantaneo com menos animacao)
    float suave(Id i, float alvo, double duracao);

private:
    struct Focavel {
        Id  id;
        int grupo;
    };
    bool hover(const Ret& r) const { return r.contem(entrada_.mx, entrada_.my); }
    // registra o componente para o foco; devolve true se foi ativado pelo teclado ou pelo mouse
    bool interagir(Id i, int grupo, const Ret& r, bool habilitado, bool aceitaEspaco = true);
    void anel(const Ret& r, float raio);

    Pintor*              p_ = nullptr;
    Entrada              entrada_;
    double               agora_ = 0, ultimo_ = 0;
    bool                 menosAnim_ = false, animando_ = false;
    Id                   foco_ = 0, pressionado_ = 0;
    bool                 focoPedido_ = false;
    bool                 teclado_ = false;
    std::vector<Focavel> ordem_, ordemAnterior_;
    // teclas pendentes deste quadro
    bool                 tab_ = false, shiftTab_ = false, enter_ = false, espaco_ = false, esc_ = false, f1_ = false;
    int                  seta_ = 0;  // -1 anterior, +1 proximo
    bool                 enterFoco_ = false, enterPadrao_ = false;
    Id                   focoNovo_ = 0;  // foco mudou por seta neste quadro: ativa a opcao
    std::map<Id, float>  suaves_;
    struct Cursor {
        size_t pos = 0;
        bool   tudo = false;
    };
    std::map<Id, Cursor> cursores_;
};

} // namespace bs::builder::instalador::gui
