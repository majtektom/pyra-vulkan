#include "camera_controller.h"
#include "types.h"

#include <algorithm>
#include <cmath>

namespace engine3d {

CameraController::CameraController() = default;

void CameraController::update(float deltaTime, const bool* keys,
                              int mouseDeltaX, int mouseDeltaY,
                              CameraConfig& cfg) {
    if (!initializedFromConfig_) {
        synchronizeFromConfig(cfg);
    }

    const float frameScale = std::max(deltaTime * 60.0f, 0.0f);

    constexpr int KEY_W = 25, KEY_S = 39, KEY_A = 38, KEY_D = 40;
    constexpr int KEY_SPACE = 65, KEY_LSHIFT = 50;

    changeHeading(-static_cast<float>(mouseDeltaX) * mouseSensitivityDegrees_, frameScale);
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

    // Stary Plazer::Camera:
    //   m_qPitch = kwaternion obrotu wokol osi X o kat pitch
    //   m_qHeading = kwaternion obrotu wokol osi Y o kat heading
    // Negujemy pitch, bo prawoskrętny obrót wokół +X daje forward.y = -sin(pitch).
    // Chcemy forward.y = sin(pitch), czyli dodatni pitch = patrzenie w górę.
    const Quaternion qPitch = quaternionFromAxisAngle({1.0f, 0.0f, 0.0f}, -radians(pitchDegrees_));
    const Quaternion qHeading = quaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, radians(headingDegrees_));

    const Quaternion combined = normalize(multiply(qHeading, qPitch));
    // Silnik (lookAt, CameraComponent) uzywa konwencji -Z jako forward.
    const Vec3 forward = rotateVector(combined, {0.0f, 0.0f, -1.0f});
    const Vec3 right = rotateVector(combined, {1.0f, 0.0f, 0.0f});

    directionVector_ = forward;
    directionVectorLR_ = right;

    Vec3 movement = add(multiply(directionVector_, forwardVelocity_),
                        multiply(directionVectorLR_, strafeVelocity_));

    lastPosition_ = position_;
    realPosition_ = add(realPosition_, movement);

    if (keys[KEY_SPACE]) {
        realPosition_.y += acceleration_ * deltaTime;
    }
    if (keys[KEY_LSHIFT]) {
        realPosition_.y -= acceleration_ * deltaTime;
    }

    if (stopY_) {
        realPosition_.y = lastPosition_.y;
        position_.y = lastPosition_.y;
        stopY_ = false;
    } else {
        position_ = realPosition_;
    }

    cfg.position = position_;
    cfg.target = add(position_, forward);
    cfg.up = normalize(cross(right, forward));
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
    headingDegrees_ = std::atan2(-forward.x, -forward.z) * 180.0f / 3.1415926535f;
    initializedFromConfig_ = true;
}

void CameraController::stop() {
    forwardVelocity_ = 0.0f;
    strafeVelocity_ = 0.0f;
}

}  // namespace engine3d
