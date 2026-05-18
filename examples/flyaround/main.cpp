#include "engine/Engine.hpp"
#include "engine/CameraController.hpp"
#include "engine/InputManager.hpp"
#include "engine/Math.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <utility>

int main() {
    engine3d::AppConfig config;
    const std::string skyboxDir = "assets/skybox/noc";

    config.windowTitle = "engine3d: flyaround";
    config.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

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

    config.camera.position = {0.0f, 15.0f, 12.0f};
    config.camera.target = {0.0f, 0.0f, 0.0f};
    config.camera.fieldOfViewDegrees = 50.0f;
    config.camera.nearPlane = 1.5f;
    config.camera.farPlane = 16000.0f;

    config.grid.enabled = true;
    config.grid.size = 200.0f;
    config.grid.spacing = 5.0f;

    config.skybox.enabled = true;
    config.skybox.directory = skyboxDir;
    config.skybox.size = 3500.0f;
    config.skybox.verticalOffset = 100.0f;

    config.textures = {
        {"bmp_metal", "assets/textures/metal.bmp"},
        {"dds_wood", "assets/textures/drewno.dds"},
        {"dds_grass", "assets/textures/trawa.dds"},
        {"dds_brick", "assets/textures/blacha.dds"},
        // {"piasek", "assets/textures/trawa.dds", true},
        {"piasek", "assets/textures/piasek.bmp", true},
        {"skybox_fb", skyboxDir + "/box_12.dds"},
        {"skybox_lr", skyboxDir + "/box_34.dds"},
        {"skybox_top", skyboxDir + "/box_5.dds"},
    };

#if ENGINE3D_HAS_FREETYPE
    config.fonts = {
        {"ui", "assets/fonts/times.ttf", 36},
    };

    config.textLabels = {
        {"ui", "engine3d: flyaround  |  WASD + Mysz + Spacja/Shift  |  Q = skybox", 24.0f, 24.0f, 0.8f, {1.0f, 1.0f, 0.85f, 1.0f}},
        {"ui", "FPS: --", static_cast<float>(config.width) - 24.0f, 24.0f, 0.8f, {0.9f, 1.0f, 0.8f, 0.95f}, engine3d::TextHorizontalAlign::Right},
        {"ui", "ESC = quit", 24.0f, 56.0f, 0.5f, {0.7f, 0.7f, 0.7f, 0.85f}},
    };

    config.updateTextLabels = [](float elapsedSeconds, uint32_t viewportWidth, uint32_t, std::vector<engine3d::TextLabel>& labels) {
        if (labels.size() < 2) return;
        static float prev = elapsedSeconds;
        const float dt = elapsedSeconds - prev;
        prev = elapsedSeconds;
        if (dt > 0.0f) {
            const int fps = static_cast<int>(std::lround(1.0f / dt));
            labels[1].text = "FPS: " + std::to_string(fps);
        }
        labels[1].x = static_cast<float>(viewportWidth) - 24.0f;
    };
#endif

    auto cameraCtrl = std::make_shared<engine3d::CameraController>();
    bool skyboxToggled = false;

    config.onUpdate = [cameraCtrl, &skyboxToggled](float dt, engine3d::AppConfig& cfg) {
        constexpr int KEY_Q = 24;

        cameraCtrl->update(dt, cfg.input.keys, cfg.input.mouseDeltaX, cfg.input.mouseDeltaY, cfg.camera);

        if (cfg.input.keys[KEY_Q] && !skyboxToggled) {
            cfg.skybox.enabled = !cfg.skybox.enabled;
            skyboxToggled = true;
        }
        if (!cfg.input.keys[KEY_Q]) {
            skyboxToggled = false;
        }
    };

    config.meshes = {
        {
            "cube",
            {
                {
                    {{{-0.75f, -0.75f, 0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, -0.75f, 0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, 0.75f, 0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, 0.75f, 0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
                    {{{0.75f, -0.75f, 0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, -0.75f, -0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, 0.75f, -0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{0.75f, 0.75f, 0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
                    {{{-0.75f, -0.75f, -0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, -0.75f, -0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, 0.75f, -0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, 0.75f, -0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
                    {{{-0.75f, -0.75f, 0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{-0.75f, -0.75f, -0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{-0.75f, 0.75f, -0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, 0.75f, 0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
                    {{{-0.75f, 0.75f, 0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, 0.75f, 0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, 0.75f, -0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, 0.75f, -0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
                    {{{-0.75f, -0.75f, 0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, -0.75f, 0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, -0.75f, -0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, -0.75f, -0.75f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
                },
                {
                    0, 1, 2, 2, 3, 0,
                    4, 5, 6, 6, 7, 4,
                    8, 9, 10, 10, 11, 8,
                    12, 13, 14, 14, 15, 12,
                    16, 17, 18, 18, 19, 16,
                    20, 21, 22, 22, 23, 20,
                },
            },
        },
        {
            "floor",
            {
                {
                    {{{-6200.0f, -10.0f, -6200.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{ 6200.0f, -10.0f, -6200.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1280.0f, 0.0f}}},
                    {{{ 6200.0f, -10.0f,  6200.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1280.0f, 1280.0f}}},
                    {{{-6200.0f, -10.0f,  6200.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1280.0f}}},
                },
                {
                    0, 1, 2, 2, 3, 0,
                },
            },
        },
    };

    config.sceneObjects = {
        engine3d::SceneObject{"floor", "piasek", {{0.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}}, {0.0f, 0.0f, 0.0f}},
        engine3d::SceneObject{"cube", "dds_wood", {{0.0f, 0.0f, 0.0f}, {}, {2.0f, 2.0f, 2.0f}}},
        engine3d::SceneObject{"cube", "dds_brick", {{-5.0f, 0.0f, -5.0f}, {}, {1.5f, 1.5f, 1.5f}}},
        engine3d::SceneObject{"cube", "bmp_metal", {{5.0f, 0.0f, 5.0f}, {}, {1.5f, 1.5f, 1.5f}}},
        engine3d::SceneObject{"cube", "dds_grass", {{-5.0f, 0.0f, 5.0f}, {}, {1.5f, 1.5f, 1.5f}}},
        engine3d::SceneObject{"cube", "dds_wood", {{5.0f, 0.0f, -5.0f}, {}, {1.5f, 1.5f, 1.5f}}},
    };

    return engine3d::runEngine(std::move(config));
}
