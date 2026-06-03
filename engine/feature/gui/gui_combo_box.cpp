#include "feature/gui/gui_combo_box.h"
#include "feature/gui/gui_renderer.h"

namespace engine3d {

GuiComboBox::GuiComboBox(GuiWindow* parent, const std::string& name, int x, int y, int w)
    : GuiWidget(parent), name_(name), x_(x), y_(y), w_(w) {}

GuiComboBox::~GuiComboBox() = default;

bool GuiComboBox::isHovered(int mx, int my) const {
    return mx >= x_ && mx <= x_ + w_ && my >= y_ && my <= y_ + h();
}

bool GuiComboBox::isInside(int mx, int my) const {
    if (open_) {
        return mx >= x_ && mx <= x_ + w_ && my >= y_ && my <= y_ + h() + (int)items_.size() * 25;
    }
    return mx >= x_ && mx <= x_ + w_ && my >= y_ && my <= y_ + h();
}

void GuiComboBox::update(int mx, int my, bool leftClicked, bool, int) {
    bool hovered = isHovered(mx, my);
    if (hovered != hovered_) {
        hovered_ = hovered;
    }
    color_ = hovered_ ? std::array<float,4>{0.2f,0.2f,0.28f,0.9f} : std::array<float,4>{0.3f,0.3f,0.38f,0.9f};

    hoveredIdx_ = -1;
    if (open_) {
        int itemH = 25;
        int itemBaseY = y_ + GuiComboBox::h();
        int idx = (my - itemBaseY) / itemH;
        if (idx >= 0 && idx < (int)items_.size() && mx >= x_ && mx <= x_ + w_) {
            hoveredIdx_ = idx;
        }

        if (leftClicked) {
            if (hoveredIdx_ >= 0) {
                selected_ = hoveredIdx_; open_ = false;
                if (onSelect_) onSelect_(items_[selected_]);
            } else { open_ = false; }
        }
    } else if (hovered && leftClicked) {
        open_ = true;
    }
}

void GuiComboBox::draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const {
    int ax = winX + x_, ay = winY + y_;
    float baseZ = r.getDepth();

    r.setDepth(baseZ);
    r.addRectPx(ax - 2, ay - 2, w_ + 4, 25 + 4, {0.7f, 0.7f, 0.7f, 1.0f}, sw, sh);
    r.addRectPx(ax, ay, w_, 25, color_, sw, sh);
    if (!items_.empty())
        textFn(items_[selected_], float(ax + 4), float(ay) + 17.0f, 0.6f, {1, 1, 1, 1});

    if (open_) {
        r.setDepth(baseZ - 0.005f);
        const int itemH = 25;
        int totalH = (int)items_.size() * itemH;
        // Obramowanie rozwinięcia listboxa
        r.addRectPx(ax - 2, ay + h() - 2, w_ + 4, totalH + 4, {0.7f, 0.7f, 0.7f, 1.0f}, sw, sh);

        for (int i = 0; i < (int)items_.size(); ++i) {
            int iy = ay + h() + i * itemH;
            std::array<float, 4> itemColor = (i == hoveredIdx_) ? std::array<float, 4>{0.2f, 0.2f, 0.25f, 0.95f} : std::array<float, 4>{0.3f, 0.3f, 0.35f, 0.95f};
            r.addRectPx(ax, iy, w_, itemH, itemColor, sw, sh);
            textFn(items_[i], float(ax + 4), float(iy) + 17.0f, 0.5f, {1, 1, 1, 1});
        }
    }
    r.setDepth(baseZ);
}

} // namespace engine3d
