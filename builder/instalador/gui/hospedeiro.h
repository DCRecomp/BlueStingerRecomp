#pragma once
// N9.1: o que cada sistema oferece ao instalador (janela_win.cpp, janela_sdl.cpp) e o modo sem janela, comum aos dois:
//   --captura <pasta>: todas as telas, nos dois idiomas e nas escalas pedidas, em PNG, com o relatorio de texto cortado
//                      (cortes.txt; saida 4 se houver algum);
//   --teclas <roteiro>: a interface real conduzida so pelo teclado, sem janela (a prova da navegacao pelo teclado; com um
//                      CDI e uma pasta, instala de verdade). Linhas do roteiro:
//                        tecla <Tab|ShiftTab|Enter|Espaco|Esc|Cima|Baixo|Esquerda|Direita|SelecionarTudo|Backspace> [n]
//                        texto <caracteres>      espera <tela> [segundos]      confere <tela>      captura <nome>
//                        repete <tecla> ate <tela> [segundos]      estado <chave> <valor> (Instalador::estado)
//                      Saida: a do instalador no fim, ou 5 se uma linha falhar.
#include "instalador/gui/telas.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bs::builder::instalador::gui {

// Fabrica de pintor fora da tela (cada sistema tem o seu): quadro de w x h DIP na escala; salvar(png).
class QuadroOffscreen {
public:
    virtual ~QuadroOffscreen() = default;
    virtual Pintor& comecar() = 0;
    virtual void    terminar() = 0;
    virtual bool    salvar(const std::filesystem::path& png) = 0;
};
using FabricaQuadro = std::function<std::unique_ptr<QuadroOffscreen>(float wDip, float hDip, float escala)>;

// Icone dos atalhos do Linux (o .desktop aponta um PNG): o emblema em 256 px, fundo transparente, em pasta_usuario()/
// bluestinger.png. Comum a janela e ao modo sem janela (--teclas, --auto --integrar). Vazio se nao gravar.
std::filesystem::path icone_atalho_png(const FabricaQuadro& f);

int capturar(const Config& c, const std::filesystem::path& pasta, const std::vector<int>& escalas, const FabricaQuadro& f);
int roteiro_teclas(const Config& c, const std::filesystem::path& roteiro, const std::filesystem::path& capturas,
                   const FabricaQuadro& f);

// Linha de comando comum ao instalador nos dois sistemas. Devolve falso se um argumento for invalido.
struct Linha {
    Config                c;
    std::filesystem::path captura, teclas;
    std::vector<int>      escalas = {100, 125, 150, 200};
};
bool ler_linha(const std::vector<std::wstring>& args, Linha& l);

} // namespace bs::builder::instalador::gui
