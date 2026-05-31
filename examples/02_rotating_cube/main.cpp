#include "core/engine.h"
#include "render/renderer.h"
#include "core/mesh_component.h"
#include "camera/camera_component.h"

using namespace engine3d;

int main() {
    Engine engine;

    if (!engine.initialize("engine3d: rotating cube", 1024, 768)) {
        return 1;
    }

    MeshData cubeMesh;
    cubeMesh.vertices = {
        {{{-0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
        {{{0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
        {{{0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
        {{{-0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
        {{{0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
        {{{-0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
        {{{-0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
        {{{0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
        {{{-0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
        {{{-0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
        {{{-0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
        {{{-0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
        {{{0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
        {{{0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
        {{{0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
        {{{0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
        {{{-0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
        {{{0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
        {{{0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
        {{{-0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
        {{{-0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
        {{{0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
        {{{0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
        {{{-0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
    };
    cubeMesh.indices = {
        0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4,
        8, 9, 10, 10, 11, 8, 12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16, 20, 21, 22, 22, 23, 20,
    };
    engine.getMeshManager()->loadMesh("cube", cubeMesh);

    auto createMaterial = [&](const std::string& name, const std::string& texName, const std::string& texPath) {
        engine.getRenderer()->loadTexture(texName, texPath);
        Material mat;
        mat.name = name;
        mat.textureName = texName;
        engine.getMaterialManager()->add(mat);
        return engine.getMaterialSystem()->getOrCreateMaterialInstance(mat);
    };

    auto mat1 = createMaterial("metal", "metal_tex", "assets/textures/metal.bmp");
    auto mat2 = createMaterial("cactus", "cactus_tex", "assets/sprite/cactus.tga");
    auto mat3 = createMaterial("wood", "wood_tex", "assets/textures/drewno.dds");

    auto makeCube = [&](const std::string& name, const Vec3& pos,
                        const Vec3& scale, std::shared_ptr<MaterialInstance> mat) {
        auto* e = engine.createEntity(name);
        auto* mc = e->addComponent<MeshComponent>("cube");
        mc->materialInstance = mat;
        e->transform().position = pos;
        e->transform().scale = scale;
    };

    makeCube("cube1", {-2.2f, 0.0f, 0.0f}, {0.8f, 0.8f, 0.8f}, mat1);
    makeCube("cube2", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, mat2);
    makeCube("cube3", {2.2f, 0.0f, 0.0f}, {0.8f, 0.8f, 0.8f}, mat3);

    auto* camEntity = engine.createEntity("camera");
    auto* camera = camEntity->addComponent<CameraComponent>();
    camEntity->transform().position = {0.0f, 0.0f, 5.5f};
    camera->fieldOfViewDegrees = 45.0f;
    engine.setActiveCamera(camera);

    engine.onUpdate = [](float dt, Engine& e) {
        auto* cam = const_cast<CameraComponent*>(e.getActiveCamera());
        if (cam) {
            cam->getOwner()->transform().position = {0.0f, 0.0f, 5.5f};
            cam->target = {0.0f, 0.0f, 0.0f};
            cam->up = {0.0f, 1.0f, 0.0f};
        }
        auto rotate = [&](const std::string& name, float rx, float ry, float rz) {
            auto* entity = e.getEntity(name);
            if (entity) {
                entity->transform().rotation.x += dt * rx;
                entity->transform().rotation.y += dt * ry;
                entity->transform().rotation.z += dt * rz;
            }
        };
        rotate("cube1", 0.65f, 1.0f, 0.0f);
        rotate("cube2", 0.0f, -1.2f, 0.4f);
        rotate("cube3", 0.3f, 0.9f, -0.5f);
    };

    engine.run();
    return 0;
}
