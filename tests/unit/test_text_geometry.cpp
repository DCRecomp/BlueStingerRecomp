// N9-L (L4): a mesma prova de texto nas duas plataformas (Windows: DirectWrite e Segoe UI; Linux: FreeType e Noto Sans). Cada caso
// desenha legendas do filme, do gameplay e o menu pela interface publica e mede a tinta: caixa, linhas e pixels. Aqui so as
// regras que valem para as duas (texto embaixo e ao centro, dentro das margens, quebra quando nao cabe). Com
// BS_TEXT_GEOM=<pasta>, grava texto_geometria.tsv e uma imagem PPM por caso; tools/dev/texto_geometria.py compara a medida do
// Windows com a do Linux (posicao, linhas e tamanho dentro de limites fixados, porque a fonte muda).
#include "test.h"

#include "menu_overlay.h"
#include "video/subtitles.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace nv = bs::native::video;

namespace {

struct Ink {
    int    x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1, lines = 0;
    size_t lit = 0;
};

// tinta = pixel que passa do limite (canal verde do texto branco, ou alfa da camada); linhas = faixas de tinta separadas por
// pelo menos max(3, h/100) linhas vazias (o vao entre o acento e a letra e menor que o entre linhas de texto)
template <class F>
Ink measure(const std::vector<uint32_t>& px, uint32_t w, uint32_t h, F inked) {
    Ink            k;
    const uint32_t gap   = std::max<uint32_t>(3, h / 100);
    uint32_t       empty = gap;
    for (uint32_t y = 0; y < h; ++y) {
        bool row = false;
        for (uint32_t x = 0; x < w; ++x)
            if (inked(px[size_t(y) * w + x])) {
                k.x0 = std::min(k.x0, int(x)), k.x1 = std::max(k.x1, int(x)), row = true, ++k.lit;
                k.y0 = std::min(k.y0, int(y)), k.y1 = std::max(k.y1, int(y));
            }
        if (row) k.lines += empty >= gap, empty = 0;
        else ++empty;
    }
    return k;
}

struct Dump {
    std::filesystem::path dir;
    std::ofstream         tsv;
    Dump() {
        if (const char* d = std::getenv("BS_TEXT_GEOM"); d && *d) {
            dir = d;
            std::filesystem::create_directories(dir);
            tsv.open(dir / "texto_geometria.tsv", std::ios::binary);
            tsv << "caso\tlargura\taltura\tx0\ty0\tx1\ty1\tlinhas\tpixels\n";
        }
    }
    void add(const std::string& name, const std::vector<uint32_t>& px, uint32_t w, uint32_t h, const Ink& k) {
        if (dir.empty()) return;
        tsv << name << '\t' << w << '\t' << h << '\t' << k.x0 << '\t' << k.y0 << '\t' << k.x1 << '\t' << k.y1 << '\t' << k.lines << '\t'
            << k.lit << '\n';
        std::ofstream ppm(dir / (name + ".ppm"), std::ios::binary);  // sobre cinza, para ver a camada transparente
        ppm << "P6\n" << w << ' ' << h << "\n255\n";
        for (uint32_t p : px) {
            const uint32_t a = p >> 24;
            for (int sh : {16, 8, 0}) ppm.put(char(std::min<uint32_t>(255, ((p >> sh) & 0xFF) + 0x40 * (255 - a) / 255)));
        }
    }
};

bool white(uint32_t p) { return ((p >> 8) & 0xFF) >= 0xC0; }
bool alpha(uint32_t p) { return (p >> 24) >= 0x40; }

} // namespace

BS_TEST(text_geometry_same_rules_both_platforms) {
    auto r = nv::make_subtitle_rasterizer();
    if (!r || !r->ok()) BS_SKIP("rasterizador de texto");
    Dump d;
    // legenda do filme (320x240, desenhada sobre o quadro)
    struct Film {
        const char*  name;
        std::wstring text;
        int          lines;
    } films[] = {// sem letras que descem (g, j, p, q, y): a ultima linha com tinta e a linha de base
                 {"filme_1linha", L"Uma linha sobre o filme.", 1},
                 {"filme_2linhas", L"Primeira linha da fala\nSeu tom muda no final", 2},
                 {"filme_quebra", L"Uma fala mais extensa tem de ser dividida em duas.", 2},
                 {"filme_acentos", L"Ação ¿qué? Größe «œuvre»", 1}};
    for (const auto& f : films) {
        std::vector<uint32_t> px(320 * 240, 0xFF000000u);
        r->draw(px, 320, 240, f.text);
        const Ink k = measure(px, 320, 240, white);
        d.add(f.name, px, 320, 240, k);
        BS_CHECK(k.lit > 100 && k.lines == f.lines);
        BS_CHECK(k.y1 <= 240 - 12 + 3 && k.y0 > 120);                // embaixo, acima da margem de 5 %
        BS_CHECK(k.x0 >= 16 - 2 && k.x1 <= 320 - 16 + 2);            // dentro das margens laterais
        BS_CHECK(std::abs((k.x0 + k.x1) / 2 - 160) <= 4);            // centralizada
        bool opaque = true;
        for (uint32_t p : px) opaque &= (p >> 24) == 0xFF;
        BS_CHECK(opaque);
    }
    // legenda do gameplay: camada 1920x1080, area 4:3 no meio
    struct Game {
        const char*        name;
        std::wstring       text;
        nv::OverlayStyle   st;
        int                lines;
    } games[] = {{"jogo_1linha", L"Esta e uma fala da cena de abertura.", {}, 1},
                 {"jogo_quebra", L"Uma fala da cena bem extensa e lenta nao cabe numa linha da tela inteira do mundo.", {}, 2},
                 {"jogo_grande", L"Texto maior.", {0.045f, 2.0f, true, false, 0.6f, 0.06f}, 1}};
    for (const auto& g : games) {
        std::vector<uint32_t> px(1920 * 1080, 0);
        r->draw_overlay(px, 1920, 1080, g.text, 240, 0, 1440, 1080, g.st);
        const Ink k = measure(px, 1920, 1080, alpha);
        d.add(g.name, px, 1920, 1080, k);
        BS_CHECK(k.lit > 500 && k.lines == g.lines);
        BS_CHECK(k.y1 <= 1080 - 65 + 4 && k.y0 > 700);
        BS_CHECK(k.x0 >= 240 + 65 - 4 && k.x1 <= 240 + 1440 - 65 + 4);
        BS_CHECK(std::abs((k.x0 + k.x1) / 2 - 960) <= 6);
    }
    {   // fundo escuro atras do texto
        nv::OverlayStyle st;
        st.box = true;
        std::vector<uint32_t> px(1920 * 1080, 0);
        r->draw_overlay(px, 1920, 1080, L"Com fundo.", 240, 0, 1440, 1080, st);
        const Ink k = measure(px, 1920, 1080, [](uint32_t p) { return (p >> 24) > 0; });
        d.add("jogo_fundo", px, 1920, 1080, k);
        BS_CHECK(k.lines == 1 && k.y1 <= 1080 - 65 + 30 && std::abs((k.x0 + k.x1) / 2 - 960) <= 6);
    }
    // menu: a mesma pagina do teste do menu, numa saida mais larga que a area 4:3
    bs::MenuOverlay o;
    if (!o.ok()) return;
    bs::MenuView v;
    v.title    = "Configurações > Áudio";
    v.items    = {{"Volume geral", "90 %", true}, {"Música", "100 %", true}, {"Em breve", "x (em breve)", false}, {"Voltar", "", true}};
    v.selected = 1;
    v.help     = "Enter: escolher   Esc: voltar";
    const auto& l = o.layer(v, 1280, 960, 160, 0, 960, 960);
    const Ink   k = measure(l, 1280, 960, white);
    d.add("menu", l, 1280, 960, k);
    BS_CHECK(k.lit > 500 && k.x0 > 160 && k.x1 < 1120);
    // as areas do mouse sao geometria do layout, igual nas duas plataformas: a linha de cada altura
    std::string rows;
    for (int y = 0; y < 960; y += 8)
        if (auto h = o.hit(300, float(y))) rows += std::to_string(y) + ":" + std::to_string(h->item) + " ";
    if (!d.dir.empty()) d.tsv << "menu_linhas\t" << rows << '\n';
    BS_CHECK(!rows.empty());
}
