#pragma once

#include <functional>

#include "core/math.h"

namespace engine3d {

class CameraComponent;

/**
 * @brief Kontroler kamery strategicznej (RTS / city-builder).
 *
 * Kamera orbituje wokół punktu skupienia (focus) leżącego na ziemi:
 * - pan: przesuwanie punktu skupienia w płaszczyźnie XZ (klawisze / krawędź ekranu / przeciąganie),
 * - zoom: zmiana odległości od punktu skupienia (kółko myszy),
 * - rotate: obrót wokół osi Y (np. Q/E lub środkowy przycisk myszy),
 * - tilt: kąt nachylenia kamery (pitch).
 *
 * Wszystkie wartości docelowe są wygładzane wykładniczo, co daje płynny ruch.
 * Opcjonalna funkcja groundHeight pozwala trzymać punkt skupienia na wysokości terenu.
 */
class RtsCameraController {
public:
    RtsCameraController() = default;

    // Natychmiastowe ustawienie stanu (bez wygładzania).
    void setFocus(const Vec3& focus);
    void setDistance(float distance);
    void setYaw(float degrees);
    void setPitch(float degrees);

    // Sterowanie (wartości docelowe, wygładzane w update):
    // Przesunięcie w lokalnych osiach kamery rzutowanych na ziemię, w jednostkach świata.
    void pan(float rightAmount, float forwardAmount);
    // Dodatnie kroki przybliżają, ujemne oddalają.
    void zoom(float steps);
    void rotate(float degrees);
    void tilt(float degrees);
    // Płynny przelot do wskazanego punktu.
    void moveTo(const Vec3& focus);

    /**
     * @brief Aktualizuje wygładzanie i pozycjonuje kamerę.
     */
    void update(float deltaTime, CameraComponent& camera);

    [[nodiscard]] Vec3 focus() const { return focus_; }
    [[nodiscard]] Vec3 targetFocus() const { return targetFocus_; }
    [[nodiscard]] float distance() const { return distance_; }
    [[nodiscard]] float yaw() const { return yaw_; }
    [[nodiscard]] float pitch() const { return pitch_; }
    [[nodiscard]] Vec3 eyePosition() const;

    // Kierunki w płaszczyźnie ziemi (znormalizowane, y = 0).
    [[nodiscard]] Vec3 groundForward() const;
    [[nodiscard]] Vec3 groundRight() const;

    // Parametry
    float minDistance = 40.0f;
    float maxDistance = 900.0f;
    float minPitch = 25.0f;
    float maxPitch = 85.0f;
    float zoomStepFactor = 0.12f;       // ułamek odległości na jeden krok kółka
    float smoothing = 10.0f;            // szybkość dochodzenia do wartości docelowych (1/s)
    float minClearance = 12.0f;         // minimalna wysokość oka nad terenem
    Vec3 boundsMin{-1e9f, -1e9f, -1e9f};
    Vec3 boundsMax{1e9f, 1e9f, 1e9f};

    // Zwraca wysokość terenu w punkcie (x, z). Gdy pusta, focus.y pozostaje bez zmian.
    std::function<float(float, float)> groundHeight;

private:
    void clampTargets();

    Vec3 focus_{};
    Vec3 targetFocus_{};
    float distance_ = 300.0f;
    float targetDistance_ = 300.0f;
    float yaw_ = 45.0f;
    float targetYaw_ = 45.0f;
    float pitch_ = 55.0f;
    float targetPitch_ = 55.0f;
};

} // namespace engine3d
