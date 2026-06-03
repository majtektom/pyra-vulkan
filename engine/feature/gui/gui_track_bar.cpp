#include "feature/gui/gui_track_bar.h"
#include "feature/gui/gui_renderer.h"
#include <algorithm>

namespace engine3d {

GuiTrackBar::GuiTrackBar(GuiWindow* parent, const std::string& name, int x, int y, int w, int minVal, int maxVal)
    : GuiWidget(parent), name_(name), x_(x), y_(y), w_(w), min_(minVal), max_(maxVal), value_((minVal+maxVal)/2) {}
GuiTrackBar::~GuiTrackBar() = default;

bool GuiTrackBar::isHovered(int mx, int my) const { return mx >= x_ && mx <= x_ + w_ && my >= y_ - 5 && my <= y_ + 20; }

void GuiTrackBar::update(int mx, int my, bool, bool leftDown, int) {
    bool h = isHovered(mx, my);
    if (h != hovered_) {
        hovered_ = h;
        color_ = h ? std::array<float,4>{0.2f, 0.2f, 0.45f, 0.9f} : std::array<float,4>{0.3f, 0.3f, 0.6f, 0.9f};
    }
    if (leftDown && isHovered(mx, my)) {
        float t = float(mx - x_) / float(w_);
        value_ = int(min_ + t * float(max_ - min_));
        value_ = std::clamp(value_, min_, max_);
        if (onChange_) onChange_(value_);
    }
}

void GuiTrackBar::draw(GuiRenderer& r, TextDrawFn, int winX, int winY, float sw, float sh) const {
    int ax = winX + x_, ay = winY + y_;
    int trackH = 6, trackY = ay + 7;
    // Tor — obramowanie
    r.addRectPx(ax - 2, trackY - 2, w_ + 4, trackH + 4, {0.7f, 0.7f, 0.7f, 1.0f}, sw, sh);
    // Tor — wypełnienie
    r.addRectPx(ax, trackY, w_, trackH, {0.3f, 0.3f, 0.3f, 0.8f}, sw, sh);

    float t = float(value_ - min_) / float(std::max(1, max_ - min_));
    float handleX = float(ax) + t * float(w_);
    int hw = 5, hh = 20;
    // Uchwyt — obramowanie
    r.addRectPx(int(handleX) - hw - 2, ay - 2, hw * 2 + 4, hh + 4, {0.7f, 0.7f, 0.7f, 1.0f}, sw, sh);
    // Uchwyt — wypełnienie
    r.addRectPx(int(handleX) - hw, ay, hw * 2, hh, color_, sw, sh);
}

} // namespace engine3d
