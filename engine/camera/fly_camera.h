#pragma once

namespace engine3d {

class Engine;
class InputActionMap;

/**
 * @brief Kamera swobodna ("latająca") sterowana klawiaturą i myszą - wspólna dla przykładów.
 *
 * Mysz obraca kamerę, akcje ruchu przesuwają ją (płynnie, z hamowaniem w CameraController).
 * Ruch jest mnożony przez czas klatki, więc prędkość nie zależy od liczby FPS.
 *
 * @code
 *   FlyCamera::bindDefaultKeys(engine.inputActions());   // W/S/A/D, spacja, lewy Ctrl
 *   engine.onUpdate = [&](float dt, Engine& e) {
 *       FlyCamera::update(e, dt, 15.0f);                 // prędkość w jednostkach świata na sekundę
 *   };
 * @endcode
 */
namespace FlyCamera {

// Nazwy akcji ruchu - gra może przypisać im inne klawisze przez InputActionMap.
inline constexpr const char* kMoveForward = "move.forward";
inline constexpr const char* kMoveBackward = "move.backward";
inline constexpr const char* kMoveLeft = "move.left";
inline constexpr const char* kMoveRight = "move.right";
inline constexpr const char* kMoveUp = "move.up";
inline constexpr const char* kMoveDown = "move.down";

// W/S - przód/tył, A/D - boki, spacja - w górę, lewy Ctrl - w dół.
void bindDefaultKeys(InputActionMap& map);

// Obrót myszą i ruch akcjami dla aktywnej kamery sceny. Nic nie robi, gdy scena nie ma kamery.
void update(Engine& engine, float deltaTime, float moveSpeed);

} // namespace FlyCamera

} // namespace engine3d
