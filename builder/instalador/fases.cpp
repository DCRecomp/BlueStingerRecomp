#include "instalador/fases.h"

#include <algorithm>

namespace bs::builder::instalador {

namespace {
// faixas do progresso total do Builder (rangeOf em builder_core.cpp) que cada fase cobre
struct Faixa {
    double a, b;
};
Faixa faixa(Fase f) {
    switch (f) {
    case Fase::Extracao: return {0.00, 0.20};
    case Fase::Preparacao: return {0.20, 0.88};
    case Fase::Instalacao: return {0.88, 1.00};
    case Fase::Concluida: return {1.00, 1.00};
    }
    return {0, 1};
}
} // namespace

Fase fase_de(Step s) {
    switch (s) {
    case Step::Validate:
    case Step::Toolchain:
    case Step::Extract: return Fase::Extracao;
    case Step::Recompile:
    case Step::Configure:
    case Step::Compile: return Fase::Preparacao;
    case Step::Package:
    case Step::Verify:
    case Step::Export:
    case Step::Install: return Fase::Instalacao;
    case Step::Done: return Fase::Concluida;
    }
    return Fase::Extracao;
}

const char* fase_chave(Fase f) {
    switch (f) {
    case Fase::Extracao: return "fase.extracao";
    case Fase::Preparacao: return "fase.preparacao";
    case Fase::Instalacao: return "fase.instalacao";
    case Fase::Concluida: return "fase.concluida";
    }
    return "fase.extracao";
}

Progresso::Estado Progresso::avancar(Step s, double fracao) {
    fracao = std::clamp(fracao, 0.0, 1.0);
    if (fracao < e_.total - 1e-9) {  // ruido de arredondamento entre faixas nao conta
        if (!recuos_++) primeiro_ = std::string(step_name(s)) + ": " + std::to_string(e_.total) + " -> " + std::to_string(fracao);
    }
    const Fase f = fase_de(s);
    // a fase tambem nao volta (uma etapa de uma fase anterior depois de uma posterior fica na posterior)
    if (int(f) >= int(e_.fase)) {
        e_.fase  = f;
        e_.etapa = s;
    }
    e_.total = std::max(e_.total, fracao);
    const Faixa r = faixa(e_.fase);
    e_.daFase = r.b > r.a ? std::clamp((e_.total - r.a) / (r.b - r.a), 0.0, 1.0) : 1.0;
    return e_;
}

Progresso::Estado Progresso::concluir() {
    e_.fase   = Fase::Concluida;
    e_.etapa  = Step::Done;
    e_.total  = 1;
    e_.daFase = 1;
    return e_;
}

} // namespace bs::builder::instalador
