#include "engine.h"
#include "renderer.h"
#include "types.h"
#include "math.h"
#include "mesh_component.h"
#include "transform_component.h"
#include "camera_component.h"
#include "particle_system.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace engine3d;

int main() {
    std::srand(42);
    Engine engine;

    if (!engine.initialize("engine3d: particles", 1024, 768)) {
        return 1;
    }

    auto* renderer = engine.getRenderer();
    renderer->setClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    renderer->setGridEnabled(true);
    renderer->loadFont("times", "assets/fonts/times.ttf", 48);

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

    std::vector<std::string> meshNames;
    int numThreads = std::max(1u, std::thread::hardware_concurrency());

    for (int ei = 0; ei < particleSys.emitterCount(); ei++) {
        auto* e = particleSys.getEmitter(ei);
        if (!e) continue;

        int numP = e->numParticles();
        if (numP <= 0) continue;

        std::string meshName = "particles_" + std::to_string(ei);
        meshNames.push_back(meshName);

        std::string texPath = e->textureName();
        if (texPath.empty()) texPath = "assets/textures/particles/particle.dds";
        std::string texName = "particle_tex_" + std::to_string(ei);
        renderer->loadTexture(texName, texPath);

        Material mat;
        mat.name = "particleMat_" + std::to_string(ei);
        mat.textureName = texName;
        mat.blendMode = BlendMode::Alpha;
        engine.getMaterialManager()->add(mat);
        auto matInst = engine.getMaterialSystem()->getOrCreateMaterialInstance(mat);

        MeshData particleMesh;
        particleMesh.vertices.resize(static_cast<std::size_t>(numP) * 4);
        particleMesh.indices.resize(static_cast<std::size_t>(numP) * 6);
        for (int j = 0; j < numP; j++) {
            auto vi = static_cast<std::size_t>(j) * 4;
            auto ii = static_cast<std::size_t>(j) * 6;
            float s = 0.5f;
            particleMesh.vertices[vi]   = {{{-s, -s, 0.0f}}, {{0,0,1}}, {{1,1,1}}, {{0,0}}};
            particleMesh.vertices[vi+1] = {{{ s, -s, 0.0f}}, {{0,0,1}}, {{1,1,1}}, {{1,0}}};
            particleMesh.vertices[vi+2] = {{{ s,  s, 0.0f}}, {{0,0,1}}, {{1,1,1}}, {{1,1}}};
            particleMesh.vertices[vi+3] = {{{-s,  s, 0.0f}}, {{0,0,1}}, {{1,1,1}}, {{0,1}}};
            particleMesh.indices[ii]   = static_cast<uint32_t>(vi);
            particleMesh.indices[ii+1] = static_cast<uint32_t>(vi+1);
            particleMesh.indices[ii+2] = static_cast<uint32_t>(vi+2);
            particleMesh.indices[ii+3] = static_cast<uint32_t>(vi+2);
            particleMesh.indices[ii+4] = static_cast<uint32_t>(vi+3);
            particleMesh.indices[ii+5] = static_cast<uint32_t>(vi);
        }
        engine.getMeshManager()->loadMesh(meshName, particleMesh);

        auto* entity = engine.createEntity("emitter_" + std::to_string(ei));
        auto* mc = entity->addComponent<MeshComponent>(meshName);
        mc->materialInstance = matInst;
    }

    auto* camera = engine.createEntity("main_camera")->addComponent<CameraComponent>();
    camera->position = {0.0f, 5.0f, -15.0f};
    camera->fieldOfViewDegrees = 60.0f;
    camera->nearPlane = 0.5f;
    camera->farPlane = 16000.0f;
    engine.setActiveCamera(camera);

    engine.onUpdate = [&particleSys, &meshNames, numThreads](float dt, Engine& e) {
        auto* cam = const_cast<CameraComponent*>(e.getActiveCamera());
        if (!cam) return;

        Vec3 forward = normalize(subtract(cam->target, cam->position));
        Vec3 camRight = normalize(cross(forward, cam->up));
        Vec3 camUp = normalize(cross(camRight, forward));

        particleSys.setCameraVectors(camRight, camUp);
        particleSys.updateParallel(dt, numThreads);

        for (int ei = 0; ei < particleSys.emitterCount(); ei++) {
            MeshData meshData;
            particleSys.buildMeshDataParallel(meshData, ei, numThreads);
            e.getMeshManager()->updateMesh(meshNames[ei], meshData);
        }

        std::ostringstream oss;
        oss << "FPS: " << static_cast<int>(e.getCurrentFPS());
        oss << " C: " << particleSys.emitterCount();
        e.getRenderer()->setTextLabel(oss.str(), "times",
                                       e.getRenderer()->width() - 430.0f, 90.0f, 1.0f,
                                       {1.0f, 1.0f, 0.2f, 1.0f});
    };

    engine.run();
    return 0;
}
