#include "3ds.h"
#include "engine/Engine.hpp"
#include "engine/CameraController.hpp"
#include "engine/InputManager.hpp"
#include "engine/Math.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

// Przykladowa warstwa gry - dziedziczy po engine3d::Layer
// i definiuje wlasna scene, kamere, grid, skybox.
class GameLayer : public engine3d::Layer {
public:
    GameLayer(std::string name, std::shared_ptr<engine3d::CameraController> cameraCtrl)
        : Layer(std::move(name)), cameraCtrl_(std::move(cameraCtrl)) {}

    void update(float deltaTime) override {
        // Kamera jest aktualizowana w onUpdate, nie ma potrzeby robic tego drugi raz.
        // Tutaj mogly byc aktualizacje logiki gry, AI, fizyka itp.
    }

    void setInput(const engine3d::InputState& input) { input_ = input; }

private:
    std::shared_ptr<engine3d::CameraController> cameraCtrl_;
    engine3d::InputState input_;
};

// Sprawdza czy plik tekstury ma kanal alpha (szybki odczyt naglowka).
bool textureHasAlpha(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    std::string ext = path.substr(path.find_last_of('.') + 1);
    for (auto& c : ext) c = char(tolower(c));

    if (ext == "dds") {
        char magic[4] = {}, header[124] = {};
        f.read(magic, 4);
        f.read(header, 124);
        if (std::string(magic, 4) != "DDS ") return false;
        auto read32 = [](const char* h, int off) -> uint32_t {
            return uint32_t(uint8_t(h[off])) | (uint32_t(uint8_t(h[off+1])) << 8)
                 | (uint32_t(uint8_t(h[off+2])) << 16) | (uint32_t(uint8_t(h[off+3])) << 24);
        };
        uint32_t pfFlags = read32(header, 76); // ddspf.dwFlags
        // DDPF_ALPHAPIXELS = 0x1, DDPF_FOURCC = 0x4
        if (pfFlags & 1) return true; // explicit ALPHAPIXELS flag set
        // For compressed formats, also check if FourCC is DXT3/DXT5 (always have alpha)
        if (pfFlags & 4) {
            uint32_t fourCC = read32(header, 80);
            return fourCC == 0x33545844 || fourCC == 0x35545844; // DXT3 or DXT5
        }
        return false;
    }
    if (ext == "tga") {
        char tga[18] = {};
        f.read(tga, 18);
        return (tga[1] == 0 && (tga[2] == 2 || tga[2] == 10)) && tga[16] == 32;
    }
    return false;
}

int main() {
    engine3d::AppConfig config;
    const std::string skyboxDir = "assets/skybox/noc";

    config.windowTitle = "engine3d: flyaround + materials + layers";
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

    // Textury uzywane przez materialy.
    config.textures = {
        {"bmp_metal", "assets/textures/metal.bmp"},
        {"dds_wood", "assets/textures/drewno.dds"},
        {"dds_grass", "assets/textures/trawa.dds"},
        {"dds_brick", "assets/textures/blacha.dds"},
        {"piasek", "assets/textures/piasek.bmp", true},
        {"skybox_fb", skyboxDir + "/box_12.dds"},
        {"skybox_lr", skyboxDir + "/box_34.dds"},
        {"skybox_top", skyboxDir + "/box_5.dds"},
    };

    // Materialy definiuja wyglad obiektow: teksture, kolor, filtrowanie, mieszanie itp.
    config.materials = {
        {"drewno", "dds_wood"},
        {"cegla", "dds_brick"},
        {"metal", "bmp_metal"},
        {"trawa", "dds_grass"},
        {"podloga", "piasek"},
    };

    // Kamera (przykladowa, warstwa moze nadpisac).
    config.camera.position = {-60.0f, 15.0f, -62.0f};
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

#if ENGINE3D_HAS_FREETYPE
    config.fonts = {
        {"ui", "assets/fonts/times.ttf", 36},
    };

    config.textLabels = {
        {"ui", "engine3d: flyaround  |  WASD + Mysz + Spacja/Shift  |  Q = skybox  |  L = layers", 24.0f, 24.0f, 0.8f, {1.0f, 1.0f, 0.85f, 1.0f}},
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

    // Tworzymy warstwe gry z wlasnymi obiektami.
    auto gameLayer = std::make_unique<GameLayer>("game", cameraCtrl);
    gameLayer->camera = config.camera;
    gameLayer->grid = config.grid;
    gameLayer->skybox = config.skybox;
    gameLayer->sceneObjects = {
        // Obiekty uzywaja materialName zamiast bezposrednio tekstury.
        engine3d::SceneObject{"floor", "", "podloga", {{0.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}}, {0.0f, 0.0f, 0.0f}},
        engine3d::SceneObject{"cube", "", "drewno", {{0.0f, 0.0f, 0.0f}, {}, {2.0f, 2.0f, 2.0f}}},
        engine3d::SceneObject{"cube", "", "cegla", {{-5.0f, 0.0f, -5.0f}, {}, {1.5f, 1.5f, 1.5f}}},
        engine3d::SceneObject{"cube", "", "metal", {{5.0f, 0.0f, 5.0f}, {}, {1.5f, 1.5f, 1.5f}}},
        engine3d::SceneObject{"cube", "", "trawa", {{-5.0f, 0.0f, 5.0f}, {}, {1.5f, 1.5f, 1.5f}}},
        engine3d::SceneObject{"cube", "", "drewno", {{5.0f, 0.0f, -5.0f}, {}, {1.5f, 1.5f, 1.5f}}},
    };

    config.meshes = {
        {
            "cube",
            {
                {
                    {{{-0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
                    {{{0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
                    {{{-0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
                    {{{-0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{-0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{-0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
                    {{{-0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
                    {{{-0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
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
                    {{{-6200.0f, -10.0f, -6200.0f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{ 6200.0f, -10.0f, -6200.0f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1280.0f, 0.0f}}},
                    {{{ 6200.0f, -10.0f,  6200.0f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1280.0f, 1280.0f}}},
                    {{{-6200.0f, -10.0f,  6200.0f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1280.0f}}},
                },
                {
                    0, 1, 2, 2, 3, 0,
                },
            },
        },
    };

    // Wczytujemy modele 3DS i dodajemy jako obiekty sceny.
    auto addModel = [&](const std::string& path, const std::string& prefix,
                        const engine3d::Vec3& position, bool disableAlpha = false,
                        const std::string& fallbackTexture = "") {
        auto model = std::make_unique<engine3d::Model3ds>();
        if (!model->load(path)) {
            std::cerr << prefix << " load skipped: " << path << "\n";
            return;
        }
        std::cout << "Loaded " << prefix << ": " << model->objectList.size()
                  << " meshes, " << model->materialList.size() << " materials\n";

        std::vector<std::string> finalMatNames(model->materialList.size());
        for (size_t mi = 0; mi < model->materialList.size(); ++mi) {
            const auto& mat = *model->materialList[mi];
            std::string matName = prefix + "_mat" + std::to_string(mi)
                + (mat.name.empty() ? "" : "_" + mat.name);

            std::string texFile;
            size_t ts = mat.texturePath.find_last_of("/\\");
            if (ts != std::string::npos) texFile = mat.texturePath.substr(ts + 1);
            else texFile = mat.texturePath;

            bool texExists = !mat.texturePath.empty();
            if (texExists) {
                FILE* test = fopen(mat.texturePath.c_str(), "rb");
                if (test) { fclose(test); } else {
                    texExists = false;
                    std::cerr << "  [" << prefix << "] texture '" << texFile
                              << "' NOT FOUND at: " << mat.texturePath << "\n";
                }
            }

            std::string texPathForAlpha = mat.texturePath;
            if (!texExists && !fallbackTexture.empty()) {
                texFile = fallbackTexture;
                std::string fbPath = fallbackTexture;
                FILE* test = fopen(fbPath.c_str(), "rb");
                if (!test && fbPath.find('/') == std::string::npos) {
                    fbPath = "assets/textures/" + fallbackTexture;
                    test = fopen(fbPath.c_str(), "rb");
                }
                if (test) {
                    fclose(test);
                    matName += "_fb";
                    texExists = true;
                    texPathForAlpha = fbPath;
                    config.textures.push_back({matName, fbPath});
                    std::cout << "  [" << prefix << "] using fallback texture: " << fbPath << "\n";
                }
            }

            std::string opacityInfo;
            if (!mat.opacityMapPath.empty()) {
                std::string opFile;
                size_t os = mat.opacityMapPath.find_last_of("/\\");
                if (os != std::string::npos) opFile = mat.opacityMapPath.substr(os + 1);
                else opFile = mat.opacityMapPath;
                opacityInfo = " + opacity '" + opFile + "'";
            }
            std::cout << "  [" << prefix << "] material " << mi << ": '"
                      << mat.name << "' transp=" << mat.transparency
                      << (texExists ? " + texture '" + texFile + "'" : " (no texture)")
                      << opacityInfo << "\n";

            if (texExists && matName.find("_fb") == std::string::npos) {
                config.textures.push_back({matName, mat.texturePath});
            }

            engine3d::Material modelMat;
            modelMat.name = matName;
            modelMat.textureName = texExists ? matName : "";
            modelMat.color = mat.diffuseColor;
            modelMat.color[3] = mat.transparency;
            bool useAlpha = !disableAlpha && (mat.transparency < 0.99f || (texExists && textureHasAlpha(texPathForAlpha)));
            modelMat.blendMode = useAlpha ? engine3d::BlendMode::Alpha : engine3d::BlendMode::Disabled;
            config.materials.push_back(modelMat);

            finalMatNames[mi] = matName;
        }

        for (size_t i = 0; i < model->objectList.size(); ++i) {
            const auto& obj = *model->objectList[i];
            std::string meshName = prefix + "_" + (obj.strName.empty()
                ? std::to_string(i) : obj.strName);

            config.meshes.push_back({meshName, obj.mesh});

            std::string matName;
            if (!finalMatNames.empty()) {
                int mi = obj.materialIndex;
                if (mi < 0 || mi >= static_cast<int>(finalMatNames.size())) {
                    mi = 0;
                }
                matName = finalMatNames[mi];
            }

            std::cout << "  [" << prefix << "] mesh " << i << ": '"
                      << obj.strName << "'"
                      << " (" << obj.mesh.vertices.size() << " verts)"
                      << " -> material '" << (matName.empty() ? "none" : matName) << "'"
                      << "\n";

            gameLayer->sceneObjects.push_back({
                meshName, "", matName,
                {position, {}, {0.3f, 0.3f, 0.3f}},
                {0.0f, 0.0f, 0.0f},
            });
        }
    };

    addModel("assets/models/3ds/bronie/M4.3DS",  "m4",  {-20.0f, 0.0f, 0.0f}, true, "assets/models/3ds/bronie/m4.dds");
    addModel("assets/models/3ds/drzewko5.3DS",  "DRZEWKO",  {20.0f, 0.0f, 10.0f});
    addModel("assets/models/3ds/trawa/grass.3ds",  "2",  {15.0f, 0.0f, 0.0f});
    addModel("assets/models/3ds/D1.3DS",  "D1",  {25.0f, 0.0f, 0.0f});

    gameLayer->setActive(true);

    engine3d::SceneManager sceneManager;
    sceneManager.addLayer(std::move(gameLayer));
    config.sceneManager = &sceneManager;

    // Callback aktualizacyjny
    bool skyboxToggled = false;
    bool layerToggleHeld = false;
    bool layersVisible = true;

    config.onUpdate = [&](float dt, engine3d::AppConfig& cfg) {
        constexpr int KEY_Q = 24;
        constexpr int KEY_L = 46;

        GameLayer* activeLayer = static_cast<GameLayer*>(sceneManager.getLayer("game"));
        if (activeLayer) {
            activeLayer->setInput(cfg.input);
        }

        cameraCtrl->update(dt, cfg.input.keys, cfg.input.mouseDeltaX, cfg.input.mouseDeltaY, cfg.camera);

        if (cfg.input.keys[KEY_Q] && !skyboxToggled) {
            cfg.skybox.enabled = !cfg.skybox.enabled;
            skyboxToggled = true;
        }
        if (!cfg.input.keys[KEY_Q]) {
            skyboxToggled = false;
        }

        if (cfg.input.keys[KEY_L] && !layerToggleHeld) {
            layersVisible = !layersVisible;
            if (layersVisible) {
                sceneManager.activateLayer("game");
            } else {
                sceneManager.deactivateLayer("game");
            }
            layerToggleHeld = true;
        }
        if (!cfg.input.keys[KEY_L]) {
            layerToggleHeld = false;
        }
    };

    return engine3d::runEngine(std::move(config));
}
