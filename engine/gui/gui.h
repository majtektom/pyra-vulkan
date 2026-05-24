#pragma once

#include "gui_renderer.h"
#include "math.h"
#include "types.h"
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <memory>

namespace engine3d {

class Platform;
class Engine;

using TextDrawFn = std::function<void(const std::string& text, float x, float y, float scale, std::array<float,4> color)>;

static constexpr int kTitleBarH = 25;

struct GuiMsg {
    void* element = nullptr;
    enum { CLICK, STRING, BOOL, REAL } type = CLICK;
    std::string text;
    bool flag = false;
    float value = 0.0f;
};

class GuiWindow;

class GuiWidget {
public:
    explicit GuiWidget(GuiWindow* parent);
    virtual ~GuiWidget();
    virtual void update(int mx, int my, bool leftClicked, bool leftDown, int wheelDelta);
    virtual void draw(GuiRenderer& r, TextDrawFn textFn, int winX, int winY, float sw, float sh) const = 0;

protected:
    GuiWindow* parent_;
};

class GuiMgr {
public:
    enum class CursorMode { System, Custom };

    GuiMgr() = default;
    ~GuiMgr();

    void init(VkDevice device, VkPhysicalDevice pd, VkRenderPass rp, VkCommandPool cp, VkQueue q, VkExtent2D ext,
              TextDrawFn textFn);

    GuiWindow* addWindow(const std::string& name, int x, int y, int w, int h);
    bool removeWindow(const std::string& name);
    GuiWindow* getWindow(const std::string& name);
    void forEachWindow(std::function<void(GuiWindow&)> fn);

    void setActive(const std::string& name);
    void update(Platform& platform, float screenW, float screenH);

    void enableCursor(bool enabled) { cursorEnabled_ = enabled; }
    [[nodiscard]] bool isCursorEnabled() const { return cursorEnabled_; }
    [[nodiscard]] int mouseX() const { return mouseX_; }
    [[nodiscard]] int mouseY() const { return mouseY_; }

    void setCursorMode(CursorMode mode) { cursorMode_ = mode; }
    [[nodiscard]] CursorMode cursorMode() const { return cursorMode_; }

    void render();

    void build(uint32_t frameIndex);
    void recordDrawCommands(VkCommandBuffer cmd, uint32_t frameIndex);
    [[nodiscard]] bool isReady() const { return guiRenderer_.isReady() && textFn_ != nullptr; }

private:
    void drawCursor();

    std::map<std::string, std::unique_ptr<GuiWindow>> windows_;
    GuiWindow* activeWindow_ = nullptr;
    int mouseX_ = 0, mouseY_ = 0;
    bool cursorEnabled_ = true;
    CursorMode cursorMode_ = CursorMode::Custom;

    GuiRenderer guiRenderer_;
    TextDrawFn textFn_;
    float screenW_ = 1024.0f, screenH_ = 768.0f;
};

class GuiWindow {
public:
    GuiWindow(const std::string& name, int x, int y, int w, int h);
    ~GuiWindow();

    const std::string& name() const { return name_; }
    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    bool isActive() const { return active_; }
    void setActive(bool a) { active_ = a; }

    // Każde okno ma tytuł i prostokątne tło, chyba że ukryte przez setChromeVisible(false)
    int x() const { return x_; } int y() const { return y_; }
    int width() const { return w_; } int height() const { return h_; }

    void setChromeVisible(bool v) { chromeVisible_ = v; }
    [[nodiscard]] bool isChromeVisible() const { return chromeVisible_; }

    void addWidget(GuiWidget* w);
    void removeWidget(GuiWidget* w);
    void render(GuiRenderer& r, TextDrawFn textFn, float sw, float sh) const;
    void updateWidgets(int mx, int my, bool leftClicked, bool leftDown, int wheelDelta);

     // Callback dla kliknięć w przycisk zamknięcia itp
    std::function<void(GuiMsg)> onMessage;

private:
    friend class GuiMgr;
    void handleMouse(int mx, int my, bool leftDown, bool leftUp);

    std::string name_;
    int x_, y_, w_, h_;
    bool visible_ = true, active_ = false;
    bool chromeVisible_ = true;
    bool dragging_ = false, resizing_ = false;
    int dragOffX_ = 0, dragOffY_ = 0;
    std::vector<GuiWidget*> widgets_;

    static constexpr int kBorderW = 4;
};

} // namespace engine3d
