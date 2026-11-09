#include "core/entity.h"
#include "camera/camera_component.h"

namespace engine3d {

CameraComponent::CameraComponent() : Component("Camera") {}

Matrix4 CameraComponent::getViewMatrix() const {
    // LookAt tworzy macierz widoku na podstawie pozycji kamery (pobranej z TransformComponent encji),
    // celu patrzenia (target) oraz wektora pionu (up).
    return lookAt(owner->transform().position, target, up);
}

Matrix4 CameraComponent::getProjectionMatrix() const {
    // Proporcje ekranu (aspect ratio) - aktualizowane przez Engine na podstawie
    // wymiarów okna, gdy autoAspect == true.
    const float aspect = aspectRatio > 0.0001f ? aspectRatio : 4.0f / 3.0f;
    
    // Budujemy macierz rzutowania perspektywicznego, konwertując FOV ze stopni na radiany.
    return perspective(radians(fieldOfViewDegrees), aspect, nearPlane, farPlane);
}

} // namespace engine3d
