#pragma once

#include "core/component.h"
#include "core/math.h"

namespace engine3d {

struct TransformComponent : public Component {
    TransformComponent() : Component("Transform") {}

    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    Matrix4 getLocalMatrix() const {
        return multiply(
            translation(position.x, position.y, position.z),
            multiply(
                rotationZ(rotation.z),
                multiply(
                    rotationY(rotation.y),
                    multiply(
                        rotationX(rotation.x),
                        engine3d::scale(scale.x, scale.y, scale.z)
                    )
                )
            )
        );
    }
};

} // namespace engine3d
