#include "input_manager.h"
#include "x11_window.h"

#include <algorithm>
#include <cstring>

namespace engine3d {

// InputManager przyjmuje referencje do okna, z ktorego bedzie czytac stan urzadzen.
// Nie przejmuje wlasnosci okna - okno zyje dluzej niz InputManager.
InputManager::InputManager(X11Window& window)
    : window_(window) {
    keysCurrent_.fill(false);
    keysPrevious_.fill(false);
}

// poll() wywoluje X11Window::pollInput() i zapamietuje stan klawiszy.
// Dzieki zapamietaniu poprzedniego stanu mozliwe jest wykrywanie zboczy (isKeyJustPressed).
// Buforuje tez pelna migawke myszy, zeby uniknac wielokrotnych wywolan pollInput().
void InputManager::poll() {
    // Przesuwamy biezycy stan do poprzedniego.
    keysPrevious_ = keysCurrent_;

    // Pobieramy nowa migawke z okna.
    const InputSnapshot snapshot = window_.pollInput();

    // Kopiujemy stan klawiszy z migawki.
    std::copy(std::begin(snapshot.keys), std::end(snapshot.keys),
              std::begin(keysCurrent_));

    mouseDeltaX_ = snapshot.mouseDeltaX;
    mouseDeltaY_ = snapshot.mouseDeltaY;
    std::copy(std::begin(snapshot.mouseButtons), std::end(snapshot.mouseButtons),
              std::begin(mouseButtons_));
    mouseX_ = snapshot.mouseX;
    mouseY_ = snapshot.mouseY;
}

// isKeyDown sprawdza, czy klawisz jest fizycznie wcisniety w tej klatce.
bool InputManager::isKeyDown(int keycode) const {
    if (keycode < 0 || keycode >= 256) return false;
    return keysCurrent_[static_cast<std::size_t>(keycode)];
}

// isKeyJustPressed zwraca true tylko w pierwszej klatce, gdy klawisz zostal wcisniety.
// Dzieki temu mozna obslugiwac jednorazowe akcje (skok, strzal) bez wielokrotnego wywolania.
bool InputManager::isKeyJustPressed(int keycode) const {
    if (keycode < 0 || keycode >= 256) return false;
    const std::size_t k = static_cast<std::size_t>(keycode);
    return keysCurrent_[k] && !keysPrevious_[k];
}

bool InputManager::isMouseButtonDown(int button) const {
    const InputSnapshot snapshot = window_.pollInput();
    if (button < 0 || button >= 5) return false;
    return mouseButtons_[button];
}

int InputManager::mouseDeltaX() const {
    return mouseDeltaX_;
}

int InputManager::mouseDeltaY() const {
    return mouseDeltaY_;
}

int InputManager::mouseX() const {
    return mouseX_;
}

int InputManager::mouseY() const {
    return mouseY_;
}

void InputManager::grabPointer(bool grab) {
    window_.grabPointer(grab);
}

bool InputManager::isPointerGrabbed() const {
    return window_.pointerGrabbed();
}

}  // namespace engine3d
