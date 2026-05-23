#pragma once

#include <cstdint>
#include <string>

typedef struct _XDisplay Display;

namespace engine3d {

struct InputSnapshot {
    bool keys[256] = {};
    int mouseDeltaX = 0;
    int mouseDeltaY = 0;
    bool mouseButtons[5] = {};
    int mouseX = 0;
    int mouseY = 0;
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

    void* nativeDisplay() const;
    unsigned long nativeWindow() const;

    void grabPointer(bool grab);
    bool pointerGrabbed() const;
    InputSnapshot pollInput();

private:
    Display* display_ = nullptr;
    unsigned long window_ = 0;
    unsigned long wmDeleteMessage_ = 0;
    bool shouldClose_ = false;
    bool resized_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    bool keys_[256] = {};
    int mouseDeltaX_ = 0;
    int mouseDeltaY_ = 0;
    bool mouseButtons_[5] = {};
    int mouseX_ = 0;
    int mouseY_ = 0;
    bool pointerGrabbed_ = false;
    unsigned long invisibleCursor_ = 0;
};

} // namespace engine3d
