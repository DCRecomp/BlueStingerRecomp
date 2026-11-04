#pragma once
// N9.1, Linux: o pintor em software (decisao do mantenedor, 2026-11-04): um buffer ARGB pre-multiplicado (o formato do
// native/text e do SDL_PIXELFORMAT_ARGB8888), com as formas rasterizadas aqui (cobertura por distancia e por amostras) e o
// texto pelo native/text (FreeType + Noto Sans embutida, o mesmo motor das legendas do Linux). O SDL3 so mostra o buffer.
#include "instalador/gui/pintor.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace bs::builder::instalador::gui {

class PintorSW final : public Pintor {
public:
    PintorSW(uint32_t wPx, uint32_t hPx, float escala);
    void redimensionar(uint32_t wPx, uint32_t hPx, float escala);
    const std::vector<uint32_t>& pixels() const { return px_; }
    uint32_t largura() const { return w_; }
    uint32_t altura() const { return h_; }
    bool     salvar_png(const std::filesystem::path& f) const;

    float  escala() const override { return k_; }
    void   limpar(const Cor& c) override;
    void   retangulo(const Ret& r, const Cor& c) override;
    void   arredondado(const Ret& r, float raio, const Cor& c) override;
    void   contorno(const Ret& r, float raio, float espessura, const Cor& c) override;
    void   triangulo(float x0, float y0, float x1, float y1, float x2, float y2, const Cor& c) override;
    void   degrade(const Ret& r, const Cor& topo, const Cor& base) override;
    Medida medir(std::wstring_view t, const Estilo& e, float largura) override;
    void   texto(std::wstring_view t, const Estilo& e, const Ret& caixa, const Cor& c) override;
    void   recortar(const Ret& r) override;
    void   soltar_recorte() override;

private:
    struct Clip {
        int x0, y0, x1, y1;  // pixels, [x0, x1)
    };
    void misturar(int x, int y, float cobertura, const Cor& c);
    Clip clip() const { return clips_.empty() ? Clip{0, 0, int(w_), int(h_)} : clips_.back(); }
    // cobertura pela distancia assinada a um retangulo arredondado (negativa dentro), em pixels
    template <class F> void por_distancia(const Ret& r, float margem, F cobertura, const Cor& c);

    std::vector<uint32_t> px_;
    uint32_t              w_ = 0, h_ = 0;
    float                 k_ = 1;
    std::vector<Clip>     clips_;
};

} // namespace bs::builder::instalador::gui
