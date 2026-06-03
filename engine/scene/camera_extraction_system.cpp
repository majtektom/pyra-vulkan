#include "scene/camera_extraction_system.h"

#include "camera/camera_component.h"
#include "core/entity.h"

namespace engine3d {

void CameraExtractionSystem::update(Scene& scene, float /*deltaTime*/) {
    const CameraComponent* camera = scene.activeCamera();
    if (!camera || !camera->isActive() || !camera->getOwner() || !camera->getOwner()->isActive()) {
        scene.clearFrameView();
        return;
    }

    RenderView view{};
    view.view = camera->getViewMatrix();
    view.projection = camera->getProjectionMatrix();
    view.cameraPosition = camera->getOwner()->transform().position;

    // Wyliczamy bazę kamery dla feature'ow renderingu zaleznych od orientacji.
    const Vec3 camPos = camera->getOwner()->transform().position;
    const Vec3 forward = normalize(subtract(camera->target, camPos));
    view.cameraRight = normalize(cross(forward, camera->up));
    view.cameraUp = normalize(cross(view.cameraRight, forward));

    scene.setFrameView(view);
}

} // namespace engine3d
