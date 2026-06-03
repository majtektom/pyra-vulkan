#include "feature/gui/gui_memo.h"
#include "feature/gui/gui_renderer.h"
#include <algorithm>

namespace engine3d {

GuiMemo::GuiMemo(GuiWindow* parent, const std::string& name, int x, int y, int w, int h)
    : GuiWidget(parent), name_(name), x_(x), y_(y), w_(w), h_(h) {}
GuiMemo::~GuiMemo() = default;

bool GuiMemo::isHovered(int mx, int my) const { return mx >= x_ && mx <= x_ + w_ && my >= y_ && my <= y_ + h_; }
int GuiMemo::lineCount() const { return 1 + (int)std::count(text_.begin(), text_.end(), '\n'); }

std::vector<std::string> GuiMemo::wrappedLines() const {
    std::vector<std::string> out;
    if (text_.empty()) return out;

    const float charWidth = 8.0f;
    const int textAreaW = w_ - 20;
    const int maxChars = std::max(1, textAreaW / static_cast<int>(charWidth));

    size_t start = 0;
    while (start <= text_.size()) {
        size_t nl = text_.find('\n', start);
        std::string line = text_.substr(start, (nl == std::string::npos ? text_.size() : nl) - start);

        if (line.empty()) {
            out.push_back("");
        } else {
            size_t pos = 0;
            while (pos < line.size()) {
                int len = std::min(static_cast<int>(line.size() - pos), maxChars);
                out.push_back(line.substr(pos, len));
                pos += len;
            }
        }

        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

void GuiMemo::update(int mx, int my, bool, bool, int wheelDelta) {
    if (wheelDelta == 0 || !isHovered(mx, my)) return;
    auto lines = wrappedLines();
    const float lineH = 16.0f;
    int visibleLines = std::max(1, (h_ - 8) / static_cast<int>(lineH));
    int maxScroll = std::max(0, static_cast<int>(lines.size()) - visibleLines);
    scroll_ = std::clamp(scroll_ - wheelDelta * 3, 0, maxScroll);
}

void GuiMemo::draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const {
    int ax = winX + x_, ay = winY + y_;
    r.addRectPx(ax - 2, ay - 2, w_ + 4, h_ + 4, {0.7f, 0.7f, 0.7f, 1.0f}, sw, sh);
    r.addRectPx(ax, ay, w_, h_, color_, sw, sh);

    auto lines = wrappedLines();
    if (lines.empty()) return;

    const float fontScale = 0.5f;
    const float lineH = 16.0f;
    int visibleLines = std::max(1, (h_ - 8) / static_cast<int>(lineH));
    int totalLines = static_cast<int>(lines.size());
    int maxScroll = std::max(0, totalLines - visibleLines);
    scroll_ = std::clamp(scroll_, 0, maxScroll);

    float baseY = static_cast<float>(ay + 4) + lineH;
    for (int i = scroll_; i < totalLines; ++i) {
        float y = baseY + (i - scroll_) * lineH;
        if (y > ay + h_ + lineH) break;
        textFn(lines[i], static_cast<float>(ax + 4), y, fontScale, {1, 1, 1, 1});
    }

    if (maxScroll > 0) {
        int sbX = ax + w_ - 12;
        int sbH = h_ - 4;
        r.addRectPx(sbX, ay + 2, 8, sbH, {0.1f, 0.1f, 0.1f, 0.8f}, sw, sh);
        int thumbH = std::max(12, sbH * visibleLines / totalLines);
        int thumbY = ay + 2 + (sbH - thumbH) * scroll_ / maxScroll;
        r.addRectPx(sbX, thumbY, 8, thumbH, {0.5f, 0.5f, 0.5f, 0.9f}, sw, sh);
    }
}

} // namespace engine3d
