#pragma once

#include <cstdint>
#include <string>

namespace engine3d {

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

private:
    void* display_ = nullptr;
    unsigned long window_ = 0;
    unsigned long wmDeleteMessage_ = 0;
    bool shouldClose_ = false;
    bool resized_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

}  // namespace engine3d