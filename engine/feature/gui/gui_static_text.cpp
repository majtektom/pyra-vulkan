#include "feature/gui/gui_static_text.h"
namespace engine3d {
GuiStaticText::GuiStaticText(GuiWindow* parent, const std::string& name, int x, int y)
    : GuiWidget(parent), name_(name), text_(name), x_(x), y_(y) {}
GuiStaticText::~GuiStaticText() = default;

void GuiStaticText::draw(GuiRenderer&, TextDrawFn textFn, int winX, int winY, float, float) const {
    textFn(text_, float(winX + x_), float(winY + y_), 0.6f, color_);
}
} // namespace engine3d
