#include "camera/rts_camera_controller.h"

#include <algorithm>
#include <cmath>

#include "camera/camera_component.h"
#include "core/entity.h"

namespace engine3d {

void RtsCameraController::setFocus(const Vec3& focus) {
    targetFocus_ = focus;
    clampTargets();
    focus_ = targetFocus_;
}

void RtsCameraController::setDistance(float distance) {
    targetDistance_ = distance;
    clampTargets();
    distance_ = targetDistance_;
}

void RtsCameraController::setYaw(float degrees) {
    yaw_ = targetYaw_ = degrees;
}

void RtsCameraController::setPitch(float degrees) {
    targetPitch_ = degrees;
    clampTargets();
    pitch_ = targetPitch_;
}

Vec3 RtsCameraController::groundForward() const {
    // Kamera patrzy z pozycji (focus - forward * d), więc forward to kierunek od oka do celu.
    const float yawRad = radians(yaw_);
    return {-std::sin(yawRad), 0.0f, -std::cos(yawRad)};
}

Vec3 RtsCameraController::groundRight() const {
    const Vec3 f = groundForward();
    return {-f.z, 0.0f, f.x};
}

void RtsCameraController::pan(float rightAmount, float forwardAmount) {
    const Vec3 f = groundForward();
    const Vec3 r = groundRight();
    targetFocus_.x += r.x * rightAmount + f.x * forwardAmount;
    targetFocus_.z += r.z * rightAmount + f.z * forwardAmount;
    clampTargets();
}

void RtsCameraController::zoom(float steps) {
    targetDistance_ *= std::pow(1.0f - zoomStepFactor, steps);
    clampTargets();
}

void RtsCameraController::rotate(float degrees) {
    targetYaw_ += degrees;
}

void RtsCameraController::tilt(float degrees) {
    targetPitch_ += degrees;
    clampTargets();
}

void RtsCameraController::moveTo(const Vec3& focus) {
    targetFocus_.x = focus.x;
    targetFocus_.z = focus.z;
    clampTargets();
}

void RtsCameraController::clampTargets() {
    targetFocus_.x = std::clamp(targetFocus_.x, boundsMin.x, boundsMax.x);
    targetFocus_.z = std::clamp(targetFocus_.z, boundsMin.z, boundsMax.z);
    targetDistance_ = std::clamp(targetDistance_, minDistance, maxDistance);
    targetPitch_ = std::clamp(targetPitch_, minPitch, maxPitch);
}

Vec3 RtsCameraController::eyePosition() const {
    const float yawRad = radians(yaw_);
    const float pitchRad = radians(pitch_);
    const float horizontal = std::cos(pitchRad) * distance_;
    return {
        focus_.x + std::sin(yawRad) * horizontal,
        focus_.y + std::sin(pitchRad) * distance_,
        focus_.z + std::cos(yawRad) * horizontal,
    };
}

void RtsCameraController::update(float deltaTime, CameraComponent& camera) {
    // Wygładzanie wykładnicze niezależne od liczby klatek.
    const float k = 1.0f - std::exp(-smoothing * std::max(0.0f, deltaTime));

    if (groundHeight) {
        targetFocus_.y = groundHeight(targetFocus_.x, targetFocus_.z);
    }

    focus_ = lerp(focus_, targetFocus_, k);
    distance_ += (targetDistance_ - distance_) * k;
    yaw_ += (targetYaw_ - yaw_) * k;
    pitch_ += (targetPitch_ - pitch_) * k;

    Vec3 eye = eyePosition();
    if (groundHeight) {
        const float ground = groundHeight(eye.x, eye.z);
        eye.y = std::max(eye.y, ground + minClearance);
    }

    if (auto* owner = camera.getOwner()) {
        owner->transform().position = eye;
    }
    camera.target = focus_;
    camera.up = {0.0f, 1.0f, 0.0f};
}

} // namespace engine3d
