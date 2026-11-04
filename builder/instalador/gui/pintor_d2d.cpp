#include "instalador/gui/pintor_d2d.h"

#include <algorithm>
#include <cfloat>

namespace bs::builder::instalador::gui {

Fabricas& Fabricas::get() {
    // de proposito sem destruir: os objetos COM nao podem ser soltos depois do CoUninitialize, na saida do processo
    static Fabricas& r = *new Fabricas;
    static bool      feito = [] {
        r.ok = SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, r.d2d.put())) &&
               SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                             reinterpret_cast<IUnknown**>(r.dw.put()))) &&
               SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(r.wic.put())));
        return true;
    }();
    (void)feito;
    return r;
}

namespace {
D2D1_COLOR_F cor(const Cor& c) { return {c.r, c.g, c.b, c.a}; }
D2D1_RECT_F  ret(const Ret& r) { return {r.x, r.y, r.x + r.w, r.y + r.h}; }
} // namespace

PintorD2D::PintorD2D(ID2D1RenderTarget* rt, float escala) : rt_(rt), escala_(escala) {
    rt_->SetDpi(96.f * escala, 96.f * escala);
    rt_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), pincel_.put());
}

PintorD2D::~PintorD2D() {
    while (recortes_ > 0) soltar_recorte();
    for (auto& [k, f] : formatos_) f->Release();
}

ID2D1SolidColorBrush* PintorD2D::pincel(const Cor& c) {
    pincel_->SetColor(cor(c));
    return pincel_.p;
}

void PintorD2D::limpar(const Cor& c) { rt_->Clear(cor(c)); }
void PintorD2D::retangulo(const Ret& r, const Cor& c) { rt_->FillRectangle(ret(r), pincel(c)); }

void PintorD2D::arredondado(const Ret& r, float raio, const Cor& c) {
    raio = std::min({raio, r.w / 2, r.h / 2});
    rt_->FillRoundedRectangle(D2D1::RoundedRect(ret(r), raio, raio), pincel(c));
}

void PintorD2D::contorno(const Ret& r, float raio, float e, const Cor& c) {
    const Ret d{r.x + e / 2, r.y + e / 2, r.w - e, r.h - e};
    raio = std::max(0.f, std::min({raio - e / 2, d.w / 2, d.h / 2}));
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(ret(d), raio, raio), pincel(c), e);
}

void PintorD2D::triangulo(float x0, float y0, float x1, float y1, float x2, float y2, const Cor& c) {
    Com<ID2D1PathGeometry> g;
    if (FAILED(Fabricas::get().d2d->CreatePathGeometry(g.put()))) return;
    Com<ID2D1GeometrySink> s;
    if (FAILED(g->Open(s.put()))) return;
    s->BeginFigure({x0, y0}, D2D1_FIGURE_BEGIN_FILLED);
    const D2D1_POINT_2F pts[] = {{x1, y1}, {x2, y2}};
    s->AddLines(pts, 2);
    s->EndFigure(D2D1_FIGURE_END_CLOSED);
    s->Close();
    rt_->FillGeometry(g.p, pincel(c));
}

void PintorD2D::degrade(const Ret& r, const Cor& topo, const Cor& base) {
    const D2D1_GRADIENT_STOP st[] = {{0, cor(topo)}, {1, cor(base)}};
    Com<ID2D1GradientStopCollection> col;
    if (FAILED(rt_->CreateGradientStopCollection(st, 2, col.put()))) return;
    Com<ID2D1LinearGradientBrush> b;
    if (FAILED(rt_->CreateLinearGradientBrush({{r.x, r.y}, {r.x, r.y + r.h}}, col.p, b.put()))) return;
    rt_->FillRectangle(ret(r), b.p);
}

IDWriteTextFormat* PintorD2D::formato(const Estilo& e) {
    const uint32_t k = uint32_t(e.tamanho * 8) << 8 | uint32_t(e.peso) << 4 | uint32_t(e.h) << 2 | uint32_t(e.v);
    if (auto it = formatos_.find(k); it != formatos_.end()) return it->second;
    IDWriteTextFormat* f = nullptr;
    if (FAILED(Fabricas::get().dw->CreateTextFormat(L"Segoe UI", nullptr,
                                                   e.peso == Peso::SemiNegrito ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, e.tamanho, L"", &f)))
        return nullptr;
    f->SetTextAlignment(e.h == Alinha::Centro ? DWRITE_TEXT_ALIGNMENT_CENTER
                        : e.h == Alinha::Fim  ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                              : DWRITE_TEXT_ALIGNMENT_LEADING);
    f->SetParagraphAlignment(e.v == Alinha::Centro ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                             : e.v == Alinha::Fim  ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                                   : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    formatos_[k] = f;
    return f;
}

bool PintorD2D::layout(std::wstring_view t, const Estilo& e, float w, float h, Com<IDWriteTextLayout>& out) {
    IDWriteTextFormat* f = formato(e);
    if (!f) return false;
    if (FAILED(Fabricas::get().dw->CreateTextLayout(t.data(), UINT32(t.size()), f, w, h, out.put()))) return false;
    out->SetWordWrapping(e.quebra ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    return true;
}

Medida PintorD2D::medir(std::wstring_view t, const Estilo& e, float largura) {
    Estilo semAlinhar = e;
    semAlinhar.h = Alinha::Inicio, semAlinhar.v = Alinha::Inicio;
    if (largura <= 0) semAlinhar.quebra = false;
    Com<IDWriteTextLayout> l;
    if (!layout(t, semAlinhar, largura > 0 ? largura : 100000.f, 100000.f, l)) return {};
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return {m.width, m.height, int(m.lineCount)};
}

void PintorD2D::texto(std::wstring_view t, const Estilo& e, const Ret& caixa, const Cor& c) {
    Com<IDWriteTextLayout> l;
    if (!layout(t, e, std::max(1.f, caixa.w), std::max(1.f, caixa.h), l)) return;
    rt_->DrawTextLayout({caixa.x, caixa.y}, l.p, pincel(c), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void PintorD2D::recortar(const Ret& r) {
    rt_->PushAxisAlignedClip(ret(r), D2D1_ANTIALIAS_MODE_ALIASED);
    ++recortes_;
}

void PintorD2D::soltar_recorte() {
    if (recortes_ <= 0) return;
    rt_->PopAxisAlignedClip();
    --recortes_;
}

Quadro::Quadro(float wDip, float hDip, float escala) {
    auto& f = Fabricas::get();
    if (!f.ok) return;
    const UINT w = UINT(wDip * escala + 0.5f), h = UINT(hDip * escala + 0.5f);
    if (FAILED(f.wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bmp_.put()))) return;
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                    96.f * escala, 96.f * escala);
    f.d2d->CreateWicBitmapRenderTarget(bmp_.p, props, rt_.put());
    if (rt_) rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
}

bool Quadro::salvar(const std::filesystem::path& png) {
    auto&                   f = Fabricas::get();
    Com<IWICStream>         st;
    Com<IWICBitmapEncoder>  enc;
    Com<IWICBitmapFrameEncode> fr;
    if (FAILED(f.wic->CreateStream(st.put())) || FAILED(st->InitializeFromFilename(png.c_str(), GENERIC_WRITE)) ||
        FAILED(f.wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, enc.put())) ||
        FAILED(enc->Initialize(st.p, WICBitmapEncoderNoCache)) || FAILED(enc->CreateNewFrame(fr.put(), nullptr)) ||
        FAILED(fr->Initialize(nullptr)))
        return false;
    UINT w = 0, h = 0;
    bmp_->GetSize(&w, &h);
    fr->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
    fr->SetPixelFormat(&fmt);
    return SUCCEEDED(fr->WriteSource(bmp_.p, nullptr)) && SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
}

} // namespace bs::builder::instalador::gui
