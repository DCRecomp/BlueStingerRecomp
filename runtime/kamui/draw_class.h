#pragma once
// N9.5 fase 2 (so validacao; docs/N9-5-AUDITORIA.md secao 13): a marca cena 3D x HUD 2D de cada desenho da IR, pela
// proveniencia (a cadeia de chamadas do guest que o enviou, kamui::DrawOrigin) e pela semantica do desenho. So
// metadados e diagnostico: nada do desenho muda, e o produto nao tem este arquivo.
//
// A regra (medida contra o oraculo diferencial da fase 0, bs-ir-tela comparar; secoes 13 e 14 da auditoria):
//   - a "entrada na biblioteca" e a ultima entrada da cadeia (de dentro para fora) antes da primeira rotina do jogo
//     (endereco abaixo de kGameCodeEnd), e a "rotina do jogo" e essa primeira rotina;
//   - o par (entrada, rotina) e procurado na tabela medida (kamui/hud_table.h); fora dela, desconhecida (nunca chutada);
//   - um parametro de 64 bytes com as metades de cadeias diferentes = desconhecida;
//   - pela copia do canal 2 (DMA), a cadeia e a de quem ESCREVEU o bloco no buffer da RAM (BS_PROVENIENCIA_DMA=1, fase
//     3A). Sem o escritor conhecido (a primeira copia do bloco, ou sem a variavel), vale a regra da fase 2: a cadeia e a de
//     quem disparou a copia (8C173044); 1/w variavel no desenho = cena, constante = ambiguo.
#if BS_VALIDACAO
#include "kamui/hud_table.h"
#include "kamui/list_sink.h"

#include <cstdint>

namespace bs::kamui {

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
