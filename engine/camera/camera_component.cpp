#include "camera_component.h"

namespace engine3d {

CameraComponent::CameraComponent() : Component("Camera") {}

Matrix4 CameraComponent::getViewMatrix() const {
    return lookAt(position, target, up);
}

Matrix4 CameraComponent::getProjectionMatrix() const {
    float aspect = 4.0f / 3.0f;
    return perspective(radians(fieldOfViewDegrees), aspect, nearPlane, farPlane);
}

} // namespace engine3d
