#pragma once

#include "component.h"
#include "math.h"

namespace engine3d {

class TransformComponent : public Component {
public:
    TransformComponent();
    TransformComponent(const Vec3& translation,
                       const Vec3& rotation,
                       const Vec3& scale);

    Matrix4 getModelMatrix() const;

    Vec3 translation{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

} // namespace engine3d
