#pragma once
// N9.1: a arte do instalador, toda em geometria (triangulos, retangulos, degrades): nitida em qualquer escala, sem arquivo de
// imagem e sem nada do jogo. Autoria do projeto, MIT (docs/N9-1-ARTE.md). O icone dos executaveis sai do mesmo desenho
// (instalador_icone).
#include "instalador/gui/pintor.h"

namespace bs::builder::instalador::gui::arte {

// O emblema (o ferrao estilizado) num quadrado de lado `lado`, com o fundo arredondado se `comFundo`.
void emblema(Pintor& p, float x, float y, float lado, bool comFundo = true);
// Emblema + nome ("BLUE STINGER", "Recompiled"), na largura dada; devolve a altura usada.
float logotipo(Pintor& p, float x, float y, float largura);
// Ilustracoes das edicoes, na caixa dada (proporcao livre; o quadro interno fica em 4:3 ou 16:9).
void ilustracao_original(Pintor& p, const Ret& r);
void ilustracao_remastered(Pintor& p, const Ret& r);
// Fundo decorativo da faixa lateral (triangulos discretos).
void fundo_lateral(Pintor& p, const Ret& r);
// Simbolos simples: visto, x, alerta (no quadrado de lado `lado`).
void simbolo_ok(Pintor& p, float x, float y, float lado, const Cor& c);
void simbolo_erro(Pintor& p, float x, float y, float lado, const Cor& c);
void simbolo_alerta(Pintor& p, float x, float y, float lado, const Cor& c);

} // namespace bs::builder::instalador::gui::arte
