#include "gui.h"
#include "platform.h"
#include "../render/renderer.h"
#include <algorithm>

namespace engine3d {

// --- GuiWidget ---
GuiWidget::GuiWidget(GuiWindow* parent) : parent_(parent) {
    if (parent_) parent_->addWidget(this);
}

GuiWidget::~GuiWidget() {
    if (parent_) parent_->removeWidget(this);
}

void GuiWidget::update(int, int, bool, bool, int) {}

// --- GuiMgr ---
GuiMgr::~GuiMgr() { windows_.clear(); }

void GuiMgr::init(Renderer* renderer) {
    renderer_ = renderer;
    guiRenderer_.init(renderer_->device(), renderer_->physicalDevice(),
                      renderer_->colorFormat(), renderer_->depthFormat(),
                      renderer_->commandPool(), renderer_->graphicsQueue(), renderer_->extent());
}

void GuiMgr::render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView&) {
    if (!isGuiReady()) return;
    guiRenderer_.build(frameIndex);
    guiRenderer_.recordDrawCommands(cmd, frameIndex);
}

GuiWindow* GuiMgr::addWindow(const std::string& name, int x, int y, int w, int h) {
    if (windows_.count(name)) return nullptr;
    auto win = std::make_unique<GuiWindow>(name, x, y, w, h);
    GuiWindow* ptr = win.get();
    windows_[name] = std::move(win);
    return ptr;
}

bool GuiMgr::removeWindow(const std::string& name) {
    return windows_.erase(name) > 0;
}

GuiWindow* GuiMgr::getWindow(const std::string& name) {
    auto it = windows_.find(name);
    return it != windows_.end() ? it->second.get() : nullptr;
}

void GuiMgr::forEachWindow(std::function<void(GuiWindow&)> fn) {
    for (auto& [k, w] : windows_) fn(*w);
}

void GuiMgr::setActive(const std::string& name) {
    for (auto& [k, w] : windows_) w->setActive(false);
    auto* win = getWindow(name);
    if (win) { win->setActive(true); win->setVisible(true); activeWindow_ = win; }
}

void GuiMgr::update(Platform& platform, float screenW, float screenH) {
    screenW_ = screenW;
    screenH_ = screenH;

    auto snap = platform.pollInput();
    mouseX_ = snap.mouseX;
    mouseY_ = snap.mouseY;
    bool leftDown = snap.mouseButtons[0];
    static bool prevLeft = false;
    bool leftClick = leftDown && !prevLeft;
    bool leftUp = !leftDown && prevLeft;
    prevLeft = leftDown;

    if (cursorEnabled_)
        platform.grabPointer(cursorMode_ == CursorMode::Custom);

    if (!cursorEnabled_) return;

    // Aktywuj okno po kliknięciu w nie
    if (leftClick) {
        GuiWindow* top = nullptr;
        for (auto& [k, w] : windows_) {
            if (!w->isVisible()) continue;
            if (mouseX_ >= w->x() - 4 && mouseX_ <= w->x() + w->width() + 4 &&
                mouseY_ >= w->y() - kTitleBarH && mouseY_ <= w->y() + w->height() + 4) {
                top = w.get();
            }
        }
        if (top) setActive(top->name());
    }

    // Aktualizuj aktywne okno
    if (activeWindow_ && activeWindow_->isVisible()) {
        activeWindow_->handleMouse(mouseX_, mouseY_, leftDown, leftUp);
    }

    for (auto& [k, w] : windows_) {
        if (!w->isVisible()) continue;
        w->updateWidgets(mouseX_ - w->x(), mouseY_ - w->y(), leftClick, leftDown, snap.mouseWheel);
    }
}

void GuiMgr::renderGui() {
    for (auto& [name, w] : windows_) {
        if (!w->isVisible()) continue;
        w->render(guiRenderer_, textFn_, screenW_, screenH_);
    }
    drawCursor();
}

void GuiMgr::drawCursor() {
    if (!cursorEnabled_ || cursorMode_ != CursorMode::Custom) return;
    int mx = mouseX_, my = mouseY_;
    guiRenderer_.addRectPx(mx - 2, my - 2, 5, 5, {1, 1, 1, 1}, screenW_, screenH_);
    guiRenderer_.addRectPx(mx, my - 10, 1, 20, {1, 1, 1, 0.5f}, screenW_, screenH_);
    guiRenderer_.addRectPx(mx - 10, my, 20, 1, {1, 1, 1, 0.5f}, screenW_, screenH_);
}

void GuiMgr::build(uint32_t frameIndex) {
    guiRenderer_.build(frameIndex);
}

// --- GuiWindow ---
GuiWindow::GuiWindow(const std::string& name, int x, int y, int w, int h)
    : name_(name), x_(x), y_(y), w_(w), h_(h) {}

GuiWindow::~GuiWindow() = default;

void GuiWindow::addWidget(GuiWidget* w) {
    if (!w) return;
    auto it = std::find(widgets_.begin(), widgets_.end(), w);
    if (it == widgets_.end()) widgets_.push_back(w);
}

void GuiWindow::removeWidget(GuiWidget* w) {
    auto it = std::find(widgets_.begin(), widgets_.end(), w);
    if (it != widgets_.end()) widgets_.erase(it);
}

void GuiWindow::render(GuiRenderer& r, TextDrawFn textFn, float sw, float sh) const {
    if (chromeVisible_) {
        float titleH = (float)kTitleBarH;
        r.addRectPx(x_ - 4, y_, w_ + 8, h_ + 4, {0.1f, 0.1f, 0.15f, 0.95f}, sw, sh);
        r.addRectPx(x_ - 4, y_ - (int)titleH, w_ + 8, (int)titleH,
                    {0.2f, 0.2f, 0.35f, 0.95f}, sw, sh);
        r.addRectPx(x_, y_, w_, h_, {0.15f, 0.15f, 0.2f, 0.9f}, sw, sh);
        textFn(name_, float(x_ + 5), float(y_) - 6.0f, 0.7f, {1, 1, 1, 1});
    }
    for (auto* w : widgets_) w->draw(r, textFn, x_, y_, sw, sh);
}

void GuiWindow::handleMouse(int mx, int my, bool leftDown, bool leftUp) {
    if (leftDown && !dragging_ && !resizing_ && chromeVisible_) {
        // Sprawdź pasek tytułu (przeciąganie)
        if (mx >= x_ && mx <= x_ + w_ && my >= y_ - kTitleBarH && my <= y_) {
            dragging_ = true;
            dragOffX_ = mx - x_;
            dragOffY_ = my - y_;
        }
        // Sprawdź dolną krawędź (zmiana rozmiaru)
        if (mx >= x_ && mx <= x_ + w_ && my >= y_ + h_ && my <= y_ + h_ + 12) {
            resizing_ = true;
        }
    }

    if (dragging_) {
        x_ = mx - dragOffX_;
        y_ = my - dragOffY_;
    }
    if (resizing_) {
        h_ = std::max(50, my - y_);
    }

    if (leftUp) { dragging_ = false; resizing_ = false; }
}

void GuiWindow::updateWidgets(int mx, int my, bool leftClicked, bool leftDown, int wheelDelta) {
    for (auto* w : widgets_) w->update(mx, my, leftClicked, leftDown, wheelDelta);
}

} // namespace engine3d
