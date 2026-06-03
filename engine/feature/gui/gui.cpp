#include "feature/gui/gui.h"
#include "platform/platform.h"
#include "render/renderer.h"
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
GuiMgr::~GuiMgr() {
    windowOrder_.clear();
    windows_.clear();
}

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
    windowOrder_.push_back(ptr);
    return ptr;
}

bool GuiMgr::removeWindow(const std::string& name) {
    auto it = windows_.find(name);
    if (it == windows_.end()) return false;
    GuiWindow* ptr = it->second.get();
    windowOrder_.erase(std::remove(windowOrder_.begin(), windowOrder_.end(), ptr), windowOrder_.end());
    if (activeWindow_ == ptr) activeWindow_ = nullptr;
    windows_.erase(it);
    return true;
}

GuiWindow* GuiMgr::getWindow(const std::string& name) {
    auto it = windows_.find(name);
    return it != windows_.end() ? it->second.get() : nullptr;
}

void GuiMgr::forEachWindow(std::function<void(GuiWindow&)> fn) {
    for (auto& [k, w] : windows_) fn(*w);
}

void GuiMgr::setActive(const std::string& name) {
    for (auto& [k, w] : windows_) {
        if (k != name) {
            w->setActive(false);
            w->closeWidgets();
        }
    }
    auto* win = getWindow(name);
    if (win) {
        win->setActive(true);
        win->setVisible(true);
        activeWindow_ = win;
        auto it = std::find(windowOrder_.begin(), windowOrder_.end(), win);
        if (it != windowOrder_.end()) {
            windowOrder_.erase(it);
            windowOrder_.push_back(win);
        }
    }
}

void GuiMgr::update(const InputSnapshot& snap, Platform& platform, float screenW, float screenH) {
    screenW_ = screenW;
    screenH_ = screenH;

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

    // 1. Znajdź najwyższe widoczne okno pod kursem myszy
    GuiWindow* hoveredWindow = nullptr;
    // Najpierw sprawdź, czy kursor jest wewnątrz otwartego widgetu (np. rozwijanej listy comboboxa)
    for (auto it = windowOrder_.rbegin(); it != windowOrder_.rend(); ++it) {
        GuiWindow* w = *it;
        if (!w->isVisible()) continue;
        if (w->isPointInOpenWidget(mouseX_, mouseY_)) {
            hoveredWindow = w;
            break;
        }
    }
    // Jeśli nie znaleziono otwartego widgetu pod myszką, sprawdź normalne granice okna
    if (!hoveredWindow) {
        for (auto it = windowOrder_.rbegin(); it != windowOrder_.rend(); ++it) {
            GuiWindow* w = *it;
            if (!w->isVisible()) continue;
            if (mouseX_ >= w->x() - 4 && mouseX_ <= w->x() + w->width() + 4 &&
                mouseY_ >= w->y() - kTitleBarH && mouseY_ <= w->y() + w->height() + 4) {
                hoveredWindow = w;
                break;
            }
        }
    }

    // 2. Aktywuj okno po kliknięciu w nie
    if (leftClick && hoveredWindow) {
        setActive(hoveredWindow->name());
    }

    // 3. Aktualizuj aktywne okno (dragowanie / zmiana rozmiaru)
    if (activeWindow_ && activeWindow_->isVisible()) {
        activeWindow_->handleMouse(mouseX_, mouseY_, leftDown, leftUp);
    }

    // 4. Aktualizuj widgety tylko dla aktywnego lub najechanego okna
    if (leftDown) {
        if (activeWindow_ && activeWindow_->isVisible()) {
            activeWindow_->updateWidgets(mouseX_ - activeWindow_->x(), mouseY_ - activeWindow_->y(), leftClick, leftDown, snap.mouseWheel);
        }
    } else {
        if (hoveredWindow) {
            hoveredWindow->updateWidgets(mouseX_ - hoveredWindow->x(), mouseY_ - hoveredWindow->y(), leftClick, leftDown, snap.mouseWheel);
        }
    }
}

void GuiMgr::renderGui() {
    for (size_t i = 0; i < windowOrder_.size(); ++i) {
        auto* w = windowOrder_[i];
        if (!w->isVisible()) continue;

        float windowZ = 0.9f - (float)i / (float)std::max((size_t)1, windowOrder_.size()) * 0.8f;
        guiRenderer_.setDepth(windowZ);

        auto wrappedTextFn = [this](const std::string& text, float x, float y, float scale, std::array<float,4> color) {
            float textZ = guiRenderer_.getDepth() - 0.002f;
            this->textFn_(text, x, y, scale, color, textZ);
        };

        w->render(guiRenderer_, wrappedTextFn, screenW_, screenH_);
    }
    // Draw cursor on top of everything
    guiRenderer_.setDepth(0.01f);
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
    float baseZ = r.getDepth();

    if (chromeVisible_) {
        r.setDepth(baseZ);
        float titleH = (float)kTitleBarH;
        r.addRectPx(x_ - 4, y_, w_ + 8, h_ + 4, {0.1f, 0.1f, 0.15f, 0.95f}, sw, sh);
        r.addRectPx(x_ - 4, y_ - (int)titleH, w_ + 8, (int)titleH,
                    {0.2f, 0.2f, 0.35f, 0.95f}, sw, sh);
        r.addRectPx(x_, y_, w_, h_, {0.15f, 0.15f, 0.2f, 0.9f}, sw, sh);
        textFn(name_, float(x_ + 5), float(y_) - 6.0f, 0.7f, {1, 1, 1, 1});
    }
    
    // Draw normal widgets at a slightly closer depth
    r.setDepth(baseZ - 0.005f);
    GuiWidget* openWidget = nullptr;
    for (auto* w : widgets_) {
        if (w->isOpen()) {
            openWidget = w;
        } else {
            w->draw(r, textFn, x_, y_, sw, sh);
        }
    }
    if (openWidget) {
        // Draw the open widget (ComboBox dropdown) even closer so it renders on top
        r.setDepth(baseZ - 0.010f);
        openWidget->draw(r, textFn, x_, y_, sw, sh);
    }
    
    // Restore base depth
    r.setDepth(baseZ);
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
    GuiWidget* openWidget = nullptr;
    for (auto* w : widgets_) {
        if (w->isOpen()) {
            openWidget = w;
            break;
        }
    }

    if (openWidget) {
        openWidget->update(mx, my, leftClicked, leftDown, wheelDelta);
        if (leftClicked) {
            return;
        }
        if (openWidget->isInside(mx, my)) {
            return;
        }
    }

    for (auto* w : widgets_) {
        if (w == openWidget) continue;
        w->update(mx, my, leftClicked, leftDown, wheelDelta);
    }
}

bool GuiWindow::isPointOccluded(float gx, float gy, const GuiWidget* drawingWidget) const {
    for (const auto* w : widgets_) {
        if (w == drawingWidget) continue;
        if (w->isOpen()) {
            float rx = gx - x_;
            float ry = gy - y_;
            if (w->isInside(rx, ry)) {
                return true;
            }
        }
    }
    return false;
}

bool GuiWindow::isPointInOpenWidget(int gx, int gy) const {
    for (auto* w : widgets_) {
        if (w->isOpen()) {
            if (w->isInside(gx - x_, gy - y_)) {
                return true;
            }
        }
    }
    return false;
}

void GuiWindow::closeWidgets() {
    for (auto* w : widgets_) {
        w->close();
    }
}

} // namespace engine3d
