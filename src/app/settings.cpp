#include "app/settings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

static const wchar_t* kSection = L"overlay";

Settings Settings::load(const std::wstring& ini) {
    Settings s;
    const wchar_t* p = ini.c_str();
    s.x = GetPrivateProfileIntW(kSection, L"x", s.x, p);
    s.y = GetPrivateProfileIntW(kSection, L"y", s.y, p);
    s.locked = GetPrivateProfileIntW(kSection, L"locked", 0, p) != 0;
    s.chroma = GetPrivateProfileIntW(kSection, L"chroma", 0, p) != 0;
    wchar_t sc[32];
    GetPrivateProfileStringW(kSection, L"scale", L"1.0", sc, 32, p);
    s.scale = std::clamp((float)_wtof(sc), 0.6f, 2.5f);
    return s;
}

void Settings::save(const std::wstring& ini) const {
    const wchar_t* p = ini.c_str();
    wchar_t b[32];
    swprintf(b, 32, L"%d", x);       WritePrivateProfileStringW(kSection, L"x", b, p);
    swprintf(b, 32, L"%d", y);       WritePrivateProfileStringW(kSection, L"y", b, p);
    swprintf(b, 32, L"%.2f", scale); WritePrivateProfileStringW(kSection, L"scale", b, p);
    WritePrivateProfileStringW(kSection, L"locked", locked ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"chroma", chroma ? L"1" : L"0", p);
}
