#pragma once

#include "core/math.h"
#include "core/component.h"

namespace engine3d {

/**
 * @brief Komponent kamery (Camera Component).
 *
 * Reprezentuje oczy obserwatora w wirtualnym świecie 3D. Przechowuje właściwości optyczne kamery:
 * - fieldOfViewDegrees (kąt widzenia FOV)
 * - zakres widoczności (nearPlane, farPlane)
 * - punkt patrzenia (target)
 * - wektor góry kamery (up)
 *
 * Służy do generowania macierzy widoku (View Matrix) oraz macierzy rzutowania (Projection Matrix).
 */
class CameraComponent : public Component {
public:
    CameraComponent();

    /**
     * @brief Wylicza i zwraca macierz widoku (View Matrix).
     * Macierz ta transformuje współrzędne obiektów z przestrzeni świata (World Space)
     * do przestrzeni kamery/oka (View Space).
     */
    Matrix4 getViewMatrix() const;

    /**
     * @brief Wylicza i zwraca macierz rzutowania perspektywicznego (Projection Matrix).
     * Macierz ta rzutuje obiekty z przestrzeni kamery do przestrzeni ekranu (znormalizowany układ NDC),
     * nadając scenie głębię (efekt perspektywy: obiekty dalsze są mniejsze).
     */
    Matrix4 getProjectionMatrix() const;

    Vec3 target{0.0f, 0.0f, 0.0f};      // Punkt w przestrzeni 3D, na który skierowana jest kamera.
    Vec3 up{0.0f, 1.0f, 0.0f};          // Wektor kierunku "w górę" kamery (zazwyczaj {0, 1, 0}).
    float fieldOfViewDegrees = 45.0f;   // Kąt widzenia pionowego w stopniach (FOV).
    float nearPlane = 0.1f;             // Odległość do najbliższej płaszczyzny obcinania (bliżej obiekty są ignorowane).
    float farPlane = 10.0f;             // Odległość do najdalszej płaszczyzny obcinania (dalej obiekty nie są rysowane).
};

} // namespace engine3d
