#include "engine.h"
#include "renderer.h"
#include "types.h"
#include "mesh_component.h"
#include "transform_component.h"
#include "camera_component.h"
#include "terrain.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace engine3d;

int main() {
    Engine engine;
    if (!engine.initialize("engine3d: terrain LOD", 1280, 800)) return 1;

    auto* renderer = engine.getRenderer();
    renderer->setClearColor(0.4f, 0.6f, 0.9f, 1.0f);
    engine.loadFont("times", "assets/fonts/times.ttf", 28);
    auto* mats = engine.getMaterialManager();

    Terrain terrain;
    bool useJson = terrain.loadFromJson("assets/teren/demo/terrain.json");
    if (useJson) {
        terrain.buildMesh(10.0f, 2.0f, 20.0f);
        terrain.uploadTo(*engine.getMeshManager());
        std::cout << "Terrain (JSON): " << terrain.sizeX << "x" << terrain.sizeZ
              << " layers=" << terrain.layers.size() << " mesh=" << terrain.meshName() << "\n";
    } else {
        terrain.generatePlasma(256, 1.2f);
        terrain.smoothEdges();
        terrain.buildMesh(10.0f, 40.0f, 20.0f);
        terrain.uploadTo(*engine.getMeshManager());
        std::cout << "Terrain (plasma): " << terrain.sizeX << "x" << terrain.sizeZ
                  << " layers=" << terrain.layers.size() << " mesh=" << terrain.meshName() << "\n";
    }

    if (terrain.layers.empty()) {
        renderer->loadTexture("terrain_tex", "assets/textures/trawa.dds");
        Material mat;
        mat.name = "terrain_mat";
        mat.textureName = "terrain_tex";
        mats->add(mat);
        auto* ent = engine.createEntity("terrain");
        auto* mc = ent->addComponent<MeshComponent>(terrain.meshName());
        mc->materialName = "terrain_mat";
    }

    terrain.setupLayers(*renderer, engine, *mats);

    auto* camera = engine.createEntity("main_camera")->addComponent<CameraComponent>();
    camera->position = {0.0f, 120.0f, 0.0f};
    camera->target = {1.0f, 0.0f, 0.0f};
    camera->fieldOfViewDegrees = 60.0f;
    camera->nearPlane = 0.5f;
    camera->farPlane = 8000.0f;
    engine.setActiveCamera(camera);
    engine.getCameraController()->setPositionAndTarget(camera->position, camera->target);

    bool cursorGrabbed = true;
    int generatorMode = 0;
    static const char* modeNames[] = {"Plasma", "Fault", "Deposition"};

    engine.onUpdate = [&](float dt, Engine& e) {
        auto* cc = e.getCameraController();
        auto* cam = const_cast<CameraComponent*>(e.getActiveCamera());
        if (cc && cam) {
            cc->moveSpeed = 50.0f;
            auto snap = e.getPlatform()->pollInput();
            float ts = cc->mouseSensitivity;
            cc->rotateYaw(-static_cast<float>(snap.mouseDeltaX) * ts);
            cc->rotatePitch(-static_cast<float>(snap.mouseDeltaY) * ts);
            float vel = cc->moveSpeed * dt;
            if (snap.keys[SDL_SCANCODE_W]) cc->moveForward(vel);
            if (snap.keys[SDL_SCANCODE_S]) cc->moveForward(-vel);
            if (snap.keys[SDL_SCANCODE_A]) cc->moveRight(-vel);
            if (snap.keys[SDL_SCANCODE_D]) cc->moveRight(vel);
            if (snap.keys[SDL_SCANCODE_SPACE]) cc->moveUp(vel);
            if (snap.keys[SDL_SCANCODE_LCTRL]) cc->moveUp(-vel);
            cc->update(dt, *cam);
            if (snap.keys[SDL_SCANCODE_G])
                cam->position.y = terrain.getHeight(cam->position.x, cam->position.z) + 3.0f;
        }

        terrain.update(cam, *engine.getMeshManager(), terrain.meshName());

        static bool prev1 = false, prev2 = false, prev3 = false;
        auto snap = e.getPlatform()->pollInput();
        bool regen = false;
        if (snap.keys[SDL_SCANCODE_1] && !prev1) { generatorMode = 0; regen = true; }
        if (snap.keys[SDL_SCANCODE_2] && !prev2) { generatorMode = 1; regen = true; }
        if (snap.keys[SDL_SCANCODE_3] && !prev3) { generatorMode = 2; regen = true; }
        prev1 = snap.keys[SDL_SCANCODE_1]; prev2 = snap.keys[SDL_SCANCODE_2]; prev3 = snap.keys[SDL_SCANCODE_3];
        if (regen) {
            if (generatorMode == 0) terrain.generatePlasma(256, 1.2f);
            else if (generatorMode == 1) terrain.generateFault(256, 160, 100, 1, 5, 0.5f);
            else terrain.generateDeposition(256, 100, 30, 10, 20, 0.8f);
            terrain.smoothEdges();
            terrain.buildMesh(10.0f, 40.0f, 20.0f);
            engine.getMeshManager()->updateMesh(terrain.meshName(), terrain.mesh);
            std::cout << "Regenerated: " << modeNames[generatorMode] << "\n";
        }

        static bool prevF1 = false;
        if (snap.keys[SDL_SCANCODE_F1] && !prevF1) {
            cursorGrabbed = !cursorGrabbed;
            e.getPlatform()->grabPointer(cursorGrabbed);
        }
        prevF1 = snap.keys[SDL_SCANCODE_F1];

        std::ostringstream oss;
        oss << "FPS: " << static_cast<int>(e.getCurrentFPS());
        e.setTextLabel(oss.str(), "times", 10, 20, 0.7f, {1, 1, 0.3f, 1});
        std::ostringstream mode;
        mode << "[" << modeNames[generatorMode] << "]  nodes:" << terrain.visibleCount
             << "  1=Plasma 2=Fault 3=Deposition";
        e.setTextLabel(mode.str(), "times", 10, 50, 0.5f, {0.7f, 0.7f, 1.0f, 1.0f});
        if (cam) {
            std::ostringstream pos;
            pos << "pos: " << std::fixed << std::setprecision(0)
                << cam->position.x << " " << cam->position.y << " " << cam->position.z
                << "  h: " << std::setprecision(1) << terrain.getHeight(cam->position.x, cam->position.z);
            e.setTextLabel(pos.str(), "times", 10, 75, 0.6f, {0.7f, 0.7f, 1.0f, 1.0f});
        }
    };

    engine.run();
    return 0;
}
