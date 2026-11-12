#include "app/shared.h"

Shared g_shared;

static const wchar_t* kConfigFile = L"dans.json";
static const wchar_t* kMarkedFile = L"dans_marcados.txt";

void loadConfig() {
    Config c;
    std::string err;
    c.load(g_shared.exeDir + kConfigFile, err);
    c.loadMarked(g_shared.exeDir + kMarkedFile);
    std::lock_guard<std::mutex> lk(g_shared.mutex);
    g_shared.cfg = c;
    g_shared.configError = err;
    g_shared.tracker.setConfig(&g_shared.cfg);
}

void toggleMarkCurrentMap() {
    std::lock_guard<std::mutex> lk(g_shared.mutex);
    if (g_shared.tracker.checksum.empty()) return;
    g_shared.cfg.toggleMarked(g_shared.exeDir + kMarkedFile, g_shared.tracker.checksum, g_shared.tracker.mapLabel);
    g_shared.tracker.setConfig(&g_shared.cfg);
}

DanTracker snapshotTracker() {
    DanTracker t(nullptr);
    std::lock_guard<std::mutex> lk(g_shared.mutex);
    t = g_shared.tracker;
    return t;
}

std::string snapshotLastBody() {
    std::lock_guard<std::mutex> lk(g_shared.mutex);
    return g_shared.lastBody;
}
