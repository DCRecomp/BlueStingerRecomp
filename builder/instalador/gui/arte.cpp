#include "instalador/gui/arte.h"

#include "instalador/gui/tema.h"

#include <algorithm>
#include <cmath>

namespace bs::builder::instalador::gui {

Cor misturar(const Cor& a, const Cor& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

namespace arte {

namespace {

// traco reto com espessura (dois triangulos)
void linha(Pintor& p, float x0, float y0, float x1, float y1, float e, const Cor& c) {
    const float dx = x1 - x0, dy = y1 - y0, n = std::sqrt(dx * dx + dy * dy);
    if (n <= 0) return;
    const float nx = -dy / n * e / 2, ny = dx / n * e / 2;
    p.triangulo(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
    p.triangulo(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

// quadrilatero convexo (a, b, c, d em ordem)
void quad(Pintor& p, float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, const Cor& c) {
    p.triangulo(ax, ay, bx, by, cx, cy, c);
    p.triangulo(ax, ay, cx, cy, dx, dy, c);
}

// disco aproximado por arredondado (raio = metade do lado)
void disco(Pintor& p, float cx, float cy, float r, const Cor& c) { p.arredondado({cx - r, cy - r, 2 * r, 2 * r}, r, c); }

// quadro de tela (moldura, area interna) na caixa, com a proporcao a:b centrada; devolve a area interna
Ret quadro(Pintor& p, const Ret& r, float a, float b) {
    float w = r.w, h = r.w * b / a;
    if (h > r.h) h = r.h, w = r.h * a / b;
    const Ret fora{r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h};
    p.arredondado(fora, 10, rgb(0x05080E));
    p.contorno(fora, 10, 1.5f, tema::kBorda);
    return fora.recuo(std::max(5.f, w * 0.03f));
}

} // namespace

void emblema(Pintor& p, float x, float y, float s, bool comFundo) {
    namespace id = native::ui::identidade;  // N9.2: a geometria e a mesma da interface do jogo
    if (comFundo) {
        p.arredondado({x, y, s, s}, s * id::kEmblemaRaio, tema::kFundoAlto);
        p.contorno({x, y, s, s}, s * id::kEmblemaRaio, std::max(1.f, s * id::kEmblemaContorno), tema::kBorda);
    }
    auto X = [&](float u) { return x + u * s; };
    auto Y = [&](float v) { return y + v * s; };
    for (const auto& t : id::kEmblema) p.triangulo(X(t.x0), Y(t.y0), X(t.x1), Y(t.y1), X(t.x2), Y(t.y2), rgb(t.cor).com_alfa(t.alfa));
    // faixas do corpo: a largura acompanha as bordas do triangulo
    for (float v : id::kFaixas) {
        const float e = id::kFaixaEspessura, a = id::faixa_meia(v), b = id::faixa_meia(v + e);
        quad(p, X(0.5f - a), Y(v), X(0.5f + a), Y(v), X(0.5f + b), Y(v + e), X(0.5f - b), Y(v + e), tema::kAmbarEscuro);
    }
}

float logotipo(Pintor& p, float x, float y, float largura) {
    const float lado = std::min(56.f, largura * 0.28f);
    emblema(p, x, y, lado);
    const float tx = x + lado + 12, tw = largura - lado - 12;
    Estilo      e;
    e.tamanho = 17;
    e.peso    = Peso::SemiNegrito;
    e.quebra  = false;
    p.texto(L"BLUE STINGER", e, {tx, y + lado * 0.14f, tw, 24}, tema::kTexto);
    e.tamanho = 12.5f;
    e.peso    = Peso::Normal;
    p.texto(L"Recompiled", e, {tx, y + lado * 0.14f + 24, tw, 20}, tema::kAmbar);
    return lado;
}

void ilustracao_original(Pintor& p, const Ret& r) {
    const Ret tela = quadro(p, r, 4, 3);
    p.recortar(tela);
    p.degrade(tela, rgb(0x1B2350), rgb(0x3A2E5C));
    // sol em blocos (baixa resolucao)
    const float b  = tela.w / 24;  // tamanho do "pixel"
    const float sx = tela.x + tela.w * 0.62f, sy = tela.y + tela.h * 0.22f;
    const int   sol[5][5] = {{0, 1, 1, 1, 0}, {1, 1, 1, 1, 1}, {1, 1, 1, 1, 1}, {1, 1, 1, 1, 1}, {0, 1, 1, 1, 0}};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j)
            if (sol[i][j]) p.retangulo({sx + j * b, sy + i * b, b + 0.5f, b + 0.5f}, tema::kAmbar);
    // montanhas em degraus
    const float base = tela.y + tela.h;
    const int   alt[24] = {5, 6, 7, 8, 9, 8, 7, 6, 6, 7, 8, 9, 10, 11, 10, 9, 8, 7, 6, 6, 7, 8, 7, 6};
    for (int i = 0; i < 24; ++i) p.retangulo({tela.x + i * b, base - alt[i] * b, b + 0.5f, alt[i] * b}, rgb(0x2C6E68));
    for (int i = 0; i < 24; ++i) p.retangulo({tela.x + i * b, base - (alt[i] - 3) * b, b + 0.5f, (alt[i] - 3) * b}, rgb(0x1D4A47));
    // linhas de varredura
    for (float yy = tela.y; yy < base; yy += 3) p.retangulo({tela.x, yy, tela.w, 1}, rgb(0x000000, 0.22f));
    p.soltar_recorte();
}

void ilustracao_remastered(Pintor& p, const Ret& r) {
    const Ret tela = quadro(p, r, 16, 9);
    p.recortar(tela);
    p.degrade(tela, rgb(0x0D2A3F), rgb(0x2B5C74));
    // sol com brilho
    const float cx = tela.x + tela.w * 0.66f, cy = tela.y + tela.h * 0.34f, rs = tela.h * 0.11f;
    for (int i = 5; i >= 1; --i) disco(p, cx, cy, rs * (1 + i * 0.45f), tema::kAmbar.com_alfa(0.05f));
    disco(p, cx, cy, rs, rgb(0xFFD58A));
    // montanhas em camadas, da mais distante
    const float base = tela.y + tela.h * 0.78f, X0 = tela.x, W = tela.w;
    auto        serra = [&](const float (*pts)[2], int n, const Cor& c) {
        for (int i = 0; i + 1 < n; ++i)
            quad(p, X0 + pts[i][0] * W, base - pts[i][1] * tela.h, X0 + pts[i + 1][0] * W, base - pts[i + 1][1] * tela.h,
                 X0 + pts[i + 1][0] * W, base, X0 + pts[i][0] * W, base, c);
    };
    const float longe[][2] = {{0, .18f}, {.12f, .30f}, {.26f, .20f}, {.40f, .36f}, {.55f, .22f}, {.72f, .33f}, {.86f, .21f}, {1, .27f}};
    const float perto[][2] = {{0, .10f}, {.18f, .22f}, {.33f, .12f}, {.50f, .26f}, {.64f, .14f}, {.80f, .20f}, {1, .09f}};
    serra(longe, 8, rgb(0x2E7C80, 0.85f));
    serra(perto, 7, rgb(0x1B4F5A));
    // agua com reflexo
    p.degrade({tela.x, base, tela.w, tela.base() - base}, rgb(0x123A4F), rgb(0x081A28));
    for (int i = 0; i < 6; ++i) {
        const float w = rs * (2.2f - i * 0.3f), yy = base + 4 + i * (tela.base() - base) / 7;
        p.arredondado({cx - w / 2, yy, w, 2}, 1, rgb(0xFFD58A, 0.55f - i * 0.07f));
    }
    p.soltar_recorte();
}

void fundo_lateral(Pintor& p, const Ret& r) {
    p.degrade(r, tema::kLateral, tema::kFundo);
    const Cor c = tema::kAgua.com_alfa(0.05f), d = tema::kAmbar.com_alfa(0.05f);
    const float b = r.base();
    p.triangulo(r.x, b, r.x + r.w * 0.9f, b, r.x, b - r.w * 0.8f, c);
    p.triangulo(r.x + r.w * 0.2f, b, r.dir(), b, r.dir(), b - r.w * 1.1f, d);
}

void simbolo_ok(Pintor& p, float x, float y, float s, const Cor& c) {
    linha(p, x + s * 0.18f, y + s * 0.52f, x + s * 0.42f, y + s * 0.76f, s * 0.14f, c);
    linha(p, x + s * 0.38f, y + s * 0.76f, x + s * 0.84f, y + s * 0.26f, s * 0.14f, c);
}

void simbolo_erro(Pintor& p, float x, float y, float s, const Cor& c) {
    linha(p, x + s * 0.22f, y + s * 0.22f, x + s * 0.78f, y + s * 0.78f, s * 0.14f, c);
    linha(p, x + s * 0.78f, y + s * 0.22f, x + s * 0.22f, y + s * 0.78f, s * 0.14f, c);
}

void simbolo_alerta(Pintor& p, float x, float y, float s, const Cor& c) {
    p.triangulo(x + s * 0.5f, y + s * 0.08f, x + s * 0.95f, y + s * 0.9f, x + s * 0.05f, y + s * 0.9f, c);
    p.retangulo({x + s * 0.45f, y + s * 0.36f, s * 0.1f, s * 0.3f}, tema::kFundo);
    p.retangulo({x + s * 0.45f, y + s * 0.72f, s * 0.1f, s * 0.1f}, tema::kFundo);
}

} // namespace arte

} // namespace bs::builder::instalador::gui
