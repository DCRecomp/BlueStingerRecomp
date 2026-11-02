// Desenho das legendas no Windows: DirectWrite, Direct2D e WIC.
#include "video/subtitles.h"

#include "core/com_scope.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <algorithm>

namespace bs::native::video {


struct SubtitleRenderer::Impl {
    ComScope             com;  // o WIC exige COM na thread do desenho
    IDWriteFactory*      dw  = nullptr;
    ID2D1Factory*        d2d = nullptr;
    IWICImagingFactory*  wic = nullptr;
    ~Impl() {
        if (wic) wic->Release();
        if (d2d) d2d->Release();
        if (dw) dw->Release();
    }
};

std::unique_ptr<ISubtitleRasterizer> make_subtitle_rasterizer() { return std::make_unique<SubtitleRenderer>(); }

SubtitleRenderer::SubtitleRenderer() : impl_(std::make_unique<Impl>()) {
    // falhou alguma: ok() == false (exige as tres) e o destrutor do Impl libera as que foram criadas
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&impl_->dw));
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &impl_->d2d);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl_->wic));
}
SubtitleRenderer::~SubtitleRenderer() = default;
bool SubtitleRenderer::ok() const { return impl_ && impl_->dw && impl_->d2d && impl_->wic; }

void SubtitleRenderer::draw(std::span<uint32_t> argb, uint32_t w, uint32_t h, const std::wstring& text) {
    if (!ok() || text.empty() || argb.size() < size_t(w) * h) return;
    // desenha sobre uma copia do quadro (bitmap WIC PBGRA) e copia de volta
    IWICBitmap* bmp = nullptr;
    if (FAILED(impl_->wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppPBGRA, w * 4, UINT(w * h * 4),
                                                   reinterpret_cast<BYTE*>(argb.data()), &bmp)))
        return;
    ID2D1RenderTarget* rt = nullptr;
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    IDWriteTextFormat* fmt = nullptr;
    ID2D1SolidColorBrush *white = nullptr, *dark = nullptr;
    const float size = std::max(10.0f, float(h) * 0.065f);  // ~16 px em 240 linhas
    if (SUCCEEDED(impl_->d2d->CreateWicBitmapRenderTarget(bmp, props, &rt)) &&
        SUCCEEDED(impl_->dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, size, L"", &fmt)) &&
        SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &white)) &&
        SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.85f), &dark))) {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
        const float     margin = float(h) * 0.05f;
        const D2D1_RECT_F box  = D2D1::RectF(margin, margin, float(w) - margin, float(h) - margin);
        rt->BeginDraw();
        // contorno: o texto escuro deslocado em 8 direcoes, depois o branco por cima
        const float o = std::max(1.0f, size / 14.0f);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx || dy)
                    rt->DrawText(text.c_str(), UINT32(text.size()), fmt,
                                 D2D1::RectF(box.left + dx * o, box.top + dy * o, box.right + dx * o, box.bottom + dy * o), dark);
        rt->DrawText(text.c_str(), UINT32(text.size()), fmt, box, white);
        rt->EndDraw();
        // de volta ao quadro (o bitmap foi criado por copia)
        WICRect r{0, 0, INT(w), INT(h)};
        bmp->CopyPixels(&r, w * 4, UINT(w * h * 4), reinterpret_cast<BYTE*>(argb.data()));
        for (auto& p : argb) p |= 0xFF000000u;
    }
    if (dark) dark->Release();
    if (white) white->Release();
    if (fmt) fmt->Release();
    if (rt) rt->Release();
    bmp->Release();
}

void SubtitleRenderer::draw_overlay(std::span<uint32_t> out, uint32_t w, uint32_t h, const std::wstring& text, float cx,
                                    float cy, float cw, float ch, const OverlayStyle& st) {
    if (out.size() < size_t(w) * h) return;
    std::fill(out.begin(), out.begin() + size_t(w) * h, 0u);  // transparente
    if (!ok() || text.empty() || !w || !h || cw <= 0 || ch <= 0) return;
    IWICBitmap* bmp = nullptr;
    if (FAILED(impl_->wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bmp))) return;
    ID2D1RenderTarget*    rt  = nullptr;
    IDWriteTextFormat*    fmt = nullptr;
    IDWriteTextLayout*    lay = nullptr;
    ID2D1SolidColorBrush *white = nullptr, *dark = nullptr, *back = nullptr;
    const auto  props  = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    const float size   = std::max(12.0f, ch * st.heightFraction * st.scale);
    const float margin = ch * st.safeMargin;
    const float boxW   = std::max(1.0f, cw - 2 * margin), boxH = std::max(1.0f, ch - 2 * margin);
    if (SUCCEEDED(impl_->d2d->CreateWicBitmapRenderTarget(bmp, props, &rt)) &&
        SUCCEEDED(impl_->dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, size, L"", &fmt)) &&
        SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &white)) &&
        SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.9f), &dark)) &&
        SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, st.boxAlpha), &back))) {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        const D2D1_RECT_F box = D2D1::RectF(cx + margin, cy + margin, cx + margin + boxW, cy + margin + boxH);
        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0, 0, 0, 0));
        if (st.box && SUCCEEDED(impl_->dw->CreateTextLayout(text.c_str(), UINT32(text.size()), fmt, boxW, boxH, &lay))) {
            DWRITE_TEXT_METRICS m{};
            lay->GetMetrics(&m);
            const float pad = size * 0.35f;
            rt->FillRectangle(D2D1::RectF(box.left + m.left - pad, box.top + m.top - pad * 0.5f, box.left + m.left + m.width + pad,
                                          box.top + m.top + m.height + pad * 0.5f),
                              back);
        }
        if (st.outline) {  // o texto escuro deslocado em 8 direcoes, depois o branco por cima
            const float o = std::max(1.0f, size / 14.0f);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dy)
                        rt->DrawText(text.c_str(), UINT32(text.size()), fmt,
                                     D2D1::RectF(box.left + dx * o, box.top + dy * o, box.right + dx * o, box.bottom + dy * o), dark);
        }
        rt->DrawText(text.c_str(), UINT32(text.size()), fmt, box, white);
        rt->EndDraw();
        WICRect r{0, 0, INT(w), INT(h)};
        bmp->CopyPixels(&r, w * 4, UINT(size_t(w) * h * 4), reinterpret_cast<BYTE*>(out.data()));
    }
    if (back) back->Release();
    if (dark) dark->Release();
    if (white) white->Release();
    if (lay) lay->Release();
    if (fmt) fmt->Release();
    if (rt) rt->Release();
    bmp->Release();
}

} // namespace bs::native::video
