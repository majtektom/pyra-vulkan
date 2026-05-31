#pragma once

#include <cstdint>
#include <string>

struct SDL_Window;
struct SDL_Cursor;

namespace engine3d {

struct InputSnapshot {
    bool keys[256] = {};
    int mouseDeltaX = 0;
    int mouseDeltaY = 0;
    bool mouseButtons[5] = {};
    int mouseX = 0;
    int mouseY = 0;
    int mouseWheel = 0;
};

class Platform {
public:
    Platform(const std::string& title, uint32_t width, uint32_t height);
    ~Platform();

    void processEvents();
    bool shouldClose() const;
    bool wasResized() const;
    void clearResizeFlag();
    uint32_t width() const;
    uint32_t height() const;

    SDL_Window* nativeWindow() const;

    void grabPointer(bool grab);
    bool pointerGrabbed() const;
    InputSnapshot pollInput();

    void setFullscreen(bool fullscreen);
    bool isFullscreen() const;

private:
    SDL_Window* window_ = nullptr;
    SDL_Cursor* invisibleCursor_ = nullptr;
    bool shouldClose_ = false;
    bool resized_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    InputSnapshot input_;
    bool pointerGrabbed_ = false;
    bool fullscreen_ = false;
};

} // namespace engine3d
