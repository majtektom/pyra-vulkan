#pragma once
#include "gui.h"
#include <string>

namespace engine3d {

class GuiStaticText : public GuiWidget {
public:
    GuiStaticText(GuiWindow* parent, const std::string& name, int x, int y);
    ~GuiStaticText() override;

    void setText(const std::string& t) { text_ = t; }
    const std::string& text() const { return text_; }
    const std::string& name() const { return name_; }
    int x() const { return x_; } int y() const { return y_; }
    void draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const override;

    std::array<float, 4> color_{1,1,1,1};

private:
    std::string name_, text_;
    int x_, y_;
};

} // namespace engine3d
