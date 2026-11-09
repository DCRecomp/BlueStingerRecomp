// N9.5 fase 2 (so validacao; docs/N9-5-AUDITORIA.md secao 13): a cadeia de chamadas do guest (Sh4Context::fn_ring)
// que da a proveniencia dos desenhos, com funcoes escritas como o codigo gerado (as mesmas macros BS_FN_ENTER, BS_CALL e
// BS_RET). No produto as macros da cadeia nao deixam nenhum token, e este teste nao existe.
#include "test.h"

#if BS_VALIDACAO
#include "cpu/sh4_gen.h"

#include <cstdint>
#include <limits>
#include <vector>

using namespace bs;

namespace {

constexpr uint32_t kTop = 0x8C004000, kMid = 0x8C004100, kLeaf = 0x8C004200, kRec = 0x8C004300;
constexpr uint32_t kTopRet = 0x8C004010, kMidRet = 0x8C004110, kRecRet = 0x8C004310;

// a cadeia vista de dentro: da entrada mais interna para fora, ate o separador 0 ou o fim do anel
std::vector<uint32_t> chain(const Sh4Context* c) {
    std::vector<uint32_t> v;
    for (uint32_t i = 0; i < c->fn_depth && i < kFnRing; ++i) {
        const uint32_t f = c->fn_ring[(c->fn_depth - 1 - i) % kFnRing];
        if (!f) break;
        v.push_back(f);
    }
    return v;
}

std::vector<uint32_t> g_atLeaf, g_atDeep;
uint32_t              g_depthAfterCall = 0, g_fnAfterCall = 0;
int                   g_recLeft = 0;

void fLeaf(Sh4Context* c) {
    BS_FN_ENTER(kLeaf);
    g_atLeaf = chain(c);
    { const uint32_t tgt = c->pr; BS_RET(kLeaf + 2, tgt); }
}

void fMid(Sh4Context* c) {
    BS_FN_ENTER(kMid);
    c->pr = kMidRet;
    BS_CALL(fLeaf(c), kMidRet);
    // depois do retorno a cadeia volta a do chamador: a mesma funcao pode chamar outra sem herdar a anterior
    c->pr = kMidRet;
    BS_CALL(fLeaf(c), kMidRet);
    c->pr = kTopRet;
    { const uint32_t tgt = c->pr; BS_RET(kMid + 2, tgt); }
}

void fTop(Sh4Context* c) {
    BS_FN_ENTER(kTop);
    c->pr = kTopRet;
    BS_CALL(fMid(c), kTopRet);
    g_depthAfterCall = c->fn_depth, g_fnAfterCall = c->cur_fn;
}

// recursao mais funda que o anel (64): ficam as entradas mais internas, e a volta restaura a profundidade
void fRec(Sh4Context* c) {
    BS_FN_ENTER(kRec + uint32_t(g_recLeft));
    if (g_recLeft-- > 0) {
        c->pr = kRecRet;
        BS_CALL(fRec(c), kRecRet);
    } else {
        g_atDeep = chain(c);
    }
    c->pr = kRecRet;
    { const uint32_t tgt = c->pr; BS_RET(kRec + 2, tgt); }
}

Sh4Context context() {
    Sh4Context c{};
    c.next_event = std::numeric_limits<uint64_t>::max();  // sem servico nem tique de vivacidade no teste
    c.next_poll  = std::numeric_limits<uint64_t>::max();
    return c;
}

} // namespace

BS_TEST(call_chain_follows_calls_and_returns) {
    Sh4Context c = context();
    fTop(&c);
    BS_CHECK((g_atLeaf == std::vector<uint32_t>{kLeaf, kMid, kTop}));
    BS_CHECK(g_depthAfterCall == 1 && g_fnAfterCall == kTop);
    BS_CHECK((chain(&c) == std::vector<uint32_t>{kTop}));
}

BS_TEST(call_chain_ring_keeps_the_innermost_entries) {
    Sh4Context c = context();
    c.ret_expect = kRecRet;
    g_recLeft    = 100;  // 101 entradas, mais que o anel
    fRec(&c);
    // as entradas foram kRec+100, +99, ..., +0: o anel guarda as 64 mais internas, kRec+0 (a mais funda) ate kRec+63
    BS_CHECK(g_atDeep.size() == kFnRing);
    bool ordem = true;
    for (size_t i = 0; i < g_atDeep.size(); ++i) ordem &= g_atDeep[i] == kRec + uint32_t(i);
    BS_CHECK(ordem);
    BS_CHECK(c.fn_depth == 1);  // a volta de todas as chamadas devolveu a profundidade da primeira
}

// chamada de cauda (o gerador emite chamada direta + return, sem BS_CALL: translator/codegen/codegen.cpp): o destino
// entra na cadeia acima de quem saltou, e o retorno do BS_CALL de fora devolve a profundidade de antes das duas
constexpr uint32_t kTail = 0x8C004400, kTailTarget = 0x8C004500, kTailRet = 0x8C004410;
std::vector<uint32_t> g_atTail;
void fTailTarget(Sh4Context* c) {
    BS_FN_ENTER(kTailTarget);
    g_atTail = chain(c);
    { const uint32_t tgt = c->pr; BS_RET(kTailTarget + 2, tgt); }
}
void fTail(Sh4Context* c) {
    BS_FN_ENTER(kTail);
    fTailTarget(c);  // tail call
    return;
}
void fTailCaller(Sh4Context* c) {
    BS_FN_ENTER(kTop);
    c->pr = kTailRet;
    BS_CALL(fTail(c), kTailRet);
    g_depthAfterCall = c->fn_depth;
}

BS_TEST(call_chain_tail_call_is_restored_by_the_outer_call) {
    Sh4Context c = context();
    fTailCaller(&c);
    BS_CHECK((g_atTail == std::vector<uint32_t>{kTailTarget, kTail, kTop}));
    BS_CHECK(g_depthAfterCall == 1);
}

BS_TEST(call_chain_stops_at_the_interrupt_separator) {
    Sh4Context c = context();
    c.fn_ring[c.fn_depth++ % kFnRing] = kTop;  // a cadeia interrompida
    c.fn_ring[c.fn_depth++ % kFnRing] = 0;     // o separador do despacho do tratador (irq_bridge, sh4_intc)
    c.fn_ring[c.fn_depth++ % kFnRing] = kLeaf;
    BS_CHECK((chain(&c) == std::vector<uint32_t>{kLeaf}));
}
#endif
