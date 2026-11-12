#include "app/overlay_app.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "app/resource.h"
#include "app/settings.h"
#include "app/shared.h"
#include "net/tosu_client.h"
#include "obs/obs_server.h"
#include "obs/obs_state.h"
#include "ui/overlay_view.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

enum { HK_LOCK = 1, HK_RELOAD, HK_QUIT, HK_BIGGER, HK_SMALLER, HK_CHROMA, HK_MARK, HK_HIDE };
enum { CMD_LOCK = 100, CMD_RELOAD, CMD_CONFIG, CMD_QUIT, CMD_CHROMA, CMD_MARK, CMD_DUMP, CMD_OBSURL, CMD_CREDITS, CMD_HIDE };

static const UINT WM_TRAY = WM_APP + 1;
static const int kHidePad = 2;

static Settings g_set;
static std::wstring g_iniPath;
static std::wstring g_warning;
static HWND g_hwnd, g_obs;
static bool g_showCredits = false;
static bool g_hidden = false;
static POINT g_shownPos{}, g_obsShownPos{};
static UINT g_taskbarCreated;
static ui::Buttons g_buttons;
static TosuClient g_tosu;

static HICON appIcon(int metric) {
    int size = GetSystemMetrics(metric);
    return (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
}

static void registerClass(const wchar_t* name, WNDPROC proc, LPCWSTR cursor) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, cursor);
    wc.lpszClassName = name;
    wc.hIcon = appIcon(SM_CXICON);
    wc.hIconSm = appIcon(SM_CXSMICON);
    RegisterClassExW(&wc);
}

static std::wstring obsUrl() { return L"http://127.0.0.1:" + std::to_wstring(g_shared.cfg.obsPort) + L"/"; }

static std::string obsStateNow() { return obsStateJson(snapshotTracker()); }

static ui::ViewState viewState() {
    ui::ViewState v;
    v.locked = g_set.locked;
    v.showCredits = g_showCredits;
    v.tosuPort = g_shared.cfg.port;
    v.warning = g_warning;
    v.configError = g_shared.configError;
    return v;
}

static void render() {
    DanTracker t = snapshotTracker();
    ui::ViewState v = viewState();
    float H = ui::overlayHeight(t, v);
    int pw = (int)std::ceil(ui::kWidth * g_set.scale) + kHidePad, ph = (int)std::ceil(H * g_set.scale);

    HDC dc = ui::beginFrame(pw, ph, g_set.scale, D2D1::ColorF(0, 0, 0, 0));
    ui::drawOverlay(t, v, H, false, &g_buttons);
    ui::endFrame();
    RECT wr;
    GetWindowRect(g_hwnd, &wr);
    POINT dst = {wr.left, wr.top}, src = {0, 0};
    SIZE sz = {pw, ph};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_hwnd, nullptr, &dst, &sz, dc, &src, 0, &bf, ULW_ALPHA);

    if (!g_obs) return;
    ui::ViewState obsView = v;
    obsView.locked = true;
    dc = ui::beginFrame(pw, ph, g_set.scale, D2D1::ColorF(0, 1, 0, 1));
    ui::drawOverlay(t, obsView, H, true, nullptr);
    ui::endFrame();
    RECT cr;
    GetClientRect(g_obs, &cr);
    if (cr.right != pw || cr.bottom != ph) {
        RECT r = {0, 0, pw, ph};
        AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrW(g_obs, GWL_STYLE), FALSE, 0);
        SetWindowPos(g_obs, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    HDC odc = GetDC(g_obs);
    BitBlt(odc, 0, 0, pw, ph, dc, 0, 0, SRCCOPY);
    ReleaseDC(g_obs, odc);
}

static void applyLock() {
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    ex = g_set.locked ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
}

static void saveSettings() {
    RECT r;
    GetWindowRect(g_hwnd, &r);
    g_set.x = g_hidden ? g_shownPos.x : r.left;
    g_set.y = g_hidden ? g_shownPos.y : r.top;
    g_set.save(g_iniPath);
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
    n.hIcon = appIcon(SM_CXSMICON);
    wcscpy_s(n.szTip, L"Dan Tracker - click: show / hide, right-click: menu");
    Shell_NotifyIconW(action, &n);
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
    AppendMenuW(m, MF_STRING | (g_set.chroma ? MF_CHECKED : 0), CMD_CHROMA, L"OBS window (green background)\tCtrl+Shift+G");
    AppendMenuW(m, MF_STRING | (g_showCredits ? MF_CHECKED : 0), CMD_CREDITS, L"Credits");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_QUIT, L"Quit\tCtrl+Shift+Q");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(m);
}

static void copyToClipboard(const std::wstring& s) {
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    memcpy(GlobalLock(h), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
}

static void saveDiagnostics() {
    std::string body = snapshotLastBody();
    std::wstring path = g_shared.exeDir + L"tosu_debug.json";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") == 0 && f) {
        fwrite(body.data(), 1, body.size(), f);
        fclose(f);
    }
    ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

static void setObsWindow(bool on);

static void command(int id) {
    switch (id) {
        case HK_HIDE: case CMD_HIDE: setHidden(!g_hidden); break;
        case HK_LOCK: case CMD_LOCK:
            g_set.locked = !g_set.locked;
            if (g_set.locked) g_showCredits = false;
            applyLock();
            saveSettings();
            break;
        case CMD_CREDITS: g_showCredits = !g_showCredits; break;
        case HK_RELOAD: case CMD_RELOAD: loadConfig(); break;
        case HK_MARK: case CMD_MARK: toggleMarkCurrentMap(); break;
        case CMD_OBSURL: copyToClipboard(obsUrl()); break;
        case CMD_DUMP: saveDiagnostics(); break;
        case CMD_CONFIG: ShellExecuteW(nullptr, L"open", (g_shared.exeDir + L"dans.json").c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case HK_BIGGER: g_set.scale = std::min(2.5f, g_set.scale + 0.1f); saveSettings(); break;
        case HK_SMALLER: g_set.scale = std::max(0.6f, g_set.scale - 0.1f); saveSettings(); break;
        case HK_CHROMA: case CMD_CHROMA: g_set.chroma = !g_set.chroma; setObsWindow(g_set.chroma); saveSettings(); break;
        case HK_QUIT: case CMD_QUIT: DestroyWindow(g_hwnd); break;
    }
}

static void runAction(ui::Action a) {
    if (a == ui::Action::Credits) command(CMD_CREDITS);
    else if (a == ui::Action::Hide) command(CMD_HIDE);
}

static LRESULT CALLBACK obsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); render(); return 0; }
        case WM_ERASEBKGND: return 1;
        case WM_CLOSE: g_set.chroma = false; DestroyWindow(hwnd); g_obs = nullptr; saveSettings(); return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_MINIMIZE) { setHidden(true); return 0; }
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
        registerClass(L"DanTrackerOBS", obsProc, IDC_ARROW);
        registered = true;
    }
    g_obs = CreateWindowExW(0, L"DanTrackerOBS", L"Dan Tracker (OBS)",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME, 80, 80, 380, 500,
                            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (g_hidden) {
        g_obsShownPos = {80, 80};
        moveWindow(g_obs, hiddenPos(g_obs));
    }
    ShowWindow(g_obs, SW_SHOWNOACTIVATE);
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST: {
            if (g_set.locked) return HTTRANSPARENT;
            RECT wr;
            GetWindowRect(hwnd, &wr);
            float x = (GET_X_LPARAM(lp) - wr.left) / g_set.scale, y = (GET_Y_LPARAM(lp) - wr.top) / g_set.scale;
            return g_buttons.at(x, y) != ui::Action::None ? HTCLIENT : HTCAPTION;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; }
            break;
        case WM_LBUTTONUP:
            runAction(g_buttons.at(GET_X_LPARAM(lp) / g_set.scale, GET_Y_LPARAM(lp) / g_set.scale));
            return 0;
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

static void createOverlayWindow(HINSTANCE inst) {
    registerClass(L"DanTracker", wndProc, IDC_SIZEALL);
    HWND owner = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST, L"DanTracker", L"Dan Tracker", WS_POPUP, g_set.x, g_set.y,
                             (int)ui::kWidth, 200, owner, nullptr, inst, nullptr);
    applyLock();
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
}

static void registerHotkeys() {
    struct { int id; UINT vk; const wchar_t* name; } keys[] = {
        {HK_LOCK, 'L', L"L"}, {HK_RELOAD, 'R', L"R"}, {HK_QUIT, 'Q', L"Q"}, {HK_BIGGER, VK_OEM_PLUS, L"+"},
        {HK_SMALLER, VK_OEM_MINUS, L"-"}, {HK_MARK, 'D', L"D"}, {HK_CHROMA, 'G', L"G"}, {HK_HIDE, 'H', L"H"}};
    std::wstring taken;
    for (auto& k : keys)
        if (!RegisterHotKey(g_hwnd, k.id, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, k.vk)) taken += std::wstring(L" ") + k.name;
    if (!taken.empty()) g_warning = L"Hotkeys taken by another program (Ctrl+Shift+" + taken.substr(1) + L"): use right-click";
}

static void onTosuSample(const TosuSample* s, std::string& body) {
    std::lock_guard<std::mutex> lk(g_shared.mutex);
    if (s) {
        g_shared.lastBody.swap(body);
        g_shared.tracker.update(*s);
    } else {
        g_shared.tracker.update(TosuSample{});
    }
}

int runOverlayApp(HINSTANCE inst) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring exe = path;
    g_shared.exeDir = exe.substr(0, exe.find_last_of(L"\\/") + 1);
    g_iniPath = g_shared.exeDir + L"overlay.ini";
    loadConfig();
    g_set = Settings::load(g_iniPath);

    if (!ui::initCanvas()) return 1;
    createOverlayWindow(inst);
    registerHotkeys();

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    trayIcon(NIM_ADD);
    if (g_set.chroma) setObsWindow(true);
    if (!startObsServer(g_shared.cfg.obsPort, obsStateNow))
        g_warning += (g_warning.empty() ? L"" : L" | ") + (L"OBS port " + std::to_wstring(g_shared.cfg.obsPort) + L" in use");
    g_tosu.start(g_shared.cfg.host, g_shared.cfg.port, onTosuSample);
    SetTimer(g_hwnd, 1, 33, nullptr);
    render();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    g_tosu.stop();
    stopObsServer();
    return 0;
}
