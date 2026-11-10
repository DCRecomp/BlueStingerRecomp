// bs-ir-tela (N9.5 fase 0, so investigacao): le uma IR guardada pelo jogo (BS_RENDER_IR_GUARDAR=<render>:<arquivo>),
// desenha em software numa largura escolhida e mede a geometria. Nao faz parte do produto.
//
//   bs-ir-tela quadro <ir> <saida.bmp> [--largura W]       desenha a IR (padrao: a largura do quadro)
//   bs-ir-tela medir <ir> [--largura W]                     resumo: desenhos, vertices e o que cai fora de [0, W)
//   bs-ir-tela comparar <ir_4x3> <ir_larga> <dx> <saida.tsv> pareia os desenhos (pela ordem de envio e, o que sobra, pela
//        geometria com o mesmo estado de desenho) e da a classe de cada um no TSV:
//        "cena"     o mesmo desenho com todo vertice andando dx em x e igual no resto (projetado com o cx novo);
//        "tela"     o mesmo desenho sem mudar nada (2D em coordenadas de tela: HUD, textos, filtros);
//        "so_4x3" / "so_larga"  enviado so num dos dois.
//   A largura da tela larga e a do 4:3 + 2 dx (o centro anda dx).
//   bs-ir-tela geometria <ir> ...  N9.6 fase 0: posicoes da camera, normais e o sinal da luz (geometria.cpp)
// Saida 0 = ok, 1 = erro de leitura ou de desenho, 2 = uso.
#include "render/render.h"
#include "render/soft/soft_device.h"
#include "render_capture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace nr = bs::native::render;
namespace rc = bs::transitional::render_capture;

namespace {

struct Ir {
    nr::CommandList              cl;
    std::vector<rc::DumpTexture> tex;
};

bool load(const char* path, Ir& ir) {
    if (rc::load_frame_dump(path, ir.cl, ir.tex)) return true;
    std::fprintf(stderr, "nao le a IR: %s\n", path);
    return false;
}

bool write_bmp(const char* path, const std::vector<uint32_t>& argb, uint32_t w, uint32_t h) {
    const uint32_t row = (w * 3 + 3) & ~3u, size = 54 + row * h;
    std::vector<uint8_t> b(size, 0);
    auto put32 = [&](size_t o, uint32_t v) { std::memcpy(&b[o], &v, 4); };
    b[0] = 'B', b[1] = 'M';
    put32(2, size), put32(10, 54), put32(14, 40), put32(18, w), put32(22, h);
    b[26] = 1, b[28] = 24;
    put32(34, row * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t p = argb[size_t(h - 1 - y) * w + x];
            uint8_t*       d = &b[54 + size_t(y) * row + x * 3];
            d[0] = uint8_t(p), d[1] = uint8_t(p >> 8), d[2] = uint8_t(p >> 16);
        }
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    return o.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size())) && o.good();
}

int draw(const Ir& in, const char* out, uint32_t width) {
    nr::soft::SoftwareDevice       dev;
    std::vector<nr::TextureHandle> h;
    for (const auto& t : in.tex) {
        auto r = dev.create_texture(t.desc, std::as_bytes(std::span<const uint32_t>(t.argb)));
        if (!r) return std::fprintf(stderr, "textura recusada\n"), 1;
        h.push_back(r.value());
    }
    nr::CommandList c = in.cl;
    if (width) c.frame.width = width;
    for (auto& d : c.draws)
        if (d.state.texture) d.state.texture = h[d.state.texture.value - 1];
    if (!dev.submit(c)) return std::fprintf(stderr, "o software recusou a IR\n"), 1;
    std::vector<uint32_t> img;
    uint32_t              w = 0, hh = 0;
    if (!dev.read_frame(img, w, hh) || !write_bmp(out, img, w, hh)) return std::fprintf(stderr, "nao grava %s\n", out), 1;
    uint64_t hash = 1469598103934665603ull;
    for (uint32_t p : img) hash = (hash ^ p) * 1099511628211ull;
    std::printf("quadro\t%ux%u\thash\t%016llx\n", w, hh, static_cast<unsigned long long>(hash));
    return 0;
}

int measure(const Ir& in, uint32_t width) {
    const float W = float(width ? width : in.cl.frame.width);
    size_t      out_left = 0, out_right = 0, clip_inside = 0, nan = 0;
    float       x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
    for (const nr::Vertex& v : in.cl.vertices) {
        if (!std::isfinite(v.x) || !std::isfinite(v.y)) {
            ++nan;
            continue;
        }
        out_left += v.x < 0, out_right += v.x >= W;
        x0 = std::min(x0, v.x), x1 = std::max(x1, v.x), y0 = std::min(y0, v.y), y1 = std::max(y1, v.y);
    }
    for (const nr::Draw& d : in.cl.draws) clip_inside += d.state.clip == nr::Clip::Inside;
    std::printf("desenhos\t%zu\tvertices\t%zu\tvolumes\t%zu\tquadro\t%ux%u\n", in.cl.draws.size(), in.cl.vertices.size(),
                in.cl.modifiers.size(), in.cl.frame.width, in.cl.frame.height);
    std::printf("x\t%.1f\t%.1f\ty\t%.1f\t%.1f\tfora_esq\t%zu\tfora_dir(>=%.0f)\t%zu\tnao_finitos\t%zu\trecorte_dentro\t%zu\n", x0, x1,
                y0, y1, out_left, W, out_right, nan, clip_inside);
    return 0;
}

// alinha as duas sequencias de desenhos pela assinatura (lista, numero de vertices, com ou sem textura): um desenho que o
// jogo enviou so numa delas nao desalinha o resto (LCS; as sequencias tem alguns milhares de desenhos)
std::vector<std::pair<long, long>> align(const Ir& a, const Ir& b) {
    auto key = [](const nr::Draw& d) { return (uint64_t(d.state.list) << 40) | (uint64_t(bool(d.state.texture)) << 32) | d.vertex_count; };
    const size_t n = a.cl.draws.size(), m = b.cl.draws.size();
    std::vector<uint16_t> t((n + 1) * (m + 1), 0);
    auto at = [&](size_t i, size_t j) -> uint16_t& { return t[i * (m + 1) + j]; };
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
            at(i, j) = key(a.cl.draws[i]) == key(b.cl.draws[j]) ? uint16_t(at(i + 1, j + 1) + 1)
                                                                 : std::max(at(i + 1, j), at(i, j + 1));
    std::vector<std::pair<long, long>> out;
    size_t i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && key(a.cl.draws[i]) == key(b.cl.draws[j]) && at(i, j) == at(i + 1, j + 1) + 1)
            out.emplace_back(long(i++), long(j++));
        else if (j < m && (i == n || at(i, j + 1) >= at(i + 1, j))) out.emplace_back(-1, long(j++));
        else out.emplace_back(long(i++), -1);
    }
    return out;
}

void bbox(const Ir& ir, const nr::Draw& d, float& x0, float& x1, float& y0, float& y1) {
    x0 = y0 = 1e30f, x1 = y1 = -1e30f;
    for (uint32_t k = 0; k < d.vertex_count; ++k) {
        const nr::Vertex& v = ir.cl.vertices[d.first_vertex + k];
        x0 = std::min(x0, v.x), x1 = std::max(x1, v.x), y0 = std::min(y0, v.y), y1 = std::max(y1, v.y);
    }
}

// o mesmo estado de desenho (lista, blend, profundidade, filtro, recorte...), com a textura comparada so pela presenca: os
// identificadores de textura de duas IRs nao se correspondem
bool same_state(const nr::Draw& da, const nr::Draw& db) {
    nr::DrawState sa = da.state, sb = db.state;
    sa.texture.value = bool(da.state.texture) ? 1u : 0u;
    sb.texture.value = bool(db.state.texture) ? 1u : 0u;
    return sa == sb && da.vertex_count == db.vertex_count;
}

// b e a com cada vertice deslocado de dx em x (dentro do arredondamento do float nessa magnitude: perto do plano da camera
// o x de tela chega a 1e7 e o passo do float passa de 1) e igual no resto: y, 1/w, u, v e cores
bool shifted_copy(const Ir& a, const nr::Draw& da, const Ir& b, const nr::Draw& db, float dx) {
    if (!same_state(da, db)) return false;
    for (uint32_t k = 0; k < da.vertex_count; ++k) {
        const nr::Vertex &va = a.cl.vertices[da.first_vertex + k], &vb = b.cl.vertices[db.first_vertex + k];
        const float       m  = std::max(std::fabs(va.x), std::fabs(vb.x));
        if (std::fabs(vb.x - va.x - dx) > 0.01f + 2 * (std::nextafter(m, INFINITY) - m)) return false;
        if (vb.y != va.y || vb.inv_w != va.inv_w || vb.u != va.u || vb.v != va.v || vb.color != va.color ||
            vb.offset != va.offset)
            return false;
    }
    return true;
}

int compare(const Ir& a, const Ir& b, float dx, const char* tsv) {
    // 1) alinhamento pela ordem de envio; 2) os pares que nao sao copia deslocada (cena) nem igual (tela), e os enviados so
    // num dos dois, sao pareados de novo pela geometria (numa sequencia de desenhos com a mesma assinatura, um desenho a
    // menos desalinha os seguintes); 3) o TSV com o par final de cada desenho
    struct Par {
        long        ia = -1, ib = -1;
        const char* cls     = "";
        const char* metodo  = "";
        bool        ambiguo = false;
    };
    std::vector<Par>    pares;
    std::vector<size_t> restoA, restoB;
    for (const auto& [ia, ib] : align(a, b)) {
        if (ia >= 0 && ib >= 0) {
            const nr::Draw &da = a.cl.draws[size_t(ia)], &db = b.cl.draws[size_t(ib)];
            if (shifted_copy(a, da, b, db, dx)) {
                pares.push_back({ia, ib, "cena", "ordem"});
                continue;
            }
            if (shifted_copy(a, da, b, db, 0.f)) {
                pares.push_back({ia, ib, "tela", "ordem"});
                continue;
            }
        }
        if (ia >= 0) restoA.push_back(size_t(ia));
        if (ib >= 0) restoB.push_back(size_t(ib));
    }
    std::vector<bool> usado(restoB.size(), false);
    for (size_t ia : restoA) {
        const nr::Draw& da    = a.cl.draws[ia];
        long            achou = -1;
        bool            amb   = false;
        const char*     cls   = "";
        for (int pass = 0; pass < 2 && achou < 0; ++pass)
            for (size_t j = 0; j < restoB.size(); ++j)
                if (!usado[j] && shifted_copy(a, da, b, b.cl.draws[restoB[j]], pass == 0 ? dx : 0.f)) {
                    if (achou >= 0) {
                        amb = true;  // mais de um candidato identico: fica o primeiro, marcado
                        break;
                    }
                    achou = long(j), cls = pass == 0 ? "cena" : "tela";
                }
        if (achou >= 0) {
            usado[size_t(achou)] = true;
            pares.push_back({long(ia), long(restoB[size_t(achou)]), cls, "geometria", amb});
        } else pares.push_back({long(ia), -1, "so_4x3", ""});
    }
    for (size_t j = 0; j < restoB.size(); ++j)
        if (!usado[j]) pares.push_back({-1, long(restoB[j]), "so_larga", ""});
    std::sort(pares.begin(), pares.end(), [](const Par& p, const Par& q) {
        const long kp = p.ia >= 0 ? p.ia : p.ib, kq = q.ia >= 0 ? q.ia : q.ib;
        return kp != kq ? kp < kq : p.ia > q.ia;
    });

    std::FILE* f = std::fopen(tsv, "w");
    if (!f) return std::fprintf(stderr, "nao grava %s\n", tsv), 1;
    std::fprintf(f, "desenho_4x3\tdesenho_larga\tlista\tvertices\tclasse\tpareado_por\tambiguo\tx0_4x3\tx1_4x3\tx0_larga\t"
                    "x1_larga\ty0\ty1\n");
    const float wa = float(a.cl.frame.width), wb = wa + 2 * dx;  // a tela larga: o centro anda dx
    size_t      cena = 0, tela = 0, por_geometria = 0, ambiguos = 0, cena_lado = 0, tri_lado = 0, fora = 0, fora_visivel = 0;
    size_t      so_a = 0, so_b = 0, so_a_dentro = 0, so_b_dentro = 0;
    for (const Par& p : pares) {
        float           ax0 = NAN, ax1 = NAN, bx0 = NAN, bx1 = NAN, y0 = NAN, y1 = NAN;
        const nr::Draw* d = nullptr;
        if (p.ia >= 0) d = &a.cl.draws[size_t(p.ia)], bbox(a, *d, ax0, ax1, y0, y1);
        if (p.ib >= 0) {
            float u0, u1;
            if (!d) d = &b.cl.draws[size_t(p.ib)];
            bbox(b, b.cl.draws[size_t(p.ib)], bx0, bx1, u0, u1);
            if (p.ia < 0) y0 = u0, y1 = u1;
        }
        const std::string cls = p.cls;
        if (cls == "cena") {
            ++cena;
            // um desenho da cena que, no 4:3, saia da tela (x < 0 ou x >= largura): na tela larga ele aparece nas faixas
            if (ax0 < 0 || ax1 >= wa) ++cena_lado, tri_lado += d->vertex_count >= 3 ? d->vertex_count - 2 : 0;
            // inteiro fora do 4:3 (o jogo nao o descartou pela largura da tela) e, na tela larga, dentro de [0, wb)
            if (ax1 < 0 || ax0 >= wa) ++fora, fora_visivel += bx1 >= 0 && bx0 < wb;
        } else if (cls == "tela") ++tela;
        else if (cls == "so_4x3") ++so_a, so_a_dentro += ax1 >= 0 && ax0 < wa && y1 >= 0 && y0 < float(a.cl.frame.height);
        else ++so_b, so_b_dentro += bx1 >= 0 && bx0 < wb && y1 >= 0 && y0 < float(b.cl.frame.height);
        por_geometria += std::strcmp(p.metodo, "geometria") == 0, ambiguos += p.ambiguo;
        std::fprintf(f, "%ld\t%ld\t%u\t%u\t%s\t%s\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\n", p.ia, p.ib, unsigned(d->state.list),
                     d->vertex_count, p.cls, p.metodo, int(p.ambiguo), ax0, ax1, bx0, bx1, y0, y1);
    }
    std::fclose(f);
    std::printf("desenhos\t%zu\t%zu\tcena\t%zu\ttela\t%zu\tpareados_pela_geometria\t%zu\tambiguos\t%zu\n", a.cl.draws.size(),
                b.cl.draws.size(), cena, tela, por_geometria, ambiguos);
    std::printf("cena_alem_do_4x3\t%zu\ttriangulos\t%zu\tcena_toda_fora_do_4x3\t%zu\tvisivel_na_larga\t%zu\n", cena_lado,
                tri_lado, fora, fora_visivel);
    std::printf("so_4x3\t%zu\tna_tela_4x3\t%zu\tso_larga\t%zu\tna_tela_larga\t%zu\n", so_a, so_a_dentro, so_b, so_b_dentro);
    return 0;
}

uint32_t opt_width(int argc, char** argv) {
    for (int i = 0; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--largura") == 0) return uint32_t(std::strtoul(argv[i + 1], nullptr, 10));
    return 0;
}

} // namespace

bool ir_tela_write_bmp(const char* path, const std::vector<uint32_t>& argb, uint32_t w, uint32_t h) {
    return write_bmp(path, argb, w, h);
}
int ir_tela_geometria(int argc, char** argv);  // geometria.cpp (N9.6 fase 0)

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "geometria") == 0) return ir_tela_geometria(argc, argv);
    if (argc >= 4 && std::strcmp(argv[1], "quadro") == 0) {
        Ir ir;
        return load(argv[2], ir) ? draw(ir, argv[3], opt_width(argc, argv)) : 1;
    }
    if (argc >= 3 && std::strcmp(argv[1], "medir") == 0) {
        Ir ir;
        return load(argv[2], ir) ? measure(ir, opt_width(argc, argv)) : 1;
    }
    if (argc == 6 && std::strcmp(argv[1], "comparar") == 0) {
        Ir a, b;
        if (!load(argv[2], a) || !load(argv[3], b)) return 1;
        return compare(a, b, std::strtof(argv[4], nullptr), argv[5]);
    }
    std::fprintf(stderr, "uso: bs-ir-tela quadro <ir> <saida.bmp> [--largura W] | medir <ir> [--largura W] |\n"
                         "      comparar <ir_4x3> <ir_larga> <dx> <saida.tsv> |\n"
                         "      geometria <ir> [--dist D] [--cx X] [--cy Y] [--normais <bmp>] [--tsv <tsv>]\n");
    return 2;
}
