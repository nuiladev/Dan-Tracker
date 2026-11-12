#include "ui/canvas.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "gdi32.lib")

namespace ui {

Fonts F;

static ID2D1Factory* g_d2d;
static IDWriteFactory* g_dw;
static ID2D1DCRenderTarget* g_rt;
static ID2D1SolidColorBrush* g_brush;
static HDC g_memDC;
static HBITMAP g_dib;
static int g_pxW = 0, g_pxH = 0;

static IDWriteTextFormat* makeFont(const wchar_t* family, float size, DWRITE_FONT_WEIGHT w) {
    IDWriteTextFormat* f = nullptr;
    g_dw->CreateTextFormat(family, nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    IDWriteInlineObject* ell = nullptr;
    g_dw->CreateEllipsisTrimmingSign(f, &ell);
    f->SetTrimming(&trim, ell);
    if (ell) ell->Release();
    return f;
}

static void createTarget() {
    D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    g_d2d->CreateDCRenderTarget(&p, &g_rt);
    g_rt->CreateSolidColorBrush(rgb(0xFFFFFF), &g_brush);
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
}

bool initCanvas() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw))) return false;
    F.tiny = makeFont(L"Segoe UI", 10.5f, DWRITE_FONT_WEIGHT_BOLD);
    F.sm = makeFont(L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    F.body = makeFont(L"Segoe UI", 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    F.bold = makeFont(L"Segoe UI", 15.0f, DWRITE_FONT_WEIGHT_BOLD);
    F.title = makeFont(L"Segoe UI", 18.0f, DWRITE_FONT_WEIGHT_BOLD);
    F.huge = makeFont(L"Bahnschrift", 44.0f, DWRITE_FONT_WEIGHT_BOLD);
    F.num = makeFont(L"Bahnschrift", 15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    F.mini = makeFont(L"Bahnschrift", 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    createTarget();
    g_memDC = CreateCompatibleDC(nullptr);
    return g_rt && g_memDC;
}

static void ensureSurface(int w, int h) {
    if (w == g_pxW && h == g_pxH && g_dib) return;
    if (g_dib) DeleteObject(g_dib);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    g_dib = CreateDIBSection(g_memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(g_memDC, g_dib);
    g_pxW = w;
    g_pxH = h;
}

HDC beginFrame(int width, int height, float scale, D2D1_COLOR_F clear) {
    ensureSurface(width, height);
    RECT rc = {0, 0, width, height};
    g_rt->BindDC(g_memDC, &rc);
    g_rt->BeginDraw();
    g_rt->Clear(clear);
    g_rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
    return g_memDC;
}

void endFrame() {
    if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET) {
        g_brush->Release();
        g_rt->Release();
        createTarget();
    }
}

D2D1_COLOR_F rgb(unsigned hex, float a) {
    return D2D1::ColorF(((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, a);
}

void fillRound(D2D1_RECT_F r, float rad, D2D1_COLOR_F c) {
    g_brush->SetColor(c);
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), g_brush);
}

void strokeRound(D2D1_RECT_F r, float rad, D2D1_COLOR_F c, float w) {
    g_brush->SetColor(c);
    g_rt->DrawRoundedRectangle(D2D1::RoundedRect(r, rad, rad), g_brush, w);
}

void text(const std::wstring& s, IDWriteTextFormat* f, D2D1_RECT_F r, D2D1_COLOR_F c, DWRITE_TEXT_ALIGNMENT a) {
    f->SetTextAlignment(a);
    g_brush->SetColor(c);
    g_rt->DrawText(s.c_str(), (UINT32)s.size(), f, r, g_brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void gradientBar(D2D1_RECT_F r, float rad, unsigned c1, unsigned c2) {
    D2D1_GRADIENT_STOP stops[2] = {{0.0f, rgb(c1)}, {1.0f, rgb(c2)}};
    ID2D1GradientStopCollection* coll = nullptr;
    g_rt->CreateGradientStopCollection(stops, 2, &coll);
    ID2D1LinearGradientBrush* gb = nullptr;
    g_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({r.left, r.top}, {r.right, r.top}), coll, &gb);
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), gb);
    gb->Release();
    coll->Release();
}

}
