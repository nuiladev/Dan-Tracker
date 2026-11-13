#pragma once
#include <string>

#include "core/dan.h"
#include "ui/canvas.h"

namespace ui {

constexpr float kWidth = 360.0f;

enum class Action { None, Credits, Hide };

struct Buttons {
    struct Item { D2D1_RECT_F r; Action action; };
    Item items[2];
    int count = 0;
    Action at(float x, float y) const;
};

struct ViewState {
    bool locked = false;
    bool showCredits = false;
    int tosuPort = 0;
    std::wstring warning;
    std::string configError;
    bool operator==(const ViewState& o) const {
        return locked == o.locked && showCredits == o.showCredits && tosuPort == o.tosuPort && warning == o.warning &&
               configError == o.configError;
    }
};

float overlayHeight(const DanTracker& t, const ViewState& v);
void drawOverlay(const DanTracker& t, const ViewState& v, float height, bool opaque, Buttons* buttons);

}
