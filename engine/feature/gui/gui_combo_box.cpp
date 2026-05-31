#include "feature/gui/gui_combo_box.h"
#include "feature/gui/gui_renderer.h"

namespace engine3d {

GuiComboBox::GuiComboBox(GuiWindow* parent, const std::string& name, int x, int y, int w)
    : GuiWidget(parent), name_(name), x_(x), y_(y), w_(w) {}

GuiComboBox::~GuiComboBox() = default;

bool GuiComboBox::isHovered(int mx, int my) const {
    return mx >= x_ && mx <= x_ + w_ && my >= y_ && my <= y_ + h();
}

void GuiComboBox::update(int mx, int my, bool leftClicked, bool, int) {
    bool hovered = isHovered(mx, my);
    if (hovered != hovered_) { hovered_ = hovered; }
    if (open_) {
        if (leftClicked) {
            int itemH = 25;
            int itemBaseY = y_ + GuiComboBox::h();
            int idx = (my - itemBaseY) / itemH;
            if (idx >= 0 && idx < (int)items_.size() && mx >= x_ && mx <= x_ + w_) {
                selected_ = idx; open_ = false;
                if (onSelect_) onSelect_(items_[selected_]);
            } else { open_ = false; }
        }
    } else if (hovered && leftClicked) {
        open_ = true;
    }
}

void GuiComboBox::draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const {
    int ax = winX + x_, ay = winY + y_;
    r.addRectPx(ax - 1, ay - 1, w_ + 2, 27, {0.2f, 0.2f, 0.2f, 0.95f}, sw, sh);
    r.addRectPx(ax, ay, w_, 25, color_, sw, sh);
    if (!items_.empty())
        textFn(items_[selected_], float(ax + 4), float(ay) + 17.0f, 0.6f, {1, 1, 1, 1});

    if (open_) {
        const int itemH = 25;
        for (int i = 0; i < (int)items_.size(); ++i) {
            int iy = ay + h() + i * itemH;
            r.addRectPx(ax, iy, w_, itemH, {0.3f, 0.3f, 0.3f, 0.95f}, sw, sh);
            textFn(items_[i], float(ax + 4), float(iy) + 17.0f, 0.5f, {1, 1, 1, 1});
        }
    }
}

} // namespace engine3d
