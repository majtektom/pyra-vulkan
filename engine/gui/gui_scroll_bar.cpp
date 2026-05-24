#include "gui_scroll_bar.h"
#include "gui_renderer.h"
#include <algorithm>

namespace engine3d {

GuiScrollBar::GuiScrollBar(GuiWindow* parent, const std::string& name, int x, int y, int h, int maxVal)
    : GuiWidget(parent), name_(name), x_(x), y_(y), h_(h), max_(maxVal) {}
GuiScrollBar::~GuiScrollBar() = default;

bool GuiScrollBar::isHovered(int mx, int my) const { return mx >= x_ && mx <= x_ + 16 && my >= y_ && my <= y_ + h_; }

void GuiScrollBar::update(int mx, int my, bool, bool leftDown, int wheelDelta) {
    if (isHovered(mx, my) && wheelDelta != 0) {
        value_ = std::clamp(value_ - wheelDelta, 0, max_);
        if (onChange_) onChange_(value_);
    }
    if (leftDown && isHovered(mx, my)) {
        float t = float(my - y_) / float(h_);
        value_ = int(t * float(max_));
        value_ = std::clamp(value_, 0, max_);
        if (onChange_) onChange_(value_);
    }
}

void GuiScrollBar::draw(GuiRenderer& r, TextDrawFn, int winX, int winY, float sw, float sh) const {
    int ax = winX + x_, ay = winY + y_;
    r.addRectPx(ax, ay, 16, h_, {0.15f, 0.15f, 0.15f, 0.9f}, sw, sh);
    float t = max_ > 0 ? float(value_) / float(max_) : 0.0f;
    float thumbH = float(h_) * 0.2f;
    float thumbY = float(ay) + t * (float(h_) - thumbH);
    r.addRectPx(ax + 1, int(thumbY), 14, int(thumbH), color_, sw, sh);
}

} // namespace engine3d
