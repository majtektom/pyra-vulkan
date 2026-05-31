#include "core/engine.h"
#include "render/renderer.h"
#include "core/mesh_component.h"
#include "camera/camera_component.h"

using namespace engine3d;

int main() {
    Engine engine;

    if (!engine.initialize("engine3d: rotating triangle", 800, 600)) {
        return 1;
    }

    MeshData triangleMesh;
    triangleMesh.vertices = {
        {{{0.0f, -0.5f, 0.0f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 0.0f, 0.0f}}, {{0.5f, 0.0f}}},
        {{{0.5f, 0.5f, 0.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.0f, 1.0f, 0.0f}}, {{1.0f, 1.0f}}},
        {{{-0.5f, 0.5f, 0.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.0f, 1.0f}}},
    };
    triangleMesh.indices = {0, 1, 2};
    engine.getMeshManager()->loadMesh("triangle", triangleMesh);

    Material mat;
    mat.name = "default";
    engine.getMaterialManager()->add(mat);
    auto matInst = engine.getMaterialSystem()->getOrCreateMaterialInstance(mat);

    auto* entity = engine.createEntity("triangle");
    auto* mc = entity->addComponent<MeshComponent>("triangle");
    mc->materialInstance = matInst;

    auto* camEntity = engine.createEntity("camera");
    auto* camera = camEntity->addComponent<CameraComponent>();
    camEntity->transform().position = {0.0f, 0.0f, 2.0f};
    camera->fieldOfViewDegrees = 45.0f;
    engine.setActiveCamera(camera);

    engine.onUpdate = [](float dt, Engine& e) {
        auto* cam = const_cast<CameraComponent*>(e.getActiveCamera());
        if (cam) {
            cam->getOwner()->transform().position = {0.0f, 0.0f, 2.0f};
            cam->target = {0.0f, 0.0f, 0.0f};
            cam->up = {0.0f, 1.0f, 0.0f};
        }
        if (auto* tri = e.getEntity("triangle")) {
            tri->transform().rotation.z += dt * 0.5f;
        }
    };

    engine.run();
    return 0;
}
