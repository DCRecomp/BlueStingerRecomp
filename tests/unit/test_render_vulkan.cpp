// N7b: o backend Vulkan (native/render/vulkan) nas cenas de fidelidade (native/render/fidelity_scenes): cada prova
// das cenas fieis tem de sair na tolerancia da cena. Sem GPU/Vulkan na maquina: [skip] (com BS_EXIGIR_DADOS=1,
// falha). Tambem: determinismo (o mesmo quadro duas vezes, a mesma imagem) e 0 objetos vivos no fim.
#include "test.h"

#include "render/fidelity_scenes.h"
#include "render/frame_prep.h"
#include "render/material_catalog.h"
#include "render/soft/soft_device.h"
#include "render/vulkan/vk_device.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace nr = bs::native::render;

namespace {
bool close(uint32_t a, uint32_t b, int tol) {
    for (int sh = 0; sh <= 16; sh += 8)
        if (std::abs(int((a >> sh) & 0xFF) - int((b >> sh) & 0xFF)) > tol) return false;
    return true;
}

// renderiza a cena no dispositivo; false se algo falhar
bool run(nr::VulkanDevice& d, const nr::FidelityScene& s, std::vector<uint32_t>& img) {
    uint32_t w = 0, h = 0;
    if (s.image) return d.present_image(s.image_argb, s.cl.frame.width, s.cl.frame.height).ok() && d.read_frame(img, w, h).ok();
    std::vector<nr::TextureHandle> handles;
    for (const auto& t : s.textures) {
        auto r = d.create_texture(t.desc, std::as_bytes(std::span<const uint32_t>(t.argb)));
        if (!r) return false;
        handles.push_back(r.value());
    }
    nr::CommandList cl = s.cl;
    for (size_t i = 0; i < cl.draws.size(); ++i)
        cl.draws[i].state.texture = s.draw_texture[i] >= 0 ? handles[size_t(s.draw_texture[i])] : nr::TextureHandle{};
    const bool ok = d.submit(cl).ok() && d.read_frame(img, w, h).ok() && w == cl.frame.width && h == cl.frame.height;
    for (auto h : handles) d.destroy_texture(h);
    return ok;
}
} // namespace

BS_TEST(render_vulkan_fidelity_scenes) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) {
            std::fprintf(stderr, "  (sem Vulkan: %s)\n", err.c_str());
            BS_SKIP("dispositivo Vulkan");
        }
        std::fprintf(stderr, "  dispositivo: %s\n", d->stats().device.c_str());
        int scenes = 0, eligible = 0, probes = 0;
        for (const auto& s : nr::fidelity_scenes()) {
            if (!s.blocked.empty() || s.mode != nr::SceneMode::Faithful) continue;
            ++eligible;
            std::vector<uint32_t> img;
            const bool            ok = run(*d, s, img);
            BS_CHECK(ok);
            if (!ok) continue;
            ++scenes;
            for (const auto& p : s.probes) {
                ++probes;
                const uint32_t got  = img[size_t(p.y) * s.cl.frame.width + p.x] & 0xFFFFFF;
                const bool     good = close(got, p.rgb, p.tolerance);
                if (!good) std::fprintf(stderr, "  cena %s (%u,%u): Vulkan %06X, esperado %06X (tol %u)\n", s.name.c_str(), p.x, p.y, got, p.rgb, p.tolerance);
                BS_CHECK(good);
            }
            // determinismo: o mesmo quadro de novo
            std::vector<uint32_t> again;
            BS_CHECK(run(*d, s, again) && again == img);
        }
        std::fprintf(stderr, "  cenas fieis %d de %d, provas %d\n", scenes, eligible, probes);
        BS_CHECK(scenes == eligible && eligible >= 7);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// N7c: resolucao interna. Com escala, o Vulkan desenha a GEOMETRIA no alvo maior (as contas do modo fiel em cada
// pixel do alvo), nao amplia a imagem do jogo: (1) as provas das cenas fieis continuam certas nos pontos
// correspondentes do alvo; (2) a borda inclinada de um triangulo tem degraus mais finos que a imagem original
// ampliada; (3) a imagem reduzida volta perto da original. Caminho B: a imagem maior fica num slot da GPU (lida aqui
// com read_slot); read_frame devolve a reducao feita na GPU, que tem de ser IGUAL a da CPU (frame_prep: downsample).
BS_TEST(render_vulkan_internal_resolution) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        for (const float sc : {1.5f, 2.25f}) {
            d->set_scale(sc);
            int probes = 0, expected = 0;
            for (const auto& s : nr::fidelity_scenes()) {
                if (!s.blocked.empty() || s.mode != nr::SceneMode::Faithful || s.image) continue;
                expected += int(s.probes.size());
                std::vector<nr::TextureHandle> handles;
                for (const auto& t : s.textures)
                    handles.push_back(d->create_texture(t.desc, std::as_bytes(std::span<const uint32_t>(t.argb))).value());
                nr::CommandList cl = s.cl;
                for (size_t i = 0; i < cl.draws.size(); ++i)
                    cl.draws[i].state.texture = s.draw_texture[i] >= 0 ? handles[size_t(s.draw_texture[i])] : nr::TextureHandle{};
                std::vector<uint32_t> img, small, cpu;
                uint32_t              w = 0, h = 0, sw = 0, shh = 0;
                BS_CHECK(d->submit(cl).ok() && d->read_frame(small, sw, shh).ok());
                BS_CHECK(sw == cl.frame.width && shh == cl.frame.height);  // a leitura de volta: na resolucao do jogo
                const uint64_t seq = d->last_slot(w, h);
                BS_CHECK(seq && w == nr::scaled_size(cl.frame.width, sc) && h == nr::scaled_size(cl.frame.height, sc));
                uint32_t rw = 0;
                BS_CHECK(d->read_slot(seq, 0, h, img, rw) && rw == w && img.size() == size_t(w) * h);
                nr::downsample(img, w, h, cpu, sw, shh);
                BS_CHECK(cpu == small);  // a reducao da GPU e a da CPU sao a mesma conta
                for (auto hd : handles) d->destroy_texture(hd);
                if (img.size() != size_t(w) * h) continue;
                for (const auto& p : s.probes) {  // o pixel do alvo com o centro no centro do pixel da prova
                    const uint32_t x = uint32_t((float(p.x) + 0.5f) * sc), y = uint32_t((float(p.y) + 0.5f) * sc);
                    const uint32_t got = img[size_t(y) * w + x] & 0xFFFFFF;
                    ++probes;
                    if (!close(got, p.rgb, p.tolerance + 2))
                        std::fprintf(stderr, "  escala %.2f cena %s (%u,%u): %06X, esperado %06X\n", sc, s.name.c_str(), p.x, p.y, got, p.rgb);
                    BS_CHECK(close(got, p.rgb, p.tolerance + 2));
                }
            }
            BS_CHECK(probes == expected && expected >= 10);
        }
        // um triangulo branco de borda inclinada num quadro 64 x 48
        nr::CommandList cl;
        cl.frame.width = 64, cl.frame.height = 48, cl.frame.clear_color = 0xFF000000;
        auto vtx = [](float x, float y) {
            nr::Vertex v;
            v.x = x, v.y = y, v.inv_w = 1, v.color = 0xFFFFFFFF;
            return v;
        };
        cl.vertices = {vtx(3.3f, 2.1f), vtx(60.7f, 9.4f), vtx(11.2f, 45.6f)};
        nr::Draw dr;
        dr.vertex_count = 3, dr.state.depth = nr::DepthTest::Always;
        cl.draws = {dr};
        std::vector<uint32_t> lo, hi, dummy;
        uint32_t              lw = 0, lh = 0, hw = 0, hh = 0, rw = 0;
        d->set_scale(1);
        BS_CHECK(d->submit(cl).ok() && d->read_frame(lo, lw, lh).ok() && lw == 64 && lh == 48);
        BS_CHECK(d->last_slot(hw, hh) == 0);  // na escala 1 nao ha slot
        d->set_scale(3);
        BS_CHECK(d->submit(cl).ok() && d->read_frame(dummy, lw, lh).ok() && lw == 64 && lh == 48);
        const uint64_t seq = d->last_slot(hw, hh);
        BS_CHECK(seq && hw == 192 && hh == 144 && d->read_slot(seq, 0, hh, hi, rw) && rw == hw);
        if (lo.size() == 64u * 48 && hi.size() == 192u * 144) {
            // a borda esquerda (do vertice de cima ao de baixo) por linha do alvo: na imagem ampliada ela so anda de 3
            // em 3 pixels; desenhada no alvo, aparece em posicoes que nao sao multiplos de 3
            int offGrid = 0, rows = 0;
            for (uint32_t y = 12; y < 132; ++y) {
                uint32_t x = 0;
                while (x < hw && (hi[size_t(y) * hw + x] & 0xFFFFFF) == 0) ++x;
                if (x == hw) continue;
                ++rows, offGrid += x % 3 != 0;
            }
            std::fprintf(stderr, "  borda: %d de %d linhas fora da grade da imagem original\n", offGrid, rows);
            BS_CHECK(rows > 100 && offGrid > rows / 3);
            // reduzida, volta perto da original (so a cobertura parcial das bordas difere)
            std::vector<uint32_t> down;
            nr::downsample(hi, hw, hh, down, 64, 48);
            double se = 0;
            for (size_t i = 0; i < lo.size(); ++i)
                for (int s = 0; s < 24; s += 8) {
                    const double dd = double(int((lo[i] >> s) & 0xFF) - int((down[i] >> s) & 0xFF));
                    se += dd * dd;
                }
            const double psnr = 10 * std::log10(255.0 * 255.0 / std::max(se / (3.0 * double(lo.size())), 1e-9));
            std::fprintf(stderr, "  reduzida x original: PSNR %.1f dB\n", psnr);
            BS_CHECK(psnr > 20);
        }
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// a reducao: media exata de blocos inteiros e cobertura completa das colunas com fator fracionario
BS_TEST(render_downsample_box) {
    std::vector<uint32_t> src(4 * 2), dst;
    for (uint32_t i = 0; i < src.size(); ++i) src[i] = 0xFF000000u | (i * 10u);
    nr::downsample(src, 4, 2, dst, 2, 1);
    BS_CHECK(dst.size() == 2 && dst[0] == (0xFF000000u | ((0 + 10 + 40 + 50 + 2) / 4)) &&
             dst[1] == (0xFF000000u | ((20 + 30 + 60 + 70 + 2) / 4)));
    std::vector<uint32_t> big(9 * 9, 0xFF808080u), small;
    nr::downsample(big, 9, 9, small, 4, 4);  // 2,25 por pixel: tudo cinza continua cinza
    bool all = small.size() == 16;
    for (uint32_t c : small) all = all && c == 0xFF808080u;
    BS_CHECK(all);
}

// N8.7 (docs/N8-7-DIAGNOSTICO.md, causa da perda do dispositivo na AMD integrada): quando o buffer de triangulos cresce,
// o descritor do quadro e SEMPRE regravado. O driver pode dar ao buffer novo o mesmo handle do liberado; comparar os
// handles deixava o descritor na memoria liberada (Invalid Read). Aqui: um quadro pequeno, uma grade opaca com dezenas
// de milhares de triangulos (forca a realocacao, sem sobreposicao) e o pequeno de novo, com a mesma imagem.
namespace {
nr::CommandList grid_frame(int rows, int step) {
    nr::CommandList cl;
    cl.frame.width = 640, cl.frame.height = 480, cl.frame.clear_color = 0xFF000000;
    for (int r = 0; r < rows; ++r) {
        nr::Draw d;
        d.state.list = nr::ListType::Opaque, d.state.gouraud = false;
        d.first_vertex = uint32_t(cl.vertices.size());
        const float y0 = float(r * 480 / rows), y1 = float((r + 1) * 480 / rows);
        for (int x = 0; x <= 640; x += step)
            for (const float y : {y0, y1}) {
                nr::Vertex v;
                v.x = float(x), v.y = y, v.inv_w = 1.f, v.color = 0xFF20A040;
                cl.vertices.push_back(v);
            }
        d.vertex_count = uint32_t(cl.vertices.size()) - d.first_vertex;
        cl.draws.push_back(d);
    }
    return cl;
}
bool run_cl(nr::VulkanDevice& d, const nr::CommandList& cl, std::vector<uint32_t>& img) {
    uint32_t w = 0, h = 0;
    return d.submit(cl).ok() && d.read_frame(img, w, h).ok() && w == cl.frame.width && h == cl.frame.height;
}
} // namespace

BS_TEST(render_vulkan_triangle_buffer_growth_rewrites_descriptors) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        const nr::CommandList small = grid_frame(4, 160), big = grid_frame(120, 2);  // 40 e 76 800 triangulos
        std::vector<uint32_t> first, again, bigImg;
        BS_CHECK(run_cl(*d, small, first));
        const uint64_t re0 = d->stats().tri_reallocs, wr0 = d->stats().frame_set_writes;
        const bool     bigOk = run_cl(*d, big, bigImg);
        if (!bigOk) std::fprintf(stderr, "  quadro grande: %s\n", d->stats().last_error.c_str());
        BS_CHECK(bigOk);
        const uint64_t re = d->stats().tri_reallocs - re0, wr = d->stats().frame_set_writes - wr0;
        std::fprintf(stderr, "  realocacoes %llu, regravacoes dos descritores %llu\n", (unsigned long long)re, (unsigned long long)wr);
        BS_CHECK(re >= 1 && wr >= re);
        // a grade cobre a tela inteira com a cor dos vertices: o shader leu os dados dos triangulos novos
        bool covered = bigImg.size() == size_t(640) * 480;
        for (size_t i = 0; covered && i < bigImg.size(); ++i) covered = (bigImg[i] & 0xFFFFFF) == 0x20A040;
        BS_CHECK(covered);
        BS_CHECK(run_cl(*d, small, again) && again == first);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// N8.7: o renderizador em software sobre a IR (native/render/soft) e o Vulkan desenham as cenas fieis com as mesmas
// contas (inclusive a fog por tabela e os volumes da lista 1): as provas na tolerancia da cena e as imagens inteiras
// identicas (o modo fiel e bit a bit).
BS_TEST(render_vulkan_equals_software_device) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        int compared = 0;
        for (const auto& s : nr::fidelity_scenes()) {
            if (!s.blocked.empty() || s.mode != nr::SceneMode::Faithful || s.image) continue;
            nr::soft::SoftwareDevice  sw;
            std::vector<nr::TextureHandle> hs;
            for (const auto& t : s.textures) hs.push_back(sw.create_texture(t.desc, std::as_bytes(std::span<const uint32_t>(t.argb))).value());
            nr::CommandList cl = s.cl;
            for (size_t i = 0; i < cl.draws.size(); ++i)
                cl.draws[i].state.texture = s.draw_texture[i] >= 0 ? hs[size_t(s.draw_texture[i])] : nr::TextureHandle{};
            std::vector<uint32_t> soft, vk;
            uint32_t              w = 0, h = 0;
            BS_CHECK(sw.submit(cl).ok() && sw.read_frame(soft, w, h).ok());
            BS_CHECK(run(*d, s, vk));
            for (const auto& p : s.probes) {
                const uint32_t got = soft.empty() ? 0 : soft[size_t(p.y) * s.cl.frame.width + p.x] & 0xFFFFFF;
                if (!close(got, p.rgb, p.tolerance))
                    std::fprintf(stderr, "  cena %s (%u,%u): software %06X, esperado %06X\n", s.name.c_str(), p.x, p.y, got, p.rgb);
                BS_CHECK(close(got, p.rgb, p.tolerance));
            }
            size_t diff = 0;  // os 32 bits (o alfa tambem: DstAlpha/InvDstAlpha o leem; revisao Codex 20261028-103445)
            for (size_t i = 0; i < std::min(soft.size(), vk.size()); ++i) diff += soft[i] != vk[i];
            if (diff || soft.size() != vk.size())
                std::fprintf(stderr, "  cena %s: software x Vulkan, %zu pixels diferentes\n", s.name.c_str(), diff);
            BS_CHECK(diff == 0 && soft.size() == vk.size());
            ++compared;
        }
        std::fprintf(stderr, "  cenas comparadas %d\n", compared);
        BS_CHECK(compared >= 9);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// N8.7 (revisao Codex 20261028-115305): sem a profundidade crua, um volume que nao e aplicado (lista 3, aqui com 1/w
// extremo) nao entra no intervalo da profundidade: a imagem continua igual a do software.
BS_TEST(render_vulkan_ignored_volume_does_not_shift_depth_range) {
    _putenv_s("BS_VK_SEM_PROFUNDIDADE_CRUA", "1");
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        _putenv_s("BS_VK_SEM_PROFUNDIDADE_CRUA", "");
        if (!d) BS_SKIP("dispositivo Vulkan");
        BS_CHECK(!d->stats().raw_depth);
        const std::vector<nr::FidelityScene> scenes = nr::fidelity_scenes();
        const nr::FidelityScene*             base   = nullptr;
        for (const auto& s : scenes)
            if (s.name == "modificador_opaco") base = &s;
        BS_CHECK(base != nullptr);
        if (!base) return;
        nr::FidelityScene s = *base;
        nr::ModifierVolume m;
        m.first_vertex = uint32_t(s.cl.vertices.size()), m.triangle_count = 1, m.list = nr::ListType::TranslucentModifier;
        for (auto [x, y] : {std::pair{0.f, 0.f}, {64.f, 0.f}, {0.f, 64.f}}) s.cl.vertices.push_back({x, y, -1e30f, 0, 0, 0, 0});
        s.cl.modifiers.push_back(m);
        nr::soft::SoftwareDevice sw;
        std::vector<uint32_t>    soft, vk;
        uint32_t                 w = 0, h = 0;
        BS_CHECK(sw.submit(s.cl).ok() && sw.read_frame(soft, w, h).ok());
        BS_CHECK(run(*d, s, vk));
        BS_CHECK(soft == vk && !soft.empty());
        for (const auto& p : s.probes) BS_CHECK(close(vk[size_t(p.y) * s.cl.frame.width + p.x] & 0xFFFFFF, p.rgb, p.tolerance));
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// N9.6 fase 1 (docs/N9-6-AUDITORIA.md): a oclusao ambiente. Uma cena projetada como a do jogo (dist 500, cx 320, cy 240,
// 1/w = 1/z): o chao (y = 100) encontra a parede do fundo (z = 600) numa quina; um quadro 2D (1/w constante, o HUD) por
// cima. Desligado: a imagem e a do software (o modo fiel). Ligado: a quina escurece dos dois lados, o longe da quina fica,
// o HUD fica bit a bit e o render de outro tamanho (desenho em textura) nao recebe AO.
namespace {
nr::CommandList ao_scene(uint32_t w = 640, uint32_t h = 480) {
    nr::CommandList cl;
    cl.frame.width = w, cl.frame.height = h, cl.frame.clear_color = 0xFF000000;
    auto proj = [&](float x, float y, float z) {
        nr::Vertex v;
        v.x = float(w) / 2 + 500.f * x / z, v.y = float(h) / 2 + 500.f * y / z, v.inv_w = 1.f / z, v.color = 0xFFC0C0C0;
        return v;
    };
    auto strip = [&](std::initializer_list<nr::Vertex> vs) {
        nr::Draw d;
        d.first_vertex = uint32_t(cl.vertices.size()), d.vertex_count = uint32_t(vs.size());
        for (const auto& v : vs) cl.vertices.push_back(v);
        cl.draws.push_back(d);
    };
    strip({proj(-400, 100, 200), proj(400, 100, 200), proj(-400, 100, 580), proj(400, 100, 620)});      // chao
    // a parede um pouco inclinada (z de 580 a 620): de frente exata para a camera, o 1/w seria constante no desenho e o
    // criterio da cena (1/w variando) a deixaria fora, como o HUD (limite registrado na auditoria, secao 6)
    strip({proj(-500, -300, 580), proj(500, -300, 620), proj(-500, 100, 580), proj(500, 100, 620)});    // parede
    nr::Vertex a{20, 20, 0.01f, 0, 0, 0xFFFFFFFF, 0}, b{120, 20, 0.01f, 0, 0, 0xFFFFFFFF, 0};
    nr::Vertex c{20, 60, 0.01f, 0, 0, 0xFFFFFFFF, 0}, e{120, 60, 0.01f, 0, 0, 0xFFFFFFFF, 0};
    strip({a, b, c, e});  // o HUD: 1/w constante, na frente de tudo
    return cl;
}
double lum(uint32_t p) { return 0.299 * ((p >> 16) & 0xFF) + 0.587 * ((p >> 8) & 0xFF) + 0.114 * (p & 0xFF); }
} // namespace

BS_TEST(render_vulkan_ambient_occlusion) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        const nr::CommandList cl = ao_scene();
        nr::soft::SoftwareDevice sw;
        std::vector<uint32_t>    soft, off, on, again;
        uint32_t                 w = 0, h = 0;
        BS_CHECK(sw.submit(cl).ok() && sw.read_frame(soft, w, h).ok());
        BS_CHECK(d->submit(cl).ok() && d->read_frame(off, w, h).ok());
        BS_CHECK(off == soft && !off.empty());  // desligado = o modo fiel
        const uint64_t before = d->stats().ao_frames;
        d->set_ambient_occlusion(true);
        BS_CHECK(d->submit(cl).ok() && d->read_frame(on, w, h).ok() && w == 640 && h == 480);
        BS_CHECK(d->stats().ao_frames == before + 1);
        BS_CHECK(d->submit(cl).ok() && d->read_frame(again, w, h).ok() && again == on);  // determinismo
        if (on.size() != off.size()) return;
        const float yq = 240.f + 500.f * 100.f / 600.f;  // a quina na tela (~323)
        auto        at = [&](const std::vector<uint32_t>& img, int x, int y) { return img[size_t(y) * 640 + x]; };
        auto        ratio = [&](int x, int y) { return lum(at(on, x, y)) / std::max(1.0, lum(at(off, x, y))); };
        const int   q = int(yq);
        const double wall = ratio(320, q - 3), flo = ratio(320, q + 3), farWall = ratio(320, 60), farFloor = ratio(320, 470);
        std::fprintf(stderr, "  AO: quina parede %.3f chao %.3f; longe parede %.3f chao %.3f\n", wall, flo, farWall, farFloor);
        BS_CHECK(wall < 0.9 && flo < 0.9);           // a quina escurece dos dois lados
        BS_CHECK(farWall > 0.97 && farFloor > 0.97);   // longe dela, quase nada
        for (int y = 20; y < 60; ++y)
            for (int x = 20; x < 120; ++x) BS_CHECK(at(on, x, y) == at(off, x, y));  // o HUD fica bit a bit
        // o render de outro tamanho (o desenho em textura do jogo, 256x256) nao recebe AO
        const nr::CommandList small = ao_scene(256, 256);
        std::vector<uint32_t> s_soft, s_on;
        BS_CHECK(sw.submit(small).ok() && sw.read_frame(s_soft, w, h).ok());
        BS_CHECK(d->submit(small).ok() && d->read_frame(s_on, w, h).ok() && s_on == s_soft);
        // a parede de frente exata para a camera (1/w constante no desenho) fica fora da cena, como o HUD: o lado seguro do
        // limite registrado (revisao Codex 20261110-113738, MEDIA, adiada para a fase 2 / N9.12): nunca escurece um 2D
        nr::CommandList front = cl;
        for (uint32_t k = 0; k < 4; ++k) {
            nr::Vertex& v = front.vertices[cl.draws[1].first_vertex + k];
            v.inv_w = 1.f / 600.f;
        }
        std::vector<uint32_t> f_soft, f_on;
        BS_CHECK(sw.submit(front).ok() && sw.read_frame(f_soft, w, h).ok());
        BS_CHECK(d->submit(front).ok() && d->read_frame(f_on, w, h).ok() && f_on.size() == f_soft.size());
        if (f_on.size() == f_soft.size())
            for (int y = 0; y < int(yq) - 2; ++y) BS_CHECK(at(f_on, 320, y) == at(f_soft, 320, y));  // a parede, acima da quina
        d->set_ambient_occlusion(false);
        std::vector<uint32_t> back;
        BS_CHECK(d->submit(cl).ok() && d->read_frame(back, w, h).ok() && back == off);  // desligado de novo = o fiel
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// N9.6 fase 1: as tres qualidades (alta: o alvo inteiro, a primeira versao; media e baixa: meia resolucao com a reconstrucao
// pela profundidade). Uma parede estreita (x de -150 a 150, z de 580 a 620) na frente de um plano distante (z ~3000): a
// quina com o chao escurece nas tres; o plano distante logo ao lado da borda da parede nao recebe o AO dela (nenhum
// vazamento entre profundidades diferentes); o HUD fica bit a bit; so a media e a baixa contam como meia resolucao.
BS_TEST(render_vulkan_ambient_occlusion_quality) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        nr::CommandList cl;
        cl.frame.width = 640, cl.frame.height = 480, cl.frame.clear_color = 0xFF000000;
        auto proj = [&](float x, float y, float z) {
            nr::Vertex v;
            v.x = 320.f + 500.f * x / z, v.y = 240.f + 500.f * y / z, v.inv_w = 1.f / z, v.color = 0xFFC0C0C0;
            return v;
        };
        auto strip = [&](std::initializer_list<nr::Vertex> vs) {
            nr::Draw dr;
            dr.first_vertex = uint32_t(cl.vertices.size()), dr.vertex_count = uint32_t(vs.size());
            for (const auto& v : vs) cl.vertices.push_back(v);
            cl.draws.push_back(dr);
        };
        strip({proj(-6000, -4000, 3000), proj(6000, -4000, 3100), proj(-6000, 4000, 3000), proj(6000, 4000, 3100)});  // fundo
        strip({proj(-400, 100, 200), proj(400, 100, 200), proj(-400, 100, 580), proj(400, 100, 620)});      // chao
        strip({proj(-150, -300, 580), proj(150, -300, 620), proj(-150, 100, 580), proj(150, 100, 620)});    // parede
        nr::Vertex a{20, 20, 0.01f, 0, 0, 0xFFFFFFFF, 0}, b{120, 20, 0.01f, 0, 0, 0xFFFFFFFF, 0};
        nr::Vertex c{20, 60, 0.01f, 0, 0, 0xFFFFFFFF, 0}, e{120, 60, 0.01f, 0, 0, 0xFFFFFFFF, 0};
        strip({a, b, c, e});  // o HUD
        nr::soft::SoftwareDevice sw;
        std::vector<uint32_t>    soft, off;
        uint32_t                 w = 0, h = 0;
        BS_CHECK(sw.submit(cl).ok() && sw.read_frame(soft, w, h).ok());
        BS_CHECK(d->submit(cl).ok() && d->read_frame(off, w, h).ok() && off == soft && !off.empty());
        if (off.size() != size_t(640) * 480) return;
        auto at = [](const std::vector<uint32_t>& img, int x, int y) { return img[size_t(y) * 640 + x]; };
        const int edge = int(std::ceil(320.f + 500.f * 150.f / 620.f));  // a borda direita da parede (~441)
        for (nr::AoQuality q : {nr::AoQuality::High, nr::AoQuality::Medium, nr::AoQuality::Low}) {
            d->set_ambient_occlusion_quality(q);
            d->set_ambient_occlusion(true);
            const uint64_t half0 = d->stats().ao_half_frames;
            std::vector<uint32_t> on, again;
            BS_CHECK(d->submit(cl).ok() && d->read_frame(on, w, h).ok() && on.size() == off.size());
            BS_CHECK(d->submit(cl).ok() && d->read_frame(again, w, h).ok() && again == on);  // determinismo
            BS_CHECK(d->stats().ao_half_frames - half0 == (q == nr::AoQuality::High ? 0u : 2u));
            // revisao Codex 20261110-161728: so a imagem da qualidade escolhida fica viva (a outra sai na troca)
            BS_CHECK(d->stats().ao_image_bytes == (q == nr::AoQuality::High ? uint64_t(640) * 480 * 4 : uint64_t(320) * 240 * 16));
            if (on.size() != off.size()) continue;
            auto ratio = [&](int x, int y) { return lum(at(on, x, y)) / std::max(1.0, lum(at(off, x, y))); };
            const int    yq = int(240.f + 500.f * 100.f / 600.f);
            const double wall = ratio(320, yq - 3), flo = ratio(320, yq + 3), farFloor = ratio(320, 470);
            double       leak = 1.0;  // o fundo logo a direita da borda, da altura da parede ate a quina
            for (int y = 2; y < yq - 4; ++y)
                for (int x = edge + 2; x < edge + 8; ++x) leak = std::min(leak, ratio(x, y));
            std::fprintf(stderr, "  AO qualidade %d: quina parede %.3f chao %.3f; chao longe %.3f; fundo junto a borda %.3f\n",
                         int(q), wall, flo, farFloor, leak);
            BS_CHECK(wall < 0.9 && flo < 0.9);  // a quina escurece dos dois lados
            BS_CHECK(farFloor > 0.97);          // longe dela, quase nada
            BS_CHECK(leak > 0.97);              // o fundo nao recebe o AO da parede
            for (int y = 20; y < 60; ++y)
                for (int x = 20; x < 120; ++x) BS_CHECK(at(on, x, y) == at(off, x, y));  // o HUD fica bit a bit
            for (size_t i = 0; i < on.size(); ++i) {  // o AO so escurece
                const uint32_t p = on[i], o = off[i];
                if (((p >> 16) & 0xFF) > ((o >> 16) & 0xFF) || ((p >> 8) & 0xFF) > ((o >> 8) & 0xFF) || (p & 0xFF) > (o & 0xFF)) {
                    BS_CHECK(!"o AO clareou um pixel");
                    break;
                }
            }
        }
        d->set_ambient_occlusion(false);
        std::vector<uint32_t> back;
        BS_CHECK(d->submit(cl).ok() && d->read_frame(back, w, h).ok() && back == off);  // desligado = o fiel
        BS_CHECK(d->stats().ao_image_bytes == 0);                                         // e a memoria do AO volta
        // revisao Codex 20261110-161728: um alvo de lados impares (641 x 481, a escala 481/480): a meia resolucao
        // arredonda para cima (321 x 241) e as bordas direita e inferior nao saem do alvo nem clareiam
        d->set_scale(481.f / 480.f);
        std::vector<uint32_t> oddOff;
        BS_CHECK(d->submit(cl).ok() && d->read_frame(oddOff, w, h).ok() && w == 640 && h == 480);
        for (nr::AoQuality q : {nr::AoQuality::High, nr::AoQuality::Medium, nr::AoQuality::Low}) {
            d->set_ambient_occlusion_quality(q);
            d->set_ambient_occlusion(true);
            std::vector<uint32_t> oddOn, oddAgain;
            BS_CHECK(d->submit(cl).ok() && d->read_frame(oddOn, w, h).ok() && oddOn.size() == oddOff.size());
            BS_CHECK(d->submit(cl).ok() && d->read_frame(oddAgain, w, h).ok() && oddAgain == oddOn);
            BS_CHECK(d->stats().ao_image_bytes ==
                     (q == nr::AoQuality::High ? uint64_t(641) * 481 * 4 : uint64_t(321) * 241 * 16));
            bool lighter = false, changed = false;
            for (size_t i = 0; i < oddOn.size() && i < oddOff.size(); ++i) {
                const uint32_t p = oddOn[i], o = oddOff[i];
                lighter = lighter || ((p >> 16) & 0xFF) > ((o >> 16) & 0xFF) || ((p >> 8) & 0xFF) > ((o >> 8) & 0xFF) ||
                          (p & 0xFF) > (o & 0xFF);
                changed = changed || p != o;
            }
            BS_CHECK(!lighter && changed);
            for (int y = 22; y < 58; ++y)
                for (int x = 22; x < 118; ++x) BS_CHECK(at(oddOn, x, y) == at(oddOff, x, y));  // o HUD (na reducao, sem a borda)
        }
        d->set_ambient_occlusion(false);
        BS_CHECK(d->submit(cl).ok() && d->read_frame(back, w, h).ok() && back == oddOff && d->stats().ao_image_bytes == 0);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// N9.6 fase 2b (docs/N9-6-AUDITORIA.md secao 9): o AO sob as translucidas do catalogo de materiais, nas tres qualidades
// (revisao Codex 20261110-225319: a media e a baixa usam o passe final de meia resolucao). A quina do chao com a parede
// (ao_scene) e translucidas brancas de alfa cheio por cima: (A) um decalque cadastrado 0,5 % de 1/w acima do chao, junto a
// quina (como os racks medidos, 0,66 %); (A2) idem a 0,9 % (dentro do limite de 1 %); (D) idem a 1,2 % (fora); (B) uma
// cadastrada longe de qualquer opaco (a frente da quina); (C) um decalque NAO cadastrado no chao. Com o AO: so A e A2 mostram
// o AO do chao; B, C e D ficam como sem o material. Desligado: igual ao software.
BS_TEST(render_vulkan_ambient_occlusion_material_surface) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        nr::soft::SoftwareDevice sw;
        nr::TextureDesc          td;
        td.width = td.height = 8;
        const std::vector<uint32_t> white(64, 0xFFFFFFFFu);
        const auto                  bytes = std::as_bytes(std::span<const uint32_t>(white));
        enum { A, A2, D, B, C, N };
        nr::TextureHandle v[N], s[N];
        for (int i = 0; i < N; ++i) v[i] = d->create_texture(td, bytes).value(), s[i] = sw.create_texture(td, bytes).value();
        // o retangulo de cada uma na tela (para as contagens), na ordem de A ate C
        struct Rect { float x0, x1, z0, z1, y0, y1; };
        const Rect r[N] = {{-40, 0, 450, 595, 100, 100}, {20, 60, 450, 595, 100, 100}, {80, 120, 450, 595, 100, 100},
                           {200, 260, 300, 300, 20, 60}, {-300, -200, 450, 590, 100, 100}};
        const float lift[N] = {1.005f, 1.009f, 1.012f, 1.f, 1.f};
        auto scene = [&](const nr::TextureHandle* t) {
            nr::CommandList cl   = ao_scene();
            auto            proj = [&](float x, float y, float z, float u, float vv, float k) {
                nr::Vertex vx;
                vx.x = 320.f + 500.f * x / z, vx.y = 240.f + 500.f * y / z, vx.inv_w = k / z, vx.color = 0xFFC0C0C0;
                vx.u = u, vx.v = vv;
                return vx;
            };
            for (int i = 0; i < N; ++i) {
                nr::Draw dr;
                dr.state.list = nr::ListType::Translucent, dr.state.texture = t[i];
                dr.state.src = nr::Blend::SrcAlpha, dr.state.dst = nr::Blend::InvSrcAlpha, dr.state.use_alpha = true;
                dr.first_vertex = uint32_t(cl.vertices.size()), dr.vertex_count = 4;
                const Rect& q = r[i];
                if (i == B) {  // uma placa de frente, a z 300
                    for (auto [x, y] : {std::pair{q.x0, q.y0}, {q.x1, q.y0}, {q.x0, q.y1}, {q.x1, q.y1}})
                        cl.vertices.push_back(proj(x, y, q.z0, 0, 0, 1.f));
                } else {  // no plano do chao (y = 100), com o 1/w multiplicado por `lift`
                    for (auto [x, z] : {std::pair{q.x0, q.z0}, {q.x1, q.z0}, {q.x0, q.z1}, {q.x1, q.z1}})
                        cl.vertices.push_back(proj(x, 100, z, 0, 0, lift[i]));
                }
                cl.draws.push_back(dr);
            }
            return cl;
        };
        const nr::CommandList vk = scene(v), soft = scene(s);
        std::vector<uint32_t> s_img, off;
        uint32_t              w = 0, h = 0;
        BS_CHECK(sw.submit(soft).ok() && sw.read_frame(s_img, w, h).ok());
        auto mark = [&](bool on) {
            for (int i : {A, A2, D, B}) d->set_texture_material(v[i], on ? nr::kMaterialAoSurface : 0);
        };
        mark(true);
        BS_CHECK(d->submit(vk).ok() && d->read_frame(off, w, h).ok() && off == s_img && !off.empty());  // desligado: o fiel
        if (off.size() != size_t(640) * 480) return;
        auto at = [](const std::vector<uint32_t>& img, int x, int y) { return img[size_t(y) * 640 + x]; };
        auto px = [](float x, float y, float z) { return std::pair{int(320.f + 500.f * x / z), int(240.f + 500.f * y / z)}; };
        auto box = [&](int i) {  // o retangulo na tela, um pixel para dentro
            const Rect& q = r[i];
            const auto [ax, ay] = px(q.x0, i == B ? q.y0 : 100.f, q.z0);
            const auto [bx, by] = px(q.x1, i == B ? q.y1 : 100.f, i == B ? q.z0 : q.z1);
            return std::array<int, 4>{std::min(ax, bx) + 1, std::max(ax, bx) - 1, std::min(ay, by) + 1, std::max(ay, by) - 1};
        };
        for (nr::AoQuality qa : {nr::AoQuality::High, nr::AoQuality::Medium, nr::AoQuality::Low}) {
            d->set_ambient_occlusion_quality(qa);
            d->set_ambient_occlusion(true);
            std::vector<uint32_t> nomat, mat, again;
            mark(false);
            const uint64_t f0 = d->stats().ao_surface_frames;
            BS_CHECK(d->submit(vk).ok() && d->read_frame(nomat, w, h).ok());  // o AO sem nenhum material: o de antes
            BS_CHECK(d->stats().ao_surface_frames == f0);
            mark(true);
            BS_CHECK(d->submit(vk).ok() && d->read_frame(mat, w, h).ok() && mat.size() == nomat.size());
            BS_CHECK(d->submit(vk).ok() && d->read_frame(again, w, h).ok() && again == mat);  // determinismo
            BS_CHECK(d->stats().ao_surface_frames == f0 + 2);
            const uint64_t aoBytes = qa == nr::AoQuality::High ? uint64_t(640) * 480 * 4 : uint64_t(320) * 240 * 16;
            BS_CHECK(d->stats().ao_image_bytes == aoBytes + uint64_t(640) * 480 * 4);  // + a imagem do catalogo
            if (mat.size() != nomat.size()) continue;
            // A e A2 junto a quina: escurecem (o AO do chao aparece); longe da quina, quase nada
            auto ratio = [&](float x, float z) {
                const auto [xx, yy] = px(x, 100, z);
                return lum(at(mat, xx, yy)) / std::max(1.0, lum(at(nomat, xx, yy)));
            };
            const double nearA = ratio(-20, 585), nearA2 = ratio(40, 585), farA = ratio(-20, 460);
            std::fprintf(stderr, "  AO qualidade %d sob a translucida do catalogo: A junto a quina %.3f, A2 %.3f, A longe %.3f\n",
                         int(qa), nearA, nearA2, farA);
            BS_CHECK(nearA < 0.95 && nearA2 < 0.95 && farA > 0.97);
            bool lighter = false;
            int  diff[N] = {}, total = 0;
            for (int y = 0; y < 480; ++y)
                for (int x = 0; x < 640; ++x) {
                    const uint32_t p = at(mat, x, y), o = at(nomat, x, y);
                    lighter = lighter || ((p >> 16) & 0xFF) > ((o >> 16) & 0xFF) || ((p >> 8) & 0xFF) > ((o >> 8) & 0xFF) ||
                              (p & 0xFF) > (o & 0xFF);
                    total += p != o;
                    for (int i = 0; i < N; ++i) {
                        const auto b = box(i);
                        diff[i] += p != o && x >= b[0] && x <= b[1] && y >= b[2] && y <= b[3];
                    }
                }
            BS_CHECK(!lighter && total > 0 && diff[A] > 0 && diff[A2] > 0);
            BS_CHECK(diff[D] == 0);  // a 1,2 % de 1/w: fora do limite, como antes
            BS_CHECK(diff[B] == 0);  // cadastrada, mas longe de qualquer opaco: como antes
            BS_CHECK(diff[C] == 0);  // nao cadastrada: como antes
            for (int y = 20; y < 60; ++y)
                for (int x = 20; x < 120; ++x) BS_CHECK(at(mat, x, y) == at(off, x, y));  // o HUD fica bit a bit
        }
        d->set_ambient_occlusion(false);
        std::vector<uint32_t> back;
        BS_CHECK(d->submit(vk).ok() && d->read_frame(back, w, h).ok() && back == s_img);  // desligado de novo = o fiel
        BS_CHECK(d->stats().ao_image_bytes == 0);
        for (nr::TextureHandle t : v) d->destroy_texture(t);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// N9.6 fase 2b: o catalogo de materiais por TextureId (so identidades medidas, sem curinga)
BS_TEST(material_catalog_lookup) {
    BS_CHECK(!nr::material_table().empty());
    for (const nr::MaterialEntry& e : nr::material_table()) {
        BS_CHECK(nr::material_flags(e.id) == e.flags && e.flags != 0 && e.what && *e.what);
        BS_CHECK(nr::parse_texture_id(e.id.text()) == e.id);
        int same = 0;
        for (const nr::MaterialEntry& f : nr::material_table()) same += f.id == e.id;
        BS_CHECK(same == 1);  // uma linha por textura
    }
    BS_CHECK(nr::material_flags(nr::TextureId{128, 128, 0x743d83eda77f1bb8ull}) == nr::kMaterialAoSurface);
    BS_CHECK(nr::material_flags(nr::TextureId{128, 128, 0x743d83eda77f1bb9ull}) == 0);  // um bit do conteudo
    BS_CHECK(nr::material_flags(nr::TextureId{64, 256, 0x743d83eda77f1bb8ull}) == 0);   // outra forma, o mesmo hash
    BS_CHECK(nr::material_flags(nr::TextureId{128, 128, 0x563c7fcc146c1f60ull}) == 0);  // o piso de grade: medido e recusado
    BS_CHECK(nr::material_flags(nr::TextureId{}) == 0);
}

// N9.6 fase 3 (docs/N9-6-AUDITORIA.md secao 10): a luz das fontes do catalogo. A quina (ao_scene) com duas partes de uma
// lampada cadastrada (kMaterialEmissive) perto do chao, a direita (x 300, y 60, z 560), e uma placa NAO cadastrada a esquerda.
// Ligada: o chao junto da lampada clareia; a parede longe (fora do raio) e o HUD ficam bit a bit; as duas partes viram uma luz
// so; a placa sem cadastro nao acende nada. Desligada: igual ao software.
BS_TEST(render_vulkan_lighting_emissive) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        nr::soft::SoftwareDevice sw;
        nr::TextureDesc          td;
        td.width = td.height = 8;
        const std::vector<uint32_t> white(64, 0xFFFFFFFFu);
        const auto                  bytes = std::as_bytes(std::span<const uint32_t>(white));
        const nr::TextureHandle vL = d->create_texture(td, bytes).value(), vU = d->create_texture(td, bytes).value();
        const nr::TextureHandle sL = sw.create_texture(td, bytes).value(), sU = sw.create_texture(td, bytes).value();
        auto scene = [&](nr::TextureHandle lamp, nr::TextureHandle plain) {
            nr::CommandList cl   = ao_scene();
            auto            proj = [&](float x, float y, float z) {
                nr::Vertex vx;
                vx.x = 320.f + 500.f * x / z, vx.y = 240.f + 500.f * y / z, vx.inv_w = 1.f / z, vx.color = 0xFFFFFFFF;
                return vx;
            };
            auto quad = [&](nr::TextureHandle t, float x, float y, float z, float r) {
                nr::Draw dr;
                dr.state.list = nr::ListType::Translucent, dr.state.texture = t;
                dr.state.src = nr::Blend::SrcAlpha, dr.state.dst = nr::Blend::InvSrcAlpha, dr.state.use_alpha = true;
                dr.first_vertex = uint32_t(cl.vertices.size()), dr.vertex_count = 4;
                for (auto [a, b] : {std::pair{-r, -r}, {r, -r}, {-r, r}, {r, r}}) cl.vertices.push_back(proj(x + a, y + b, z));
                cl.draws.push_back(dr);
            };
            quad(lamp, 295, 60, 560, 6), quad(lamp, 305, 60, 560, 6);  // duas partes da mesma lampada
            quad(plain, -200, -150, 560, 6);                           // uma placa sem cadastro
            return cl;
        };
        const nr::CommandList vk = scene(vL, vU), soft = scene(sL, sU);
        std::vector<uint32_t> s_img, off, on, again;
        uint32_t              w = 0, h = 0;
        BS_CHECK(sw.submit(soft).ok() && sw.read_frame(s_img, w, h).ok());
        d->set_texture_material(vL, nr::kMaterialEmissive), d->set_texture_light(vL, 0xFFFFFF);
        BS_CHECK(d->submit(vk).ok() && d->read_frame(off, w, h).ok() && off == s_img && !off.empty());  // desligada: o fiel
        const uint64_t f0 = d->stats().light_frames;
        d->set_lighting(true);
        BS_CHECK(d->submit(vk).ok() && d->read_frame(on, w, h).ok() && on.size() == off.size());
        BS_CHECK(d->submit(vk).ok() && d->read_frame(again, w, h).ok() && again == on);  // determinismo
        BS_CHECK(d->stats().light_frames == f0 + 2 && d->stats().lights_max == 1);    // as duas partes: uma luz
        if (on.size() != size_t(640) * 480) return;
        auto at  = [](const std::vector<uint32_t>& img, int x, int y) { return img[size_t(y) * 640 + x]; };
        auto px  = [](float x, float y, float z) { return std::pair{int(320.f + 500.f * x / z), int(240.f + 500.f * y / z)}; };
        const auto [nx, ny] = px(250, 100, 540);   // o chao junto da lampada
        const auto [fx, fy] = px(-250, -200, 580);  // a parede longe (fora do raio de 400)
        const double nearR = lum(at(on, nx, ny)) / std::max(1.0, lum(at(off, nx, ny)));
        std::fprintf(stderr, "  luz das fontes: chao junto da lampada %.3f\n", nearR);
        BS_CHECK(nearR > 1.05);
        BS_CHECK(at(on, fx, fy) == at(off, fx, fy));
        bool darker = false;
        for (size_t i = 0; i < on.size(); ++i) {
            const uint32_t p = on[i], o = off[i];
            darker = darker || ((p >> 16) & 0xFF) < ((o >> 16) & 0xFF) || ((p >> 8) & 0xFF) < ((o >> 8) & 0xFF) || (p & 0xFF) < (o & 0xFF);
        }
        BS_CHECK(!darker);  // a luz so soma
        for (int y = 20; y < 60; ++y)
            for (int x = 20; x < 120; ++x) BS_CHECK(at(on, x, y) == at(off, x, y));  // o HUD fica bit a bit
        // a placa sem cadastro: sem a lampada no quadro, nenhuma luz
        d->set_texture_material(vL, 0);
        std::vector<uint32_t> none;
        const uint64_t        f1 = d->stats().light_frames;
        BS_CHECK(d->submit(vk).ok() && d->read_frame(none, w, h).ok() && none == off && d->stats().light_frames == f1);
        d->set_texture_material(vL, nr::kMaterialEmissive);
        d->set_lighting(false);
        std::vector<uint32_t> back;
        BS_CHECK(d->submit(vk).ok() && d->read_frame(back, w, h).ok() && back == s_img);  // desligada de novo = o fiel
        for (nr::TextureHandle t : {vL, vU}) d->destroy_texture(t);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// N9.6 fase 3: a cor da luz de uma fonte, pelos texels claros e visiveis
BS_TEST(material_emissive_color) {
    BS_CHECK(nr::emissive_color(std::vector<uint32_t>(16, 0xFFFFFFFFu)) == 0xFFFFFF);
    BS_CHECK(nr::emissive_color(std::vector<uint32_t>(16, 0xFF800000u)) == 0xFF0000);  // o tom, normalizado
    BS_CHECK(nr::emissive_color(std::vector<uint32_t>(16, 0x00FF0000u)) == 0xFFFFFF);  // alfa zero: sem brilho, branco
    BS_CHECK(nr::emissive_color(std::vector<uint32_t>{}) == 0xFFFFFF);
    // os escuros pesam pouco: muito preto e um pouco de azul claro dao azul
    std::vector<uint32_t> t(64, 0xFF000000u);
    for (int i = 0; i < 8; ++i) t[i] = 0xFF4080FFu;
    BS_CHECK(nr::emissive_color(t) == 0x4080FF);
    int fontes = 0;
    for (const nr::MaterialEntry& e : nr::material_table()) fontes += (e.flags & nr::kMaterialEmissive) != 0;
    BS_CHECK(fontes == 4);
}
