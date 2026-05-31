#pragma once

#include "core/math.h"
#include "core/component.h"

namespace engine3d {

class CameraComponent : public Component {
public:
    CameraComponent();

    Matrix4 getViewMatrix() const;
    Matrix4 getProjectionMatrix() const;

    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fieldOfViewDegrees = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 10.0f;
};

} // namespace engine3d
