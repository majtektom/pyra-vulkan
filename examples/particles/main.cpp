#include "particleSystem.h"
#include "engine/Engine.hpp"
#include "engine/CameraController.hpp"
#include "engine/InputManager.hpp"
#include "engine/Math.hpp"

#include <cmath>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

class GameLayer : public engine3d::Layer {
public:
    GameLayer(std::string name, std::shared_ptr<engine3d::CameraController> cameraCtrl,
              engine3d::CParticleSystem* particleSystem)
        : Layer(std::move(name)), cameraCtrl_(std::move(cameraCtrl)),
          particleSystem_(particleSystem) {}

    void update(float deltaTime) override {}

    void setInput(const engine3d::InputState& input) { input_ = input; }

private:
    std::shared_ptr<engine3d::CameraController> cameraCtrl_;
    engine3d::InputState input_;
    engine3d::CParticleSystem* particleSystem_;
};

int main() {
    std::srand(42);

    engine3d::AppConfig config;
    config.windowTitle = "engine3d: particles";
    config.clearColor = {0.05f, 0.05f, 0.1f, 1.0f};

    config.shaderManifest.entries = {
        {
            "default",
            {
                std::string(EXAMPLE_SHADER_DIR) + "/cube.vert.spv",
                std::string(EXAMPLE_SHADER_DIR) + "/cube.frag.spv",
                "vertexMain",
                "fragmentMain",
            },
        },
    };

    config.camera.position = {0.0f, 5.0f, -15.0f};
    config.camera.target = {0.0f, 3.0f, 0.0f};
    config.camera.fieldOfViewDegrees = 60.0f;
    config.camera.nearPlane = 0.5f;
    config.camera.farPlane = 16000.0f;

    config.grid.enabled = true;
    config.grid.size = 200.0f;
    config.grid.spacing = 5.0f;

    int numThreads = std::thread::hardware_concurrency();
    if (numThreads < 1) numThreads = 4;

    engine3d::CParticleSystem particleSys;

    // proba wczytania z pliku .particle
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
    }

    config.fonts = {
        {"ui", "assets/fonts/times.ttf", 48},
    };

    config.textLabels = {
        {"ui", "FPS: --", static_cast<float>(config.width) - 24.0f, 24.0f, 0.7f, {0.9f, 1.0f, 0.8f, 0.95f}, engine3d::TextHorizontalAlign::Right},
        {"ui", "particles: --", 24.0f, 88.0f, 0.6f, {0.8f, 0.8f, 1.0f, 0.9f}},
        {"ui", "ESC = quit  WASD + Mysz", 24.0f, 56.0f, 0.5f, {0.7f, 0.7f, 0.7f, 0.85f}},
    };

    config.updateTextLabels = [&](float elapsedSeconds, uint32_t viewportWidth, uint32_t, std::vector<engine3d::TextLabel>& labels) {
        if (labels.size() < 2) return;
        static float prev = elapsedSeconds;
        const float dt = elapsedSeconds - prev;
        prev = elapsedSeconds;
        if (dt > 0.0f) {
            const int fps = static_cast<int>(std::lround(1.0f / dt));
            labels[0].text = "FPS: " + std::to_string(fps);
            labels[0].x = static_cast<float>(viewportWidth) - 24.0f;
        }
        std::string info;
        for (int ei = 0; ei < particleSys.emitterCount(); ei++) {
            if (ei > 0) info += " | ";
            info += particleSys.infoParticles(ei);
        }
        labels[1].text = info;
    };

    auto cameraCtrl = std::make_shared<engine3d::CameraController>();

    auto gameLayer = std::make_unique<GameLayer>("game", cameraCtrl, &particleSys);
    gameLayer->camera = config.camera;
    gameLayer->grid = config.grid;

    // tworzymy osobny obiekt sceny + material dla kazdego emitera
    for (int ei = 0; ei < particleSys.emitterCount(); ei++) {
        auto* e = particleSys.getEmitter(ei);
        if (!e) continue;

        std::string meshName = "particles_" + std::to_string(ei);
        std::string matName = "particleMat_" + std::to_string(ei);

        std::string texName = "particle_tex_" + std::to_string(ei);
        std::string texPath = e->textureName();
        if (!texPath.empty()) {
            FILE* f = fopen(texPath.c_str(), "rb");
            if (!f) {
                std::cerr << "[particles] Texture not found: " << texPath
                          << " for emitter '" << e->name() << "'\n";
                texPath.clear();
            } else {
                fclose(f);
            }
        }
        if (texPath.empty()) texPath = "assets/textures/particles/particle.dds";

        config.textures.push_back({texName, texPath});

        engine3d::Material mat;
        mat.name = matName;
        mat.textureName = texName;
        mat.blendMode = engine3d::BlendMode::Alpha;
        config.materials.push_back(mat);

        gameLayer->sceneObjects.push_back({
            meshName, "", matName,
            {{0.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}},
            {0.0f, 0.0f, 0.0f},
        });

        int numP = 1;
        if (auto* e = particleSys.getEmitter(ei)) numP = e->numParticles();
        engine3d::MeshData initialMesh;
        initialMesh.vertices.resize(numP * 4);
        initialMesh.indices.resize(numP * 6);
        config.dynamicMeshes.push_back({meshName, initialMesh});
    }

    float elapsed = 0.0f;
    config.onUpdate = [&](float dt, engine3d::AppConfig& cfg) {
        elapsed += dt;

        cameraCtrl->update(dt, cfg.input.keys, cfg.input.mouseDeltaX, cfg.input.mouseDeltaY, cfg.camera);

        {
            auto fwd = engine3d::normalize(engine3d::subtract(cfg.camera.target, cfg.camera.position));
            auto right = engine3d::normalize(engine3d::cross(fwd, cfg.camera.up));
            auto up = engine3d::normalize(engine3d::cross(right, fwd));
            particleSys.setCameraVectors(right, up);
        }
        // aktualizuj emitory rownolegle
        {
            std::vector<std::future<void>> futs;
            int n = particleSys.emitterCount();
            for (int ei = 0; ei < n; ei++) {
                auto* e = particleSys.getEmitter(ei);
                if (!e) continue;
                futs.push_back(std::async(std::launch::async, [e, dt]() {
                    e->update(dt);
                }));
            }
            for (auto& f : futs) f.get();
        }

        // buduj meshe rownolegle (wewnatrz kazdego emitera)
        for (int ei = 0; ei < particleSys.emitterCount(); ei++) {
            auto* e = particleSys.getEmitter(ei);
            if (!e || e->activeParticles() == 0) continue;
            std::string meshName = "particles_" + std::to_string(ei);
            for (auto& dm : cfg.dynamicMeshes) {
                if (dm.name != meshName) continue;
                particleSys.buildMeshDataParallel(dm.mesh, ei, numThreads);
                break;
            }
        }

        (void)elapsed;
    };

    gameLayer->setActive(true);

    engine3d::SceneManager sceneManager;
    sceneManager.addLayer(std::move(gameLayer));
    config.sceneManager = &sceneManager;

    return engine3d::runEngine(std::move(config));
}
