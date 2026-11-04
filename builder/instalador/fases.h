#pragma once
// N9.1: as tres fases que a tela de progresso mostra, sobre as etapas reais do Builder (builder_core.h, Step), na ordem em
// que o build() as percorre: extracao (entradas, compilador, leitura e conferencia do CDI), preparacao (C++ gerado,
// configuracao e compilacao) e instalacao (pacote, verificacao do executavel, troca da pasta). O progresso que a tela
// mostra nunca volta (o Builder ja e monotonico dentro de cada etapa; a guarda vale contra qualquer mudanca dele).
#include "builder_core.h"

#include <string>

namespace bs::builder::instalador {

enum class Fase { Extracao, Preparacao, Instalacao, Concluida };

Fase        fase_de(Step s);
const char* fase_chave(Fase f);  // chave do catalogo: "fase.extracao"...

class Progresso {
public:
    struct Estado {
        Fase   fase = Fase::Extracao;
        Step   etapa = Step::Validate;
        double total = 0;     // 0..1, nunca diminui
        double daFase = 0;    // 0..1 dentro da fase atual
    };
    // Um aviso do Builder (fracao total dele). Devolve o estado mostrado.
    Estado avancar(Step s, double fracao);
    Estado concluir();
    const Estado& estado() const { return e_; }
    // quantas vezes o Builder mandou um valor menor que o ja mostrado (o registro de teste confere que e 0)
    unsigned recuos() const { return recuos_; }
    // o primeiro recuo (etapa e fracoes), para o registro
    std::string primeiro_recuo() const { return primeiro_; }

private:
    Estado      e_;
    unsigned    recuos_ = 0;
    std::string primeiro_;
};

} // namespace bs::builder::instalador
