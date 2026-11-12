#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <string>

namespace ui {

struct Fonts {
    IDWriteTextFormat *tiny, *sm, *body, *bold, *title, *huge, *num, *mini;
};
extern Fonts F;

bool initCanvas();

HDC beginFrame(int width, int height, float scale, D2D1_COLOR_F clear);
void endFrame();

D2D1_COLOR_F rgb(unsigned hex, float a = 1.0f);
void fillRound(D2D1_RECT_F r, float rad, D2D1_COLOR_F c);
void strokeRound(D2D1_RECT_F r, float rad, D2D1_COLOR_F c, float w = 1.0f);
void text(const std::wstring& s, IDWriteTextFormat* f, D2D1_RECT_F r, D2D1_COLOR_F c,
          DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING);
void gradientBar(D2D1_RECT_F r, float rad, unsigned c1, unsigned c2);

}
