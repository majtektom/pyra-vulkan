#pragma once
#include "feature/gui/gui.h"
#include <array>
#include <functional>
#include <string>

namespace engine3d {

class GuiButton : public GuiWidget {
public:
    GuiButton(GuiWindow* parent, const std::string& name, int x, int y, int w, int h);
    ~GuiButton() override;

    void setText(const std::string& text) { text_ = text; }
    const std::string& text() const { return text_; }
    const std::string& name() const { return name_; }

    int x() const { return x_; } int y() const { return y_; }
    int width() const { return w_; } int height() const { return h_; }

    bool isHovered(int mx, int my) const;
    void setOnClick(std::function<void()> fn) { onClick_ = fn; }

    void update(int mx, int my, bool leftClicked, bool leftDown, int wheelDelta) override;
    void draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const override;

    std::array<float, 4> color_{0.5f, 0.5f, 0.5f, 0.9f};

private:
    std::string name_, text_;
    int x_, y_, w_, h_;
    bool hovered_ = false;
    std::function<void()> onClick_;
};

} // namespace engine3d
