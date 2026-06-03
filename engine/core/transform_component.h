#pragma once

#include "core/component.h"
#include "core/math.h"

namespace engine3d {

// Komponent przechowujacy transformacje lokalna obiektu oraz
// wynikowa transformacje swiatowa wyliczana przez system ECS.
struct TransformComponent : public Component {
    TransformComponent() : Component("Transform") {}

    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Matrix4 worldMatrix = Matrix4::identity();

    // Zwraca macierz lokalna (TRS) dla encji.
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

    // Ustawia macierz swiatowa wyliczona przez system transformacji.
    void setWorldMatrix(const Matrix4& matrix) {
        worldMatrix = matrix;
    }

    // Pobiera aktualna macierz swiatowa encji.
    const Matrix4& getWorldMatrix() const {
        return worldMatrix;
    }
};

} // namespace engine3d
