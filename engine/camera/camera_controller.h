#pragma once

#include "core/math.h"

struct SDL_Window;

namespace engine3d {

class CameraComponent;

class CameraController {
public:
    CameraController();

    void rotatePitch(float degrees);
    void rotateYaw(float degrees);
    void moveForward(float velocityDelta);
    void moveRight(float velocityDelta);
    void moveUp(float velocityDelta);
    void stop();

    // Wywołaj co klatkę — stosuje tarcie, całkuje prędkości, aktualizuje cam.
    void update(float deltaTime, CameraComponent& cam);

    // Ustaw pozycję i kierunek patrzenia (kąty Eulera z wektora forward).
    void setPositionAndTarget(const Vec3& pos, const Vec3& target);
    void setPosition(const Vec3& pos);
    [[nodiscard]] Vec3 getPosition() const { return position_; }

    void setYaw(float deg) { headingDegrees_ = deg; }
    [[nodiscard]] float getYaw() const { return headingDegrees_; }
    void setPitch(float deg) { pitchDegrees_ = deg; }
    [[nodiscard]] float getPitch() const { return pitchDegrees_; }

    float moveSpeed = 2.0f;
    float mouseSensitivity = 0.25f;
    float friction = 1.7f;

private:
    float headingDegrees_ = 0.0f, pitchDegrees_ = 0.0f;
    Vec3 position_, realPosition_;
    float forwardVelocity_ = 0.0f, strafeVelocity_ = 0.0f, verticalVelocity_ = 0.0f;
};

} // namespace engine3d
