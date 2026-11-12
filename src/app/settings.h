#pragma once
#include <string>

struct Settings {
    int x = 40, y = 120;
    float scale = 1.0f;
    bool locked = false;
    bool chroma = false;

    static Settings load(const std::wstring& iniPath);
    void save(const std::wstring& iniPath) const;
};
