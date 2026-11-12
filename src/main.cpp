#include "app/overlay_app.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE single = CreateMutexW(nullptr, TRUE, L"DanTracker.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    int code = runOverlayApp(instance);
    ReleaseMutex(single);
    return code;
}
