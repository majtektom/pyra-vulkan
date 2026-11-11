#include "camera/fly_camera.h"

#include <SDL3/SDL.h>

#include "camera/camera_component.h"
#include "camera/camera_controller.h"
#include "core/engine.h"
#include "core/input_actions.h"
#include "scene/scene.h"

namespace engine3d::FlyCamera {

void bindDefaultKeys(InputActionMap& map) {
    map.bindKey(kMoveForward, SDL_SCANCODE_W);
    map.bindKey(kMoveBackward, SDL_SCANCODE_S);
    map.bindKey(kMoveLeft, SDL_SCANCODE_A);
    map.bindKey(kMoveRight, SDL_SCANCODE_D);
    map.bindKey(kMoveUp, SDL_SCANCODE_SPACE);
    map.bindKey(kMoveDown, SDL_SCANCODE_LCTRL);
}

void update(Engine& engine, float deltaTime, float moveSpeed) {
    CameraController* controller = engine.getCameraController();
    Scene* scene = engine.getSceneManager()->activeScene();
    CameraComponent* camera = scene ? scene->activeCamera() : nullptr;
    if (!controller || !camera) return;

    // Mysz: ruch w poziomie obraca w lewo/prawo (yaw), w pionie pochyla (pitch).
    const auto& input = engine.input();
    controller->moveSpeed = moveSpeed;
    controller->rotateYaw(-static_cast<float>(input.mouseDeltaX) * controller->mouseSensitivity);
    controller->rotatePitch(-static_cast<float>(input.mouseDeltaY) * controller->mouseSensitivity);

    const float step = moveSpeed * deltaTime;
    if (engine.actionDown(kMoveForward)) controller->moveForward(step);
    if (engine.actionDown(kMoveBackward)) controller->moveForward(-step);
    if (engine.actionDown(kMoveLeft)) controller->moveRight(-step);
    if (engine.actionDown(kMoveRight)) controller->moveRight(step);
    if (engine.actionDown(kMoveUp)) controller->moveUp(step);
    if (engine.actionDown(kMoveDown)) controller->moveUp(-step);

    controller->update(deltaTime, *camera);
}

} // namespace engine3d::FlyCamera
