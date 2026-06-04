#include "camera/camera_controller.h"
#include "camera/camera_component.h"
#include "core/entity.h"
#include "core/types.h"
#include <algorithm>
#include <cmath>

namespace engine3d {

static float degToRad(float d) { return d * PI / 180.0f; }

CameraController::CameraController() = default;

void CameraController::rotatePitch(float degrees) {
    // Pochylenie (góra-dół) ograniczamy do [-89, 89] stopni, aby uniknąć "koziołkowania" kamery (gimbal lock).
    pitchDegrees_ = std::clamp(pitchDegrees_ + degrees, -89.0f, 89.0f);
}

void CameraController::rotateYaw(float degrees) {
    // Obrót w poziomie (lewo-prawo) utrzymujemy w zakresie [-360, 360] stopni.
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
    
    // Obliczamy wektor kierunku forward (wprost od pozycji do celu).
    Vec3 forward = normalize(subtract(target, pos));
    
    // Wyznaczamy kąt Pitch (pochylenia) za pomocą funkcji arcsin (y to wysokość wektora).
    pitchDegrees_ = std::asin(std::clamp(forward.y, -1.0f, 1.0f)) * 180.0f / PI;
    
    // Wyznaczamy kąt Yaw (obrotu wokół osi Y) za pomocą funkcji atan2 z rzutu wektora na płaszczyznę XZ.
    headingDegrees_ = std::atan2(-forward.x, -forward.z) * 180.0f / PI;
}

void CameraController::update(float deltaTime, CameraComponent& cam) {
    // Krok 1: Aplikujemy tarcie (płynne hamowanie prędkości ruchu).
    float f = std::max(0.0f, 1.0f - friction * deltaTime);
    forwardVelocity_ *= f;
    strafeVelocity_ *= f;
    verticalVelocity_ *= f;

    // Krok 2: Tworzymy kwaterniony reprezentujące rotację wokół osi X (Pitch) i osi Y (Yaw).
    const Quaternion qPitch = quaternionFromAxisAngle({1.0f, 0.0f, 0.0f}, degToRad(pitchDegrees_));
    const Quaternion qYaw = quaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, degToRad(headingDegrees_));
    
    // Łączymy rotacje mnożąc kwaterniony i normalizując wynik.
    const Quaternion combined = normalize(multiply(qYaw, qPitch));

    // Krok 3: Wyznaczamy lokalne wektory kierunków kamery (forward i right) w przestrzeni świata,
    // obracając wektory jednostkowe o wyliczony kwaternion rotacji.
    const Vec3 forward = rotateVector(combined, {0.0f, 0.0f, -1.0f}); // W OpenGL/Vulkan kamera domyślnie patrzy w kierunku -Z.
    const Vec3 right = rotateVector(combined, {1.0f, 0.0f, 0.0f});    // Oś X w prawo.

    // Krok 4: Obliczamy wektor ruchu na podstawie naciśniętych klawiszy i kierunków kamery.
    Vec3 movement = add(add(multiply(forward, forwardVelocity_ * moveSpeed * deltaTime),
                            multiply(right, strafeVelocity_ * moveSpeed * deltaTime)),
                       Vec3{0.0f, verticalVelocity_ * moveSpeed * deltaTime, 0.0f});

    // Krok 5: Aktualizujemy pozycję i zapisujemy ją w encji kamery.
    realPosition_ = add(realPosition_, movement);
    position_ = realPosition_;

    cam.getOwner()->transform().position = position_;
    
    // Nowy punkt target (cel patrzenia) to pozycja przesunięta w kierunku forward.
    cam.target = add(position_, forward);
    
    // Nowy wektor góry kamery (up) jest prostopadły do wektora prawego (right) i forward.
    cam.up = normalize(cross(right, forward));
}

} // namespace engine3d
