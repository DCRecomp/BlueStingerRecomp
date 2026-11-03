// N9-L (L4): o motor de texto do Linux (native/text, FreeType + Noto Sans): UTF-8, cobertura de caracteres dos idiomas, layout
// (alinhamento, quebra de linha, espacos no fim), desenho dentro da caixa medida, formas da camada, mistura pre-multiplicada,
// determinismo e uso por varias threads.
#include "test.h"

#include "text/text_engine.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

namespace tx = bs::native::text;

namespace {

struct Bbox {
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    size_t lit = 0;
};
Bbox litBox(const std::vector<uint32_t>& px, uint32_t w, uint32_t h, uint32_t minAlpha = 0x40) {
    Bbox b;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            if ((px[size_t(y) * w + x] >> 24) >= minAlpha)
                b.x0 = std::min(b.x0, int(x)), b.y0 = std::min(b.y0, int(y)), b.x1 = std::max(b.x1, int(x)), b.y1 = std::max(b.y1, int(y)), ++b.lit;
    return b;
}

tx::TextStyle style(float size, tx::HAlign a = tx::HAlign::Leading, tx::VAlign v = tx::VAlign::Near, bool wrap = true) {
    tx::TextStyle s;
    s.size = size, s.align = a, s.valign = v, s.wrap = wrap;
    return s;
}

} // namespace

BS_TEST(text_engine_ready) {
    tx::TextEngine& e = tx::TextEngine::shared();
    BS_CHECK(e.ok());
    BS_CHECK(e.describe().find("FreeType 2.13.3") != std::string::npos);
}

BS_TEST(text_utf8_decoding) {
    BS_CHECK(tx::from_utf8("A\xC3\xA7\xC3\xA3o \xE2\x82\xAC") == U"Ação €");
    BS_CHECK(tx::from_utf8("\xF0\x9F\x98\x80") == U"\U0001F600");
    BS_CHECK(tx::from_utf8("a\xFF" "b") == U"a�b");                 // byte invalido
    BS_CHECK(tx::from_utf8("\xC0\xAF") == U"��");               // forma longa
    BS_CHECK(tx::from_utf8("\xED\xA0\x80") == U"���");     // surrogate
    BS_CHECK(tx::from_utf8("x\xE2\x82") == U"x��");             // truncado
    BS_CHECK(tx::from_wide(L"Olá\n") == U"Olá\n");
}

// Os caracteres dos idiomas da interface e das legendas (pt-BR, en, es, fr, de, it) e os de pontuacao; grego e cirilico para
// idiomas futuros. CJK nao esta na fonte: medido como falta (limitacao registrada, sem queda).
BS_TEST(text_glyph_coverage_languages) {
    tx::TextEngine&        e = tx::TextEngine::shared();
    const std::u32string sets[] = {
        U"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
        U"áàâãäéèêëíìîïóòôõöúùûüçñÁÀÂÃÄÉÈÊËÍÌÎÏÓÒÔÕÖÚÙÛÜÇÑ",  // pt, es, fr, de, it
        U"ßœŒæÆøØåÅÿŸ¿¡«»ªº",
        U"“”‘’–—…•·°©®™€£¥%&@#*+-=/\\|()[]{}<>!?.,;:'\"_~^`›",
        U"ΑΒΓΔαβγδΩω",  // grego
        U"АБВГДабвгдЖжЯя",  // cirilico
        U"ĞğŞşİıŁłČčŐőŽž",  // latino estendido
    };
    uint32_t missing = 0;
    for (const auto& s : sets)
        for (char32_t c : s) missing += !e.has_glyph(c, tx::Weight::Normal) + !e.has_glyph(c, tx::Weight::SemiBold);
    BS_CHECK(missing == 0);
    BS_CHECK(e.measure(U"Ação, coração e pingüim: ¿qué? Größe — «œuvre»", style(20), 0, 0).missing == 0);
    BS_CHECK(e.measure(U"日本語", style(20), 0, 0).missing == 3);  // sem fonte CJK
}

BS_TEST(text_layout_alignment_and_metrics) {
    tx::TextEngine&       e  = tx::TextEngine::shared();
    const tx::TextMetrics m1 = e.measure(U"Teste", style(32), 400, 200);
    BS_CHECK(m1.lines == 1 && m1.left == 0 && m1.top == 0 && m1.width > 60 && m1.width < 120);
    BS_CHECK(std::fabs(m1.height - 32 * 1.362f) < 0.05f);  // altura da linha da Noto Sans: (1069 + 293) / 1000 em
    const tx::TextMetrics c = e.measure(U"Teste", style(32, tx::HAlign::Center, tx::VAlign::Center), 400, 200);
    BS_CHECK(std::fabs(c.left - (400 - c.width) / 2) < 0.01f && std::fabs(c.top - (200 - c.height) / 2) < 0.01f);
    const tx::TextMetrics f = e.measure(U"Teste", style(32, tx::HAlign::Trailing, tx::VAlign::Far), 400, 200);
    BS_CHECK(std::fabs(f.left + f.width - 400) < 0.01f && std::fabs(f.top + f.height - 200) < 0.01f);
    // espacos no fim nao entram no alinhamento (como no DirectWrite)
    BS_CHECK(e.measure(U"abc   ", style(20), 0, 0).width == e.measure(U"abc", style(20), 0, 0).width);
    BS_CHECK(e.measure(U"", style(20), 100, 100).lines == 0);
}

BS_TEST(text_layout_line_breaks) {
    tx::TextEngine& e = tx::TextEngine::shared();
    BS_CHECK(e.measure(U"um\ndois", style(20), 0, 0).lines == 2);
    BS_CHECK(e.measure(U"um\n\ndois", style(20), 0, 0).lines == 3);  // a linha vazia conta
    BS_CHECK(e.measure(U"um\r\ndois", style(20), 0, 0).lines == 2);
    const std::u32string longo = U"Uma frase longa de legenda que nao cabe numa linha so e precisa quebrar nas palavras.";
    const auto           one   = e.measure(longo, style(20), 0, 0);
    const auto           w     = e.measure(longo, style(20), 260, 0);
    BS_CHECK(one.lines == 1 && w.lines >= 3 && w.width <= 260.01f);
    BS_CHECK(e.measure(longo, style(20, tx::HAlign::Leading, tx::VAlign::Near, false), 260, 0).lines == 1);  // sem quebra
    // palavra maior que a linha: quebra dentro dela
    const auto big = e.measure(U"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", style(20), 120, 0);
    BS_CHECK(big.lines >= 3 && big.width <= 120.01f);
    // quebra depois do hifen
    BS_CHECK(e.measure(U"bem-vindo", style(20), e.measure(U"bem-vin", style(20), 0, 0).width, 0).lines == 2);
}

BS_TEST(text_draw_inside_measured_box) {
    tx::TextEngine&       e = tx::TextEngine::shared();
    const uint32_t        W = 480, H = 160;
    std::vector<uint32_t> px(size_t(W) * H, 0);
    tx::Canvas            c(px, W, H);
    const tx::Box         box{20, 10, 460, 150};
    const auto            st = style(28, tx::HAlign::Center, tx::VAlign::Far);
    const auto            m  = e.draw(c, U"Olá, mundo! Ação", st, box, {1, 1, 1, 1});
    const Bbox            b  = litBox(px, W, H);
    BS_CHECK(b.lit > 400);
    // a tinta fica dentro da caixa medida (com 2 px de folga do antisserrilhado) e centralizada
    BS_CHECK(b.x0 >= int(box.l + m.left) - 2 && b.x1 <= int(box.l + m.left + m.width) + 2);
    BS_CHECK(b.y0 >= int(box.t + m.top) - 2 && b.y1 <= int(box.t + m.top + m.height) + 2);
    BS_CHECK(std::abs((b.x0 + b.x1) / 2 - int((box.l + box.r) / 2)) <= 3);
    // branco opaco pre-multiplicado: cor = alfa
    bool premul = true;
    for (uint32_t p : px) premul &= ((p >> 16) & 0xFF) == (p >> 24) && ((p >> 8) & 0xFF) == (p >> 24) && (p & 0xFF) == (p >> 24);
    BS_CHECK(premul);
    // determinismo: o mesmo desenho, os mesmos pixels
    std::vector<uint32_t> px2(size_t(W) * H, 0);
    tx::Canvas            c2(px2, W, H);
    e.draw(c2, U"Olá, mundo! Ação", st, box, {1, 1, 1, 1});
    BS_CHECK(px == px2);
}

BS_TEST(text_canvas_shapes_and_blend) {
    std::vector<uint32_t> px(16 * 16, 0);
    tx::Canvas            c(px, 16, 16);
    c.fill_rect({2, 2, 6, 6}, {1, 1, 1, 1});
    BS_CHECK(px[3 * 16 + 3] == 0xFFFFFFFFu && px[1 * 16 + 1] == 0 && px[6 * 16 + 6] == 0);
    c.clear();
    c.fill_rect({2.5f, 2, 3.5f, 3}, {1, 1, 1, 1});  // meio pixel em cada coluna
    BS_CHECK((px[2 * 16 + 2] >> 24) == 0x80 && (px[2 * 16 + 3] >> 24) == 0x80);
    c.clear();
    c.blend(0, 0, 1, {1, 1, 1, 0.5f});  // branco a 50 % sobre transparente
    BS_CHECK(px[0] == 0x80808080u);
    c.blend(0, 0, 1, {0, 0, 0, 1});  // preto opaco por cima
    BS_CHECK(px[0] == 0xFF000000u);
    c.blend(99, 99, 1, {1, 1, 1, 1});  // fora: ignorado
    c.clear();
    c.fill_round_rect({0, 0, 16, 16}, 6, {1, 1, 1, 1});
    BS_CHECK((px[0] >> 24) < 0x40 && px[8 * 16 + 8] == 0xFFFFFFFFu);  // canto arredondado, centro cheio
    c.clear();
    c.stroke_round_rect({3, 3, 13, 13}, 2, 2, {1, 1, 1, 1});
    BS_CHECK((px[8 * 16 + 8] >> 24) == 0 && (px[8 * 16 + 3] >> 24) > 0x80);  // so o contorno
    c.clear();
    c.fill_triangle(2, 2, 14, 8, 2, 14, {1, 1, 1, 1});
    BS_CHECK((px[8 * 16 + 5] >> 24) == 0xFF && (px[2 * 16 + 13] >> 24) == 0);
}

// legendas do filme, do gameplay e o menu desenham de threads diferentes: o mesmo resultado que em sequencia
BS_TEST(text_threads_same_result) {
    tx::TextEngine& e = tx::TextEngine::shared();
    const uint32_t  W = 320, H = 80;
    auto            render = [&](const std::u32string& s) {
        std::vector<uint32_t> px(size_t(W) * H, 0);
        tx::Canvas            c(px, W, H);
        e.draw(c, s, style(22, tx::HAlign::Center, tx::VAlign::Center), {0, 0, float(W), float(H)}, {1, 1, 1, 1});
        return px;
    };
    const std::u32string texts[4] = {U"Primeira legenda", U"Segunda: ação", U"Third subtitle", U"Größe ¿qué?"};
    std::vector<uint32_t> ref[4];
    for (int i = 0; i < 4; ++i) ref[i] = render(texts[i]);
    bool                     same = true;
    std::vector<std::thread> t;
    std::mutex               m;
    for (int i = 0; i < 4; ++i)
        t.emplace_back([&, i] {
            for (int r = 0; r < 25; ++r)
                if (render(texts[(i + r) % 4]) != ref[(i + r) % 4]) {
                    const std::lock_guard<std::mutex> g(m);
                    same = false;
                }
        });
    for (auto& x : t) x.join();
    BS_CHECK(same);
}
