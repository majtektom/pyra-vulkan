#pragma once
#include "feature/gui/gui.h"
#include <array>
#include <functional>
#include <string>

namespace engine3d {

class GuiInputText : public GuiWidget {
public:
    GuiInputText(GuiWindow* parent, const std::string& name, int x, int y, int w, int h);
    ~GuiInputText() override;

    const std::string& text() const { return history_[0]; }
    const std::string& name() const { return name_; }
    int x() const { return x_; } int y() const { return y_; }
    int width() const { return w_; } int height() const { return h_; }
    bool isHovered(int mx, int my) const;

    void update(int mx, int my, bool leftClicked, bool leftDown, int wheelDelta) override;
    void draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const override;

    std::array<float, 4> color_{0.3f, 0.3f, 0.38f, 0.9f};

private:
    std::string name_;
    int x_, y_, w_, h_;
    std::array<std::string, 10> history_;
    int historyIdx_ = 0;
    bool hovered_ = false;
};

} // namespace engine3d
