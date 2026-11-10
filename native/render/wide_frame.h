#pragma once
// N9.5 fase 1 (docs/N9-5-AUDITORIA.md, secao 12): o quadro largo da Remastered (hor+) por translacao da IR.
//
// O jogo projeta os vertices em coordenadas de tela com sx = cx + dist*ax*x/z (njProjectScreen 8C170A54; a copia do
// desenho em 8C2D9C70). Mudar so o cx e uma translacao pura: a fase 0 mediu que, com cx 427, todo vertice 3D cai
// EXATAMENTE no do 4:3 + 107 (no arredondamento do float), e que o jogo ja envia a geometria que fica fora do 4:3. Entao
// o hor+ nao precisa tocar no jogo: o renderizador desenha a MESMA IR deslocada de (largura - 640) / 2 num alvo mais
// largo. O jogo continua projetando em 4:3 (RAM, njSetScreen, o pan do som, os testes "na tela" e os efeitos ficam os
// do Original: o estado e o som nao mudam por construcao).
//
// O que fica 2D (HUD, textos, telas do jogo) tambem anda o deslocamento: fica centrado num 4:3 virtual. A fase 3 do N9.5
// passa uma ancora por desenho (a marca de HUD do produto): os ancorados a esquerda ficam na borda
// esquerda do quadro largo (nao andam). Este modulo nao decide nada pelo conteudo de um desenho.
#include "render/render.h"

#include <cstdint>
#include <vector>

namespace bs::native::render {

// Largura logica do quadro largo para a altura do jogo (480): 16:9 arredondado para par, para o deslocamento ser inteiro
// (o recorte central de 640 colunas fica alinhado aos pixels do jogo).
inline constexpr uint32_t kWideWidth = 854;

// O deslocamento horizontal (pixels do jogo) de um quadro `width` levado a `wide_width`.
inline constexpr uint32_t wide_offset(uint32_t width, uint32_t wide_width) {
    return wide_width > width ? (wide_width - width) / 2 : 0;
}

// Leva a IR de um quadro do jogo (frame.width x frame.height) para o quadro largo: todos os vertices (desenhos e volumes
// modificadores) e os retangulos de recorte andam o deslocamento em x, e frame.width passa a wide_width. Nada muda com
// wide_width <= frame.width.
//
// Excecao, as faixas de largura inteira: um desenho SEM textura, com todos os vertices nas bordas laterais do quadro
// (x <= 0 ou x >= largura, as duas presentes) e com a mesma cor e o mesmo 1/w em todos (uma cor uniforme: o
// escurecimento de tela cheia medido na rota de combate, os fades, barras de cinema) cobre a largura larga inteira: os
// vertices da esquerda ficam em x (sem o deslocamento) e os da direita andam o dobro. Uma cor uniforme estendida nao
// distorce nada; imagens (com textura) e gradientes nunca sao esticados. Devolve quantos desenhos foram estendidos.
//
// N9.5 fase 3B: `anchors` (alinhado a cl.draws; vazio = todos no centro) da a ancora de cada desenho: kAnchorCenter anda o
// deslocamento (o 4:3 centrado), kAnchorLeft fica onde esta (na borda esquerda do quadro largo), vertices e recorte.
inline constexpr uint8_t kAnchorCenter = 0, kAnchorLeft = 1;
uint32_t widen(CommandList& cl, uint32_t wide_width, const std::vector<uint8_t>& anchors = {});

// As colunas centrais (o 4:3 do jogo) de uma imagem larga: dst = src[y][offset .. offset + width).
void crop_center(const uint32_t* src, uint32_t src_width, uint32_t height, uint32_t* dst, uint32_t width);

// Areas na saida (pixels inteiros): o maior retangulo fw:fh centrado em w x h, e a parte central de largura logica
// inner_w de um retangulo de largura logica outer_w (o 4:3 do jogo dentro do quadro largo, onde ficam as camadas).
struct AreaRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool  operator==(const AreaRect&) const = default;
};
// Um slot de w x h e largo (mais largo que 4:3). A apresentacao so desenha um slot largo com o quadro largo ligado: um que
// sobrou de antes do 4:3 (o jogo pausado depois da troca) da lugar a imagem da CPU (revisao Codex 20261108-134326).
inline constexpr bool is_wide_slot(uint32_t w, uint32_t h) { return uint64_t(w) * 480 > uint64_t(h) * 640; }
inline constexpr bool use_slot(uint64_t seq, uint32_t w, uint32_t h, bool wide_on) { return seq && (wide_on || !is_wide_slot(w, h)); }
AreaRect fit_area(int w, int h, uint32_t fw, uint32_t fh);

// N9.5 fase 4 (docs/N9-5-AUDITORIA.md secao 15; decisao do mantenedor: o pilar dinamico): onde a sala nao tem geometria
// numa faixa nova (o vazio fora da sala, B2), a faixa vira pilar. Tudo sobre a IR ja alargada (widen), sem ler pixels.
//
// band_uncovered: a fracao de uma grade de 8 x 24 pontos de cada faixa (as `off` colunas de cada lado) que nenhum
// triangulo cobre. Nao contam os ancorados (o HUD) nem as faixas de largura inteira (fades). Com as regras de descarte do
// rasterizador (frame_prep): triangulo degenerado, culling por orientacao e o "small cull", e o recorte "dentro"/"fora" do
// desenho em cada ponto (revisao Codex 20261109-203527).
struct BandCoverage {
    float left = 0, right = 0;  // fracao descoberta (0 = toda coberta, 1 = vazia)
};
BandCoverage band_uncovered(const CommandList& cl, uint32_t off, const std::vector<uint8_t>& anchors = {});
// pillar_bands: os desenhos que nao sao ancorados nem faixas de largura inteira ficam fora da faixa (o recorte "dentro"
// de cada um perde as colunas da faixa): a faixa mostra o fundo, com borda reta, e o HUD continua por cima. Um desenho
// com recorte "fora" fica como esta. Devolve quantos desenhos foram recortados.
uint32_t pillar_bands(CommandList& cl, uint32_t off, bool left, bool right, const std::vector<uint8_t>& anchors = {});
// A histerese: uma faixa liga o pilar depois de kPillarFrames renders seguidos com >= kPillarOn descoberta e desliga
// depois de kPillarFrames seguidos com <= kPillarOff (o pilar nao pisca com a camera andando). Revisao Codex
// 20261109-203527: o pilar recorta a faixa inteira, entao so liga com a faixa quase toda vazia (no maximo 20 % da grade com
// cena; os casos medidos no RCG02 tem 89-95 % de vazio). E uma escolha visual: a cena da faixa ate esse limite some.
inline constexpr float    kPillarOn = 0.8f, kPillarOff = 0.5f;
inline constexpr uint32_t kPillarFrames = 10;
class PillarState {
public:
    // um render: a cobertura medida; devolve se algum lado mudou
    bool update(const BandCoverage& c);
    bool left() const { return left_.on; }
    bool right() const { return right_.on; }
    void reset() { left_ = right_ = {}; }

private:
    struct Side {
        bool     on  = false;
        uint32_t run = 0;  // renders seguidos pedindo a troca
    };
    static bool step(Side& s, float uncovered);
    Side        left_, right_;
};
AreaRect center_part(const AreaRect& outer, uint32_t inner_w, uint32_t outer_w);

} // namespace bs::native::render
