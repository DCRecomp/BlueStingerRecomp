#pragma once
// N9.1, Windows: o pintor sobre Direct2D + DirectWrite (decisao 12.2). Desenha num ID2D1RenderTarget com o DPI da escala
// (as coordenadas ficam em DIP): o da janela (HWND) ou o de um bitmap WIC (capturas de teste, PNG).
#include "instalador/gui/pintor.h"

#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <filesystem>
#include <map>
#include <vector>

namespace bs::builder::instalador::gui {

template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() {
        if (p) p->Release(), p = nullptr;
    }
    T** put() {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// Fabricas compartilhadas do processo (D2D, DirectWrite, WIC).
struct Fabricas {
    Com<ID2D1Factory>       d2d;
    Com<IDWriteFactory>     dw;
    Com<IWICImagingFactory> wic;
    bool                    ok = false;
    static Fabricas& get();
};

class PintorD2D final : public Pintor {
public:
    // `rt` precisa estar entre BeginDraw e EndDraw quando as funcoes de desenho forem chamadas.
    PintorD2D(ID2D1RenderTarget* rt, float escala);
    float  escala() const override { return escala_; }
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
    ID2D1SolidColorBrush* pincel(const Cor& c);
    IDWriteTextFormat*    formato(const Estilo& e);
    bool                  layout(std::wstring_view t, const Estilo& e, float w, float h, Com<IDWriteTextLayout>& out);

    ID2D1RenderTarget*              rt_;
    float                           escala_;
    Com<ID2D1SolidColorBrush>       pincel_;
    std::map<uint32_t, IDWriteTextFormat*> formatos_;
    int                             recortes_ = 0;

public:
    ~PintorD2D() override;
};

// Desenho fora da tela (capturas): bitmap WIC de w x h DIP na escala dada; salvar() grava o PNG.
class Quadro {
public:
    Quadro(float wDip, float hDip, float escala);
    ID2D1RenderTarget* rt() const { return rt_.p; }
    bool               ok() const { return bool(rt_); }
    bool               salvar(const std::filesystem::path& png);

private:
    Com<IWICBitmap>        bmp_;
    Com<ID2D1RenderTarget> rt_;
};

} // namespace bs::builder::instalador::gui
