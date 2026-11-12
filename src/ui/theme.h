#pragma once
#include <cwchar>
#include <string>

#include "core/dan.h"

namespace ui {

namespace col {
const unsigned bg = 0x14141C, card = 0x1E1E2A, line = 0x2C2C3C, text = 0xF2F2F7, dim = 0x8A8AA0, faint = 0x55556A,
               accent = 0xFF66AA, accent2 = 0x8A6CFF, max = 0xBFF4FF, h300 = 0xFFD45A, h200 = 0x6EE07A,
               h100 = 0x4FA8FF, h50 = 0xA8A8B8, miss = 0xFF5A5A;
}

struct Judge { const wchar_t* label; long long v; unsigned c; };
struct Judges { Judge j[6]; };

inline Judges judges(const Hits& h) {
    return {{{L"MAX", h.max, col::max}, {L"300", h.h300, col::h300}, {L"200", h.h200, col::h200},
             {L"100", h.h100, col::h100}, {L"50", h.h50, col::h50}, {L"MISS", h.miss, col::miss}}};
}

struct Grade { const wchar_t* label; unsigned color; };

inline Grade grade(double acc, long long judged) {
    if (judged == 0) return {L"-", col::faint};
    if (acc >= 100.0) return {L"SS", 0xFFF4C2};
    if (acc > 95.0) return {L"S", col::h300};
    if (acc > 90.0) return {L"A", col::h200};
    if (acc > 80.0) return {L"B", col::h100};
    if (acc > 70.0) return {L"C", 0xC27BFF};
    return {L"D", col::miss};
}

inline std::wstring fmtAcc(double a) {
    wchar_t b[32];
    swprintf(b, 32, L"%.2f%%", a);
    return b;
}

inline std::wstring fmtNum(long long v) { return std::to_wstring(v); }

}
