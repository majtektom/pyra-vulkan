#pragma once
#include "feature/gui/gui.h"
#include <string>
#include <vector>

namespace engine3d {

class GuiMemo : public GuiWidget {
public:
    GuiMemo(GuiWindow* parent, const std::string& name, int x, int y, int w, int h);
    ~GuiMemo() override;

    void addLine(const std::string& line) { text_ += line + "\n"; }
    void clear() { text_.clear(); scroll_ = 0; }
    const std::string& content() const { return text_; }
    const std::string& name() const { return name_; }
    int x() const { return x_; } int y() const { return y_; }
    int w() const { return w_; } int h() const { return h_; }
    bool isHovered(int mx, int my) const;
    int lineCount() const;

    void update(int mx, int my, bool leftClicked, bool leftDown, int wheelDelta) override;
    void draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const override;

    std::array<float, 4> color_{0.2f, 0.2f, 0.2f, 0.9f};

private:
    std::vector<std::string> wrappedLines() const;

    std::string name_, text_;
    int x_, y_, w_, h_;
    mutable int scroll_ = 0;
};

} // namespace engine3d
