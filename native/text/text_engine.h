#pragma once
// N9-L (L4): texto sem DirectWrite, para o Linux (docs/N9-L-PLANO.md, secao 3: o menu e as legendas usam a mesma interface).
// FreeType sobre a Noto Sans (third_party/fonts/noto-sans, OFL 1.1), embutida no executavel. O Windows continua no DirectWrite
// (subtitles_dwrite.cpp, menu_overlay.cpp): este modulo so entra no build do Linux.
//
// O layout segue as regras que o desenho do Windows usa do DirectWrite: tamanho em pixels de em (DIP a 96 dpi), altura de linha
// pelas metricas da fonte (ascendente + descendente + espaco entre linhas), quebra nas palavras e, se uma palavra nao cabe
// sozinha, dentro dela; espacos no fim da linha nao contam no alinhamento; alinhamento horizontal e vertical na caixa, sem
// cortar o que passar dela. A fonte e outra (a Segoe UI e da Microsoft), entao a forma das letras e a largura mudam um pouco.
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace bs::native::text {

enum class Weight : uint8_t { Normal, SemiBold };
enum class HAlign : uint8_t { Leading, Center, Trailing };
enum class VAlign : uint8_t { Near, Center, Far };

struct Color {  // sem pre-multiplicar, 0..1
    float r = 1, g = 1, b = 1, a = 1;
};
struct Box {
    float l = 0, t = 0, r = 0, b = 0;
};

struct TextStyle {
    float  size   = 16;  // em, em pixels
    Weight weight = Weight::Normal;
    HAlign align  = HAlign::Leading;
    VAlign valign = VAlign::Near;
    bool   wrap   = true;
};

// Caixa ocupada pelo texto, relativa ao canto da caixa de layout (como DWRITE_TEXT_METRICS)
struct TextMetrics {
    float    left = 0, top = 0, width = 0, height = 0;
    int      lines   = 0;
    uint32_t missing = 0;  // caracteres sem glifo na fonte (desenhados como o glifo de falta)
};

// Camada ARGB pre-multiplicada (0xAARRGGBB: em memoria B, G, R, A, o PBGRA do Windows). Mistura "por cima" (source over).
class Canvas {
public:
    Canvas(std::span<uint32_t> px, uint32_t w, uint32_t h) : px_(px), w_(w), h_(h) {}
    uint32_t width() const { return w_; }
    uint32_t height() const { return h_; }
    void     clear(uint32_t v = 0);
    void     fill_rect(const Box& r, const Color& c);
    void     fill_round_rect(const Box& r, float radius, const Color& c);
    void     stroke_round_rect(const Box& r, float radius, float stroke, const Color& c);  // traco centrado na borda
    void     fill_triangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color& c);
    void     blend(int x, int y, float coverage, const Color& c);  // coverage 0..1

private:
    std::span<uint32_t> px_;
    uint32_t            w_, h_;
};

class TextEngine {
public:
    // Uma instancia por processo (as fontes e o cache de glifos), protegida por trava: legendas do filme, legendas do gameplay
    // e o menu desenham de threads diferentes.
    static TextEngine& shared();
    virtual ~TextEngine() = default;
    virtual bool ok() const = 0;
    virtual TextMetrics measure(std::u32string_view text, const TextStyle& st, float boxW, float boxH) = 0;
    virtual TextMetrics draw(Canvas& c, std::u32string_view text, const TextStyle& st, const Box& box, const Color& color) = 0;
    virtual bool        has_glyph(char32_t ch, Weight w) = 0;
    virtual std::string describe() const = 0;  // versao do FreeType e das fontes (registro)
};

std::u32string from_utf8(std::string_view s);    // sequencia invalida vira U+FFFD
std::u32string from_wide(std::wstring_view s);   // wchar_t: UTF-32 no Linux

} // namespace bs::native::text
