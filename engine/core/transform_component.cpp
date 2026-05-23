#include "transform_component.h"

namespace engine3d {

TransformComponent::TransformComponent()
    : Component("Transform") {}

TransformComponent::TransformComponent(const Vec3& translation,
                                       const Vec3& rotation,
                                       const Vec3& scale)
    : Component("Transform"), translation(translation), rotation(rotation), scale(scale) {}

Matrix4 TransformComponent::getModelMatrix() const {
    auto t = translation;
    auto r = rotation;
    auto s = scale;
    return multiply(
        engine3d::translation(t.x, t.y, t.z),
        multiply(
            rotationX(r.x),
            multiply(
                rotationY(r.y),
                multiply(
                    rotationZ(r.z),
                    engine3d::scale(s.x, s.y, s.z)
                )
            )
        )
    );
}

} // namespace engine3d
