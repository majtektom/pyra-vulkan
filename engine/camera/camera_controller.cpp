#include "camera/camera_controller.h"
#include "camera/camera_component.h"
#include "core/entity.h"
#include "core/types.h"
#include <algorithm>
#include <cmath>

namespace engine3d {

static float degToRad(float d) { return d * 3.1415926535f / 180.0f; }

CameraController::CameraController() = default;

void CameraController::rotatePitch(float degrees) {
    pitchDegrees_ = std::clamp(pitchDegrees_ + degrees, -89.0f, 89.0f);
}

void CameraController::rotateYaw(float degrees) {
    headingDegrees_ += degrees;
    if (headingDegrees_ > 360.0f) headingDegrees_ -= 360.0f;
    if (headingDegrees_ < -360.0f) headingDegrees_ += 360.0f;
}

void CameraController::moveForward(float v) { forwardVelocity_ = std::clamp(forwardVelocity_ + v, -1.0f, 1.0f); }
void CameraController::moveRight(float v)   { strafeVelocity_ = std::clamp(strafeVelocity_ + v, -1.0f, 1.0f); }
void CameraController::moveUp(float v)      { verticalVelocity_ = std::clamp(verticalVelocity_ + v, -1.0f, 1.0f); }
void CameraController::stop() { forwardVelocity_ = strafeVelocity_ = verticalVelocity_ = 0.0f; }

void CameraController::setPosition(const Vec3& pos) { position_ = realPosition_ = pos; }

void CameraController::setPositionAndTarget(const Vec3& pos, const Vec3& target) {
    position_ = realPosition_ = pos;
    Vec3 forward = normalize(subtract(target, pos));
    pitchDegrees_ = std::asin(std::clamp(forward.y, -1.0f, 1.0f)) * 180.0f / 3.1415926535f;
    headingDegrees_ = std::atan2(-forward.x, -forward.z) * 180.0f / 3.1415926535f;
}

void CameraController::update(float deltaTime, CameraComponent& cam) {
    float f = std::max(0.0f, 1.0f - friction * deltaTime);
    forwardVelocity_ *= f;
    strafeVelocity_ *= f;
    verticalVelocity_ *= f;

    const Quaternion qPitch = quaternionFromAxisAngle({1.0f, 0.0f, 0.0f}, degToRad(pitchDegrees_));
    const Quaternion qYaw = quaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, degToRad(headingDegrees_));
    const Quaternion combined = normalize(multiply(qYaw, qPitch));

    const Vec3 forward = rotateVector(combined, {0.0f, 0.0f, -1.0f});
    const Vec3 right = rotateVector(combined, {1.0f, 0.0f, 0.0f});

    Vec3 movement = add(add(multiply(forward, forwardVelocity_ * moveSpeed * deltaTime),
                            multiply(right, strafeVelocity_ * moveSpeed * deltaTime)),
                       Vec3{0.0f, verticalVelocity_ * moveSpeed * deltaTime, 0.0f});

    realPosition_ = add(realPosition_, movement);
    position_ = realPosition_;

    cam.getOwner()->transform().position = position_;
    cam.target = add(position_, forward);
    cam.up = normalize(cross(right, forward));
}

} // namespace engine3d
