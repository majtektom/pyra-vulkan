#include "feature/gui/gui_scroll_bar.h"
#include "feature/gui/gui_renderer.h"
#include <algorithm>

namespace engine3d {

GuiScrollBar::GuiScrollBar(GuiWindow* parent, const std::string& name, int x, int y, int h, int maxVal)
    : GuiWidget(parent), name_(name), x_(x), y_(y), h_(h), max_(maxVal) {}
GuiScrollBar::~GuiScrollBar() = default;

bool GuiScrollBar::isHovered(int mx, int my) const { return mx >= x_ && mx <= x_ + 16 && my >= y_ && my <= y_ + h_; }

void GuiScrollBar::update(int mx, int my, bool, bool leftDown, int wheelDelta) {
    bool h = isHovered(mx, my);
    if (h != hovered_) {
        hovered_ = h;
        color_ = h ? std::array<float,4>{0.2f, 0.2f, 0.28f, 0.9f} : std::array<float,4>{0.3f, 0.3f, 0.38f, 0.9f};
    }
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
    // Tło suwaka — obramowanie i wypełnienie
    r.addRectPx(ax - 2, ay - 2, 16 + 4, h_ + 4, {0.7f, 0.7f, 0.7f, 1.0f}, sw, sh);
    r.addRectPx(ax, ay, 16, h_, {0.15f, 0.15f, 0.15f, 0.9f}, sw, sh);

    float t = max_ > 0 ? float(value_) / float(max_) : 0.0f;
    float thumbH = float(h_) * 0.2f;
    float thumbY = float(ay) + t * (float(h_) - thumbH);
    // Suwak (thumb) — obramowanie i wypełnienie
    r.addRectPx(ax - 1, int(thumbY) - 1, 16 + 2, int(thumbH) + 2, {0.7f, 0.7f, 0.7f, 1.0f}, sw, sh);
    r.addRectPx(ax, int(thumbY), 16, int(thumbH), color_, sw, sh);
}

} // namespace engine3d
