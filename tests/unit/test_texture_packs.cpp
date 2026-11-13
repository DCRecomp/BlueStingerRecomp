// N9.8 fase 8.2: o provedor das texturas HD (native/render/texture_packs.h): os pacotes de mods\ pelo mod.toml, o indice
// pelo nome (o TextureId da original), a prioridade, o PNG decodificado (stb_image) igual aos texels escritos (o codificador
// proprio, png_write.h), o recuo por textura com o motivo, o limite de carregamento por render e os nomes ignorados.
#include "test.h"

#include "render/png_write.h"
#include "render/texture_packs.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace nr = bs::native::render;
namespace fs = std::filesystem;

namespace {

struct Mods {
    fs::path root;
    explicit Mods(const char* name) : root(fs::temp_directory_path() / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
    }
    ~Mods() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    fs::path pack(const std::string& id, int priority, const char* game = "T13001N V1.000") const {
        const fs::path d = root / id;
        fs::create_directories(d / "texturas");
        std::ofstream(d / "mod.toml") << "formato = 1\nid = \"" << id << "\"\nnome = \"Pacote " << id << "\"\nversao = \"1.0\"\n"
                                      << "prioridade = " << priority << "\njogo = \"" << game << "\"\n";
        return d / "texturas";
    }
};

void put(const fs::path& f, const std::string& bytes) { std::ofstream(f, std::ios::binary).write(bytes.data(), std::streamsize(bytes.size())); }

std::vector<uint32_t> pattern(uint32_t w, uint32_t h, uint32_t seed) {
    std::vector<uint32_t> v(size_t(w) * h);
    for (size_t i = 0; i < v.size(); ++i) {
        const uint32_t x = uint32_t(i) * 2654435761u + seed;
        v[i] = (((x >> 24) | 0x10u) << 24) | (x & 0x00FFFFFFu);  // alfa variado (nunca 0)
    }
    return v;
}

// uma textura "original" de w x h e o seu id
nr::TextureId original(uint32_t w, uint32_t h, uint32_t seed) { return nr::texture_id(w, h, pattern(w, h, seed)); }

} // namespace

BS_TEST(texture_packs_png_round_trip_and_alpha) {
    Mods m("bs_texpacks_png");
    const fs::path t  = m.pack("hd", 10);
    const auto     a  = original(8, 4, 1), b = original(4, 4, 2);
    const auto     ha = pattern(32, 16, 7);  // fator 4, RGBA
    const auto     hb = pattern(8, 8, 9);    // fator 2, gravado em RGB (sem alfa)
    put(t / (a.text() + ".png"), nr::encode_png(32, 16, ha));
    put(t / (b.text() + ".png"), nr::encode_png(8, 8, hb, false));
    std::vector<std::string> problems;
    auto p = nr::TexturePacks::discover(m.root, "T13001N V1.000", &problems);
    BS_CHECK(problems.empty() && p.packages().size() == 1 && p.packages()[0].textures == 2 && p.stats().indexed == 2);
    const nr::ReplacementTexture* ra = p.find(a);
    BS_CHECK(ra && ra->width == 32 && ra->height == 16 && ra->argb == ha);  // os texels exatos (a ordem dos canais)
    BS_CHECK(ra && nr::replacement_compatible(a, *ra));
    const nr::ReplacementTexture* rb = p.find(b);
    BS_CHECK(rb && rb->width == 8 && rb->argb.size() == 64);
    bool opaque = rb != nullptr;
    for (size_t i = 0; rb && i < hb.size(); ++i) opaque = opaque && rb->argb[i] == (hb[i] | 0xFF000000u);
    BS_CHECK(opaque);  // sem alfa no PNG: opaco, as cores iguais
    BS_CHECK(p.find(a) == ra && p.stats().loaded == 2);  // decodificada uma vez
    BS_CHECK(p.stats().bytes == (32 * 16 + 8 * 8) * 4 && p.stats().max_factor == 4);
    BS_CHECK(!p.find(original(8, 4, 3)));  // fora do indice: o original
    BS_CHECK(!p.deferred(original(8, 4, 3)));
}

BS_TEST(texture_packs_priority_names_and_packages) {
    Mods m("bs_texpacks_prio");
    const fs::path lo = m.pack("baixo", 1), hi = m.pack("alto", 50);
    const auto     a  = original(4, 4, 1), b = original(4, 4, 2);
    put(lo / (a.text() + ".png"), nr::encode_png(4, 4, std::vector<uint32_t>(16, 0xFF0000FFu)));
    put(lo / (b.text() + ".png"), nr::encode_png(4, 4, std::vector<uint32_t>(16, 0xFF00FF00u)));
    put(hi / (a.text() + ".png"), nr::encode_png(8, 8, std::vector<uint32_t>(64, 0xFFFF0000u)));
    put(hi / "4x4-0123.png", nr::encode_png(4, 4, std::vector<uint32_t>(16, 0)));               // nome nao canonico
    put(hi / "4X4-0123456789ABCDEF.png", nr::encode_png(4, 4, std::vector<uint32_t>(16, 0)));  // maiusculas
    put(hi / "LEIA-ME.txt", "texto");                                                          // nao e PNG: nao conta
    fs::create_directories(m.root / "sem-manifesto" / "texturas");                              // sem mod.toml: nao e pacote
    put(m.root / "sem-manifesto" / "texturas" / (b.text() + ".png"), nr::encode_png(4, 4, std::vector<uint32_t>(16, 0xFFFFFFFFu)));
    const fs::path other = m.pack("outro-jogo", 99, "T13001D");  // de outra versao: recusado, com aviso (tem texturas)
    put(other / (a.text() + ".png"), nr::encode_png(4, 4, std::vector<uint32_t>(16, 0xFFFFFFFFu)));
    fs::create_directories(m.root / "so-legendas");  // um pacote sem texturas: nada
    std::ofstream(m.root / "so-legendas" / "mod.toml") << "formato = 1\nid = \"so-legendas\"\n";
    std::vector<std::string> problems;
    auto p = nr::TexturePacks::discover(m.root, "T13001N V1.000", &problems);
    BS_CHECK(p.packages().size() == 2 && p.packages()[0].id == "alto" && p.packages()[1].id == "baixo");
    BS_CHECK(p.packages()[0].textures == 1 && p.packages()[1].textures == 1);  // a de "baixo" que "alto" tambem tem nao conta
    BS_CHECK(p.stats().indexed == 2 && p.stats().overridden == 1 && p.stats().bad_names == 2);
    BS_CHECK(problems.size() == 2);  // o pacote de outro jogo e os dois nomes ignorados de "alto"
    bool sawGame = false, sawNames = false;
    for (const auto& s : problems) {
        sawGame  = sawGame || (s.find("outro-jogo") != std::string::npos && s.find("recusado") != std::string::npos);
        sawNames = sawNames || (s.find("alto") != std::string::npos && s.find("2 PNG") != std::string::npos);
    }
    BS_CHECK(sawGame && sawNames);
    const auto* ra = p.find(a);
    BS_CHECK(ra && ra->width == 8 && ra->argb[0] == 0xFFFF0000u);  // a do pacote de prioridade maior
    const auto* rb = p.find(b);
    BS_CHECK(rb && rb->width == 4 && rb->argb[0] == 0xFF00FF00u);  // so "baixo" tem
    BS_CHECK(nr::TexturePacks::discover(m.root / "nao-existe", "T13001N V1.000").empty());
}

BS_TEST(texture_packs_rejections_fall_back_once) {
    Mods m("bs_texpacks_rej");
    const fs::path t = m.pack("hd", 0);
    const auto     aspect = original(8, 4, 1), f17 = original(1, 1, 2), broken = original(4, 4, 3), big = original(2, 2, 4),
               side = original(16, 16, 5), cut = original(4, 4, 6);
    put(t / (aspect.text() + ".png"), nr::encode_png(16, 16, std::vector<uint32_t>(256, ~0u)));      // outra proporcao
    put(t / (f17.text() + ".png"), nr::encode_png(17, 17, std::vector<uint32_t>(289, ~0u)));         // fator 17
    put(t / (broken.text() + ".png"), "isto nao e um PNG");
    std::string png = nr::encode_png(8, 8, std::vector<uint32_t>(64, ~0u));
    put(t / (cut.text() + ".png"), png.substr(0, png.size() / 2));                                   // cortado
    put(t / (big.text() + ".png"), nr::encode_png(32, 32, std::vector<uint32_t>(1024, ~0u)));        // acima do limite do arquivo
    put(t / (side.text() + ".png"), nr::encode_png(64, 64, std::vector<uint32_t>(4096, ~0u)));       // acima do lado maximo
    auto p               = nr::TexturePacks::discover(m.root, "T13001N V1.000");
    p.limits().max_side  = 32;
    p.limits().per_frame = 100;
    BS_CHECK(p.stats().indexed == 6);
    for (const auto& id : {aspect, f17, broken, cut, side}) BS_CHECK(!p.find(id));
    BS_CHECK(p.find(big));  // 32x32 (fator 16) cabe no lado maximo
    BS_CHECK(p.stats().rejected == 5 && p.stats().loaded == 1 && p.rejections().size() == 5);
    for (const auto& id : {aspect, f17, broken, cut, side}) BS_CHECK(!p.find(id) && !p.deferred(id));
    BS_CHECK(p.stats().rejected == 5);  // nao tenta de novo
    // o limite do arquivo (o PNG de 32x32 sem compressao tem mais de 4 KiB)
    auto q              = nr::TexturePacks::discover(m.root, "T13001N V1.000");
    q.limits().max_file = 3000;
    BS_CHECK(!q.find(big) && q.rejections().size() == 1 && q.rejections()[0].find("acima do limite de 3000 bytes") != std::string::npos);
    auto has = [&](const nr::TextureId& id, const char* why) {
        for (const auto& r : p.rejections())
            if (r.rfind(id.text(), 0) == 0) return r.find(why) != std::string::npos && r.find("(hd)") != std::string::npos;
        return false;
    };
    BS_CHECK(has(aspect, "fator inteiro") && has(f17, "fator inteiro") && has(broken, "ilegivel") && has(cut, "ilegivel"));
    BS_CHECK(has(side, "acima do limite de 32"));
}

BS_TEST(texture_packs_per_frame_limit_defers) {
    Mods m("bs_texpacks_lim");
    const fs::path t = m.pack("hd", 0);
    const auto     a = original(4, 4, 1), b = original(4, 4, 2), c = original(4, 4, 3);
    for (const auto& id : {a, b, c}) put(t / (id.text() + ".png"), nr::encode_png(16, 16, pattern(16, 16, id.width)));
    auto p               = nr::TexturePacks::discover(m.root, "T13001N V1.000");
    p.limits().per_frame = 2;
    p.begin_frame();
    BS_CHECK(p.find(a) && p.find(b));
    BS_CHECK(!p.find(c) && p.deferred(c) && p.stats().deferred == 1);  // o terceiro do render espera
    BS_CHECK(p.find(a));  // as ja carregadas nao contam no limite
    p.begin_frame();
    BS_CHECK(p.find(c) && !p.deferred(c) && p.stats().loaded == 3);
    // o limite de bytes: a primeira do render sempre passa (mesmo maior que o limite), a segunda espera
    Mods m2("bs_texpacks_lim2");
    const fs::path t2 = m2.pack("hd", 0);
    for (const auto& id : {a, b}) put(t2 / (id.text() + ".png"), nr::encode_png(64, 64, pattern(64, 64, 1)));
    auto q                     = nr::TexturePacks::discover(m2.root, "T13001N V1.000");
    q.limits().bytes_per_frame = 1000;
    q.begin_frame();
    BS_CHECK(q.find(a) && !q.find(b) && q.deferred(b));
    q.begin_frame();
    BS_CHECK(q.find(b));
    // a soma que cruza o limite (revisao Codex 2026-11-13): 16 KiB carregados de 20000, a proxima de 16 KiB passaria do
    // limite e espera (antes so se olhava o que ja tinha sido carregado)
    auto s                     = nr::TexturePacks::discover(m2.root, "T13001N V1.000");
    s.limits().bytes_per_frame = 20000;
    s.begin_frame();
    BS_CHECK(s.find(a) && !s.find(b) && s.deferred(b) && s.stats().loaded == 1);
    s.begin_frame();
    BS_CHECK(s.find(b) && s.stats().bytes == 2 * 16384);
    // o orcamento da sessao: a que passaria dele e recusada com o motivo (deterministica, nao fica adiada)
    auto r                  = nr::TexturePacks::discover(m2.root, "T13001N V1.000");
    r.limits().max_resident = 20000;
    r.begin_frame();
    BS_CHECK(r.find(a));
    r.begin_frame();
    BS_CHECK(!r.find(b) && !r.deferred(b) && r.stats().rejected == 1 && r.stats().bytes == 16384);
    BS_CHECK(r.rejections().size() == 1 && r.rejections()[0].find("orcamento") != std::string::npos);
    r.begin_frame();
    BS_CHECK(!r.find(b) && r.stats().rejected == 1);  // nao tenta de novo
}
