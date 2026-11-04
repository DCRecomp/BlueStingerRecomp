#pragma once
// N9.1 (docs/N9-1-INSTALADOR.md, secao 2): a interface de desenho do instalador. As telas (telas.cpp) desenham so por aqui, em
// DIP (1/96 de polegada); o hospedeiro multiplica pela escala do monitor. Duas implementacoes:
//   pintor_d2d.cpp  Windows: Direct2D + DirectWrite (decisao 12.2), na janela e num bitmap WIC (capturas)
//   pintor_sw.cpp   Linux: buffer ARGB pelo native/text (FreeType + Noto Sans), entregue ao SDL3 como textura
// O motor do instalador e este; o jogo tem o dele (decisao 12.3: so a identidade visual, tema.h, e compartilhada).
#include <cstdint>
#include <string>
#include <string_view>

namespace bs::builder::instalador::gui {

struct Cor {
    float r = 0, g = 0, b = 0, a = 1;  // 0..1, sem pre-multiplicar
    Cor   com_alfa(float k) const { return {r, g, b, a * k}; }
};
// #RRGGBB
constexpr Cor rgb(uint32_t v, float a = 1) {
    return {float((v >> 16) & 255) / 255.f, float((v >> 8) & 255) / 255.f, float(v & 255) / 255.f, a};
}
Cor misturar(const Cor& a, const Cor& b, float t);

struct Ret {
    float x = 0, y = 0, w = 0, h = 0;
    float dir() const { return x + w; }
    float base() const { return y + h; }
    bool  contem(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Ret   recuo(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Ret   recuo(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
};

enum class Peso : uint8_t { Normal, SemiNegrito };
enum class Alinha : uint8_t { Inicio, Centro, Fim };

struct Estilo {
    float  tamanho = 14;  // DIP (pixels de em a 96 dpi)
    Peso   peso    = Peso::Normal;
    Alinha h       = Alinha::Inicio;
    Alinha v       = Alinha::Inicio;
    bool   quebra  = true;  // quebra nas palavras; sem quebra, uma linha
};

struct Medida {
    float w = 0, h = 0;
    int   linhas = 0;
};

class Pintor {
public:
    virtual ~Pintor() = default;
    virtual float escala() const = 0;  // pixels por DIP
    virtual void  limpar(const Cor& c) = 0;
    virtual void  retangulo(const Ret& r, const Cor& c) = 0;
    virtual void  arredondado(const Ret& r, float raio, const Cor& c) = 0;
    virtual void  contorno(const Ret& r, float raio, float espessura, const Cor& c) = 0;  // traco por dentro da borda
    virtual void  triangulo(float x0, float y0, float x1, float y1, float x2, float y2, const Cor& c) = 0;
    virtual void  degrade(const Ret& r, const Cor& topo, const Cor& base) = 0;  // vertical
    // Medida do texto com a largura disponivel (`largura` <= 0: sem limite).
    virtual Medida medir(std::wstring_view t, const Estilo& e, float largura) = 0;
    virtual void   texto(std::wstring_view t, const Estilo& e, const Ret& caixa, const Cor& c) = 0;
    virtual void   recortar(const Ret& r) = 0;  // ate soltar_recorte (pilha)
    virtual void   soltar_recorte() = 0;
};

} // namespace bs::builder::instalador::gui
