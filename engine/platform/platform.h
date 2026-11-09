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
    int mouseWheel = 0;          // pełne "ząbki" kółka (z akumulacją ułamków)
    float mouseWheelF = 0.0f;    // dokładna wartość (myszy wysokiej rozdzielczości, touchpady)
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
    // Okno zminimalizowane — powierzchnia ma rozmiar 0, swapchaina nie da się utworzyć.
    bool isMinimized() const;

    SDL_Window* nativeWindow() const;

    void grabPointer(bool grab);
    bool pointerGrabbed() const;
    InputSnapshot pollInput();

    void setFullscreen(bool fullscreen);
    bool isFullscreen() const;

    // Rozmiar okna w trybie okienkowym (okno jest wyśrodkowywane). Swapchain odtworzy się po zdarzeniu resize.
    void setWindowSize(uint32_t width, uint32_t height);
    // Użyteczny obszar ekranu, na którym jest okno (bez paska zadań).
    bool usableDisplaySize(uint32_t& width, uint32_t& height) const;

private:
    SDL_Window* window_ = nullptr;
    SDL_Cursor* invisibleCursor_ = nullptr;
    bool shouldClose_ = false;
    bool resized_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    InputSnapshot input_;
    bool pointerGrabbed_ = false;
    float wheelRemainder_ = 0.0f;
    bool fullscreen_ = false;
};

} // namespace engine3d
