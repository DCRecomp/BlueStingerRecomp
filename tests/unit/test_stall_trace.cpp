// N8.8 (diagnostico do H9): BS_TRAVAS. Configuracao (limites validos e invalidos) e o caminho desligado, que nao pode
// ler relogio nem registrar nada (o processo do ctest roda sem BS_TRAVAS).
#include "test.h"

#include "platform/stall_trace.h"

#include <cstdlib>
#include <string>

namespace dg = bs::native::diag;

BS_TEST(stall_trace_config) {
    std::string p;
    uint64_t    l = 8'000'000;
    BS_CHECK(dg::parse_config("a.tsv", p, l) && p == "a.tsv" && l == 8'000'000);  // sem limite: o padrao
    BS_CHECK(dg::parse_config("b.tsv;12.5", p, l) && p == "b.tsv" && l == 12'500'000);
    BS_CHECK(dg::parse_config("c.tsv;0.001", p, l) && l == 1'000);
    const char* invalidos[] = {"", ";5", "d.tsv;", "d.tsv;abc", "d.tsv;5x", "d.tsv;0", "d.tsv;-3", "d.tsv;inf", "d.tsv;nan",
                               "d.tsv;1e999", "d.tsv;1e300"};
    for (const char* v : invalidos) {
        uint64_t lim = 7;
        if (dg::parse_config(v, p, lim)) std::fprintf(stderr, "  aceitou: \"%s\"\n", v);
        BS_CHECK(!dg::parse_config(v, p, lim));
    }
    BS_CHECK(!dg::parse_config(nullptr, p, l));
}

BS_TEST(stall_trace_desligado) {
    if (const char* v = std::getenv("BS_TRAVAS"); v && *v) {
        std::fprintf(stderr, "  BS_TRAVAS no ambiente: caso desligado nao se aplica\n");
        ++::bs::test::skips();
        return;
    }
    BS_CHECK(!dg::enabled());
    BS_CHECK(dg::start() == 0);    // nenhum relogio lido
    BS_CHECK(dg::mark(0) == 0);
    dg::finish(dg::Stall::GameSubmit, 0);  // tudo sem efeito (e sem alocar o estado)
    dg::between(dg::Stall::GameSubmit, 0, 5);
    dg::span(dg::Stall::Field, 1, 2);
    dg::event(dg::Stall::AudioUnderrun);
    dg::set_vblank(3), dg::set_render(4), dg::name_thread("teste");
    BS_CHECK(!dg::enabled());
}
