#include "engine.h"
#include "renderer.h"
#include "types.h"
#include "math.h"
#include "mesh_component.h"
#include "camera_component.h"
#include "particle_system.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace engine3d;

int main() {
    std::srand(42);
    Engine engine;

    if (!engine.initialize("engine3d: particles", 1024, 768)) {
        return 1;
    }

    auto* renderer = engine.getRenderer();
    renderer->setClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    engine.setGridEnabled(true);
    engine.loadFont("times", "assets/fonts/times.ttf", 48);

    MeshData floor;
    floor.vertices = {
        {{{-10,-10,-10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{0,0}}},
        {{{ 10,-10,-10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{10,0}}},
        {{{ 10,-10, 10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{10,10}}},
        {{{-10,-10, 10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{0,10}}},
    };
    floor.indices = {0,2,1,0,3,2};
    engine.getMeshManager()->loadMesh("floor", floor);
    Material mat; mat.name = "floor"; engine.getMaterialManager()->add(mat);
    auto* entity = engine.createEntity("floor");
    entity->addComponent<MeshComponent>("floor")->materialInstance =
        engine.getMaterialSystem()->getOrCreateMaterialInstance(mat);

    CParticleSystem particleSys;
    if (!particleSys.load("assets/textures/particles/przyklad.particle")) {
        std::cout << "No .particle file found, using hardcoded setup\n";
        auto* emitter = particleSys.addEmitter("fountain");
        emitter->setNumParticles(200);
        emitter->setEmitRate(30);
        emitter->setGravity({0.0f, -2.0f, 0.0f});
        emitter->setPosition({-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, 0.5f});
        emitter->setVelocity({-2.0f, 4.0f, -2.0f}, {2.0f, 8.0f, 2.0f});
        emitter->setLifeTime(1.5f, 3.0f);
        emitter->setSize(0.2f, 0.5f);
        emitter->setMass(0.5f, 1.0f);
        emitter->setColor({1.0f, 0.4f, 0.1f, 1.0f}, {1.0f, 0.8f, 0.2f, 1.0f});
        emitter->createParticles();
    } else {
        std::cout << "Loaded .particle file with " << particleSys.emitterCount() << " emitters\n";
        for (int ei = 0; ei < particleSys.emitterCount(); ei++) {
            particleSys.getEmitter(ei)->createParticles();
        }
    }

    particleSys.initialize(engine);

    auto* camera = engine.createEntity("main_camera")->addComponent<CameraComponent>();
    camera->position = {0.0f, 5.0f, -15.0f};
    camera->target = {0.0f, 0.5f, 0.0f};
    camera->fieldOfViewDegrees = 60.0f;
    camera->nearPlane = 0.5f;
    camera->farPlane = 16000.0f;
    engine.setActiveCamera(camera);

    engine.getCameraController()->setPositionAndTarget(camera->position, camera->target);

    engine.onUpdate = [&particleSys](float dt, Engine& e) {
        auto* cc = e.getCameraController();
        auto* cam = const_cast<CameraComponent*>(e.getActiveCamera());
        if (cc && cam) {
            cc->moveSpeed = 10.0f;
            auto snap = e.getPlatform()->pollInput();
            float ts = cc->mouseSensitivity;
            cc->rotateYaw(-static_cast<float>(snap.mouseDeltaX) * ts);
            cc->rotatePitch(-static_cast<float>(snap.mouseDeltaY) * ts);
            float vel = cc->moveSpeed * dt;
            if (snap.keys[SDL_SCANCODE_W]) cc->moveForward(vel);
            if (snap.keys[SDL_SCANCODE_S]) cc->moveForward(-vel);
            if (snap.keys[SDL_SCANCODE_A]) cc->moveRight(-vel);
            if (snap.keys[SDL_SCANCODE_D]) cc->moveRight(vel);
            cc->update(dt, *cam);
        }

        particleSys.update(dt, e);

        std::ostringstream oss;
        oss << "FPS: " << static_cast<int>(e.getCurrentFPS());
        e.setTextLabel(oss.str(), "times",
                                       e.getRenderer()->width() - 200.0f, 30.0f, 1.0f,
                                       {1.0f, 1.0f, 0.2f, 1.0f});

        for (int ei = 0; ei < particleSys.emitterCount(); ei++) {
            std::string info = particleSys.infoParticles(ei);
            if (!info.empty()) {
                e.setTextLabel(info, "times", 10.0f, 95.0f + ei * 25.0f, 0.7f,
                               {0.2f, 1.0f, 0.2f, 1.0f});
            }
        }

        if (cam) {
            std::ostringstream pos;
            pos << "pos: " << std::fixed << std::setprecision(0)
                << cam->position.x << " " << cam->position.y << " " << cam->position.z;
            e.setTextLabel(pos.str(), "times", 10.0f, 30.0f, 0.7f,
                                          {0.7f, 0.7f, 1.0f, 1.0f});

            Vec3 dir = normalize(subtract(cam->target, cam->position));
            std::ostringstream look;
            look << "dir: " << std::fixed << std::setprecision(2)
                 << dir.x << " " << dir.y << " " << dir.z;
            e.setTextLabel(look.str(), "times", 10.0f, 55.0f, 0.7f,
                                          {0.7f, 0.7f, 1.0f, 1.0f});
        }
    };

    engine.run();
    return 0;
}
