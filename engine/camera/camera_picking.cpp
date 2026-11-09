#include "camera/camera_picking.h"

#include <cmath>

#include "camera/camera_component.h"
#include "core/entity.h"

namespace engine3d {

namespace {

struct CameraBasis {
    Vec3 eye;
    Vec3 forward;
    Vec3 right;
    Vec3 up;
};

CameraBasis cameraBasis(const CameraComponent& camera) {
    CameraBasis b{};
    b.eye = camera.getOwner()->transform().position;
    b.forward = normalize(subtract(camera.target, b.eye));
    b.right = normalize(cross(b.forward, camera.up));
    b.up = cross(b.right, b.forward);
    return b;
}

} // namespace

Ray screenPointToRay(const CameraComponent& camera, float pixelX, float pixelY,
                     float screenWidth, float screenHeight) {
    const CameraBasis b = cameraBasis(camera);
    const float w = screenWidth > 0.0f ? screenWidth : 1.0f;
    const float h = screenHeight > 0.0f ? screenHeight : 1.0f;

    // Piksel -> NDC: X w prawo, Y w górę (ekran ma Y w dół).
    const float ndcX = (pixelX / w) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (pixelY / h) * 2.0f;

    const float tanHalfFov = std::tan(radians(camera.fieldOfViewDegrees) * 0.5f);
    const float aspect = camera.aspectRatio > 0.0001f ? camera.aspectRatio : 4.0f / 3.0f;

    Vec3 dir = b.forward;
    dir = add(dir, multiply(b.right, ndcX * tanHalfFov * aspect));
    dir = add(dir, multiply(b.up, ndcY * tanHalfFov));

    Ray ray;
    ray.origin = b.eye;
    ray.direction = normalize(dir);
    return ray;
}

bool worldToScreen(const CameraComponent& camera, const Vec3& worldPoint,
                   float screenWidth, float screenHeight, float& outX, float& outY) {
    const CameraBasis b = cameraBasis(camera);
    const Vec3 rel = subtract(worldPoint, b.eye);
    const float depth = dot(rel, b.forward);
    if (depth <= 0.001f) {
        return false;
    }
    const float tanHalfFov = std::tan(radians(camera.fieldOfViewDegrees) * 0.5f);
    const float aspect = camera.aspectRatio > 0.0001f ? camera.aspectRatio : 4.0f / 3.0f;

    const float ndcX = dot(rel, b.right) / (depth * tanHalfFov * aspect);
    const float ndcY = dot(rel, b.up) / (depth * tanHalfFov);

    outX = (ndcX * 0.5f + 0.5f) * screenWidth;
    outY = (0.5f - ndcY * 0.5f) * screenHeight;
    return true;
}

bool rayPlaneY(const Ray& ray, float planeY, Vec3& outHit) {
    if (std::abs(ray.direction.y) < 1e-6f) {
        return false;
    }
    const float t = (planeY - ray.origin.y) / ray.direction.y;
    if (t < 0.0f) {
        return false;
    }
    outHit = ray.at(t);
    return true;
}

bool raySphere(const Ray& ray, const Vec3& center, float radius, float& outT) {
    const Vec3 oc = subtract(ray.origin, center);
    const float b = dot(oc, ray.direction);
    const float c = dot(oc, oc) - radius * radius;
    const float disc = b * b - c;
    if (disc < 0.0f) {
        return false;
    }
    const float sq = std::sqrt(disc);
    float t = -b - sq;
    if (t < 0.0f) {
        t = -b + sq;
    }
    if (t < 0.0f) {
        return false;
    }
    outT = t;
    return true;
}

} // namespace engine3d
