#pragma once
#include "feature/gui/gui.h"
#include <functional>
#include <string>
#include <vector>

namespace engine3d {

class GuiComboBox : public GuiWidget {
public:
    GuiComboBox(GuiWindow* parent, const std::string& name, int x, int y, int w);
    ~GuiComboBox() override;

    void addItem(const std::string& item) { items_.push_back(item); }
    void clear() { items_.clear(); selected_ = 0; }
    int selectedIdx() const { return selected_; }
    std::string selectedText() const { return items_.empty() ? "" : items_[selected_]; }
    void setSelected(int idx) { if (idx >= 0 && idx < (int)items_.size()) selected_ = idx; }

    const std::string& name() const { return name_; }
    int x() const { return x_; } int y() const { return y_; }
    int w() const { return w_; } int h() const { return 25; }
    bool isOpen() const override { return open_; }
    void close() override { open_ = false; }
    bool isInside(int mx, int my) const override;
    bool isHovered(int mx, int my) const;
    int itemCount() const { return (int)items_.size(); }

    void setOnSelect(std::function<void(const std::string&)> fn) { onSelect_ = fn; }
    void update(int mx, int my, bool leftClicked, bool leftDown, int wheelDelta) override;
    void draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const override;

    std::array<float, 4> color_{0.3f, 0.3f, 0.38f, 0.9f};

private:
    std::string name_;
    int x_, y_, w_;
    std::vector<std::string> items_;
    int selected_ = 0;
    bool open_ = false, hovered_ = false;
    int hoveredIdx_ = -1;
    std::function<void(const std::string&)> onSelect_;
};

} // namespace engine3d
