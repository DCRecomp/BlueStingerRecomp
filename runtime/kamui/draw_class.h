#pragma once
// N9.5 fase 2 (so validacao; docs/N9-5-AUDITORIA.md secao 13): a marca cena 3D x HUD 2D de cada desenho da IR, pela
// proveniencia (a cadeia de chamadas do guest que o enviou, kamui::DrawOrigin) e pela semantica do desenho. So
// metadados e diagnostico: nada do desenho muda, e o produto nao tem este arquivo.
//
// A regra (medida contra o oraculo diferencial da fase 0, bs-ir-tela comparar, nas seis cenas da secao 11.3):
//   - a "entrada na biblioteca" e a ultima entrada da cadeia (de dentro para fora) antes da primeira rotina do jogo
//     (endereco abaixo de kGameCodeEnd);
//   - entradas da cena 3D (os modelos e os poligonos projetados): 8C15F856, 8C161738, 8C160C12, 8C1601FE;
//   - entradas da tela (2D em coordenadas de tela), so com o chamador do jogo medido: 8C1736A0 (o recorte dos sprites
//     2D do Ninja) chamada por 8C07B4E0 e 8C16CED8 chamada por 8C0899D6 (com outro chamador: desconhecida, porque o
//     recorte dos sprites tambem recebe sprites projetados da cena);
//   - um parametro de 64 bytes com as metades de cadeias diferentes = desconhecida;
//   - 8C173044 e a copia do canal 2 (DMA): a cadeia e a de quem dispara a copia, nao a de quem montou os vertices. Ali,
//     1/w variavel no desenho = cena (projetado); 1/w constante = ambiguo (o HUD translucido e os billboards da cena,
//     como as faiscas do cais, tem a mesma forma);
//   - qualquer outra entrada (ou cadeia que nao chegou ao jogo) = desconhecida: nunca chutada.
#if BS_VALIDACAO
#include "kamui/list_sink.h"

#include <cstdint>

namespace bs::kamui {

enum class DrawClass : uint8_t { Unknown, Scene, Screen, Ambiguous };

// o fim do codigo do jogo e o comeco das bibliotecas do SDK (Ninja, Kamui, Shinobi) no 1ST_READ.BIN: so serve para
// achar a entrada na biblioteca; as cadeias medidas chegam ao jogo bem abaixo dele
constexpr uint32_t kGameCodeEnd = 0x8C158000;

// a ultima entrada da biblioteca antes da primeira rotina do jogo (0 se a cadeia nao chega ao jogo)
uint32_t library_entry(const ParamOrigin& o);

// a primeira rotina do jogo da cadeia, de dentro para fora (0 se a cadeia nao chega ao jogo)
uint32_t game_routine(const ParamOrigin& o);

// a marca de um desenho: a proveniencia dele e os vertices (para o 1/w do caminho do DMA)
DrawClass classify(const DrawOrigin& o, const Draw& d, const CommandList& cl);

const char* draw_class_name(DrawClass c);

} // namespace bs::kamui
#endif
