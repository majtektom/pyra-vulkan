#include "feature/gui/gui_button.h"
#include "feature/gui/gui_renderer.h"

namespace engine3d {

GuiButton::GuiButton(GuiWindow* parent, const std::string& name, int x, int y, int w, int h)
    : GuiWidget(parent), name_(name), text_(name), x_(x), y_(y), w_(w), h_(h) {}

GuiButton::~GuiButton() = default;

bool GuiButton::isHovered(int mx, int my) const {
    return mx >= x_ && mx <= x_ + w_ && my >= y_ && my <= y_ + h_;
}

void GuiButton::update(int mx, int my, bool leftClicked, bool, int) {
    bool h = isHovered(mx, my);
    if (h != hovered_) {
        hovered_ = h;
        color_ = h ? std::array<float,4>{0.2f,0.2f,0.28f,0.9f} : std::array<float,4>{0.3f,0.3f,0.38f,0.9f};
    }
    if (hovered_ && leftClicked && onClick_) onClick_();
}

void GuiButton::draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const {
    int ax = winX + x_, ay = winY + y_;
    // Obramowanie
    r.addRectPx(ax - 2, ay - 2, w_ + 4, h_ + 4, {0.7f, 0.7f, 0.7f, 1.0f}, sw, sh);
    // Wypełnienie
    r.addRectPx(ax, ay, w_, h_, color_, sw, sh);

    float fontAscent = 28.0f * 0.6f * 0.7f;
    float baseline = float(ay) + (float(h_) + fontAscent) * 0.5f;
    textFn(text_, float(ax), baseline, 0.6f, {1,1,1,1});
}

} // namespace engine3d
