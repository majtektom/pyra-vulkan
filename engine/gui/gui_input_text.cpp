#include "gui_input_text.h"
#include "gui_renderer.h"

namespace engine3d {

GuiInputText::GuiInputText(GuiWindow* parent, const std::string& name, int x, int y, int w, int h)
    : GuiWidget(parent), name_(name), x_(x), y_(y), w_(w), h_(h) {}

GuiInputText::~GuiInputText() = default;

bool GuiInputText::isHovered(int mx, int my) const {
    return mx >= x_ && mx <= x_ + w_ && my >= y_ && my <= y_ + h_;
}

void GuiInputText::processChar(char c) { history_[0] += c; }

void GuiInputText::processBackspace() {
    if (!history_[0].empty()) history_[0].pop_back();
}

void GuiInputText::update(int, int, bool, bool, int) {}

void GuiInputText::draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const {
    int ax = winX + x_, ay = winY + y_;
    r.addRectPx(ax - 1, ay - 1, w_ + 2, h_ + 2, {0.2f, 0.2f, 0.2f, 0.95f}, sw, sh);
    r.addRectPx(ax, ay, w_, h_, color_, sw, sh);
    textFn(history_[0], float(ax + 4), float(ay) + 17.0f, 0.6f, {1, 1, 1, 1});
}

} // namespace engine3d
