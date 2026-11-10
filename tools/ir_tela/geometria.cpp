// bs-ir-tela geometria (N9.6 fase 0, so investigacao): o que a IR guardada permite reconstruir para a iluminacao.
//
//   bs-ir-tela geometria <ir> [--dist D] [--cx X] [--cy Y] [--normais <saida.bmp>] [--tsv <desenhos.tsv>]
//                            [--vertices <vertices.tsv>]
//
// O Ninja projeta sx = cx + dist*x/z e sy = cy + dist*y/z (docs/N9-5-AUDITORIA.md 11.1) e o TA recebe 1/w. Com z = 1/(1/w),
// a posicao no espaco da camera volta: x = (sx - cx)*z/dist, y = (sy - cy)*z/dist. Se 1/w = k/z (escala desconhecida), as
// posicoes so escalam por igual e os angulos ficam: os angulos entre os planos dominantes da sala (chao x paredes, perto de
// 90 graus) provam a reconstrucao e a distancia. Mede tambem:
//   - desenhos 3D (1/w variavel no desenho; o criterio da fase 2 do N9.5) e 2D;
//   - as normais por triangulo (com a orientacao para a camera) agrupadas por direcao, ponderadas pela area na tela;
//   - os vertices repetidos entre desenhos (a mesma posicao): a base para normais suaves sem os dados do modelo;
//   - o sinal da luz original nas cores: desenhos 3D com cor variando entre vertices (Gouraud aceso pela CPU) e com a cor de
//     deslocamento (especular) ligada e nao nula.
// --normais desenha a cena 3D com a normal de cada triangulo como cor (opaca, sem textura) no rasterizador em software.
#include "render/render.h"
#include "render/soft/soft_device.h"
#include "render_capture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace nr = bs::native::render;
namespace rc = bs::transitional::render_capture;

bool ir_tela_write_bmp(const char* path, const std::vector<uint32_t>& argb, uint32_t w, uint32_t h);

namespace {

struct V3 {
    double x = 0, y = 0, z = 0;
};
V3     sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3     cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double len(V3 a) { return std::sqrt(dot(a, a)); }
V3     unit(V3 a) {
    const double l = len(a);
    return l > 0 ? V3{a.x / l, a.y / l, a.z / l} : V3{};
}
double angle_deg(V3 a, V3 b) { return std::acos(std::clamp(dot(unit(a), unit(b)), -1.0, 1.0)) * 180.0 / 3.14159265358979; }

const char* opt(int argc, char** argv, const char* k) {
    for (int i = 0; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], k) == 0) return argv[i + 1];
    return nullptr;
}

bool is_3d(const nr::CommandList& cl, const nr::Draw& d) {
    if (d.vertex_count < 3 || d.first_vertex > cl.vertices.size() || d.vertex_count > cl.vertices.size() - d.first_vertex)
        return false;
    float lo = INFINITY, hi = -INFINITY;
    for (uint32_t k = 0; k < d.vertex_count; ++k) {
        const nr::Vertex& v = cl.vertices[d.first_vertex + k];
        if (!std::isfinite(v.inv_w) || v.inv_w <= 0 || !std::isfinite(v.x) || !std::isfinite(v.y)) return false;
        lo = std::min(lo, v.inv_w), hi = std::max(hi, v.inv_w);
    }
    return hi > lo;
}

} // namespace

int ir_tela_geometria(int argc, char** argv) {
    nr::CommandList              cl;
    std::vector<rc::DumpTexture> tex;
    if (!rc::load_frame_dump(argv[2], cl, tex)) return std::fprintf(stderr, "nao le a IR: %s\n", argv[2]), 1;
    const double dist = opt(argc, argv, "--dist") ? std::atof(opt(argc, argv, "--dist")) : 500.0;
    const double cx   = opt(argc, argv, "--cx") ? std::atof(opt(argc, argv, "--cx")) : cl.frame.width / 2.0;
    const double cy   = opt(argc, argv, "--cy") ? std::atof(opt(argc, argv, "--cy")) : cl.frame.height / 2.0;
    auto pos = [&](const nr::Vertex& v) {
        const double z = 1.0 / double(v.inv_w);
        return V3{(v.x - cx) * z / dist, (v.y - cy) * z / dist, z};
    };

    // grade de direcoes (octaedrica 48x48), area na tela por celula e a soma das normais para o centro do pico
    constexpr int G = 48;
    std::vector<double> area(G * G, 0);
    std::vector<V3>     acc(G * G);
    auto                cell = [&](V3 n) {
        const double s = std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z);
        double       u = n.x / s, w = n.y / s;
        if (n.z < 0) {
            const double uu = (1 - std::fabs(w)) * (u >= 0 ? 1 : -1), ww = (1 - std::fabs(u)) * (w >= 0 ? 1 : -1);
            u = uu, w = ww;
        }
        const int i = std::clamp(int((u * 0.5 + 0.5) * G), 0, G - 1), j = std::clamp(int((w * 0.5 + 0.5) * G), 0, G - 1);
        return j * G + i;
    };

    size_t d3 = 0, d2 = 0, tris = 0, degen = 0, planar = 0, multi = 0, lit = 0, spec_on = 0, spec_nz = 0, textured = 0;
    size_t list_count[5] = {};
    double area_total = 0, zmin = INFINITY, zmax = -INFINITY;
    std::vector<double> zs;
    // vertices pela posicao (quantizada em 1/1000 da profundidade): de quantos desenhos diferentes
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> seen;  // chave -> (ultimo desenho, desenhos distintos)
    size_t                                                      verts3d = 0;
    auto qkey = [](V3 p) {
        const double s = std::max(1e-6, std::fabs(p.z) * 1e-3);
        const int64_t a = std::llround(p.x / s), b = std::llround(p.y / s), c = std::llround(p.z / s);
        return (uint64_t(a) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(b) * 0xC2B2AE3D27D4EB4Full) ^ (uint64_t(c) * 0x165667B19E3779F9ull);
    };

    nr::CommandList out;
    out.frame             = cl.frame;
    out.frame.clear_color = 0xFF000000;
    std::FILE* tsv        = opt(argc, argv, "--tsv") ? std::fopen(opt(argc, argv, "--tsv"), "w") : nullptr;
    if (tsv) std::fprintf(tsv, "desenho\tlista\tvertices\ttextura\tz_min\tz_max\tplanar\tcor_varia\tespecular\tnx\tny\tnz\n");
    // --vertices: um vertice 3D por linha (posicao da camera, cor e deslocamento), para ajustar a luz original fora daqui
    std::FILE* vtsv = opt(argc, argv, "--vertices") ? std::fopen(opt(argc, argv, "--vertices"), "w") : nullptr;
    if (vtsv) std::fprintf(vtsv, "desenho\tk\tx\ty\tz\tcor\tdesloc\ttextura\tlista\n");

    for (size_t di = 0; di < cl.draws.size(); ++di) {
        const nr::Draw& d = cl.draws[di];
        if (!is_3d(cl, d)) {
            ++d2;
            continue;
        }
        ++d3, ++list_count[size_t(d.state.list) % 5], textured += bool(d.state.texture);
        bool     same_color = true, any_spec = false;
        V3       first_n{}, sum_n{};
        bool     is_planar = true;
        uint32_t ntri      = 0;
        double   dz0 = INFINITY, dz1 = -INFINITY;
        for (uint32_t k = 0; k < d.vertex_count; ++k) {
            const nr::Vertex& v = cl.vertices[d.first_vertex + k];
            const V3          p = pos(v);
            zs.push_back(p.z), zmin = std::min(zmin, p.z), zmax = std::max(zmax, p.z), dz0 = std::min(dz0, p.z), dz1 = std::max(dz1, p.z);
            if (vtsv)
                std::fprintf(vtsv, "%zu\t%u\t%.4f\t%.4f\t%.4f\t%08X\t%08X\t%u\t%u\n", di, k, p.x, p.y, p.z, v.color, v.offset,
                             d.state.texture.value, unsigned(d.state.list));
            same_color &= v.color == cl.vertices[d.first_vertex].color;
            any_spec |= (v.offset & 0x00FFFFFF) != 0;
            ++verts3d;
            auto& e = seen[qkey(p)];
            if (e.second == 0) e = {uint32_t(di), 1};
            else if (e.first != uint32_t(di)) e = {uint32_t(di), e.second + 1};
        }
        for (uint32_t k = 0; k + 2 < d.vertex_count; ++k) {
            const nr::Vertex &a = cl.vertices[d.first_vertex + k], &b = cl.vertices[d.first_vertex + k + 1],
                             &c = cl.vertices[d.first_vertex + k + 2];
            const V3 pa = pos(a), pb = pos(b), pc = pos(c);
            V3       n = cross(sub(pb, pa), sub(pc, pa));
            const double sa = std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5;  // area na tela
            if (len(n) <= 1e-12 * std::max(1.0, pa.z * pa.z) || sa < 1e-3) {
                ++degen;
                continue;
            }
            n = unit(n);
            if (dot(n, pa) > 0) n = {-n.x, -n.y, -n.z};  // para a camera (a origem)
            ++tris, ++ntri, area_total += sa;
            if (ntri == 1) first_n = n;
            else if (angle_deg(first_n, n) > 2.0) is_planar = false;
            sum_n.x += n.x * sa, sum_n.y += n.y * sa, sum_n.z += n.z * sa;
            // a imagem das normais: o triangulo com a cor (n*0.5+0.5), opaco, sem textura, a profundidade do jogo
            const uint32_t col = 0xFF000000u | (uint32_t(std::lround((n.x * 0.5 + 0.5) * 255)) << 16) |
                                 (uint32_t(std::lround((-n.y * 0.5 + 0.5) * 255)) << 8) | uint32_t(std::lround((n.z * 0.5 + 0.5) * 255));
            nr::Draw t;
            t.state              = {};
            t.state.list         = nr::ListType::Opaque;
            t.state.depth        = nr::DepthTest::GreaterEqual;
            t.state.depth_write  = true;
            t.state.gouraud      = false;
            t.first_vertex       = uint32_t(out.vertices.size());
            t.vertex_count       = 3;
            for (const nr::Vertex* s : {&a, &b, &c}) {
                nr::Vertex v = *s;
                v.color = col, v.offset = 0, v.u = v.v = 0;
                out.vertices.push_back(v);
            }
            out.draws.push_back(t);
        }
        if (ntri >= 2) planar += is_planar, multi += 1;
        lit += !same_color, spec_on += d.state.offset_color, spec_nz += d.state.offset_color && any_spec;
        if (tsv) {
            const V3 m = unit(sum_n);
            std::fprintf(tsv, "%zu\t%u\t%u\t%d\t%.1f\t%.1f\t%d\t%d\t%d\t%.3f\t%.3f\t%.3f\n", di, unsigned(d.state.list),
                         d.vertex_count, int(bool(d.state.texture)), dz0, dz1, ntri >= 2 ? int(is_planar) : -1, int(!same_color),
                         int(d.state.offset_color && any_spec), m.x, m.y, m.z);
        }
    }
    if (tsv) std::fclose(tsv);
    if (vtsv) std::fclose(vtsv);

    size_t shared = 0;
    for (const auto& [k, e] : seen) shared += e.second > 1;
    std::sort(zs.begin(), zs.end());
    std::printf("quadro\t%ux%u\tdist\t%.1f\tcx\t%.1f\tcy\t%.1f\n", cl.frame.width, cl.frame.height, dist, cx, cy);
    std::printf("desenhos_3d\t%zu\tdesenhos_2d\t%zu\topacos\t%zu\tpunch\t%zu\ttranslucidos\t%zu\tcom_textura\t%zu\n", d3, d2,
                list_count[0], list_count[4], list_count[2], textured);
    std::printf("triangulos\t%zu\tdegenerados\t%zu\n", tris, degen);
    if (!zs.empty())
        std::printf("z\tmin\t%.2f\tmediana\t%.2f\tmax\t%.2f\n", zmin, zs[zs.size() / 2], zmax);
    std::printf("faixas_planas\t%zu\tde\t%zu\t(com 2+ triangulos)\n", planar, multi);
    std::printf("posicoes_distintas\t%zu\tem_2+_desenhos\t%zu\tvertices_3d\t%zu\n", seen.size(), shared, verts3d);
    std::printf("cor_varia_no_desenho\t%zu\tespecular_ligado\t%zu\tespecular_nao_nulo\t%zu\n", lit, spec_on, spec_nz);

    // a imagem das normais (sempre desenhada): os picos contam os pixels visiveis, nao a area dos triangulos (os que
    // cruzam o plano da camera tem x de tela enorme e dominariam a soma)
    nr::soft::SoftwareDevice dev;
    if (!dev.submit(out)) return std::fprintf(stderr, "o software recusou a IR das normais\n"), 1;
    std::vector<uint32_t> img;
    uint32_t              iw = 0, ih = 0;
    if (!dev.read_frame(img, iw, ih)) return std::fprintf(stderr, "sem a imagem das normais\n"), 1;
    std::fill(area.begin(), area.end(), 0.0), std::fill(acc.begin(), acc.end(), V3{});
    double px = 0;
    for (uint32_t p : img) {
        const V3 n{((p >> 16) & 0xFF) / 127.5 - 1, -(((p >> 8) & 0xFF) / 127.5 - 1), (p & 0xFF) / 127.5 - 1};
        if (std::fabs(len(n) - 1) > 0.05) continue;  // o fundo (preto) nao e uma normal
        const int ci = cell(unit(n));
        area[size_t(ci)] += 1, acc[size_t(ci)].x += n.x, acc[size_t(ci)].y += n.y, acc[size_t(ci)].z += n.z, px += 1;
    }
    area_total = px;
    std::printf("pixels_3d\t%.0f\tde\t%zu\n", px, img.size());

    // picos: celulas em ordem de pixels, juntando o que fica a menos de 12 graus de um pico ja aceito
    std::vector<int> order(G * G);
    for (int i = 0; i < G * G; ++i) order[size_t(i)] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return area[size_t(a)] > area[size_t(b)]; });
    struct Peak {
        V3     n;
        double a;
    };
    std::vector<Peak> peaks;
    for (int ci : order) {
        if (area[size_t(ci)] <= 0) break;
        const V3 n = unit(acc[size_t(ci)]);
        bool     merged = false;
        for (Peak& p : peaks)
            if (angle_deg(p.n, n) < 12.0) {
                p.a += area[size_t(ci)], merged = true;
                break;
            }
        if (!merged) peaks.push_back({n, area[size_t(ci)]});
    }
    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.a > b.a; });
    for (size_t i = 0; i < std::min<size_t>(6, peaks.size()); ++i) {
        std::printf("pico\t%zu\tarea\t%.3f\tn\t%.3f\t%.3f\t%.3f", i, area_total > 0 ? peaks[i].a / area_total : 0, peaks[i].n.x,
                    peaks[i].n.y, peaks[i].n.z);
        for (size_t j = 0; j < i; ++j) std::printf("\tang_%zu\t%.1f", j, angle_deg(peaks[i].n, peaks[j].n));
        std::printf("\n");
    }

    if (const char* bmp = opt(argc, argv, "--normais")) {
        if (!ir_tela_write_bmp(bmp, img, iw, ih)) return std::fprintf(stderr, "nao grava %s\n", bmp), 1;
        std::printf("normais\t%s\t%ux%u\n", bmp, iw, ih);
    }
    return 0;
}
