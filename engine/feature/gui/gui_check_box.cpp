#include "feature/gui/gui_check_box.h"
#include "feature/gui/gui_renderer.h"

namespace engine3d {

GuiCheckBox::GuiCheckBox(GuiWindow* parent, const std::string& name, int x, int y)
    : GuiWidget(parent), name_(name), text_(name), x_(x), y_(y) {}

GuiCheckBox::~GuiCheckBox() = default;

bool GuiCheckBox::isHovered(int mx, int my) const { return mx >= x_ && mx <= x_ + w() && my >= y_ && my <= y_ + 20; }

void GuiCheckBox::update(int mx, int my, bool leftClicked, bool, int) {
    bool h = isHovered(mx, my);
    if (h != hovered_) { hovered_ = h; color_ = h ? std::array<float,4>{0.2f,0.2f,0.28f,0.9f} : std::array<float,4>{0.3f,0.3f,0.38f,0.9f}; }
    if (hovered_ && leftClicked) { checked_ = !checked_; if (onToggle_) onToggle_(checked_); }
}

void GuiCheckBox::draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const {
    int ax = winX + x_, ay = winY + y_;
    int boxW = 18, boxH = 18;
    // Obramowanie kwadratu
    r.addRectPx(ax - 2, ay - 2, boxW + 4, boxH + 4, {0.7f, 0.7f, 0.7f, 1.0f}, sw, sh);
    // Wypełnienie kwadratu
    r.addRectPx(ax, ay, boxW, boxH, color_, sw, sh);

    if (checked_) {
        r.addRectPx(ax + 4, ay + 4, 10, 10, {0, 1, 0, 0.8f}, sw, sh);
    }

    float fontAscent = 28.0f * 0.6f * 0.7f;
    float baseline = float(ay) + (float(boxH) + fontAscent) * 0.5f;
    textFn(text_, float(ax + 22), baseline, 0.6f, {1, 1, 1, 1});
}

} // namespace engine3d
