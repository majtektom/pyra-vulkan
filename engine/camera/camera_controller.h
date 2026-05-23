#pragma once

#include "math.h"

namespace engine3d {

struct InputState;
struct CameraConfig;

// CameraController to odpowiednik Plazer::Camera ze starego projektu (camera.h/camera.cpp).
// Obsluguje pierwszoosobowe latanie (FPS-style): pitch, yaw, ruch WASD.
// Nie zawiera kodu Vulkan - dziala wylacznie na CPU, modyfikujac CameraConfig.
class CameraController {
public:
    CameraController();

    // Wywoluj co klatke z czasem delta i stanem inputu.
    // Na podstawie wcisnietych klawiszy i ruchu myszy aktualizuje kamere w cfg.
    // Wzorowane na Plazer::Camera::SetPrespective() i ChangeHeading/ChangePitch.
    void update(float deltaTime, const bool* keys, int mouseDeltaX, int mouseDeltaY,
                CameraConfig& cfg);

    void setPosition(const Vec3& pos) {
        position_ = pos;
        realPosition_ = pos;
        lastPosition_ = pos;
        initializedFromConfig_ = true;
    }
    [[nodiscard]] Vec3 getPosition() const { return position_; }

    void stop();
    void stopY() { stopY_ = true; }

    // Ustawia/odczytuje kat pochylenia (pitch) i obrotu (yaw) w radianach.
    void setYaw(float yawDegrees) { headingDegrees_ = yawDegrees; }
    [[nodiscard]] float getYaw() const { return headingDegrees_; }
    void setPitch(float pitchDegrees) { pitchDegrees_ = pitchDegrees; }
    [[nodiscard]] float getPitch() const { return pitchDegrees_; }

    // Wspolczynniki szybkosci i czulosci myszy.
    void setMoveSpeed(float speed) { acceleration_ = speed; }
    [[nodiscard]] float getMoveSpeed() const { return acceleration_; }
    void setMouseSensitivity(float sens) { mouseSensitivityDegrees_ = sens; }
    [[nodiscard]] float getMouseSensitivity() const { return mouseSensitivityDegrees_; }

private:
    void changePitch(float degreesDelta, float frameScale);
    void changeHeading(float degreesDelta, float frameScale);
    void changeForwardVelocity(float velocityDelta);
    void changeStrafeVelocity(float velocityDelta);
    void synchronizeFromConfig(const CameraConfig& cfg);

    float headingDegrees_ = 0.0f;
    float pitchDegrees_ = 0.0f;
    Vec3 position_{0.0f, 0.0f, 0.0f};
    Vec3 lastPosition_{0.0f, 0.0f, 0.0f};
    Vec3 realPosition_{0.0f, 0.0f, 0.0f};
    Vec3 directionVector_{0.0f, 0.0f, 0.0f};
    Vec3 directionVectorLR_{0.0f, 0.0f, 0.0f};
    float maxPitchRateDegrees_ = 3.5f;
    float maxHeadingRateDegrees_ = 3.5f;
    float maxForwardVelocity_ = 0.12f;
    float forwardVelocity_ = 0.0f;
    float strafeVelocity_ = 0.0f;
    float acceleration_ = 2.0f;
    float friction_ = 1.7f;
    float mouseSensitivityDegrees_ = 0.25f;
    bool stopY_ = false;
    bool initializedFromConfig_ = false;
};

}  // namespace engine3d
