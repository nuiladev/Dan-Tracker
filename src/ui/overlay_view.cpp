#include "ui/overlay_view.h"

#include <algorithm>

#include "core/strings.h"
#include "ui/theme.h"

namespace ui {

static const float kRowH = 44.0f, kRowStep = 48.0f, kTotalH = 74.0f, kFooterH = 30.0f, kNoteH = 18.0f;

Action Buttons::at(float x, float y) const {
    for (int i = 0; i < count; ++i) {
        const D2D1_RECT_F& r = items[i].r;
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return items[i].action;
    }
    return Action::None;
}

static bool showsDan(const DanTracker& t) { return t.connected && t.isDan && !t.songs.empty(); }

float overlayHeight(const DanTracker& t, const ViewState& v) {
    float footer = v.locked ? 0.0f : kFooterH;
    if (v.showCredits) return 44.0f + 124.0f + 12.0f + footer;
    if (!t.connected || !t.isDan) return v.warning.empty() ? 150.0f : 166.0f;
    float h = 44.0f + 168.0f + 12.0f + t.songs.size() * kRowStep + 8.0f + kTotalH + 8.0f + footer;
    return t.note.empty() ? h : h + kNoteH;
}

static void drawFooter(float y, const ViewState& v, Buttons* buttons) {
    if (v.locked) return;
    const float P = 18.0f;
    text(L"Drag to move  ·  Ctrl+Shift+L lock", F.tiny, {P, y, kWidth - 150, y + 18}, rgb(col::faint));
    D2D1_RECT_F tab = {kWidth - P - 70, y - 1, kWidth - P, y + 19};
    fillRound(tab, 10, rgb(col::accent, v.showCredits ? 0.9f : 0.18f));
    strokeRound(tab, 10, rgb(col::accent, 0.8f));
    text(v.showCredits ? L"BACK" : L"CREDITS", F.tiny, tab, rgb(v.showCredits ? 0xFFFFFF : col::accent),
         DWRITE_TEXT_ALIGNMENT_CENTER);
    D2D1_RECT_F hide = {tab.left - 56, y - 1, tab.left - 6, y + 19};
    fillRound(hide, 10, rgb(col::line, 0.6f));
    strokeRound(hide, 10, rgb(col::dim, 0.6f));
    text(L"HIDE", F.tiny, hide, rgb(col::dim), DWRITE_TEXT_ALIGNMENT_CENTER);
    if (buttons) {
        buttons->items[0] = {tab, Action::Credits};
        buttons->items[1] = {hide, Action::Hide};
        buttons->count = 2;
    }
}

static void drawHeader(const std::wstring& title) {
    const float P = 16.0f;
    fillRound({P, 14, P + 40, 32}, 9, rgb(col::accent));
    text(L"DAN", F.tiny, {P, 14, P + 40, 32}, rgb(0xFFFFFF), DWRITE_TEXT_ALIGNMENT_CENTER);
    text(title, F.bold, {P + 50, 12, kWidth - P - 70, 34}, rgb(col::text));
}

static void drawCredits(const ViewState& v, Buttons* buttons) {
    const float P = 16.0f, W = kWidth;
    drawHeader(L"Dan Tracker");

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
    drawFooter(card.bottom + 12, v, buttons);
}

static void drawWaiting(const DanTracker& t, const ViewState& v, float H, Buttons* buttons) {
    text(L"DAN TRACKER", F.sm, {18, 16, 300, 34}, rgb(col::accent));
    fillRound({kWidth - 30, 21, kWidth - 22, 29}, 4, rgb(t.connected ? col::h200 : col::miss));
    std::wstring head, sub;
    if (!t.connected) {
        head = L"Waiting for tosu...";
        sub = L"Open tosu (port " + std::to_wstring(v.tosuPort) + L") and osu!";
    } else {
        head = t.mapLabel.size() > 6 ? L"No dan selected" : L"Select a dan";
        sub = widen(t.note.empty() ? "pick an osu!mania dan map" : t.note);
    }
    text(head, F.title, {18, 44, kWidth - 18, 72}, rgb(col::text));
    if (t.connected && t.mapLabel.size() > 6) text(widen(t.mapLabel), F.sm, {18, 74, kWidth - 18, 92}, rgb(col::dim));
    text(sub, F.sm, {18, 94, kWidth - 18, 112}, rgb(col::faint));
    if (!v.configError.empty()) text(widen(v.configError), F.tiny, {18, 114, kWidth - 18, 130}, rgb(col::h300));
    if (!v.warning.empty()) text(v.warning, F.tiny, {18, 130, kWidth - 18, 146}, rgb(col::miss));
    drawFooter(H - 30, v, buttons);
}

static void drawJudgeColumns(const Hits& h, float x0, float x1, float labelTop, float numTop, float numBottom) {
    Judges js = judges(h);
    float w = (x1 - x0) / 6.0f;
    for (int i = 0; i < 6; ++i) {
        float x = x0 + w * i;
        text(js.j[i].label, F.tiny, {x, labelTop, x + w, numTop}, rgb(js.j[i].c), DWRITE_TEXT_ALIGNMENT_CENTER);
        text(fmtNum(js.j[i].v), F.num, {x, numTop, x + w, numBottom}, rgb(js.j[i].v ? col::text : col::faint),
             DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

static float drawCurrentSong(const DanTracker& t, int cur) {
    const float P = 16.0f, W = kWidth;
    int n = (int)t.songs.size();
    const SongView& s = t.songs[cur];

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
    Grade g = grade(acc, s.hits.total());
    text(s.hits.total() ? fmtAcc(acc) : L"--.--%", F.huge, {cx - 2, cy + 52, cr - 60, cy + 104}, rgb(col::text));
    text(g.label, F.huge, {cr - 70, cy + 52, cr, cy + 104}, rgb(g.color), DWRITE_TEXT_ALIGNMENT_TRAILING);

    float prog = 0.0f;
    if (s.endMs > s.startMs) prog = (float)std::clamp((t.liveMs - s.startMs) / (s.endMs - s.startMs), 0.0, 1.0);
    if (t.finished || s.status == SongStatus::Done) prog = 1.0f;
    D2D1_RECT_F bar = {cx, cy + 112, cr, cy + 117};
    fillRound(bar, 2.5f, rgb(col::line));
    if (prog > 0.01f)
        gradientBar({bar.left, bar.top, bar.left + (bar.right - bar.left) * prog, bar.bottom}, 2.5f, col::accent2, col::accent);

    drawJudgeColumns(s.hits, cx, cr, cy + 126, cy + 140, cy + 160);
    return card.bottom;
}

static float drawSongList(const DanTracker& t, int cur, float y) {
    const float P = 16.0f, W = kWidth;
    for (int i = 0; i < (int)t.songs.size(); ++i) {
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
            Grade g = grade(a, sv.hits.total());
            text(sv.hits.total() ? fmtAcc(a) : L"--.--%", F.num, {W - P - 112, y + 2, W - P - 30, y + 30},
                 rgb(col::text, live ? 1.0f : 0.9f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            text(g.label, F.bold, {W - P - 26, y + 2, W - P - 2, y + 30}, rgb(g.color), DWRITE_TEXT_ALIGNMENT_TRAILING);

            Judges sj = judges(sv.hits);
            float x0 = P + 36, sw = (W - P - 2 - x0) / 6.0f;
            for (int k = 0; k < 6; ++k)
                text(fmtNum(sj.j[k].v), F.mini, {x0 + sw * k, y + 26, x0 + sw * (k + 1), y + 42},
                     rgb(sj.j[k].c, sj.j[k].v ? 1.0f : 0.35f));
        }
        y += kRowStep;
    }
    return y;
}

static float drawTotal(const DanTracker& t, float y) {
    const float P = 16.0f, W = kWidth;
    D2D1_RECT_F tot = {P - 4, y, W - P + 4, y + kTotalH};
    fillRound(tot, 12, rgb(col::card, 0.95f));
    strokeRound(tot, 12, rgb(col::line));
    text(L"TOTAL DAN", F.tiny, {P + 10, y + 6, P + 120, y + 34}, rgb(col::dim));
    double ta = maniaAccuracy(t.cumulative, t.scoreV2);
    text(t.cumulative.total() ? fmtAcc(ta) : L"--.--%", F.title, {W - P - 130, y + 6, W - P - 8, y + 34},
         rgb(col::text), DWRITE_TEXT_ALIGNMENT_TRAILING);
    drawJudgeColumns(t.cumulative, P + 4, W - P - 4, y + 36, y + 50, y + 68);
    return y + kTotalH;
}

static void drawDan(const DanTracker& t, const ViewState& v, float H, Buttons* buttons) {
    const float P = 16.0f, W = kWidth;
    int cur = std::clamp(t.current, 0, (int)t.songs.size() - 1);

    drawHeader(widen(t.danName));
    std::wstring st = t.finished ? L"COMPLETE" : (t.state == 2 ? L"PLAYING" : L"IDLE");
    text(st, F.tiny, {W - P - 72, 14, W - P - 12, 32}, rgb(t.state == 2 ? col::h200 : col::dim), DWRITE_TEXT_ALIGNMENT_TRAILING);
    fillRound({W - P - 6, 20, W - P, 26}, 3, rgb(col::h200));

    float y = drawCurrentSong(t, cur);
    y = drawSongList(t, cur, y + 12);
    y = drawTotal(t, y + 8) + 8;

    if (!t.note.empty()) text(widen(t.note), F.tiny, {P, H - 22, W - P, H - 6}, rgb(col::h300));
    drawFooter(y, v, buttons);
}

void drawOverlay(const DanTracker& t, const ViewState& v, float H, bool opaque, Buttons* buttons) {
    if (buttons) buttons->count = 0;
    D2D1_RECT_F panel = {0.5f, 0.5f, kWidth - 0.5f, H - 0.5f};
    fillRound(panel, 18, rgb(col::bg, opaque ? 1.0f : 0.90f));
    strokeRound(panel, 18, rgb(v.locked ? col::line : col::accent, v.locked ? 1.0f : 0.8f), 1.0f);
    gradientBar({18, 0.5f, kWidth - 18, 2.5f}, 1, col::accent2, col::accent);

    if (v.showCredits) drawCredits(v, buttons);
    else if (showsDan(t)) drawDan(t, v, H, buttons);
    else drawWaiting(t, v, H, buttons);
}

}
