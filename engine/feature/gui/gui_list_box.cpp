#include "feature/gui/gui_list_box.h"
#include "feature/gui/gui_renderer.h"
#include <algorithm>

namespace engine3d {

GuiListBox::GuiListBox(GuiWindow* parent, const std::string& name, int x, int y, int w, int h)
    : GuiWidget(parent), name_(name), x_(x), y_(y), w_(w), h_(h) {}

GuiListBox::~GuiListBox() = default;

bool GuiListBox::isHovered(int mx, int my) const {
    return mx >= x_ && mx <= x_ + w_ && my >= y_ && my <= y_ + h_;
}

void GuiListBox::update(int mx, int my, bool leftClicked, bool, int) {
    if (!isHovered(mx, my)) { hoveredIdx_ = -1; return; }
    int itemH = std::max(20, h_ / std::max(1, (int)items_.size()));
    hoveredIdx_ = (my - y_) / itemH;
    if (hoveredIdx_ >= (int)items_.size()) hoveredIdx_ = -1;
    if (leftClicked && hoveredIdx_ >= 0) {
        selected_ = hoveredIdx_;
        if (onSelect_) onSelect_(items_[selected_]);
    }
}

void GuiListBox::draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const {
    int ax = winX + x_, ay = winY + y_;
    // Obramowanie
    r.addRectPx(ax - 2, ay - 2, w_ + 4, h_ + 4, {0.7f, 0.7f, 0.7f, 1.0f}, sw, sh);
    // Wypełnienie
    r.addRectPx(ax, ay, w_, h_, color_, sw, sh);

    int itemH = std::max(20, h_ / std::max(1, (int)items_.size()));
    float fontAscent = 28.0f * 0.5f * 0.7f;
    float baselineOffset = (float(itemH) + fontAscent) * 0.5f;

    for (int i = 0; i < (int)items_.size(); i++) {
        float iy = float(ay + i * itemH);
        if (i == selected_) {
            r.addRectPx(ax, (int)iy, w_, itemH, {0.15f, 0.15f, 0.2f, 0.95f}, sw, sh);
        } else if (i == hoveredIdx_) {
            r.addRectPx(ax, (int)iy, w_, itemH, {0.2f, 0.2f, 0.25f, 0.95f}, sw, sh);
        }
        textFn(items_[i], float(ax + 5), iy + baselineOffset, 0.5f, {1, 1, 1, 1});
    }
}

} // namespace engine3d
