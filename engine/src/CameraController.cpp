#include "engine/CameraController.hpp"
#include "engine/Types.hpp"

#include <algorithm>
#include <cmath>

namespace engine3d {

// Wzorem starego Plazer::Camera, inicjalizujemy wszystkie wartosci na 0.
CameraController::CameraController()
    = default;

// Glowna metoda aktualizacji kamery. Wzorowana na Plazer::Camera::SetPrespective(),
// ale zamiast modyfikowac macierz OpenGL, modyfikujemy CameraConfig (pozycja, target).
// deltaTime - czas od ostatniej klatki w sekundach.
// keys - tablica stanu klawiszy (indeksowana X11 keycode).
// mouseDeltaX/Y - przesuniecie myszy od ostatniej klatki.
// cfg - konfiguracja kamery silnika, ktora bedzie uzyta do UBO.
void CameraController::update(float deltaTime, const bool* keys,
                              int mouseDeltaX, int mouseDeltaY,
                              CameraConfig& cfg) {
    if (!initializedFromConfig_) {
        synchronizeFromConfig(cfg);
    }

    const float frameScale = std::max(deltaTime * 60.0f, 0.0f);

    constexpr int KEY_W = 25, KEY_S = 39, KEY_A = 38, KEY_D = 40;
    constexpr int KEY_SPACE = 65, KEY_LSHIFT = 50;

    changeHeading(static_cast<float>(mouseDeltaX) * mouseSensitivityDegrees_, frameScale);
    changePitch(static_cast<float>(mouseDeltaY) * mouseSensitivityDegrees_, frameScale);

    const float velocityDelta = acceleration_ * deltaTime;
    if (keys[KEY_W]) {
        changeForwardVelocity(velocityDelta);
    }
    if (keys[KEY_S]) {
        changeForwardVelocity(-velocityDelta);
    }
    if (keys[KEY_A]) {
        changeStrafeVelocity(-velocityDelta);
    }
    if (keys[KEY_D]) {
        changeStrafeVelocity(velocityDelta);
    }

    if (forwardVelocity_ > 0.0f) {
        forwardVelocity_ = std::max(0.0f, forwardVelocity_ - friction_ * deltaTime);
    } else if (forwardVelocity_ < 0.0f) {
        forwardVelocity_ = std::min(0.0f, forwardVelocity_ + friction_ * deltaTime);
    }

    if (strafeVelocity_ > 0.0f) {
        strafeVelocity_ = std::max(0.0f, strafeVelocity_ - friction_ * deltaTime);
    } else if (strafeVelocity_ < 0.0f) {
        strafeVelocity_ = std::min(0.0f, strafeVelocity_ + friction_ * deltaTime);
    }

    const Quaternion pitchRotation = quaternionFromAxisAngle({0.0f, 0.0f, 1.0f}, radians(pitchDegrees_));
    const Quaternion headingRotation = quaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, radians(headingDegrees_));
    const Quaternion orientation = normalize(multiply(headingRotation, pitchRotation));

    const Vec3 forward = normalize(rotateVector(orientation, {1.0f, 0.0f, 0.0f}));
    Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    if (dot(right, right) == 0.0f) {
        right = {0.0f, 0.0f, 1.0f};
    }
    const Vec3 up = normalize(cross(right, forward));

    directionVector_ = forward;
    strafeVector_ = right;

    directionVector_ = multiply(directionVector_, forwardVelocity_);
    strafeVector_ = multiply(strafeVector_, strafeVelocity_);

    lastPosition_ = position_;
    realPosition_ = add(realPosition_, add(directionVector_, strafeVector_));

    if (keys[KEY_SPACE]) {
        realPosition_.y += acceleration_ * deltaTime;
    }
    if (keys[KEY_LSHIFT]) {
        realPosition_.y -= acceleration_ * deltaTime;
    }

    position_ = realPosition_;
    cfg.position = position_;
    cfg.target = add(position_, forward);
    cfg.up = up;
}

void CameraController::changePitch(float degreesDelta, float frameScale) {
    const float maxDelta = maxPitchRateDegrees_ * frameScale;
    const float appliedDelta = std::clamp(degreesDelta, -maxDelta, maxDelta);
    pitchDegrees_ += appliedDelta;

    if (pitchDegrees_ > 360.0f) {
        pitchDegrees_ -= 360.0f;
    } else if (pitchDegrees_ < -360.0f) {
        pitchDegrees_ += 360.0f;
    }
}

void CameraController::changeHeading(float degreesDelta, float frameScale) {
    const float maxDelta = maxHeadingRateDegrees_ * frameScale;
    float appliedDelta = std::clamp(degreesDelta, -maxDelta, maxDelta);
    const bool upsideDown =
        (pitchDegrees_ > 90.0f && pitchDegrees_ < 270.0f) ||
        (pitchDegrees_ < -90.0f && pitchDegrees_ > -270.0f);
    if (upsideDown) {
        appliedDelta = -appliedDelta;
    }

    headingDegrees_ += appliedDelta;
    if (headingDegrees_ > 360.0f) {
        headingDegrees_ -= 360.0f;
    } else if (headingDegrees_ < -360.0f) {
        headingDegrees_ += 360.0f;
    }
}

void CameraController::changeForwardVelocity(float velocityDelta) {
    const float appliedDelta = std::clamp(velocityDelta, -maxForwardVelocity_, maxForwardVelocity_);
    forwardVelocity_ += appliedDelta;
}

void CameraController::changeStrafeVelocity(float velocityDelta) {
    const float appliedDelta = std::clamp(velocityDelta, -maxForwardVelocity_, maxForwardVelocity_);
    strafeVelocity_ += appliedDelta;
}

void CameraController::synchronizeFromConfig(const CameraConfig& cfg) {
    position_ = cfg.position;
    realPosition_ = cfg.position;
    lastPosition_ = cfg.position;

    const Vec3 forward = normalize(subtract(cfg.target, cfg.position));
    pitchDegrees_ = std::asin(std::clamp(forward.y, -1.0f, 1.0f)) * 180.0f / 3.1415926535f;
    headingDegrees_ = std::atan2(forward.z, forward.x) * 180.0f / 3.1415926535f;
    initializedFromConfig_ = true;
}

}  // namespace engine3d
