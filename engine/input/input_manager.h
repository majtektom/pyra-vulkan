#pragma once

#include <array>
#include <cstdint>
#include <memory>

namespace engine3d {

class X11Window;

// InputManager obsluguje klawiaturow, mysz i ew. kontroler.
// Wczytuje surowy stan z X11Window i udostepnia go przez czysty interfejs.
// Wzorem jest stary InputDevice.h z projektu stare/src/tomsengine/input/.
class InputManager {
public:
    // Tworzy menedzer inputu dla wskazanego okna.
    explicit InputManager(X11Window& window);

    // Wywoluj co klatke przed sprawdzeniem stanu klawiszy.
    // Zapisuje migawke stanu klawiatury i myszy na poczatek klatki.
    void poll();

    // Czy dany klawisz jest wcisniety?
    // keycode to surowy kod X11 (0-255), np. 25=W, 38=A, 39=S, 40=D.
    [[nodiscard]] bool isKeyDown(int keycode) const;

    // Czy klawisz zostal wlasnie wcisniety w tej klatce (zbocze narastajace)?
    [[nodiscard]] bool isKeyJustPressed(int keycode) const;

    // Czy przycisk myszy jest wcisniety? 0=lewy, 1=srodkowy, 2=prawy.
    [[nodiscard]] bool isMouseButtonDown(int button) const;

    // Pobiera przesuniecie myszy od ostatniego poll().
    [[nodiscard]] int mouseDeltaX() const;
    [[nodiscard]] int mouseDeltaY() const;

    // Pobiera absolutna pozycje kursora.
    [[nodiscard]] int mouseX() const;
    [[nodiscard]] int mouseY() const;

    // Chwyta/zwalnia kursor (tryb FPS).
    void grabPointer(bool grab);
    [[nodiscard]] bool isPointerGrabbed() const;

private:
    X11Window& window_;

    // Stan biezejcy i poprzedni do wykrywania zboczy.
    std::array<bool, 256> keysCurrent_{};
    std::array<bool, 256> keysPrevious_{};

    // Buforowana migawka z poll(), zeby dostep do skladowych myszy
    // nie resetowal delty przez wielokrotne wywolania pollInput().
    int mouseDeltaX_ = 0;
    int mouseDeltaY_ = 0;
    bool mouseButtons_[5] = {};
    int mouseX_ = 0;
    int mouseY_ = 0;
};

}  // namespace engine3d
