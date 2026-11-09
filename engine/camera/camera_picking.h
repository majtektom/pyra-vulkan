#pragma once

#include "core/math.h"

namespace engine3d {

class CameraComponent;

/**
 * @brief Półprosta w przestrzeni świata (punkt startowy + znormalizowany kierunek).
 */
struct Ray {
    Vec3 origin{};
    Vec3 direction{0.0f, 0.0f, -1.0f};

    [[nodiscard]] Vec3 at(float t) const { return add(origin, multiply(direction, t)); }
};

/**
 * @brief Buduje promień z kamery przechodzący przez piksel ekranu (picking myszą).
 *
 * Działa na bazie kamery (forward/right/up) i jej FOV oraz proporcji, dzięki czemu
 * nie trzeba odwracać macierzy projekcji. Współrzędne piksela liczone od lewego górnego rogu.
 */
[[nodiscard]] Ray screenPointToRay(const CameraComponent& camera, float pixelX, float pixelY,
                                   float screenWidth, float screenHeight);

/**
 * @brief Rzutuje punkt świata na współrzędne ekranu w pikselach.
 * @return false, jeśli punkt leży za kamerą.
 */
bool worldToScreen(const CameraComponent& camera, const Vec3& worldPoint,
                   float screenWidth, float screenHeight, float& outX, float& outY);

/**
 * @brief Przecięcie promienia z poziomą płaszczyzną y = planeY.
 */
bool rayPlaneY(const Ray& ray, float planeY, Vec3& outHit);

/**
 * @brief Przecięcie promienia ze sferą. Zwraca odległość t wzdłuż promienia.
 */
bool raySphere(const Ray& ray, const Vec3& center, float radius, float& outT);

} // namespace engine3d
