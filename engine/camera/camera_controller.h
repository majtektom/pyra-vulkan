#pragma once

#include "core/math.h"

struct SDL_Window;

namespace engine3d {

class CameraComponent;

/**
 * @brief Kontroler kamery pierwszoosobowej (FPS Camera Controller).
 *
 * Umożliwia swobodne przemieszczanie się w trójwymiarowym świecie za pomocą klawiatury i myszy:
 * - Kąty obrotu (Yaw/Pitch) sterowane myszą.
 * - Ruch (przód/tył, lewo/prawo, góra/dół) z symulacją fizyki (tarcie i płynne hamowanie).
 *
 * Kontroler operuje na kwaternionach w celu uniknięcia problemu blokady przegubu (Gimbal Lock)
 * podczas wyznaczania wektorów kierunku kamery.
 */
class CameraController {
public:
    CameraController();

    /**
     * @brief Obraca kamerę w pionie (pochylenie - Pitch / góra-dół).
     * @param degrees Kąt obrotu w stopniach. Pochylenie jest ograniczone do zakresu [-89, 89] stopni.
     */
    void rotatePitch(float degrees);

    /**
     * @brief Obraca kamerę w poziomie (obrót - Yaw / lewo-prawo).
     */
    void rotateYaw(float degrees);

    // Sterowanie prędkościami ruchu:
    void moveForward(float velocityDelta);
    void moveRight(float velocityDelta);
    void moveUp(float velocityDelta);

    /**
     * @brief Aktualizuje fizykę ruchu kontrolera i pozycjonuje powiązaną kamerę.
     * Wywoływane co klatkę gry.
     * @param deltaTime Krok czasu (delta time).
     * @param cam Komponent kamery, który zostanie zaktualizowany.
     */
    void update(float deltaTime, CameraComponent& cam);

    /**
     * @brief Wymusza pozycję kontrolera oraz wyznacza kąty Yaw/Pitch na podstawie punktu patrzenia.
     */
    void setPositionAndTarget(const Vec3& pos, const Vec3& target);
    void setPosition(const Vec3& pos);
    
    void setYaw(float deg) { headingDegrees_ = deg; }
    void setPitch(float deg) { pitchDegrees_ = deg; }

    float moveSpeed = 2.0f;          // Prędkość poruszania się kamery.
    float mouseSensitivity = 0.25f;  // Czułość ruchów myszy.
    float friction = 1.7f;          // Współczynnik tarcia (siła automatycznego hamowania).

private:
    float headingDegrees_ = 0.0f;    // Kąt obrotu wokół osi Y (Yaw).
    float pitchDegrees_ = 0.0f;      // Kąt obrotu wokół osi X (Pitch).
    Vec3 position_;                  // Aktualna pozycja kontrolera.
    Vec3 realPosition_;              // Rzeczywista pozycja (używana do precyzyjnych obliczeń zmiennoprzecinkowych).
    
    // Prędkości ruchu w lokalnych osiach kontrolera:
    float forwardVelocity_ = 0.0f;
    float strafeVelocity_ = 0.0f;
    float verticalVelocity_ = 0.0f;
};

} // namespace engine3d
