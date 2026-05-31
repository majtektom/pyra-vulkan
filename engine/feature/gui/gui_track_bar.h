#pragma once
#include "feature/gui/gui.h"
#include <algorithm>
#include <functional>
#include <string>

namespace engine3d {

class GuiTrackBar : public GuiWidget {
public:
    GuiTrackBar(GuiWindow* parent, const std::string& name, int x, int y, int w, int minVal, int maxVal);
    ~GuiTrackBar() override;

    int value() const { return value_; }
    void setValue(int v) { value_ = std::clamp(v, min_, max_); }
    int minVal() const { return min_; } int maxVal() const { return max_; }
    const std::string& name() const { return name_; }
    int x() const { return x_; } int y() const { return y_; }
    int w() const { return w_; } int h() const { return 20; }
    bool isHovered(int mx, int my) const;

    void setOnChange(std::function<void(int)> fn) { onChange_ = fn; }
    void update(int mx, int my, bool leftClicked, bool leftDown, int wheelDelta) override;
    void draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const override;

    std::array<float, 4> color_{0.5f, 0.5f, 0.5f, 0.9f};

private:
    std::string name_;
    int x_, y_, w_;
    int min_, max_, value_;
    std::function<void(int)> onChange_;
};

} // namespace engine3d
