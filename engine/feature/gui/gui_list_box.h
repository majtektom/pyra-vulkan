#pragma once
#include "feature/gui/gui.h"
#include <functional>
#include <string>
#include <vector>

namespace engine3d {

class GuiListBox : public GuiWidget {
public:
    GuiListBox(GuiWindow* parent, const std::string& name, int x, int y, int w, int h);
    ~GuiListBox() override;

    void addItem(const std::string& item) { items_.push_back(item); }
    void clear() { items_.clear(); selected_ = -1; }
    int selected() const { return selected_; }
    std::string selectedText() const { return selected_ >= 0 && selected_ < (int)items_.size() ? items_[selected_] : ""; }

    const std::string& name() const { return name_; }
    int x() const { return x_; } int y() const { return y_; }
    int width() const { return w_; } int height() const { return h_; }
    bool isHovered(int mx, int my) const;
    int itemCount() const { return (int)items_.size(); }
    const std::string& getItem(int idx) const { return items_[idx]; }

    void setOnSelect(std::function<void(const std::string&)> fn) { onSelect_ = fn; }
    void update(int mx, int my, bool leftClicked, bool leftDown, int wheelDelta) override;
    void draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const override;

    std::array<float, 4> color_{0.3f, 0.3f, 0.38f, 0.9f};

private:
    std::string name_;
    int x_, y_, w_, h_;
    std::vector<std::string> items_;
    int selected_ = -1, hoveredIdx_ = -1;
    std::function<void(const std::string&)> onSelect_;
};

} // namespace engine3d
