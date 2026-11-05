#pragma once
// N9.2 (docs/N9-2-INTERFACE.md): o DESENHO da interface nativa da Remastered numa camada ARGB pre-multiplicada do tamanho da
// saida, por cima do jogo (a apresentacao do jogo compoe a camada, como as legendas e o menu interno). Portatil: o Canvas e o
// motor de texto de native/text (FreeType + Noto Sans nos dois sistemas) e a identidade de native/ui/identidade.h, a mesma do
// instalador (cores, emblema). O layout escala com a saida (referencia 1280 x 720).
//   - Menu inicial: a tela de titulo original continua por baixo, animada; a camada cobre so a faixa das opcoes originais e do
//     rodape (a parte de baixo da area 4:3 do jogo), com um degrade, e desenha ali as opcoes nativas.
//   - Telas abertas dele (perfis, extras, erro, configuracoes): um cartao sobre o titulo levemente escurecido.
//   - Pausa e o que se abre dela: um cartao sobre o jogo escurecido.
// `alfa` (0..1) multiplica a camada inteira: a entrada e a saida da interface sao um fade, nao um corte seco.
#include "remaster/interface.h"
#include "menu_overlay.h"  // MenuHit (as partes de uma linha das configuracoes)
#include "settings_menu.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bs::remaster {

// Um texto que nao coube na caixa dele (prova de "sem cortes" nas resolucoes e escalas), ou um glifo ausente
struct Corte {
    std::string texto;
    float       precisa = 0, cabe = 0;
};

// A area da imagem do jogo (4:3) dentro da saida, em pixels da saida
struct Area {
    float x = 0, y = 0, w = 0, h = 0;
};
Area area_4x3(uint32_t w, uint32_t h);  // a mesma regra de bs::game_area (centrada, proporcao 4:3)

class Desenho {
public:
    // Refeitas so quando o conteudo, o tamanho, a area ou o alfa mudam.
    const std::vector<uint32_t>& camada(const Vista& v, uint32_t w, uint32_t h, const Area& jogo, float alfa = 1);
    const std::vector<uint32_t>& camada_config(const MenuView& v, uint32_t w, uint32_t h, bool sobreTitulo, const Area& jogo,
                                               float alfa = 1);
    uint64_t versao() const { return versao_; }

    std::optional<int>     item_em(float x, float y) const;      // a linha sob o ponto (telas da Remastered)
    std::optional<MenuHit> config_em(float x, float y) const;    // linha e setas (configuracoes)
    const std::vector<Corte>& cortes() const { return cortes_; }  // da ultima camada desenhada
    bool texto_ok() const;  // o motor de texto carregou as fontes

private:
    struct Linha {
        int   item;
        float l, t, r, b;
        float decL = 0, decR = 0, incL = 0, incR = 0;  // as setas de valor (configuracoes)
    };
    void finalizar(float alfa);
    std::vector<uint32_t> buf_;
    std::string           chave_;
    uint64_t              versao_ = 0;
    std::vector<Linha>    linhas_;
    std::vector<Corte>    cortes_;
};

} // namespace bs::remaster
