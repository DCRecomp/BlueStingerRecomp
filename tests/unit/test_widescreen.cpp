// N9.5 fase 1 (docs/N9-5-AUDITORIA.md, secao 12): o quadro largo da Remastered por translacao da IR, as areas da janela,
// a politica (so a Remastered, so com o Vulkan) e a regra da proporcao nas configuracoes.
#include "test.h"

#include "platform/edition.h"
#include "platform/graphics_settings.h"
#include "render/frame_prep.h"
#include "render/soft/soft_device.h"
#include "render/wide_frame.h"

#include <vector>

namespace nr = bs::native::render;
namespace ed = bs::native::edition;
namespace st = bs::native::settings;

namespace {
nr::CommandList quadro_teste() {
    nr::CommandList cl;  // 640x480: um triangulo 3D, um quad 2D com recorte "dentro" e um volume modificador
    cl.frame.width = 640, cl.frame.height = 480, cl.frame.clear_color = 0xFF102030;
    auto v = [](float x, float y, uint32_t c) {
        nr::Vertex r;
        r.x = x, r.y = y, r.inv_w = 0.5f, r.color = c;
        return r;
    };
    cl.vertices = {v(10, 10, 0xFFFF0000), v(600, 40, 0xFF00FF00), v(300, 470, 0xFF0000FF),     // triangulo
                   v(-50, 100, 0xFFFFFFFF), v(700, 100, 0xFFFFFFFF), v(-50, 200, 0xFFFFFFFF),  // quad (strip)
                   v(700, 200, 0xFFFFFFFF), v(20, 20, 0), v(80, 20, 0), v(20, 80, 0)};          // volume
    nr::Draw tri;
    tri.first_vertex = 0, tri.vertex_count = 3, tri.state.depth = nr::DepthTest::Always;
    nr::Draw quad;
    quad.first_vertex = 3, quad.vertex_count = 4, quad.state.depth = nr::DepthTest::Always;
    quad.state.list = nr::ListType::Translucent, quad.state.src = nr::Blend::SrcAlpha, quad.state.dst = nr::Blend::InvSrcAlpha;
    quad.state.clip = nr::Clip::Inside, quad.state.clip_x0 = 100, quad.state.clip_x1 = 539, quad.state.clip_y0 = 0,
    quad.state.clip_y1 = 479;
    cl.draws = {tri, quad};
    cl.vertices[4].inv_w = cl.vertices[6].inv_w = 0.4f;  // o quad em perspectiva (3D), nao uma faixa uniforme
    nr::ModifierVolume mv;
    mv.first_vertex = 7, mv.triangle_count = 1;
    cl.modifiers = {mv};
    return cl;
}

std::vector<uint32_t> desenhar(const nr::CommandList& cl) {
    nr::soft::SoftwareDevice d;
    std::vector<uint32_t>    img;
    uint32_t                 w = 0, h = 0;
    if (!d.submit(cl) || !d.read_frame(img, w, h)) return {};
    return img;
}
} // namespace

// A translacao: todo vertice (desenhos e volumes) e todo recorte andam (854 - 640) / 2 = 107, e a largura vira 854. Nada
// muda sem largura maior.
BS_TEST(wide_frame_translates_the_ir) {
    BS_CHECK(nr::kWideWidth == 854 && nr::wide_offset(640, 854) == 107 && nr::wide_offset(640, 640) == 0 &&
             nr::wide_offset(640, 320) == 0);
    const nr::CommandList a = quadro_teste();
    nr::CommandList       b = a;
    nr::widen(b, nr::kWideWidth);
    BS_CHECK(b.frame.width == 854 && b.frame.height == 480 && b.vertices.size() == a.vertices.size());
    bool shifted = true;
    for (size_t i = 0; i < a.vertices.size(); ++i)
        shifted = shifted && b.vertices[i].x == a.vertices[i].x + 107.f && b.vertices[i].y == a.vertices[i].y &&
                  b.vertices[i].inv_w == a.vertices[i].inv_w && b.vertices[i].color == a.vertices[i].color;
    BS_CHECK(shifted);
    BS_CHECK(b.draws[1].state.clip_x0 == 207 && b.draws[1].state.clip_x1 == 646 && b.draws[1].state.clip_y0 == 0 &&
             b.draws[1].state.clip_y1 == 479);
    BS_CHECK(b.draws[0].state.clip == nr::Clip::None && b.draws[0].state.clip_x0 == 0);  // sem recorte: os campos ficam
    nr::CommandList c = a;
    nr::widen(c, 640);
    BS_CHECK(c.frame.width == 640 && c.vertices[0].x == a.vertices[0].x);
}

// O que o 4:3 desenha e o centro do largo, e as laterais recebem a geometria que no 4:3 saia da tela (o quad que vai de
// -50 a 700). Na referencia em software o recorte central e igual pixel a pixel nesta cena (coordenadas inteiras).
BS_TEST(wide_frame_center_is_the_4x3_and_sides_get_geometry) {
    const nr::CommandList a = quadro_teste();
    nr::CommandList       b = a;
    nr::widen(b, nr::kWideWidth);
    const std::vector<uint32_t> img43 = desenhar(a), img169 = desenhar(b);
    BS_CHECK(img43.size() == 640u * 480 && img169.size() == 854u * 480);
    if (img43.size() != 640u * 480 || img169.size() != 854u * 480) return;
    std::vector<uint32_t> centro(640u * 480);
    nr::crop_center(img169.data(), 854, 480, centro.data(), 640);
    BS_CHECK(centro == img43);
    // a faixa esquerda (x 0..106) tem o triangulo? nao: ele comeca em 10+107. Tem o fundo; e o quad com recorte so dentro
    // de [207, 646] -> a faixa e so fundo. A linha y=50 de um quad sem recorte entra nas faixas:
    nr::CommandList d = a;
    d.draws[1].state.clip = nr::Clip::None;
    nr::CommandList e = d;
    nr::widen(e, nr::kWideWidth);
    const std::vector<uint32_t> semRecorte = desenhar(e);
    BS_CHECK(semRecorte.size() == 854u * 480);
    if (semRecorte.size() == 854u * 480) {
        const uint32_t fundo = 0xFF102030;
        BS_CHECK(semRecorte[150u * 854 + 60] != fundo && semRecorte[150u * 854 + 800] != fundo);  // o quad nas duas faixas
        BS_CHECK(img169[150u * 854 + 60] == fundo);  // com o recorte "dentro", nao
    }
}

// Os fades de tela cheia (medidos na rota de combate: lista translucida, 4 vertices nos cantos, sem textura, C0000000)
// cobrem o quadro largo inteiro; uma imagem (textura), um gradiente ou um retangulo que nao cobre tudo so andam.
BS_TEST(wide_frame_extends_only_uniform_full_screen_fills) {
    auto fade = [](uint32_t c0, uint32_t c3, bool tex, float x1) {
        nr::CommandList cl;
        cl.frame.width = 640, cl.frame.height = 480;
        auto v = [](float x, float y, uint32_t c) {
            nr::Vertex r;
            r.x = x, r.y = y, r.inv_w = 0.25f, r.color = c;
            return r;
        };
        cl.vertices = {v(0, 0, c0), v(0, 480, c0), v(x1, 0, c0), v(x1, 480, c3)};
        nr::Draw d;
        d.first_vertex = 0, d.vertex_count = 4, d.state.list = nr::ListType::Translucent;
        if (tex) d.state.texture.value = 1;
        cl.draws = {d};
        return cl;
    };
    nr::CommandList f = fade(0xC0000000u, 0xC0000000u, false, 640);
    BS_CHECK(nr::widen(f, 854) == 1);
    BS_CHECK(f.vertices[0].x == 0 && f.vertices[1].x == 0 && f.vertices[2].x == 854 && f.vertices[3].x == 854);
    BS_CHECK(f.vertices[0].y == 0 && f.vertices[3].y == 480);
    nr::CommandList img = fade(0xFFFFFFFFu, 0xFFFFFFFFu, true, 640);  // uma imagem de tela cheia: nao estica
    BS_CHECK(nr::widen(img, 854) == 0 && img.vertices[0].x == 107 && img.vertices[2].x == 747);
    nr::CommandList grad = fade(0xC0000000u, 0xC0FFFFFFu, false, 640);  // gradiente: nao estica
    BS_CHECK(nr::widen(grad, 854) == 0 && grad.vertices[2].x == 747);
    nr::CommandList part = fade(0xC0000000u, 0xC0000000u, false, 600);  // nao cobre a largura: so anda
    BS_CHECK(nr::widen(part, 854) == 0 && part.vertices[2].x == 707);
    nr::CommandList fora = fade(0xC0000000u, 0xC0000000u, false, 700);  // alem da borda: ainda e o fade (x >= 640)
    BS_CHECK(nr::widen(fora, 854) == 1 && fora.vertices[2].x == 914 && fora.vertices[0].x == 0);
    // barra de largura inteira (cinema: so a faixa de cima) tambem estende; uma barra que nao toca uma das bordas, nao
    nr::CommandList barra = fade(0xFF000000u, 0xFF000000u, false, 640);
    barra.vertices[1].y = barra.vertices[3].y = 60;
    BS_CHECK(nr::widen(barra, 854) == 1 && barra.vertices[0].x == 0 && barra.vertices[3].x == 854 && barra.vertices[3].y == 60);
    nr::CommandList meia = fade(0xFF000000u, 0xFF000000u, false, 640);
    meia.vertices[0].x = meia.vertices[1].x = 10;
    BS_CHECK(nr::widen(meia, 854) == 0 && meia.vertices[0].x == 117);
    // um fade junto com a cena 3D (1/w diferente nos vertices: nao e fade): so o fade estende
    nr::CommandList cena = fade(0xFF2080C0u, 0xFF2080C0u, false, 640);
    cena.vertices[0].x = cena.vertices[1].x = -200, cena.vertices[2].x = cena.vertices[3].x = 900;
    cena.vertices[2].inv_w = cena.vertices[3].inv_w = 0.5f;
    nr::CommandList escuro = fade(0xC0000000u, 0xC0000000u, false, 640);
    escuro.draws[0].first_vertex = 4;
    cena.vertices.insert(cena.vertices.end(), escuro.vertices.begin(), escuro.vertices.end());
    cena.draws.push_back(escuro.draws[0]);
    BS_CHECK(nr::widen(cena, 854) == 1);
    BS_CHECK(cena.vertices[0].x == -93 && cena.vertices[2].x == 1007);  // a cena so andou
    BS_CHECK(cena.vertices[4].x == 0 && cena.vertices[6].x == 854);     // o fade cobre o quadro largo
}

BS_TEST(wide_frame_window_areas) {
    // janela 16:9: o quadro largo ocupa tudo, o 4:3 do jogo e o centro com a mesma altura
    const nr::AreaRect w169 = nr::fit_area(1920, 1080, 854, 480);
    BS_CHECK(w169.x == 0 && w169.w == 1920 && w169.h == 1079 && w169.y == 0);  // 1920 * 480 / 854 = 1079,1
    const nr::AreaRect c = nr::center_part(w169, 640, 854);
    BS_CHECK(c.w == 1438 && c.x == 241 && c.h == w169.h && c.y == w169.y);
    // janela 4:3: faixas em cima e embaixo; o 4:3 central fica menor que o da janela (o HUD alinhado com a imagem)
    const nr::AreaRect w43 = nr::fit_area(1024, 768, 854, 480);
    BS_CHECK(w43.w == 1024 && w43.h == 575 && w43.y == 96);
    BS_CHECK(nr::fit_area(1280, 960, 640, 480) == (nr::AreaRect{0, 0, 1280, 960}));
    BS_CHECK(nr::center_part(w43, 854, 854) == w43 && nr::center_part(w43, 640, 0) == w43);
    BS_CHECK(nr::fit_area(0, 0, 854, 480) == (nr::AreaRect{0, 0, 854, 480}));
}

// A apresentacao escolhe a fonte pela proporcao do slot: o slot largo so com o quadro largo ligado (um que sobrou de antes
// do 4:3, com o jogo pausado depois da troca para o software, cede a imagem da CPU); o slot 4:3 do N7c vale nos dois.
BS_TEST(wide_frame_slot_choice) {
    BS_CHECK(nr::is_wide_slot(854, 480) && nr::is_wide_slot(3843, 2142) && !nr::is_wide_slot(640, 480));
    BS_CHECK(!nr::is_wide_slot(1440, 1080) && !nr::is_wide_slot(0, 0));
    BS_CHECK(nr::use_slot(7, 854, 480, true));        // 16:9 ligado, slot largo: o slot
    BS_CHECK(!nr::use_slot(7, 854, 480, false));      // 4:3, slot largo antigo: a imagem da CPU
    BS_CHECK(nr::use_slot(7, 1440, 1080, false));     // 4:3 com escala (N7c): o slot, como antes
    BS_CHECK(nr::use_slot(7, 1440, 1080, true));      // 16:9 ligado, slot 4:3 (ainda nao veio o largo): o slot na area 4:3
    BS_CHECK(!nr::use_slot(0, 854, 480, true) && !nr::use_slot(0, 640, 480, false));  // sem slot
}

// A politica: o quadro largo e da Remastered e so com o Vulkan; o menu continua sem a opcao (fase 5); o automatico fica
// 4:3 enquanto o HUD nao for corrigido; a Original volta ao 4:3 com o motivo.
BS_TEST(wide_frame_policy_and_settings) {
    BS_CHECK(!ed::profile_for(ed::Edition::Original).wide_frame && ed::profile_for(ed::Edition::Remastered).wide_frame);
    st::GraphicsSettings s;
    s.aspect = st::Aspect::Wide16x9;
    st::Capabilities gpu;
    gpu.vulkan = true, gpu.max_height = 2160;
    std::vector<std::string> notes;
    BS_CHECK(st::clamp_to(s, gpu, &notes).aspect == st::Aspect::Original4x3 && !notes.empty());  // Original
    gpu.wide_frame = true;  // Remastered com a apresentacao direta
    notes.clear();
    BS_CHECK(st::clamp_to(s, gpu, &notes).aspect == st::Aspect::Wide16x9 && notes.empty());
    BS_CHECK(!gpu.features.aspect_wide);  // a opcao do menu continua "em breve" (fase 5)
    s.aspect = st::Aspect::Auto;
    BS_CHECK(st::clamp_to(s, gpu).aspect == st::Aspect::Original4x3);
    st::Capabilities semVulkan;
    semVulkan.wide_frame = true;  // nunca acontece no app; mesmo assim o software e 4:3
    s.aspect = st::Aspect::Wide16x9;
    BS_CHECK(st::clamp_to(s, semVulkan).aspect == st::Aspect::Original4x3);
    // a regra antiga (o menu do N7 com aspect_wide) nao muda
    st::Capabilities n7 = gpu;
    n7.wide_frame = false, n7.features.aspect_wide = true, n7.wide_hud = true;
    BS_CHECK(st::clamp_to(s, n7).aspect == st::Aspect::Wide16x9);
    n7.wide_hud = false;
    BS_CHECK(st::clamp_to(s, n7).aspect == st::Aspect::Original4x3);
}

// N9.5 fase 3B: a ancora por desenho. O desenho ancorado a esquerda (o HUD do canto) nao anda, nem o recorte dele; o resto
// anda o deslocamento como antes; uma lista de ancoras de outro tamanho e ignorada (todos no centro).
BS_TEST(wide_frame_left_anchored_draws_stay_at_the_left_edge) {
    const nr::CommandList a = quadro_teste();
    nr::CommandList       b = a;
    nr::widen(b, nr::kWideWidth, {nr::kAnchorCenter, nr::kAnchorLeft});
    bool ok = true;
    for (uint32_t k = 0; k < 3; ++k) ok = ok && b.vertices[k].x == a.vertices[k].x + 107.f;  // o triangulo: centro
    for (uint32_t k = 3; k < 7; ++k) ok = ok && b.vertices[k].x == a.vertices[k].x;          // o quad: na borda
    for (uint32_t k = 7; k < 10; ++k) ok = ok && b.vertices[k].x == a.vertices[k].x + 107.f; // o volume: centro
    BS_CHECK(ok);
    BS_CHECK(b.draws[1].state.clip_x0 == 100 && b.draws[1].state.clip_x1 == 539 && b.frame.width == 854);
    nr::CommandList c = a;
    nr::widen(c, nr::kWideWidth, {nr::kAnchorLeft});  // tamanho errado: nada ancorado
    BS_CHECK(c.vertices[3].x == a.vertices[3].x + 107.f && c.draws[1].state.clip_x0 == 207);
    nr::CommandList d = a;  // sem quadro largo, a ancora nao muda nada
    nr::widen(d, 640, {nr::kAnchorCenter, nr::kAnchorLeft});
    BS_CHECK(d.vertices[3].x == a.vertices[3].x && d.frame.width == 640);
}
