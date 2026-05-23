#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "types.h"

namespace engine3d {

struct InputSnapshot {
    bool keys[256] = {};
    int mouseDeltaX = 0;
    int mouseDeltaY = 0;
    bool mouseButtons[5] = {};
    int mouseX = 0;
    int mouseY = 0;

    [[nodiscard]] InputState toInputState() const {
        InputState state{};
        std::copy(std::begin(keys), std::end(keys), std::begin(state.keys));
        state.mouseDeltaX = mouseDeltaX;
        state.mouseDeltaY = mouseDeltaY;
        std::copy(std::begin(mouseButtons), std::end(mouseButtons), std::begin(state.mouseButtons));
        state.mouseX = mouseX;
        state.mouseY = mouseY;
        return state;
    }
};

// Wydzielony backend okna X11. Renderer korzysta tylko z jego prostego interfejsu.
class X11Window {
public:
    // Tworzy natywne okno o zadanym tytule i rozmiarze poczatkowym.
    X11Window(const std::string& title, uint32_t width, uint32_t height);

    // Destruktor sprzata zasoby X11 powiazane z oknem.
    ~X11Window();
    X11Window(const X11Window&) = delete;
    X11Window& operator=(const X11Window&) = delete;
    X11Window(X11Window&&) = delete;
    X11Window& operator=(X11Window&&) = delete;

    // Odswieza kolejke zdarzen i aktualizuje stan okna.
    void processEvents();

    // Informuje, czy uzytkownik zazadal zamkniecia okna.
    [[nodiscard]] bool shouldClose() const;

    // Informuje, czy od ostatniego renderu zmienil sie rozmiar okna.
    [[nodiscard]] bool wasResized() const;

    // Czyści flage resize po odtworzeniu swapchaina.
    void clearResizeFlag();

    // Aktualny rozmiar okna w pikselach.
    [[nodiscard]] uint32_t width() const;
    [[nodiscard]] uint32_t height() const;

    // Surowe uchwyty natywne potrzebne rendererowi do stworzenia powierzchni Vulkan.
    [[nodiscard]] void* nativeDisplay() const;
    [[nodiscard]] unsigned long nativeWindow() const;

    void grabPointer(bool grab);
    [[nodiscard]] bool pointerGrabbed() const;
    [[nodiscard]] InputSnapshot pollInput();

private:
    void* display_ = nullptr;
    unsigned long window_ = 0;
    unsigned long wmDeleteMessage_ = 0;
    bool shouldClose_ = false;
    bool resized_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    std::array<bool, 256> keys_{};
    int mouseDeltaX_ = 0;
    int mouseDeltaY_ = 0;
    std::array<bool, 5> mouseButtons_{};
    int mouseX_ = 0;
    int mouseY_ = 0;
    bool pointerGrabbed_ = false;
};

}  // namespace engine3d
