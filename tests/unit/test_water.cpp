// N9.7 (docs/N9-7-PLANO.md): a agua aprimorada da Remastered. A identificacao e a do catalogo de materiais (kMaterialWater,
// por TextureId); o efeito e so da variante -DWATER do fragmento no Vulkan. Sem a qualidade, sem o material ou num desenho
// que nao e translucido, a imagem e a do modo fiel (bit a bit igual ao software na escala 1).
#include "test.h"

#include "render/material_catalog.h"
#include "render/soft/soft_device.h"
#include "render/vulkan/vk_device.h"
#include "render/water.h"

#include <cstring>
#include <string>
#include <tuple>
#include <vector>

namespace nr = bs::native::render;

namespace {
constexpr nr::TextureId kSea{32, 32, 0x7458e3539d31cd8dull};  // o mar do cais RCG00 (o prototipo do Codex)

// a cena de teste: o fundo opaco em gradiente (a "cena" que a agua reflete) e o mar translucido da linha 240 para baixo
nr::CommandList water_scene(uint32_t w, nr::TextureHandle sea, bool autosort = false, bool opaque_sea = false) {
    nr::CommandList cl;
    cl.frame.width = w, cl.frame.height = 480, cl.frame.clear_color = 0xFF000000u, cl.frame.translucent_autosort = autosort;
    const float W = float(w);
    nr::Draw bg;
    bg.first_vertex = 0, bg.vertex_count = 4, bg.state.gouraud = true;
    for (auto [x, y, c] : {std::tuple{0.f, 0.f, 0xFF203040u}, {W, 0.f, 0xFFC08040u}, {0.f, 480.f, 0xFF40A060u}, {W, 480.f, 0xFF8030C0u}})
        cl.vertices.push_back({x, y, 0.5f, 0, 0, c, 0});
    cl.draws.push_back(bg);
    nr::Draw s;
    s.first_vertex = 4, s.vertex_count = 4, s.state.texture = sea;
    if (!opaque_sea) {
        s.state.list = nr::ListType::Translucent;
        s.state.src = nr::Blend::SrcAlpha, s.state.dst = nr::Blend::InvSrcAlpha, s.state.use_alpha = true;
        s.state.env = nr::TexEnv::ModulateAlpha, s.state.depth_write = false;
    }
    s.state.depth = nr::DepthTest::Always;
    for (auto [x, y, u, v] : {std::tuple{0.f, 240.f, 0.f, 0.f}, {W, 240.f, 6.f, 0.f}, {0.f, 480.f, 0.f, 4.f}, {W, 480.f, 6.f, 4.f}})
        cl.vertices.push_back({x, y, 0.6f, u, v, 0xC0FFFFFFu, 0});
    cl.draws.push_back(s);
    return cl;
}

std::vector<uint32_t> sea_texels() {
    std::vector<uint32_t> t(64);
    for (uint32_t i = 0; i < 64; ++i) t[i] = 0xFF000000u | ((i * 3u) << 16) | ((80u + i) << 8) | (160u + i);
    return t;
}

struct Shot {
    std::vector<uint32_t> img;
    bool                  ok = false;
};
Shot shoot(nr::VulkanDevice& d, const nr::CommandList& cl) {
    Shot     s;
    uint32_t w = 0, h = 0;
    s.ok = d.submit(cl).ok() && d.read_frame(s.img, w, h).ok() && w == cl.frame.width && h == cl.frame.height;
    return s;
}
// pixels diferentes acima e a partir da linha 240 (a superficie)
std::pair<size_t, size_t> diff_rows(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, uint32_t w) {
    size_t above = 0, water = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i] != b[i]) (i / w >= 240 ? water : above)++;
    return {above, water};
}

struct Cfg {
    uint32_t    w;
    float       scale;
    bool        autosort;
    const char* name;
};
constexpr Cfg kCfgs[] = {{640, 1.f, false, "640 s1"}, {640, 1.f, true, "640 s1 autosort"}, {640, 2.f, false, "640 s2"},
                         {854, 1.f, false, "854 s1"}};
} // namespace

// A1/A5: so as oito texturas medidas da animacao do mar sao agua no catalogo; vizinhos de hash e de forma nao; nenhuma
// linha mistura a agua com outro material; as tres texturas 32x32 que aparecem junto com o mar no cais nao sao agua
BS_TEST(water_catalog_identifies_only_the_measured_sea) {
    BS_CHECK(nr::material_flags(kSea) == nr::kMaterialWater);
    BS_CHECK(nr::material_flags({32, 32, 0x7458e3539d31cd8cull}) == 0);  // um bit do conteudo
    BS_CHECK(nr::material_flags({64, 16, 0x7458e3539d31cd8dull}) == 0);  // outra forma, o mesmo hash
    BS_CHECK(nr::material_flags({}) == 0);
    constexpr uint64_t kFrames[] = {0xcdbab87a1d8d396dull, 0x7458e3539d31cd8dull, 0x261eb2b953a38814ull, 0x2b81e35cd4ec3972ull,
                                    0x6737c065625ed694ull, 0x8fdc859527593d4bull, 0x0de910958b4effafull, 0x5ffc9e41aef13e20ull};
    for (uint64_t h : kFrames) BS_CHECK(nr::material_flags({32, 32, h}) == nr::kMaterialWater);
    for (uint64_t h : {0xe19fbff0ce31792aull, 0x525a701d686a76a5ull, 0x2606c337ad632f8dull})
        BS_CHECK(nr::material_flags({32, 32, h}) == 0);
    size_t water = 0;
    for (const nr::MaterialEntry& e : nr::material_table())
        if (e.flags & nr::kMaterialWater) {
            ++water;
            BS_CHECK(e.flags == nr::kMaterialWater && e.id.width == 32 && e.id.height == 32);
        }
    BS_CHECK(water == std::size(kFrames));
}

// O horizonte e o fundo so dos desenhos de agua; desenhos com vertices fora da lista ficam de fora; sem agua, false
BS_TEST(water_extent_only_water_draws) {
    nr::CommandList cl;
    cl.vertices = {{0, 50, 1}, {9, 60, 1}, {0, 70, 1}, {0, 200, 1}, {9, 300, 1}, {0, 250, 1}};
    nr::Draw a, b, bad;
    a.first_vertex = 0, a.vertex_count = 3, a.state.texture.value = 1;
    b.first_vertex = 3, b.vertex_count = 3, b.state.texture.value = 2;
    bad.first_vertex = 5, bad.vertex_count = 4, bad.state.texture.value = 2;  // passa do fim da lista
    cl.draws = {a, b, bad};
    float top = 0, bottom = 0;
    BS_CHECK(nr::water_extent(cl, [](const nr::Draw& d) { return d.state.texture.value == 2; }, top, bottom));
    BS_CHECK(top == 200.f && bottom == 300.f);
    BS_CHECK(!nr::water_extent(cl, [](const nr::Draw&) { return false; }, top, bottom));
    BS_CHECK(nr::water_phase(nr::kWaterPhasePeriod + 7) == nr::water_phase(7) && nr::water_phase(7) == 7.f);
}

// A2/A4: a agua no Vulkan, nos arranjos do jogo (640 na escala 1, com e sem autosort; escala 2; o quadro largo de 854).
// Desligada (qualidade original), sem o material, ou num desenho opaco: a imagem do modo fiel e nenhuma imagem da cena.
// Ligada: so a superficie muda; aprimorada e alta diferem; a mesma fase repete o quadro, outra fase move as ondas; desligada
// de novo, a imagem de antes e a memoria devolvida.
BS_TEST(render_vulkan_water_material) {
    const std::vector<uint32_t> tex = sea_texels();
    nr::TextureDesc             td;
    td.width = td.height = 8;
    const auto bytes = std::as_bytes(std::span<const uint32_t>(tex));
    for (const Cfg& cfg : kCfgs) {
        std::string err;
        {
            auto d = nr::VulkanDevice::create(&err);
            if (!d) BS_SKIP("dispositivo Vulkan");
            d->set_scale(cfg.scale);
            const nr::TextureHandle sea = d->create_texture(td, bytes).value();
            const nr::CommandList   cl  = water_scene(cfg.w, sea, cfg.autosort);
            const Shot              off = shoot(*d, cl);
            BS_CHECK(off.ok);
            // o modo fiel: igual ao software no autosort (o caminho do jogo). Na translucida por ordem o Vulkan ja diferia do
            // software nesta cena antes do N9.7 (8000 pixels no master f690ecb, conferido em 2026-11-11; registrado para o
            // N9.12): ali a prova e a do proprio Vulkan, a agua desligada igual a sem o material
            if (cfg.scale == 1.f && cfg.autosort) {
                nr::soft::SoftwareDevice sw;
                const nr::TextureHandle  ss = sw.create_texture(td, bytes).value();
                std::vector<uint32_t>    s;
                uint32_t                 w = 0, h = 0;
                BS_CHECK(sw.submit(water_scene(cfg.w, ss, cfg.autosort)).ok() && sw.read_frame(s, w, h).ok() && s == off.img);
            }
            d->set_water(nr::WaterQuality::High, 100);  // a qualidade sem o material: nada
            BS_CHECK(shoot(*d, cl).img == off.img && d->stats().water_frames == 0 && d->stats().water_scene_bytes == 0);
            d->set_texture_material(sea, nr::kMaterialWater);
            d->set_water(nr::WaterQuality::Original, 100);  // o material com a agua original: nada
            BS_CHECK(shoot(*d, cl).img == off.img && d->stats().water_frames == 0 && d->stats().water_scene_bytes == 0);
            const Shot opq = shoot(*d, water_scene(cfg.w, sea, cfg.autosort, true));  // o mesmo material num desenho opaco
            d->set_water(nr::WaterQuality::High, 100);
            BS_CHECK(opq.ok && shoot(*d, water_scene(cfg.w, sea, cfg.autosort, true)).img == opq.img);  // nada
            BS_CHECK(d->stats().water_frames == 0 && d->stats().water_draws == 0 && d->stats().water_scene_bytes == 0);

            d->set_water(nr::WaterQuality::Enhanced, 100);
            const Shot enh = shoot(*d, cl);
            const uint64_t tw = uint64_t(cfg.w * cfg.scale + 0.5f), th = uint64_t(480 * cfg.scale + 0.5f);
            BS_CHECK(enh.ok && d->stats().water_frames == 1 && d->stats().water_draws == 1);
            BS_CHECK(d->stats().water_scene_bytes == tw * th * 4);  // a imagem da cena so agora, do tamanho do alvo
            const auto [above, water] = diff_rows(off.img, enh.img, cfg.w);
            std::fprintf(stderr, "  agua (%s): %zu pixels da superficie mudaram, %zu acima\n", cfg.name, water, above);
            BS_CHECK(above == 0 && water > size_t(cfg.w) * 240 / 4);
            BS_CHECK(shoot(*d, cl).img == enh.img);  // a mesma fase: o mesmo quadro
            d->set_water(nr::WaterQuality::Enhanced, 160);
            const Shot later = shoot(*d, cl);
            BS_CHECK(later.img != enh.img && diff_rows(enh.img, later.img, cfg.w).first == 0);  // as ondas andam, so na agua
            d->set_water(nr::WaterQuality::High, 100);
            const Shot high = shoot(*d, cl);
            BS_CHECK(high.img != enh.img && diff_rows(off.img, high.img, cfg.w).first == 0);
            d->set_water(nr::WaterQuality::Original, 100);  // desligada de novo
            BS_CHECK(shoot(*d, cl).img == off.img && d->stats().water_scene_bytes == 0);
            d->set_texture_material(sea, 0);  // fora do catalogo: nada, mesmo com a alta
            d->set_water(nr::WaterQuality::High, 100);
            BS_CHECK(shoot(*d, cl).img == off.img && d->stats().water_scene_bytes == 0);
            d->destroy_texture(sea);
        }
        BS_CHECK(nr::vulkan_live_objects() == 0);
    }
}

// A2/A4: a troca de arranjo no MESMO dispositivo com a agua ligada (a imagem da cena recriada com o alvo) e a recriacao do
// dispositivo (a recuperacao do Vulkan): com a mesma fase, o mesmo quadro de um dispositivo novo, sem salto nas ondas
BS_TEST(render_vulkan_water_target_switch_and_recovery) {
    const std::vector<uint32_t> tex = sea_texels();
    nr::TextureDesc             td;
    td.width = td.height = 8;
    const auto bytes = std::as_bytes(std::span<const uint32_t>(tex));
    auto       fresh = [&](const Cfg& cfg, uint64_t phase, std::vector<uint32_t>& out) {
        std::string err;
        auto        d = nr::VulkanDevice::create(&err);
        if (!d) return false;
        d->set_scale(cfg.scale);
        const nr::TextureHandle sea = d->create_texture(td, bytes).value();
        d->set_texture_material(sea, nr::kMaterialWater), d->set_water(nr::WaterQuality::High, phase);
        out = shoot(*d, water_scene(cfg.w, sea, cfg.autosort)).img;
        d->destroy_texture(sea);
        return true;
    };
    const Cfg seq[] = {kCfgs[0], kCfgs[2], kCfgs[3], kCfgs[1], kCfgs[0]};
    std::vector<std::vector<uint32_t>> ref;
    for (size_t i = 0; i < std::size(seq); ++i) {
        std::vector<uint32_t> r;
        if (!fresh(seq[i], 500 + i, r)) BS_SKIP("dispositivo Vulkan");
        ref.push_back(r);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
    {
        std::string err;
        auto        d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        const nr::TextureHandle sea = d->create_texture(td, bytes).value();
        d->set_texture_material(sea, nr::kMaterialWater);
        for (size_t i = 0; i < std::size(seq); ++i) {
            d->set_scale(seq[i].scale), d->set_water(nr::WaterQuality::High, 500 + i);
            const uint64_t f0 = d->stats().water_frames;
            const Shot     s  = shoot(*d, water_scene(seq[i].w, sea, seq[i].autosort));
            const bool     same = s.img == ref[i];
            std::fprintf(stderr, "  agua, troca de arranjo %zu (%s): %s ao dispositivo novo\n", i, seq[i].name, same ? "igual" : "DIFERENTE");
            BS_CHECK(s.ok && same && d->stats().water_frames == f0 + 1);
        }
        d->destroy_texture(sea);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// A2/A4: a agua junto com o AO, a luz e as sombras do N9.6: so a superficie muda em relacao aos tres sem a agua, e o quadro
// se repete
BS_TEST(render_vulkan_water_with_lighting_effects) {
    const std::vector<uint32_t> tex = sea_texels();
    nr::TextureDesc             td;
    td.width = td.height = 8;
    const auto  bytes = std::as_bytes(std::span<const uint32_t>(tex));
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        const nr::TextureHandle sea = d->create_texture(td, bytes).value();
        d->set_texture_material(sea, nr::kMaterialWater);
        const nr::CommandList cl = water_scene(640, sea);
        d->set_ambient_occlusion(true), d->set_lighting(true), d->set_shadows(true);
        const Shot fx = shoot(*d, cl);
        d->set_water(nr::WaterQuality::High, 42);
        const Shot all = shoot(*d, cl), again = shoot(*d, cl);
        BS_CHECK(fx.ok && all.ok && all.img == again.img && d->stats().water_frames == 2);
        const auto [above, water] = diff_rows(fx.img, all.img, 640);
        BS_CHECK(above == 0 && water > 0);
        d->set_ambient_occlusion(false), d->set_lighting(false), d->set_shadows(false);
        d->set_water(nr::WaterQuality::Original, 42);
        BS_CHECK(shoot(*d, cl).ok && d->stats().water_scene_bytes == 0);
        d->destroy_texture(sea);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}
