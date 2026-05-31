#pragma once
#include "feature/gui/gui.h"
#include <functional>
#include <string>

namespace engine3d {

class GuiCheckBox : public GuiWidget {
public:
    GuiCheckBox(GuiWindow* parent, const std::string& name, int x, int y);
    ~GuiCheckBox() override;

    void setText(const std::string& t) { text_ = t; }
    const std::string& text() const { return text_; }
    bool checked() const { return checked_; }
    void setChecked(bool c) { checked_ = c; }
    const std::string& name() const { return name_; }
    int x() const { return x_; } int y() const { return y_; }
    int w() const { return 20 + (int)text_.size() * 10; } int h() const { return 20; }
    bool isHovered(int mx, int my) const;

    void setOnToggle(std::function<void(bool)> fn) { onToggle_ = fn; }
    void update(int mx, int my, bool leftClicked, bool leftDown, int wheelDelta) override;
    void draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const override;

    std::array<float, 4> color_{0.5f, 0.5f, 0.5f, 0.9f};

private:
    std::string name_, text_;
    int x_, y_;
    bool checked_ = false, hovered_ = false;
    std::function<void(bool)> onToggle_;
};

} // namespace engine3d
