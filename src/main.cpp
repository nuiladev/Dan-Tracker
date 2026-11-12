#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "core/dan.h"
#include "obs/obs_server.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

static Config g_cfg;
static std::string g_cfgError;
static std::string g_lastBody;
static std::wstring g_hotkeyWarn;
static std::mutex g_mutex;
static DanTracker g_tracker(&g_cfg);
static std::atomic<bool> g_running{true};
static std::wstring g_exeDir, g_iniPath;

static HWND g_hwnd;
static bool g_locked = false;
static bool g_showCredits = false;
struct Button { D2D1_RECT_F r; int cmd; };
static Button g_buttons[2];
static int g_buttonCount = 0;
static bool g_hidden = false;
static POINT g_shownPos{}, g_obsShownPos{};
static const UINT WM_TRAY = WM_APP + 1;
static UINT g_taskbarCreated;
static bool g_chroma = false;
static HWND g_obs;
static float g_scale = 1.0f;
static const float kWidth = 360.0f;
static const int kHidePad = 2;
static int g_pxW = 0, g_pxH = 0;

static ID2D1Factory* g_d2d;
static IDWriteFactory* g_dw;
static ID2D1DCRenderTarget* g_rt;
static HDC g_memDC;
static HBITMAP g_dib;

enum { HK_LOCK = 1, HK_RELOAD, HK_QUIT, HK_BIGGER, HK_SMALLER, HK_CHROMA, HK_MARK, HK_HIDE };
enum { CMD_LOCK = 100, CMD_RELOAD, CMD_CONFIG, CMD_QUIT, CMD_CHROMA, CMD_MARK, CMD_DUMP, CMD_OBSURL, CMD_CREDITS, CMD_HIDE };

static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static void pollThread() {
    std::wstring host = widen(g_cfg.host);
    HINTERNET session = WinHttpOpen(L"DanTracker/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    HINTERNET conn = session ? WinHttpConnect(session, host.c_str(), (INTERNET_PORT)g_cfg.port, 0) : nullptr;
    if (session) WinHttpSetTimeouts(session, 1000, 1000, 1000, 2000);
    std::string body;
    while (g_running) {
        bool ok = false;
        HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", L"/json/v2", nullptr, nullptr, nullptr, 0) : nullptr;
        if (req && WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
            body.clear();
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                size_t old = body.size();
                body.resize(old + avail);
                DWORD read = 0;
                if (!WinHttpReadData(req, &body[old], avail, &read)) break;
                body.resize(old + read);
            }
            Json j;
            if (Json::parse(body, j) && j.type == Json::Object && !j["state"].isNull()) {
                TosuSample s = TosuSample::fromJson(j);
                std::lock_guard<std::mutex> lk(g_mutex);
                g_lastBody.swap(body);
                g_tracker.update(s);
                ok = true;
            }
        }
        if (req) WinHttpCloseHandle(req);
        if (!ok) {
            std::lock_guard<std::mutex> lk(g_mutex);
            g_tracker.update(TosuSample{});
        }
        Sleep(ok ? 33 : 1000);
    }
    if (conn) WinHttpCloseHandle(conn);
    if (session) WinHttpCloseHandle(session);
}

static std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

static std::string obsState() {
    DanTracker t(nullptr);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        t = g_tracker;
    }
    char b[256];
    std::string o = "{";
    o += "\"connected\":" + std::string(t.connected ? "true" : "false");
    o += ",\"isDan\":" + std::string(t.isDan ? "true" : "false");
    o += ",\"finished\":" + std::string(t.finished ? "true" : "false");
    o += ",\"playing\":" + std::string(t.state == 2 ? "true" : "false");
    o += ",\"current\":" + std::to_string(t.current);
    snprintf(b, sizeof(b), ",\"liveMs\":%.0f", t.liveMs); o += b;
    o += ",\"danName\":" + jsonStr(t.danName) + ",\"mapLabel\":" + jsonStr(t.mapLabel) + ",\"note\":" + jsonStr(t.note);
    o += ",\"songs\":[";
    for (size_t i = 0; i < t.songs.size(); ++i) {
        const SongView& s = t.songs[i];
        const char* st = s.status == SongStatus::Done ? "done" : s.status == SongStatus::Live ? "live" : "upcoming";
        snprintf(b, sizeof(b),
                 "%s{\"status\":\"%s\",\"acc\":%.4f,\"judged\":%lld,\"max\":%lld,\"h300\":%lld,\"h200\":%lld,"
                 "\"h100\":%lld,\"h50\":%lld,\"miss\":%lld,\"startMs\":%.0f,\"endMs\":%.0f,\"name\":",
                 i ? "," : "", st, maniaAccuracy(s.hits, t.scoreV2), s.hits.total(), s.hits.max, s.hits.h300, s.hits.h200,
                 s.hits.h100, s.hits.h50, s.hits.miss, s.startMs, s.endMs);
        o += b + jsonStr(s.name) + "}";
    }
    const Hits& c = t.cumulative;
    snprintf(b, sizeof(b),
             "],\"total\":{\"acc\":%.4f,\"judged\":%lld,\"max\":%lld,\"h300\":%lld,\"h200\":%lld,\"h100\":%lld,"
             "\"h50\":%lld,\"miss\":%lld}}",
             maniaAccuracy(c, t.scoreV2), c.total(), c.max, c.h300, c.h200, c.h100, c.h50, c.miss);
    return o + b;
}

static std::wstring obsUrl() { return L"http://127.0.0.1:" + std::to_wstring(g_cfg.obsPort) + L"/"; }

static D2D1_COLOR_F rgb(unsigned hex, float a = 1.0f) {
    return D2D1::ColorF(((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, a);
}

namespace col {
const unsigned bg = 0x14141C, card = 0x1E1E2A, line = 0x2C2C3C, text = 0xF2F2F7, dim = 0x8A8AA0, faint = 0x55556A,
               accent = 0xFF66AA, accent2 = 0x8A6CFF, max = 0xBFF4FF, h300 = 0xFFD45A, h200 = 0x6EE07A,
               h100 = 0x4FA8FF, h50 = 0xA8A8B8, miss = 0xFF5A5A;
}

struct Judge { const wchar_t* label; long long v; unsigned c; };
struct Judges { Judge j[6]; };
static Judges judges(const Hits& h) {
    return {{{L"MAX", h.max, col::max}, {L"300", h.h300, col::h300}, {L"200", h.h200, col::h200},
             {L"100", h.h100, col::h100}, {L"50", h.h50, col::h50}, {L"MISS", h.miss, col::miss}}};
}

struct Fonts {
    IDWriteTextFormat *tiny, *sm, *body, *bold, *title, *huge, *num, *mini;
} F;

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

static ID2D1SolidColorBrush* g_brush;

static void fillRound(D2D1_RECT_F r, float rad, D2D1_COLOR_F c) {
    g_brush->SetColor(c);
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), g_brush);
}
static void strokeRound(D2D1_RECT_F r, float rad, D2D1_COLOR_F c, float w = 1.0f) {
    g_brush->SetColor(c);
    g_rt->DrawRoundedRectangle(D2D1::RoundedRect(r, rad, rad), g_brush, w);
}
static void text(const std::wstring& s, IDWriteTextFormat* f, D2D1_RECT_F r, D2D1_COLOR_F c,
                 DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING) {
    f->SetTextAlignment(a);
    g_brush->SetColor(c);
    g_rt->DrawText(s.c_str(), (UINT32)s.size(), f, r, g_brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
static void gradientBar(D2D1_RECT_F r, float rad, unsigned c1, unsigned c2) {
    D2D1_GRADIENT_STOP stops[2] = {{0.0f, rgb(c1)}, {1.0f, rgb(c2)}};
    ID2D1GradientStopCollection* coll = nullptr;
    g_rt->CreateGradientStopCollection(stops, 2, &coll);
    ID2D1LinearGradientBrush* gb = nullptr;
    g_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({r.left, r.top}, {r.right, r.top}), coll, &gb);
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), gb);
    gb->Release();
    coll->Release();
}

static std::wstring fmtAcc(double a) {
    wchar_t b[32];
    swprintf(b, 32, L"%.2f%%", a);
    return b;
}
static std::wstring fmtNum(long long v) { return std::to_wstring(v); }

static void grade(double acc, long long judged, const wchar_t*& g, unsigned& c) {
    if (judged == 0) { g = L"-"; c = col::faint; return; }
    if (acc >= 100.0) { g = L"SS"; c = 0xFFF4C2; }
    else if (acc > 95.0) { g = L"S"; c = col::h300; }
    else if (acc > 90.0) { g = L"A"; c = col::h200; }
    else if (acc > 80.0) { g = L"B"; c = col::h100; }
    else if (acc > 70.0) { g = L"C"; c = 0xC27BFF; }
    else { g = L"D"; c = col::miss; }
}

static const float kRowH = 44.0f, kRowStep = 48.0f, kTotalH = 74.0f, kFooterH = 30.0f;

static float contentHeight(const DanTracker& t) {
    if (g_showCredits) return 44.0f + 124.0f + 12.0f + (g_locked ? 0.0f : kFooterH);
    if (!t.connected || !t.isDan) return g_hotkeyWarn.empty() ? 150.0f : 166.0f;
    return 44.0f + 168.0f + 12.0f + t.songs.size() * kRowStep + 8.0f + kTotalH + 8.0f + (g_locked ? 0.0f : kFooterH);
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

static void drawFooter(float y) {
    if (g_locked) return;
    const float P = 18.0f;
    text(L"Drag to move  ·  Ctrl+Shift+L lock", F.tiny, {P, y, kWidth - 150, y + 18}, rgb(col::faint));
    D2D1_RECT_F tab = {kWidth - P - 70, y - 1, kWidth - P, y + 19};
    fillRound(tab, 10, rgb(col::accent, g_showCredits ? 0.9f : 0.18f));
    strokeRound(tab, 10, rgb(col::accent, 0.8f));
    text(g_showCredits ? L"BACK" : L"CREDITS", F.tiny, tab, rgb(g_showCredits ? 0xFFFFFF : col::accent), DWRITE_TEXT_ALIGNMENT_CENTER);
    D2D1_RECT_F hide = {tab.left - 56, y - 1, tab.left - 6, y + 19};
    fillRound(hide, 10, rgb(col::line, 0.6f));
    strokeRound(hide, 10, rgb(col::dim, 0.6f));
    text(L"HIDE", F.tiny, hide, rgb(col::dim), DWRITE_TEXT_ALIGNMENT_CENTER);
    g_buttons[0] = {tab, CMD_CREDITS};
    g_buttons[1] = {hide, CMD_HIDE};
    g_buttonCount = 2;
}

static void drawCredits() {
    const float P = 16.0f, W = kWidth;
    fillRound({P, 14, P + 40, 32}, 9, rgb(col::accent));
    text(L"DAN", F.tiny, {P, 14, P + 40, 32}, rgb(0xFFFFFF), DWRITE_TEXT_ALIGNMENT_CENTER);
    text(L"Dan Tracker", F.bold, {P + 50, 12, W - P, 34}, rgb(col::text));

    D2D1_RECT_F card = {P - 4, 44, W - P + 4, 44 + 124};
    fillRound(card, 14, rgb(col::card, 0.95f));
    strokeRound(card, 14, rgb(col::line), 1.0f);
    float cx = card.left + 16, cr = card.right - 16, y = card.top;
    text(L"CREDITS", F.tiny, {cx, y + 12, cr, y + 28}, rgb(col::accent));
    struct { const wchar_t *role, *name, *discord; unsigned c; } people[] = {
        {L"CREATOR & PROGRAMMER", L"NuilaDev", L"Discord - zrwz0", col::accent},
        {L"TESTER", L"eQualize", L"Discord - frawog", col::accent2}};
    y += 34;
    for (auto& p : people) {
        fillRound({cx, y + 6, cx + 3, y + 38}, 1.5f, rgb(p.c));
        text(p.role, F.tiny, {cx + 12, y + 4, cr, y + 18}, rgb(col::dim));
        text(p.name, F.title, {cx + 12, y + 16, cr - 110, y + 40}, rgb(col::text));
        text(p.discord, F.tiny, {cr - 110, y + 16, cr, y + 40}, rgb(col::faint), DWRITE_TEXT_ALIGNMENT_TRAILING);
        y += 42;
    }
    drawFooter(card.bottom + 12);
}

static void drawWaiting(const DanTracker& t, float H) {
    text(L"DAN TRACKER", F.sm, {18, 16, 300, 34}, rgb(col::accent));
    fillRound({kWidth - 30, 21, kWidth - 22, 29}, 4, rgb(t.connected ? col::h200 : col::miss));
    std::wstring head, sub;
    if (!t.connected) {
        head = L"Waiting for tosu...";
        sub = L"Open tosu (port " + std::to_wstring(g_cfg.port) + L") and osu!";
    } else {
        head = t.mapLabel.size() > 6 ? L"No dan selected" : L"Select a dan";
        sub = widen(t.note.empty() ? "pick an osu!mania dan map" : t.note);
    }
    text(head, F.title, {18, 44, kWidth - 18, 72}, rgb(col::text));
    if (t.connected && t.mapLabel.size() > 6) text(widen(t.mapLabel), F.sm, {18, 74, kWidth - 18, 92}, rgb(col::dim));
    text(sub, F.sm, {18, 94, kWidth - 18, 112}, rgb(col::faint));
    if (!g_cfgError.empty()) text(widen(g_cfgError), F.tiny, {18, 114, kWidth - 18, 130}, rgb(col::h300));
    if (!g_hotkeyWarn.empty()) text(g_hotkeyWarn, F.tiny, {18, 130, kWidth - 18, 146}, rgb(col::miss));
    drawFooter(H - 30);
}

static void drawDan(const DanTracker& t, float H) {
    const float P = 16.0f, W = kWidth;
    int n = (int)t.songs.size();
    int cur = std::clamp(t.current, 0, n - 1);
    const SongView& s = t.songs[cur];

    fillRound({P, 14, P + 40, 32}, 9, rgb(col::accent));
    text(L"DAN", F.tiny, {P, 14, P + 40, 32}, rgb(0xFFFFFF), DWRITE_TEXT_ALIGNMENT_CENTER);
    text(widen(t.danName), F.bold, {P + 50, 12, W - P - 70, 34}, rgb(col::text));
    std::wstring st = t.finished ? L"COMPLETE" : (t.state == 2 ? L"PLAYING" : L"IDLE");
    text(st, F.tiny, {W - P - 72, 14, W - P - 12, 32}, rgb(t.state == 2 ? col::h200 : col::dim), DWRITE_TEXT_ALIGNMENT_TRAILING);
    fillRound({W - P - 6, 20, W - P, 26}, 3, rgb(col::h200));

    D2D1_RECT_F card = {P - 4, 44, W - P + 4, 44 + 168};
    fillRound(card, 14, rgb(col::card, 0.95f));
    strokeRound(card, 14, rgb(col::line), 1.0f);
    float cx = card.left + 16, cr = card.right - 16, cy = card.top;

    double nextStart = -1;
    if (t.state == 2 && !t.finished) {
        if (t.liveMs > s.endMs && cur + 1 < n) nextStart = t.songs[cur + 1].startMs;
        else if (t.liveMs < s.startMs && cur > 0) nextStart = s.startMs;
    }
    wchar_t tag[64];
    swprintf(tag, 64, L"SONG %d / %d", cur + 1, n);
    text(tag, F.tiny, {cx, cy + 12, cx + 160, cy + 28}, rgb(col::accent));
    if (nextStart >= 0) {
        wchar_t b[64];
        swprintf(b, 64, L"BREAK  %.1fs", std::max(0.0, (nextStart - t.liveMs) / 1000.0));
        text(b, F.tiny, {cr - 160, cy + 12, cr, cy + 28}, rgb(col::h300), DWRITE_TEXT_ALIGNMENT_TRAILING);
    } else if (t.state == 2 && !t.finished) {
        text(L"● LIVE", F.tiny, {cr - 160, cy + 12, cr, cy + 28}, rgb(col::miss), DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    text(widen(s.name), F.title, {cx, cy + 28, cr, cy + 52}, rgb(col::text));

    double acc = maniaAccuracy(s.hits, t.scoreV2);
    const wchar_t* g;
    unsigned gc;
    grade(acc, s.hits.total(), g, gc);
    text(s.hits.total() ? fmtAcc(acc) : L"--.--%", F.huge, {cx - 2, cy + 52, cr - 60, cy + 104}, rgb(col::text));
    text(g, F.huge, {cr - 70, cy + 52, cr, cy + 104}, rgb(gc), DWRITE_TEXT_ALIGNMENT_TRAILING);

    float prog = 0.0f;
    if (s.endMs > s.startMs) prog = (float)std::clamp((t.liveMs - s.startMs) / (s.endMs - s.startMs), 0.0, 1.0);
    if (t.finished || s.status == SongStatus::Done) prog = 1.0f;
    D2D1_RECT_F bar = {cx, cy + 112, cr, cy + 117};
    fillRound(bar, 2.5f, rgb(col::line));
    if (prog > 0.01f) gradientBar({bar.left, bar.top, bar.left + (bar.right - bar.left) * prog, bar.bottom}, 2.5f, col::accent2, col::accent);

    Judges js = judges(s.hits);
    float jw = (cr - cx) / 6.0f;
    for (int i = 0; i < 6; ++i) {
        float x = cx + jw * i;
        text(js.j[i].label, F.tiny, {x, cy + 126, x + jw, cy + 140}, rgb(js.j[i].c), DWRITE_TEXT_ALIGNMENT_CENTER);
        text(fmtNum(js.j[i].v), F.num, {x, cy + 140, x + jw, cy + 160}, rgb(js.j[i].v ? col::text : col::faint), DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    float y = card.bottom + 12;
    for (int i = 0; i < n; ++i) {
        const SongView& sv = t.songs[i];
        bool live = i == cur && !t.finished && sv.status != SongStatus::Done;
        bool upcoming = sv.status == SongStatus::Upcoming && !live;
        D2D1_RECT_F row = {P - 4, y, W - P + 4, y + kRowH};
        if (live) {
            fillRound(row, 9, rgb(col::accent, 0.12f));
            fillRound({row.left, row.top + 7, row.left + 3, row.bottom - 7}, 1.5f, rgb(col::accent));
        }
        unsigned circle = sv.status == SongStatus::Done ? col::accent : (live ? col::accent2 : col::line);
        fillRound({P + 6, y + 6, P + 26, y + 26}, 10, rgb(circle));
        text(std::to_wstring(i + 1), F.tiny, {P + 6, y + 6, P + 26, y + 26}, rgb(0xFFFFFF), DWRITE_TEXT_ALIGNMENT_CENTER);
        text(widen(sv.name), F.body, {P + 36, y + 2, W - P - 118, y + 30}, rgb(upcoming ? col::faint : col::text));

        if (upcoming) {
            text(L"—", F.num, {W - P - 110, y + 2, W - P - 4, y + 30}, rgb(col::faint), DWRITE_TEXT_ALIGNMENT_TRAILING);
        } else {
            double a = maniaAccuracy(sv.hits, t.scoreV2);
            const wchar_t* gg;
            unsigned ggc;
            grade(a, sv.hits.total(), gg, ggc);
            text(sv.hits.total() ? fmtAcc(a) : L"--.--%", F.num, {W - P - 112, y + 2, W - P - 30, y + 30},
                 rgb(col::text, live ? 1.0f : 0.9f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            text(gg, F.bold, {W - P - 26, y + 2, W - P - 2, y + 30}, rgb(ggc), DWRITE_TEXT_ALIGNMENT_TRAILING);

            Judges sj = judges(sv.hits);
            float x0 = P + 36, sw = (W - P - 2 - x0) / 6.0f;
            for (int k = 0; k < 6; ++k)
                text(fmtNum(sj.j[k].v), F.mini, {x0 + sw * k, y + 26, x0 + sw * (k + 1), y + 42},
                     rgb(sj.j[k].c, sj.j[k].v ? 1.0f : 0.35f));
        }
        y += kRowStep;
    }

    y += 8;
    D2D1_RECT_F tot = {P - 4, y, W - P + 4, y + kTotalH};
    fillRound(tot, 12, rgb(col::card, 0.95f));
    strokeRound(tot, 12, rgb(col::line));
    text(L"TOTAL DAN", F.tiny, {P + 10, y + 6, P + 120, y + 34}, rgb(col::dim));
    double ta = maniaAccuracy(t.cumulative, t.scoreV2);
    text(t.cumulative.total() ? fmtAcc(ta) : L"--.--%", F.title, {W - P - 130, y + 6, W - P - 8, y + 34}, rgb(col::text),
         DWRITE_TEXT_ALIGNMENT_TRAILING);
    Judges tj = judges(t.cumulative);
    float tx = P + 4, tw = (W - P - 4 - tx) / 6.0f;
    for (int k = 0; k < 6; ++k) {
        float x = tx + tw * k;
        text(tj.j[k].label, F.tiny, {x, y + 36, x + tw, y + 50}, rgb(tj.j[k].c), DWRITE_TEXT_ALIGNMENT_CENTER);
        text(fmtNum(tj.j[k].v), F.num, {x, y + 50, x + tw, y + 68}, rgb(tj.j[k].v ? col::text : col::faint), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    y += kTotalH + 8;

    if (!t.note.empty()) text(widen(t.note), F.tiny, {P, H - 22, W - P, H - 6}, rgb(col::h300));
    drawFooter(y);
}

static void drawFrame(const DanTracker& t, float H, int pw, int ph, bool green);

static void render() {
    DanTracker t(nullptr);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        t = g_tracker;
    }
    float H = contentHeight(t) + (t.note.empty() || !t.isDan || g_showCredits ? 0.0f : 18.0f);
    int pw = (int)std::ceil(kWidth * g_scale) + kHidePad, ph = (int)std::ceil(H * g_scale);
    ensureSurface(pw, ph);

    for (int pass = 0; pass < (g_obs ? 2 : 1); ++pass) {
        bool green = pass == 1;
        drawFrame(t, H, pw, ph, green);
        if (!green) {
            RECT wr;
            GetWindowRect(g_hwnd, &wr);
            POINT dst = {wr.left, wr.top}, src = {0, 0};
            SIZE sz = {pw, ph};
            BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            UpdateLayeredWindow(g_hwnd, nullptr, &dst, &sz, g_memDC, &src, 0, &bf, ULW_ALPHA);
        } else {
            RECT cr;
            GetClientRect(g_obs, &cr);
            if (cr.right != pw || cr.bottom != ph) {
                RECT wr = {0, 0, pw, ph};
                AdjustWindowRectEx(&wr, (DWORD)GetWindowLongPtrW(g_obs, GWL_STYLE), FALSE, 0);
                SetWindowPos(g_obs, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            HDC dc = GetDC(g_obs);
            BitBlt(dc, 0, 0, pw, ph, g_memDC, 0, 0, SRCCOPY);
            ReleaseDC(g_obs, dc);
        }
    }
}

static void drawFrame(const DanTracker& t, float H, int pw, int ph, bool green) {
    RECT rc = {0, 0, pw, ph};
    g_rt->BindDC(g_memDC, &rc);
    g_rt->BeginDraw();
    g_rt->Clear(green ? D2D1::ColorF(0, 1, 0, 1) : D2D1::ColorF(0, 0, 0, 0));
    bool wasLocked = g_locked;
    if (green) g_locked = true;
    g_rt->SetTransform(D2D1::Matrix3x2F::Scale(g_scale, g_scale));

    D2D1_RECT_F panel = {0.5f, 0.5f, kWidth - 0.5f, H - 0.5f};
    fillRound(panel, 18, rgb(col::bg, green ? 1.0f : 0.90f));
    strokeRound(panel, 18, rgb(g_locked ? col::line : col::accent, g_locked ? 1.0f : 0.8f), 1.0f);
    gradientBar({18, 0.5f, kWidth - 18, 2.5f}, 1, col::accent2, col::accent);

    if (!green) g_buttonCount = 0;
    if (g_showCredits) drawCredits();
    else if (t.connected && t.isDan && !t.songs.empty()) drawDan(t, H);
    else drawWaiting(t, H);
    g_locked = wasLocked;

    if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET) {
        g_brush->Release();
        g_rt->Release();
        D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        g_d2d->CreateDCRenderTarget(&p, &g_rt);
        g_rt->CreateSolidColorBrush(rgb(0xFFFFFF), &g_brush);
        g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    }
}

static void applyLock() {
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    ex = g_locked ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
}

static int buttonAt(float x, float y) {
    for (int i = 0; i < g_buttonCount; ++i) {
        const D2D1_RECT_F& r = g_buttons[i].r;
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return i;
    }
    return -1;
}

static POINT hiddenPos(HWND h) {
    RECT r;
    GetWindowRect(h, &r);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    return {mi.rcMonitor.left - (r.right - r.left) + 1, mi.rcMonitor.bottom - 1};
}

static void moveWindow(HWND h, POINT p) {
    SetWindowPos(h, nullptr, p.x, p.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void setHidden(bool hide) {
    if (hide == g_hidden) return;
    HWND wins[2] = {g_hwnd, g_obs};
    POINT* saved[2] = {&g_shownPos, &g_obsShownPos};
    for (int i = 0; i < 2; ++i) {
        if (!wins[i]) continue;
        if (hide) {
            RECT r;
            GetWindowRect(wins[i], &r);
            *saved[i] = {r.left, r.top};
            moveWindow(wins[i], hiddenPos(wins[i]));
        } else {
            moveWindow(wins[i], *saved[i]);
        }
    }
    g_hidden = hide;
}

static void trayIcon(DWORD action) {
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = g_hwnd;
    n.uID = 1;
    n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    n.uCallbackMessage = WM_TRAY;
    n.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(n.szTip, L"Dan Tracker - click: show / hide, right-click: menu");
    Shell_NotifyIconW(action, &n);
}

static void saveSettings() {
    RECT r;
    GetWindowRect(g_hwnd, &r);
    if (g_hidden) r.left = g_shownPos.x, r.top = g_shownPos.y;
    wchar_t b[32];
    swprintf(b, 32, L"%ld", r.left);  WritePrivateProfileStringW(L"overlay", L"x", b, g_iniPath.c_str());
    swprintf(b, 32, L"%ld", r.top);   WritePrivateProfileStringW(L"overlay", L"y", b, g_iniPath.c_str());
    swprintf(b, 32, L"%.2f", g_scale); WritePrivateProfileStringW(L"overlay", L"scale", b, g_iniPath.c_str());
    WritePrivateProfileStringW(L"overlay", L"locked", g_locked ? L"1" : L"0", g_iniPath.c_str());
    WritePrivateProfileStringW(L"overlay", L"chroma", g_chroma ? L"1" : L"0", g_iniPath.c_str());
}

static void reloadConfig() {
    Config c;
    std::string err;
    c.load(g_exeDir + L"dans.json", err);
    c.loadMarked(g_exeDir + L"dans_marcados.txt");
    std::lock_guard<std::mutex> lk(g_mutex);
    g_cfg = c;
    g_cfgError = err;
    g_tracker.setConfig(&g_cfg);
}

static void showMenu(HWND hwnd) {
    POINT p;
    GetCursorPos(&p);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CMD_HIDE, g_hidden ? L"Show overlay\tCtrl+Shift+H" : L"Hide overlay (keeps showing in OBS)\tCtrl+Shift+H");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_LOCK, L"Lock (click-through)\tCtrl+Shift+L");
    AppendMenuW(m, MF_STRING, CMD_RELOAD, L"Reload dans.json\tCtrl+Shift+R");
    AppendMenuW(m, MF_STRING, CMD_MARK, L"Mark / unmark this map as a dan\tCtrl+Shift+D");
    AppendMenuW(m, MF_STRING, CMD_CONFIG, L"Open dans.json");
    AppendMenuW(m, MF_STRING, CMD_DUMP, L"Save diagnostics (tosu_debug.json)");
    AppendMenuW(m, MF_STRING, CMD_OBSURL, (L"Copy OBS Browser Source URL: " + obsUrl()).c_str());
    AppendMenuW(m, MF_STRING | (g_chroma ? MF_CHECKED : 0), CMD_CHROMA, L"OBS window (green background)\tCtrl+Shift+G");
    AppendMenuW(m, MF_STRING | (g_showCredits ? MF_CHECKED : 0), CMD_CREDITS, L"Credits");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_QUIT, L"Quit\tCtrl+Shift+Q");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(m);
}

static LRESULT CALLBACK obsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); render(); return 0; }
        case WM_ERASEBKGND: return 1;
        case WM_CLOSE: g_chroma = false; DestroyWindow(hwnd); g_obs = nullptr; saveSettings(); return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_MINIMIZE) { if (!g_hidden) setHidden(true); return 0; }
            break;
        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
            return hit >= HTLEFT && hit <= HTBOTTOMRIGHT ? HTBORDER : hit;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void setObsWindow(bool on) {
    if (!on) { if (g_obs) DestroyWindow(g_obs); g_obs = nullptr; return; }
    if (g_obs) return;
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = obsProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"DanTrackerOBS";
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        RegisterClassW(&wc);
        registered = true;
    }
    g_obs = CreateWindowExW(0, L"DanTrackerOBS", L"Dan Tracker (OBS)",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME, 80, 80, 380, 500, nullptr, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
    if (g_hidden) {
        g_obsShownPos = {80, 80};
        moveWindow(g_obs, hiddenPos(g_obs));
    }
    ShowWindow(g_obs, SW_SHOWNOACTIVATE);
}

static void command(int id) {
    switch (id) {
        case HK_HIDE: case CMD_HIDE: setHidden(!g_hidden); break;
        case HK_LOCK: case CMD_LOCK:
            g_locked = !g_locked;
            if (g_locked) g_showCredits = false;
            applyLock(); saveSettings(); break;
        case CMD_CREDITS: g_showCredits = !g_showCredits; break;
        case HK_RELOAD: case CMD_RELOAD: reloadConfig(); break;
        case HK_MARK: case CMD_MARK: {
            std::lock_guard<std::mutex> lk(g_mutex);
            if (g_tracker.checksum.empty()) break;
            g_cfg.toggleMarked(g_exeDir + L"dans_marcados.txt", g_tracker.checksum, g_tracker.mapLabel);
            g_tracker.setConfig(&g_cfg);
            break;
        }
        case CMD_OBSURL: {
            std::wstring url = obsUrl();
            if (OpenClipboard(g_hwnd)) {
                EmptyClipboard();
                HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (url.size() + 1) * sizeof(wchar_t));
                memcpy(GlobalLock(h), url.c_str(), (url.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
                CloseClipboard();
            }
            break;
        }
        case CMD_DUMP: {
            std::string body;
            { std::lock_guard<std::mutex> lk(g_mutex); body = g_lastBody; }
            std::wstring path = g_exeDir + L"tosu_debug.json";
            if (FILE* f = _wfopen(path.c_str(), L"wb")) { fwrite(body.data(), 1, body.size(), f); fclose(f); }
            ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
            break;
        }
        case CMD_CONFIG: ShellExecuteW(nullptr, L"open", (g_exeDir + L"dans.json").c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case HK_BIGGER: g_scale = std::min(2.5f, g_scale + 0.1f); saveSettings(); break;
        case HK_SMALLER: g_scale = std::max(0.6f, g_scale - 0.1f); saveSettings(); break;
        case HK_CHROMA: case CMD_CHROMA: g_chroma = !g_chroma; setObsWindow(g_chroma); saveSettings(); break;
        case HK_QUIT: case CMD_QUIT: DestroyWindow(g_hwnd); break;
    }
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST: {
            if (g_locked) return HTTRANSPARENT;
            RECT wr;
            GetWindowRect(hwnd, &wr);
            float x = (GET_X_LPARAM(lp) - wr.left) / g_scale, y = (GET_Y_LPARAM(lp) - wr.top) / g_scale;
            return buttonAt(x, y) >= 0 ? HTCLIENT : HTCAPTION;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; }
            break;
        case WM_LBUTTONUP: {
            int b = buttonAt(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale);
            if (b >= 0) command(g_buttons[b].cmd);
            return 0;
        }
        case WM_TRAY:
            if (lp == WM_LBUTTONUP) command(CMD_HIDE);
            else if (lp == WM_RBUTTONUP) showMenu(hwnd);
            return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCRBUTTONUP: showMenu(hwnd); return 0;
        case WM_EXITSIZEMOVE: saveSettings(); return 0;
        case WM_HOTKEY: command((int)wp); return 0;
        case WM_COMMAND: command(LOWORD(wp)); return 0;
        case WM_TIMER: render(); return 0;
        case WM_DESTROY:
            trayIcon(NIM_DELETE);
            saveSettings();
            PostQuitMessage(0);
            return 0;
    }
    if (msg == g_taskbarCreated) trayIcon(NIM_ADD);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE single = CreateMutexW(nullptr, TRUE, L"DanTracker.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    g_exeDir = path;
    g_exeDir = g_exeDir.substr(0, g_exeDir.find_last_of(L"\\/") + 1);
    g_iniPath = g_exeDir + L"overlay.ini";
    g_cfg.load(g_exeDir + L"dans.json", g_cfgError);
    g_cfg.loadMarked(g_exeDir + L"dans_marcados.txt");

    int x = GetPrivateProfileIntW(L"overlay", L"x", 40, g_iniPath.c_str());
    int y = GetPrivateProfileIntW(L"overlay", L"y", 120, g_iniPath.c_str());
    g_locked = GetPrivateProfileIntW(L"overlay", L"locked", 0, g_iniPath.c_str()) != 0;
    g_chroma = GetPrivateProfileIntW(L"overlay", L"chroma", 0, g_iniPath.c_str()) != 0;
    wchar_t sc[32];
    GetPrivateProfileStringW(L"overlay", L"scale", L"1.0", sc, 32, g_iniPath.c_str());
    g_scale = std::clamp((float)_wtof(sc), 0.6f, 2.5f);

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw);
    F.tiny = makeFont(L"Segoe UI", 10.5f, DWRITE_FONT_WEIGHT_BOLD);
    F.sm = makeFont(L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    F.body = makeFont(L"Segoe UI", 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    F.bold = makeFont(L"Segoe UI", 15.0f, DWRITE_FONT_WEIGHT_BOLD);
    F.title = makeFont(L"Segoe UI", 18.0f, DWRITE_FONT_WEIGHT_BOLD);
    F.huge = makeFont(L"Bahnschrift", 44.0f, DWRITE_FONT_WEIGHT_BOLD);
    F.num = makeFont(L"Bahnschrift", 15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    F.mini = makeFont(L"Bahnschrift", 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);

    D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    g_d2d->CreateDCRenderTarget(&p, &g_rt);
    g_rt->CreateSolidColorBrush(rgb(0xFFFFFF), &g_brush);
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    g_memDC = CreateCompatibleDC(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_SIZEALL);
    wc.lpszClassName = L"DanTracker";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);
    HWND owner = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST, wc.lpszClassName,
                             L"Dan Tracker", WS_POPUP, x, y, (int)kWidth, 200, owner, nullptr, hInst, nullptr);
    applyLock();
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    struct { int id; UINT vk; const wchar_t* name; } keys[] = {
        {HK_LOCK, 'L', L"L"}, {HK_RELOAD, 'R', L"R"}, {HK_QUIT, 'Q', L"Q"}, {HK_BIGGER, VK_OEM_PLUS, L"+"},
        {HK_SMALLER, VK_OEM_MINUS, L"-"}, {HK_MARK, 'D', L"D"}, {HK_CHROMA, 'G', L"G"},
        {HK_HIDE, 'H', L"H"}};
    std::wstring taken;
    for (auto& k : keys)
        if (!RegisterHotKey(g_hwnd, k.id, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, k.vk)) taken += std::wstring(L" ") + k.name;
    if (!taken.empty()) g_hotkeyWarn = L"Hotkeys taken by another program (Ctrl+Shift+" + taken.substr(1) + L"): use right-click";

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    trayIcon(NIM_ADD);
    if (g_chroma) setObsWindow(true);
    if (!startObsServer(g_cfg.obsPort, obsState))
        g_hotkeyWarn += (g_hotkeyWarn.empty() ? L"" : L" | ") + (L"OBS port " + std::to_wstring(g_cfg.obsPort) + L" in use");
    std::thread poller(pollThread);
    SetTimer(g_hwnd, 1, 33, nullptr);
    render();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    g_running = false;
    poller.join();
    stopObsServer();
    ReleaseMutex(single);
    return 0;
}
