#pragma once
#include <mutex>
#include <string>

#include "core/dan.h"

struct Shared {
    std::wstring exeDir;
    Config cfg;
    std::string configError;
    DanTracker tracker{&cfg};
    std::string lastBody;
    std::mutex mutex;
};

extern Shared g_shared;

void loadConfig();
void toggleMarkCurrentMap();
DanTracker snapshotTracker();
std::string snapshotLastBody();
