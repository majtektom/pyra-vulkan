#include "core/logger.h"
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>

#include "camera/camera_component.h"
#include "core/engine.h"
#include "core/entity.h"
#include "core/light_component.h"
#include "core/mesh_component.h"
#include "feature/gui/gui.h"
#include "feature/gui/gui_button.h"
#include "feature/gui/gui_check_box.h"
#include "feature/gui/gui_combo_box.h"
#include "feature/gui/gui_static_text.h"
#include "feature/gui/gui_track_bar.h"
#include "object/gltf/model_gltf.h"
#include "object/model_3ds.h"
#include "object/model_md2.h"
#include "object/ocean_renderer.h"
#include "object/skydome_renderer.h"
#include "object/terrain.h"
#include "render/renderer.h"

using namespace engine3d;

namespace {

// Klawisze i akcje powiązane z wejściem gry
constexpr const char* kActionMenuBack = "menu.back";
constexpr const char* kActionMoveForward = "move.forward";
constexpr const char* kActionMoveBackward = "move.backward";
constexpr const char* kActionMoveLeft = "move.left";
constexpr const char* kActionMoveRight = "move.right";
constexpr const char* kActionMoveUp = "move.up";
constexpr const char* kActionMoveDown = "move.down";
constexpr const char* kActionToggleDebug = "debug.toggle_overlay";

/**
 * @brief Stany maszyny UI / przepływu gry (UI Mode State Machine).
 */
enum class UiMode {
    MenuMain,   // Menu główne przy starcie
    MenuPause,  // Menu pauzy podczas rozgrywki
    Settings,   // Ekran ustawień graficznych
    Game,       // Aktywna rozgrywka (kamera FPP zablokowana)
};

/**
 * @brief Pomocnicze pobieranie nazwy tekstury z zasobów.
 */
std::string textureNameFromHandle(AssetRegistry* assets, TextureHandle textureHandle, const std::string& fallback = "") {
    if (!assets || !textureHandle.isValid()) {
        return fallback;
    }
    const auto name = assets->textureName(textureHandle);
    return name.has_value() ? *name : fallback;
}

/**
 * @brief Wysyła żądanie zamknięcia aplikacji (SDL Quit Event).
 */
void pushQuitEvent() {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}

/**
 * @brief Główna klasa gry (Game Application).
 * 
 * Pokazuje jak w spójny sposób zintegrować wszystkie elementy silnika:
 * system scen, ECS, hierarchię encji (parenting), ładowanie różnych modeli (3DS, MD2, GLTF),
 * dynamiczne ukształtowanie terenu z LOD, ocean, niebo, zaawansowane GUI oraz diagnostykę klatek.
 */
class GameApp {
public:
    /**
     * @brief Inicjalizuje silnik, ustawia oświetlenie globalne, ładuje menu oraz GUI.
     */
    bool initialize() {
        if (!engine_.initialize("engine3d: game", 1440, 900)) {
            return false;
        }

        setupInputProfile();

        auto* renderer = engine_.getRenderer();
        renderer->setClearColor(0.01f, 0.02f, 0.04f, 1.0f);

        engine_.loadFont("times", "assets/fonts/times.ttf", 32);
        engine_.setLightingEnabled(true);
        engine_.setShadowsEnabled(true);
        engine_.configureDirectionalShadow(220.0f);
        engine_.setAmbientStrength(0.28f);
        engine_.setAmbientColor(0.35f, 0.38f, 0.45f);

        // Załadowanie sceny Menu (osobna scena od samej gry)
        if (!setupMenuScene()) {
            return false;
        }
        if (!setupGui()) {
            return false;
        }

        // Podpięcie głównej metody aktualizacji pod pętlę silnika
        engine_.onUpdate = [this](float dt, Engine& e) {
            this->update(dt, e);
        };

        showMainMenu();
        return true;
    }

    /**
     * @brief Uruchamia pętlę gry.
     */
    void run() {
        engine_.run();
    }

private:
    /**
     * @brief Konfiguruje profile sterowania.
     */
    void setupInputProfile() {
        auto& map = engine_.inputActions();
        map.clearAll();

        map.bindKey(kActionMenuBack, SDL_SCANCODE_ESCAPE);
        map.bindKey(kActionMoveForward, SDL_SCANCODE_W);
        map.bindKey(kActionMoveBackward, SDL_SCANCODE_S);
        map.bindKey(kActionMoveLeft, SDL_SCANCODE_A);
        map.bindKey(kActionMoveRight, SDL_SCANCODE_D);
        map.bindKey(kActionMoveUp, SDL_SCANCODE_SPACE);
        map.bindKey(kActionMoveDown, SDL_SCANCODE_LCTRL);
        map.bindKey(kActionToggleDebug, SDL_SCANCODE_F2);
    }

    /**
     * @brief Przygotowuje dedykowaną scenę dla Menu Głównego.
     * 
     * Scena menu posiada własną statyczną kamerę skierowaną w próżnię,
     * co pozwala na odseparowanie zasobów menu od ciężkiej sceny 3D samej rozgrywki.
     */
    bool setupMenuScene() {
        sceneManager_ = engine_.getSceneManager();
        renderer_ = engine_.getRenderer();
        materialManager_ = engine_.getMaterialManager();
        meshManager_ = engine_.getMeshManager();
        assets_ = engine_.getAssetRegistry();

        menuScene_ = sceneManager_->createScene("menu");
        if (!menuScene_) {
            return false;
        }
        engine_.configureScene(*menuScene_);

        menuCamera_ = menuScene_->createEntity("menu_camera")->addComponent<CameraComponent>();
        menuCamera_->getOwner()->transform().position = {0.0f, 8.0f, -18.0f};
        menuCamera_->target = {0.0f, 0.0f, 0.0f};
        menuCamera_->fieldOfViewDegrees = 50.0f;
        menuCamera_->nearPlane = 0.1f;
        menuCamera_->farPlane = 10000.0f;
        menuScene_->setActiveCamera(menuCamera_);

        sceneManager_->setActiveScene("menu");
        return true;
    }

    /**
     * @brief Zbudowanie interfejsu graficznego (Menu, Pauza, Ustawienia).
     */
    bool setupGui() {
        guiMgr_ = std::make_shared<GuiMgr>();
        guiMgr_->setTextDrawFn([this](const std::string& text, float x, float y, float scale, std::array<float, 4> color, float z) {
            engine_.setTextLabel(text, "times", x, y, scale, color, z);
        });
        renderer_->addFeature(guiMgr_);

        // Dodanie okien i widgetów
        menuMainWindow_ = guiMgr_->addWindow("Menu", 520, 180, 360, 300);
        menuPauseWindow_ = guiMgr_->addWindow("Pauza", 520, 180, 360, 300);
        settingsWindow_ = guiMgr_->addWindow("Ustawienia", 480, 120, 440, 570);
        if (!menuMainWindow_ || !menuPauseWindow_ || !settingsWindow_) {
            return false;
        }

        // Widgety Menu Głównego
        auto* mainTitle = new GuiStaticText(menuMainWindow_, "title", 10, 10);
        mainTitle->setText("engine3d game");
        btnNewGame_ = new GuiButton(menuMainWindow_, "new_game", 10, 60, 200, 34);
        btnNewGame_->setText("Nowa gra");
        btnSettingsMain_ = new GuiButton(menuMainWindow_, "settings", 10, 105, 200, 34);
        btnSettingsMain_->setText("Ustawienia");
        btnLoadMain_ = new GuiButton(menuMainWindow_, "load_game", 10, 150, 200, 34);
        btnLoadMain_->setText("Wczytaj gre");
        btnExitMain_ = new GuiButton(menuMainWindow_, "exit", 10, 195, 200, 34);
        btnExitMain_->setText("Wyjscie");

        // Widgety Menu Pauzy
        auto* pauseTitle = new GuiStaticText(menuPauseWindow_, "title", 10, 10);
        pauseTitle->setText("Menu gry");
        btnContinue_ = new GuiButton(menuPauseWindow_, "continue", 10, 60, 200, 34);
        btnContinue_->setText("Kontynuuj");
        btnSettingsPause_ = new GuiButton(menuPauseWindow_, "settings", 10, 105, 200, 34);
        btnSettingsPause_->setText("Ustawienia");
        btnSavePause_ = new GuiButton(menuPauseWindow_, "save_game", 10, 150, 200, 34);
        btnSavePause_->setText("Zapisz gre");
        btnLoadPause_ = new GuiButton(menuPauseWindow_, "load_game", 10, 195, 200, 34);
        btnLoadPause_->setText("Wczytaj gre");
        btnEnd_ = new GuiButton(menuPauseWindow_, "end", 10, 240, 200, 34);
        btnEnd_->setText("Wyjscie");

        // Widgety Ustawień Graficznych
        auto* settingsTitle = new GuiStaticText(settingsWindow_, "title", 10, 10);
        settingsTitle->setText("Ustawienia grafiki");

        auto* graphicsLabel = new GuiStaticText(settingsWindow_, "graphics_label", 10, 50);
        graphicsLabel->setText("Grafika");
        comboGraphics_ = new GuiComboBox(settingsWindow_, "graphics", 10, 74, 260);
        comboGraphics_->addItem("Niska");
        comboGraphics_->addItem("Srednia");
        comboGraphics_->addItem("Wysoka");
        comboGraphics_->setSelected(2);

        auto* refreshLabel = new GuiStaticText(settingsWindow_, "refresh_label", 10, 118);
        refreshLabel->setText("Odswiezanie (limit FPS)");
        comboRefresh_ = new GuiComboBox(settingsWindow_, "refresh", 10, 142, 260);
        comboRefresh_->addItem("Bez limitu");
        comboRefresh_->addItem("30 FPS");
        comboRefresh_->addItem("60 FPS");
        comboRefresh_->addItem("120 FPS");
        comboRefresh_->setSelected(0);

        auto* syncLabel = new GuiStaticText(settingsWindow_, "sync_label", 10, 186);
        syncLabel->setText("Synchronizacja GPU-CPU");
        comboSync_ = new GuiComboBox(settingsWindow_, "sync", 10, 210, 260);
        comboSync_->addItem("Standard");
        comboSync_->addItem("Timeline");
        comboSync_->addItem("Off");
        comboSync_->setSelected(0);

        auto* presentLabel = new GuiStaticText(settingsWindow_, "present_label", 10, 254);
        presentLabel->setText("Synchronizacja pionowa");
        comboPresent_ = new GuiComboBox(settingsWindow_, "present", 10, 278, 260);
        comboPresent_->addItem("FIFO (ON)");
        comboPresent_->addItem("Immediate (OFF)");
        comboPresent_->addItem("Mailbox (OFF)");
        comboPresent_->setSelected(0);

        checkFullscreen_ = new GuiCheckBox(settingsWindow_, "fullscreen", 10, 322);
        checkFullscreen_->setText("Pelny ekran");
        checkFullscreen_->setChecked(engine_.isFullscreen());

        checkWireframe_ = new GuiCheckBox(settingsWindow_, "wireframe", 10, 352);
        checkWireframe_->setText("Wyswietlaj siatke (Wireframe)");
        checkWireframe_->setChecked(engine_.isWireframeEnabled());

        checkFog_ = new GuiCheckBox(settingsWindow_, "fog", 10, 382);
        checkFog_->setText("Wlacz mgle (Fog)");
        checkFog_->setChecked(engine_.isFogEnabled());

        checkLighting_ = new GuiCheckBox(settingsWindow_, "lighting", 10, 412);
        checkLighting_->setText("Wlacz oswietlenie (Lighting)");
        checkLighting_->setChecked(engine_.isLightingEnabled());

        checkShadows_ = new GuiCheckBox(settingsWindow_, "shadows", 10, 442);
        checkShadows_->setText("Wlacz cienie (Shadows)");
        checkShadows_->setChecked(engine_.shadowsEnabled());

        auto* timeSpeedLabel = new GuiStaticText(settingsWindow_, "time_speed_label", 10, 466);
        timeSpeedLabel->setText("Szybkosc uplywu czasu (mnoznik)");
        trackTimeSpeed_ = new GuiTrackBar(settingsWindow_, "time_speed", 10, 490, 260, 1, 500);
        trackTimeSpeed_->setValue(static_cast<int>(timeSpeedMultiplier_));

        btnSettingsBack_ = new GuiButton(settingsWindow_, "back", 10, 524, 180, 30);
        btnSettingsBack_->setText("Wroc");

        wireGuiCallbacks();
        return true;
    }

    /**
     * @brief Podpina wywołania zwrotne (callbacks) pod kliknięcia widgetów.
     */
    void wireGuiCallbacks() {
        btnNewGame_->setOnClick([this]() {
            if (!ensureGameScene()) {
                LOG_ERROR("Nie udalo sie zbudowac sceny gry.");
                return;
            }
            enterGame();
        });
        btnContinue_->setOnClick([this]() {
            if (!gameSceneReady_) {
                return;
            }
            enterGame();
        });
        btnLoadMain_->setOnClick([this]() {
            if (loadGameScene("save/save.json")) {
                statusMessage_ = "Wczytano stan gry!";
                statusMessageTimer_ = 3.0f;
                enterGame();
            } else {
                statusMessage_ = "Nie znaleziono zapisu gry!";
                statusMessageTimer_ = 3.0f;
            }
        });
        btnSavePause_->setOnClick([this]() {
            if (saveGameScene("save/save.json")) {
                statusMessage_ = "Stan gry zostal zapisany!";
                statusMessageTimer_ = 3.0f;
                enterGame();
            } else {
                statusMessage_ = "Nie udalo sie zapisac gry!";
                statusMessageTimer_ = 3.0f;
            }
        });
        btnLoadPause_->setOnClick([this]() {
            if (loadGameScene("save/save.json")) {
                statusMessage_ = "Wczytano stan gry!";
                statusMessageTimer_ = 3.0f;
                enterGame();
            } else {
                statusMessage_ = "Nie znaleziono zapisu gry!";
                statusMessageTimer_ = 3.0f;
            }
        });
        btnSettingsMain_->setOnClick([this]() {
            showSettings();
        });
        btnSettingsPause_->setOnClick([this]() {
            showSettings();
        });
        btnSettingsBack_->setOnClick([this]() {
            backFromSettings();
        });
        trackTimeSpeed_->setOnChange([this](int val) {
            timeSpeedMultiplier_ = static_cast<float>(val) / 100.0f;
        });
        btnExitMain_->setOnClick([]() {
            pushQuitEvent();
        });
        btnEnd_->setOnClick([]() {
            pushQuitEvent();
        });

        comboGraphics_->setOnSelect([this](const std::string& value) {
            applyGraphicsPreset(value);
        });
        comboRefresh_->setOnSelect([this](const std::string& value) {
            if (value == "30 FPS") {
                engine_.setFrameRateLimit(30);
            } else if (value == "60 FPS") {
                engine_.setFrameRateLimit(60);
            } else if (value == "120 FPS") {
                engine_.setFrameRateLimit(120);
            } else {
                engine_.setFrameRateLimit(0);
            }
        });
        comboSync_->setOnSelect([this](const std::string& value) {
            if (value == "Timeline") {
                engine_.setSynchronizationMode(SynchronizationMode::Timeline);
            } else if (value == "Off") {
                engine_.setSynchronizationMode(SynchronizationMode::Off);
            } else {
                engine_.setSynchronizationMode(SynchronizationMode::Standard);
            }
        });
        comboPresent_->setOnSelect([this](const std::string& value) {
            if (value == "Immediate (OFF)") {
                engine_.setPresentMode(PresentMode::Immediate);
            } else if (value == "Mailbox (OFF)") {
                engine_.setPresentMode(PresentMode::Mailbox);
            } else {
                engine_.setPresentMode(PresentMode::Fifo);
            }
        });
        checkFullscreen_->setOnToggle([this](bool on) {
            engine_.setFullscreen(on);
        });
        checkWireframe_->setOnToggle([this](bool on) {
            engine_.setWireframeEnabled(on);
        });
        checkFog_->setOnToggle([this](bool on) {
            engine_.setFogEnabled(on);
        });
        checkLighting_->setOnToggle([this](bool on) {
            engine_.setLightingEnabled(on);
        });
        checkShadows_->setOnToggle([this](bool on) {
            engine_.setShadowsEnabled(on);
        });
    }

    /**
     * @brief Aplikuje gotowe zestawy ustawień graficznych (Presets).
     */
    void applyGraphicsPreset(const std::string& preset) {
        if (preset == "Niska") {
            engine_.setShadowsEnabled(false);
            if (checkShadows_) checkShadows_->setChecked(false);
            engine_.setAmbientStrength(0.55f);
        } else if (preset == "Srednia") {
            engine_.setShadowsEnabled(true);
            if (checkShadows_) checkShadows_->setChecked(true);
            engine_.configureDirectionalShadow(130.0f);
            engine_.setAmbientStrength(0.40f);
        } else {
            engine_.setShadowsEnabled(true);
            if (checkShadows_) checkShadows_->setChecked(true);
            engine_.configureDirectionalShadow(220.0f);
            engine_.setAmbientStrength(0.28f);
        }
    }

    /**
     * @brief Leniwa alokacja i konfiguracja sceny gry (Lazy Initialization).
     * 
     * Scena gry jest tworzona dopiero przy kliknięciu "Nowa gra", dzięki czemu 
     * menu ładuje się natychmiast bez blokowania programu wczytywaniem ciężkich modeli 3D.
     */
    bool ensureGameScene() {
        if (gameSceneReady_) {
            return true;
        }

        gameScene_ = sceneManager_->createScene("game");
        if (!gameScene_) {
            return false;
        }
        engine_.configureScene(*gameScene_);

        setupLighting();
        setupTerrain();
        setupAtmosphere();
        setupCamera();
        load3dsSet();
        loadMd2Character();
        loadGltfSet();
        loadBrainStemSet();

        gameSceneReady_ = true;
        return true;
    }

    void setupLighting() {
        // Światło słońca jest teraz tworzone i aktualizowane dynamicznie przez SkydomeRenderer w pętli gry
    }

    void setupTerrain() {
        gameTerrain_ = std::make_shared<Terrain>();
        bool loaded = gameTerrain_->loadFromJson("assets/teren/demo/terrain.json");
        if (loaded) {
            gameTerrain_->buildMesh();
        } else {
            LOG_WARN("nie wczytano terenu, generuję losowy teren");
            gameTerrain_->generatePlasma(256, 1.2f);
            gameTerrain_->smoothEdges();
            gameTerrain_->buildMesh();
        }

        gameTerrain_->uploadTo(*meshManager_);

        // Konfiguracja splat-map materiału terenu
        Material terrainMaterial;
        terrainMaterial.name = "game_terrain_mat";
        terrainMaterial.shaderProgram = "terrain";
        terrainMaterial.cullMode = CullMode::Back;
        terrainMaterial.depthWrite = true;
        terrainMaterial.depthTest = true;
        if (!gameTerrain_->layers.empty() && !gameTerrain_->layers[0].texturePath.empty()) {
            TextureHandle texture = renderer_->loadTexture("game_terrain_mat_tex", gameTerrain_->layers[0].texturePath);
            terrainMaterial.textureName = textureNameFromHandle(assets_, texture, "");
        }
        const MaterialHandle terrainMaterialHandle = materialManager_->add(terrainMaterial);

        Entity* terrainEntity = gameScene_->createEntity("game_terrain_entity");
        terrainEntity->transform().position = gameTerrain_->worldOffset();
        auto* terrainMesh = terrainEntity->addComponent<MeshComponent>(gameTerrain_->meshName());
        terrainMesh->materialHandle = terrainMaterialHandle;
        terrainMesh->castShadows = true;
        terrainMesh->layerBlendEnabled = true;
        terrainMesh->layerCount = std::min<uint32_t>(
            static_cast<uint32_t>(gameTerrain_->layers.size()), kMaxTerrainLayers);
        terrainMesh->layerWorldSize = gameTerrain_->worldSizeXZ();
        
        for (uint32_t i = 0; i < terrainMesh->layerCount; ++i) {
            const std::string texName = "game_terrain_layer_tex_" + std::to_string(i);
            terrainMesh->layerTextureHandles[i] = renderer_->loadTexture(texName, gameTerrain_->layers[i].texturePath);
            if (i > 0 && !gameTerrain_->layers[i].maskPath.empty()) {
                const std::string maskName = "game_terrain_layer_mask_" + std::to_string(i);
                terrainMesh->layerMaskHandles[i] = renderer_->loadTexture(maskName, gameTerrain_->layers[i].maskPath);
            }
        }
    }

    void setupAtmosphere() {
        skydome_ = std::make_shared<SkydomeRenderer>();
        SkydomeConfig skydomeCfg;
        skydomeCfg.enabled = true;
        skydomeCfg.domeScale = 1.6f;
        skydomeCfg.timeOfDay = 23.0f;
        skydomeCfg.cloudCoverage = 0.35f;
        skydomeCfg.cloudSpeed0 = 0.25f;
        skydomeCfg.cloudSpeed1 = 0.12f;
        skydomeCfg.cloudScale0 = 1200.0f;
        skydomeCfg.cloudScale1 = 820.0f;
        skydomeCfg.sunElevation = 50.0f;
        skydomeCfg.enableSunLight = true;
        skydome_->configure(skydomeCfg, "bin/assets/shaders/skydome", "skydome");
        skydome_->setEnabled(true);
        renderer_->addFeature(skydome_);

        ocean_ = std::make_shared<OceanRenderer>();
        OceanConfig oceanCfg;
        oceanCfg.enabled = true;
        oceanCfg.waterLevel = 20.0f;
        oceanCfg.waveAmplitude = 2.3f;
        oceanCfg.waveSpeed = 0.9f;
        oceanCfg.waveFrequency = 9.0f;
        oceanCfg.foamThreshold = 15.9f;
        ocean_->configure(oceanCfg, "bin/assets/shaders/ocean", "ocean");
        ocean_->setEnabled(true);
        renderer_->addFeature(ocean_);

        // Konfiguracja domyslnej mgly zintegrowanej z horyzontem skydome
        engine_.setFogEnabled(true);
        engine_.setFogColor(0.575f, 0.68f, 0.88f); // Jasny niebieski zblizony do horyzontu skydome
        engine_.setFogStart(150.0f);
        engine_.setFogEnd(3500.0f);
        engine_.setFogDensity(0.83f);
        if (checkFog_) checkFog_->setChecked(true);
    }

    void setupCamera() {
        gameCamera_ = gameScene_->createEntity("game_camera")->addComponent<CameraComponent>();
        const float centerX = 0.0f;
        const float centerZ = 0.0f;

        gameCamera_->getOwner()->transform().position = {centerX - 110.0f, 90.0f, centerZ - 110.0f};
        gameCamera_->target = {centerX, 28.0f, centerZ};
        gameCamera_->fieldOfViewDegrees = 55.0f;
        gameCamera_->nearPlane = 0.5f;
        gameCamera_->farPlane = 12000.0f;
        gameScene_->setActiveCamera(gameCamera_);
    }

    MaterialHandle createBasicMaterial(const std::string& name, const std::string& texturePath, bool alpha = false) {
        Material material;
        material.name = name;
        if (!texturePath.empty()) {
            TextureHandle texture = renderer_->loadTexture(name + "_tex", texturePath);
            material.textureName = textureNameFromHandle(assets_, texture, "");
        }
        material.cullMode = alpha ? CullMode::Disabled : CullMode::Back;
        material.blendMode = alpha ? BlendMode::Alpha : BlendMode::Disabled;
        material.depthWrite = !alpha;
        return materialManager_->add(material);
    }

    /**
     * @brief Przykleja pozycję encji pionowo do wysokości terenu pod nią.
     */
    void snapEntityToTerrain(Entity* entity, float yOffset) {
        if (!entity || !gameTerrain_) {
            return;
        }
        auto& tr = entity->transform();
        tr.position.y = gameTerrain_->getHeight(tr.position.x, tr.position.z) + yOffset;
    }

    /**
     * @brief Rejestruje encję jako "zakorzenioną" w terenie.
     * 
     * Każdy zarejestrowany obiekt będzie automatycznie przyklejany do wysokości
     * terenu przy aktualizacji klatki.
     */
    void registerTerrainRoot(Entity* entity, float yOffset) {
        if (!entity) {
            return;
        }
        snapEntityToTerrain(entity, yOffset);
        terrainRoots_.push_back(entity);
        terrainRootOffsets_.push_back(yOffset);
    }

    /**
     * @brief Przykleja ponownie wszystkie zakorzenione encje (np. przy regeneracji terenu).
     */
    void resnapTerrainRoots() {
        const size_t count = std::min(terrainRoots_.size(), terrainRootOffsets_.size());
        for (size_t i = 0; i < count; ++i) {
            snapEntityToTerrain(terrainRoots_[i], terrainRootOffsets_[i]);
        }
    }

    /**
     * @brief Ładuje obiekty z modelu 3DS (np. drzewka) i rozstawia je jako pod-drzewa (Hierarchia ECS).
     */
    void load3dsSet() {
        Model3ds model;
        if (!model.load("assets/models/3ds/drzewko5.3DS")) {
            LOG_WARN("Pomijam 3DS: assets/models/3ds/drzewko5.3DS");
            return;
        }

        std::vector<MaterialHandle> sourceMaterials;
        sourceMaterials.reserve(model.materialList.size());
        for (size_t i = 0; i < model.materialList.size(); ++i) {
            const auto& src = *model.materialList[i];
            Material material = Convert3dsMaterialToEngineMaterial(src);
            material.name = "forest_3ds_mat_" + std::to_string(i);
            if (!src.texturePath.empty()) {
                TextureHandle texture = renderer_->loadTexture(material.name + "_tex", src.texturePath);
                material.textureName = textureNameFromHandle(assets_, texture, "");
            }
            sourceMaterials.push_back(materialManager_->add(material));
        }

        struct SubMeshRef {
            std::string meshName;
            MaterialHandle material;
        };
        std::vector<SubMeshRef> subMeshes;
        subMeshes.reserve(model.objectList.size());

        for (size_t i = 0; i < model.objectList.size(); ++i) {
            const auto& object = *model.objectList[i];
            const std::string meshName = "forest_3ds_mesh_" + std::to_string(i);
            meshManager_->loadMesh(meshName, object.mesh);

            MaterialHandle material{};
            if (!sourceMaterials.empty()) {
                int materialIndex = object.materialIndex;
                if (materialIndex < 0 || materialIndex >= static_cast<int>(sourceMaterials.size())) {
                    materialIndex = 0;
                }
                material = sourceMaterials[materialIndex];
            }
            subMeshes.push_back({meshName, material});
        }

        const std::vector<Vec3> treePositions = {
            {-220.0f, 0.0f, -180.0f},
            {-120.0f, 0.0f, 40.0f},
            {0.0f, 0.0f, -60.0f},
            {130.0f, 0.0f, 120.0f},
            {230.0f, 0.0f, -140.0f},
            {320.0f, 0.0f, 70.0f},
        };

        // Tworzenie hierarchii: węzeł-rodzic (Root) kontroluje transformację globalną, 
        // a jego dzieci (części pnia i liści) dziedziczą pozycję, rotację i skalę.
        for (size_t treeIndex = 0; treeIndex < treePositions.size(); ++treeIndex) {
            Entity* treeRoot = gameScene_->createEntity("forest_tree_root_" + std::to_string(treeIndex));
            treeRoot->transform().position = treePositions[treeIndex];
            treeRoot->transform().scale = {0.26f, 0.26f, 0.26f};
            registerTerrainRoot(treeRoot, 0.2f);
            treeRoots_.push_back(treeRoot);

            for (size_t i = 0; i < subMeshes.size(); ++i) {
                Entity* part = gameScene_->createEntity("forest_tree_" + std::to_string(treeIndex) + "_part_" + std::to_string(i));
                
                // Ustawienie rodzica (Parent-Child relation)
                if (gameScene_->setParent(part, treeRoot)) {
                    parentLinkSuccessCount_++;
                } else {
                    parentLinkFailCount_++;
                }

                auto* mesh = part->addComponent<MeshComponent>(subMeshes[i].meshName);
                mesh->materialHandle = subMeshes[i].material;
            }
        }
    }

    void loadMd2Character() {
        md2Model_ = std::make_shared<Md2ModelData>(loadMd2Model("assets/models/md2/goblin/goblin.md2"));
        if (!md2Model_ || md2Model_->frames.empty()) {
            LOG_WARN("Pomijam MD2: assets/models/md2/goblin/goblin.md2");
            md2Model_.reset();
            return;
        }

        md2Anim_ = std::make_shared<Md2AnimationState>();
        md2Anim_->startFrame = 0;
        md2Anim_->endFrame = static_cast<int>(md2Model_->frames.size()) - 1;
        md2Anim_->speed = 10.0f;

        meshManager_->loadMesh("goblin_md2", md2Model_->triangles);
        MaterialHandle goblinMat = createBasicMaterial("goblin_md2_mat", "assets/models/md2/goblin/goblin.bmp", false);

        Entity* goblin = gameScene_->createEntity("goblin_md2_entity");
        auto* mesh = goblin->addComponent<MeshComponent>("goblin_md2");
        mesh->materialHandle = goblinMat;

        goblin->transform().position = {-24.0f, 0.0f, -6.0f};
        goblin->transform().rotation = {-90.0f, 0.0f, 0.0f};
        goblin->transform().scale = {0.46f, 0.46f, 0.46f};
        snapEntityToTerrain(goblin, 0.0f);
    }

    /**
     * @brief Ładuje samochodziki z formatu GLTF/GLB i linkuje hierarchicznie części.
     */
    void loadGltfSet() {
        GltfModel model = loadGltfModel("assets/models/gLTF/toycar/bin/ToyCar.glb");
        if (model.primitives.empty()) {
            LOG_WARN("Pomijam glTF: assets/models/gLTF/toycar/bin/ToyCar.glb");
            return;
        }

        struct SubMeshRef {
            std::string meshName;
            MaterialHandle material;
        };
        std::vector<SubMeshRef> subMeshes;
        subMeshes.reserve(model.primitives.size());

        for (size_t i = 0; i < model.primitives.size(); ++i) {
            const auto& primitive = model.primitives[i];
            const std::string meshName = "toycar_mesh_" + std::to_string(i);
            const std::string materialName = "toycar_mat_" + std::to_string(i);

            meshManager_->loadMesh(meshName, primitive.mesh);

            Material material;
            material.name = materialName;
            material.cullMode = CullMode::Disabled;
            if (!primitive.texturePath.empty()) {
                TextureHandle texture = renderer_->loadTexture(materialName + "_tex", primitive.texturePath);
                material.textureName = textureNameFromHandle(assets_, texture, "");
            }
            if (!primitive.normalMapPath.empty()) {
                TextureHandle texture = renderer_->loadTexture(materialName + "_n", primitive.normalMapPath);
                material.normalMapName = textureNameFromHandle(assets_, texture, "");
                material.bumpIntensity = 1.0f;
            }
            if (!primitive.ormPath.empty()) {
                TextureHandle texture = renderer_->loadTexture(materialName + "_orm", primitive.ormPath);
                material.metallicRoughnessTextureName = textureNameFromHandle(assets_, texture, "");
            }
            if (!primitive.occlusionPath.empty()) {
                TextureHandle texture = renderer_->loadTexture(materialName + "_occ", primitive.occlusionPath);
                material.occlusionTextureName = textureNameFromHandle(assets_, texture, "");
            }
            if (!primitive.emissivePath.empty()) {
                TextureHandle texture = renderer_->loadTexture(materialName + "_em", primitive.emissivePath);
                material.emissiveTextureName = textureNameFromHandle(assets_, texture, "");
            }
            material.roughnessFactor = 0.5f;

            MaterialHandle handle = materialManager_->add(material);

            subMeshes.push_back({meshName, handle});
        }

        const std::vector<Vec3> carPositions = {
            {-260.0f, 0.0f, -90.0f},
            {-40.0f, 0.0f, 180.0f},
            {170.0f, 0.0f, -40.0f},
            {320.0f, 0.0f, 210.0f},
        };

        for (size_t car = 0; car < carPositions.size(); ++car) {
            const float yaw = static_cast<float>(car) * 52.0f;

            Entity* carRoot = gameScene_->createEntity("toycar_root_" + std::to_string(car));
            carRoot->transform().position = carPositions[car];
            carRoot->transform().rotation = {0.0f, yaw, 0.0f};
            carRoot->transform().scale = {0.18f, 0.18f, 0.18f};
            registerTerrainRoot(carRoot, 0.25f);
            carRoots_.push_back(carRoot);

            for (size_t i = 0; i < subMeshes.size(); ++i) {
                Entity* part = gameScene_->createEntity("toycar_" + std::to_string(car) + "_part_" + std::to_string(i));
                if (gameScene_->setParent(part, carRoot)) {
                    parentLinkSuccessCount_++;
                } else {
                    parentLinkFailCount_++;
                }

                auto* mesh = part->addComponent<MeshComponent>(subMeshes[i].meshName);
                mesh->materialHandle = subMeshes[i].material;
            }
        }
    }

    /**
     * @brief Ładuje i ustawia animowane szkieletowo modele BrainStem na wysokości terenu.
     */
    void loadBrainStemSet() {
        if (!brainModel_.load(renderer_, meshManager_, materialManager_, assets_, "assets/models/gLTF/BrainStem.glb")) {
            LOG_WARN("Pomijam glTF: assets/models/gLTF/BrainStem.glb");
            return;
        }

        const std::vector<Vec3> brainPositions = {
            {-40.0f, 0.0f, 20.0f},
            {10.0f, 0.0f, 10.0f},
            {-20.0f, 0.0f, 30.0f},
            {-50.0f, 0.0f, -10.0f},
            {30.0f, 0.0f, -30.0f},
        };

        for (size_t i = 0; i < brainPositions.size(); ++i) {
            GltfSkinnedInstance inst;
            if (inst.initialize(renderer_, brainModel_)) {
                Entity* root = inst.instantiate(gameScene_, "brainstem_" + std::to_string(i));
                root->transform().position = brainPositions[i];
                root->transform().rotation = {0.0f, static_cast<float>(i) * 72.0f, 0.0f};
                root->transform().scale = {10.0f, 10.0f, 10.0f};
                
                // Snap to terrain and register to stay updated with terrain changes
                registerTerrainRoot(root, 0.0f);
                
                // Offset animation start time so they do not animate in phase-lock sync
                inst.setAnimationTime(static_cast<float>(i) * 0.7f);

                brainInstances_.push_back(std::move(inst));
            }
        }
    }

    /**
     * @brief Rozpoczyna lub wznawia rozgrywkę (zmiana sceny i ukrycie GUI).
     */
    void enterGame() {
        if (!gameSceneReady_) {
            return;
        }
        uiMode_ = UiMode::Game;
        sceneManager_->setActiveScene("game"); // Włączenie renderingu świata gry

        menuMainWindow_->setVisible(false);
        menuPauseWindow_->setVisible(false);
        settingsWindow_->setVisible(false);

        guiMgr_->enableCursor(false); // Blokowanie i ukrycie kursora (potrzebne do obrotu kamery myszką)
        if (gameCamera_) {
            engine_.getCameraController()->setPositionAndTarget(
                gameCamera_->getOwner()->transform().position,
                gameCamera_->target);
        }
    }

    void showMainMenu() {
        uiMode_ = UiMode::MenuMain;
        sceneManager_->setActiveScene("menu");

        menuMainWindow_->setVisible(true);
        menuPauseWindow_->setVisible(false);
        settingsWindow_->setVisible(false);

        guiMgr_->setActive("Menu");
        guiMgr_->enableCursor(true);
    }

    void showPauseMenu() {
        uiMode_ = UiMode::MenuPause;
        sceneManager_->setActiveScene("menu");

        menuMainWindow_->setVisible(false);
        menuPauseWindow_->setVisible(true);
        settingsWindow_->setVisible(false);

        guiMgr_->setActive("Pauza");
        guiMgr_->enableCursor(true);
    }

    void showSettings() {
        modeBeforeSettings_ = uiMode_;
        uiMode_ = UiMode::Settings;

        menuMainWindow_->setVisible(false);
        menuPauseWindow_->setVisible(false);
        settingsWindow_->setVisible(true);

        guiMgr_->setActive("Ustawienia");
        guiMgr_->enableCursor(true);
    }

    void backFromSettings() {
        if (modeBeforeSettings_ == UiMode::MenuPause) {
            showPauseMenu();
        } else {
            showMainMenu();
        }
    }

    /**
     * @brief Wywoływana co klatkę przez silnik, rozdziela logikę zależnie od stanu UI.
     */
    void update(float dt, Engine& e) {
        const auto& snap = e.input();

        if (statusMessageTimer_ > 0.0f) {
            statusMessageTimer_ -= dt;
        }

        if (e.actionPressed(kActionToggleDebug)) {
            debugOverlayEnabled_ = !debugOverlayEnabled_;
        }

        if (uiMode_ == UiMode::Game) {
            updateGameplay(dt, e, snap);
        } else {
            updateMenuFlow(e);
        }

        updateGuiAndHud(e, snap);
    }

    /**
     * @brief Logika rozgrywki (ruch gracza, animacja wody, chmur i postaci).
     */
    void updateGameplay(float dt, Engine& e, const InputSnapshot& snap) {
        if (!gameCamera_) {
            return;
        }

        auto* controller = e.getCameraController();
        if (controller) {
            controller->moveSpeed = 52.0f;
            const float sensitivity = controller->mouseSensitivity;
            controller->rotateYaw(-static_cast<float>(snap.mouseDeltaX) * sensitivity);
            controller->rotatePitch(-static_cast<float>(snap.mouseDeltaY) * sensitivity);

            const float velocity = controller->moveSpeed * dt;
            if (e.actionDown(kActionMoveForward)) controller->moveForward(velocity);
            if (e.actionDown(kActionMoveBackward)) controller->moveForward(-velocity);
            if (e.actionDown(kActionMoveLeft)) controller->moveRight(-velocity);
            if (e.actionDown(kActionMoveRight)) controller->moveRight(velocity);
            if (e.actionDown(kActionMoveUp)) controller->moveUp(velocity);
            if (e.actionDown(kActionMoveDown)) controller->moveUp(-velocity);
            controller->update(dt, *gameCamera_);
        }

        // Aktualizacja podziału LOD terenu
        if (gameTerrain_) {
            gameTerrain_->update(gameCamera_, *e.getMeshManager(), gameTerrain_->meshName());
        }

        // Animacja morfingu klatkowego postaci MD2
        if (md2Model_ && md2Anim_ && !md2Model_->frames.empty()) {
            MeshData animatedMesh;
            animateMd2(*md2Model_, *md2Anim_, dt, animatedMesh);
            e.getMeshManager()->updateMesh("goblin_md2", animatedMesh);
        }

        // Aktualizacja animacji szkieletowej modeli BrainStem
        for (auto& inst : brainInstances_) {
            inst.update(dt, 0, 1.0f);
        }

        worldTime_ += dt * timeSpeedMultiplier_;
        resnapTerrainRoots(); // Aktualizacja przyklejenia obiektów do terenu

        if (skydome_) {
            skydome_->setTime(worldTime_);
            if (skydome_->isSunLightEnabled()) {
                Vec3 sunDir = skydome_->getSunDirection();
                Vec3 sunCol = skydome_->getSunColor();
                float sunIntensity = skydome_->getSunIntensity();

                auto* sunLight = gameScene_->getEntity("game_sun");
                if (sunLight) {
                    auto* lightComp = sunLight->getComponent<LightComponent>();
                    if (lightComp) {
                        lightComp->direction = sunDir;
                        lightComp->color = sunCol;
                        lightComp->intensity = sunIntensity;
                    }
                } else {
                    gameScene_->createDirectionalLight("game_sun", sunDir, sunCol, sunIntensity);
                }

                // Dynamic ambient light adjusted by skydome
                float ambientStrength = skydome_->getAmbientStrength();
                Vec3 ambientColor = skydome_->getAmbientColor();
                engine_.setAmbientStrength(ambientStrength);
                engine_.setAmbientColor(ambientColor.x, ambientColor.y, ambientColor.z);

                // Dynamic fog color linked to ambient light intensity (almost black at night)
                float t = std::clamp((ambientStrength - 0.065f) / (0.28f - 0.065f), 0.0f, 1.0f);
                float fogR = 0.005f + (0.575f - 0.005f) * t;
                float fogG = 0.006f + (0.68f - 0.006f) * t;
                float fogB = 0.012f + (0.88f - 0.012f) * t;
                engine_.setFogColor(fogR, fogG, fogB);
            }
        }
        if (ocean_) {
            ocean_->setTime(worldTime_);
        }

        // ESC wywołuje pauzę w locie gry
        if (e.actionPressed(kActionMenuBack)) {
            showPauseMenu();
        }
    }

    void updateMenuFlow(Engine& e) {
        if (!e.actionPressed(kActionMenuBack)) {
            return;
        }

        if (uiMode_ == UiMode::Settings) {
            backFromSettings();
            return;
        }

        pushQuitEvent();
    }

    /**
     * @brief Aktualizacja okien GUI oraz rysowanie tekstowego HUD diagnostycznego.
     */
    void updateGuiAndHud(Engine& e, const InputSnapshot& snap) {
        guiMgr_->update(snap, *e.getPlatform(),
                        static_cast<float>(e.getRenderer()->width()),
                        static_cast<float>(e.getRenderer()->height()));
        guiMgr_->renderGui();

        if (statusMessageTimer_ > 0.0f) {
            e.setTextLabel(statusMessage_, "times", 12.0f, static_cast<float>(e.getRenderer()->height() - 40), 0.6f, {0.2f, 1.0f, 0.2f, 1.0f});
        }

        std::ostringstream fps;
        fps << "FPS: " << static_cast<int>(e.getCurrentFPS());
        e.setTextLabel(fps.str(), "times", 12.0f, 20.0f, 0.6f, {1.0f, 0.95f, 0.3f, 1.0f});

        if (uiMode_ == UiMode::Game) {
            e.setTextLabel("ESC: menu", "times", 12.0f, 46.0f, 0.52f, {0.8f, 0.9f, 1.0f, 1.0f});
        } else {
            e.setTextLabel("ESC: wyjscie", "times", 12.0f, 46.0f, 0.52f, {0.8f, 0.9f, 1.0f, 1.0f});
        }

        // Wyświetlanie godziny ze skydome oraz czasu do zachodu/wschodu
        std::ostringstream hudTime;
        if (skydome_) {
            float tod = skydome_->getTimeOfDay();
            int hours = static_cast<int>(tod);
            int minutes = static_cast<int>((tod - hours) * 60.0f);
            
            hudTime << "Czas gry: " 
                    << (hours < 10 ? "0" : "") << hours << ":" 
                    << (minutes < 10 ? "0" : "") << minutes;
            
            if (tod >= 6.0f && tod < 18.0f) {
                float diff = 18.0f - tod;
                int diffH = static_cast<int>(diff);
                int diffM = static_cast<int>((diff - diffH) * 60.0f);
                hudTime << " (Do zachodu: " << diffH << "h " << diffM << "m)";
            } else {
                float diff = (tod >= 18.0f) ? (24.0f - tod + 6.0f) : (6.0f - tod);
                int diffH = static_cast<int>(diff);
                int diffM = static_cast<int>((diff - diffH) * 60.0f);
                hudTime << " (Do wschodu: " << diffH << "h " << diffM << "m)";
            }
        } else {
            hudTime << "Czas gry: --:--";
        }
        e.setTextLabel(hudTime.str(), "times", 12.0f, 72.0f, 0.52f, {0.3f, 1.0f, 0.3f, 1.0f});

        if (!debugOverlayEnabled_) {
            e.setTextLabel("F2: debug", "times", 12.0f, 98.0f, 0.46f, {0.65f, 0.75f, 0.9f, 1.0f});
            return;
        }

        const char* modeText = "unknown";
        if (uiMode_ == UiMode::MenuMain) modeText = "menu.main";
        else if (uiMode_ == UiMode::MenuPause) modeText = "menu.pause";
        else if (uiMode_ == UiMode::Settings) modeText = "menu.settings";
        else if (uiMode_ == UiMode::Game) modeText = "game";

        const Scene* active = sceneManager_ ? sceneManager_->activeScene() : nullptr;
        const std::size_t activeEntities = active ? active->getEntities().size() : 0;
        const std::size_t gameEntities = gameScene_ ? gameScene_->getEntities().size() : 0;

        e.setTextLabel("F2: debug ON", "times", 12.0f, 98.0f, 0.50f, {0.95f, 0.82f, 0.35f, 1.0f});

        std::ostringstream line1;
        line1 << "mode=" << modeText
              << " scene=" << (active ? "active" : "none")
              << " scene_entities=" << activeEntities
              << " game_entities=" << gameEntities;
        e.setTextLabel(line1.str(), "times", 12.0f, 120.0f, 0.46f, {0.7f, 0.85f, 1.0f, 1.0f});

        std::ostringstream line2;
        line2 << "terrain=" << (gameTerrain_ ? "yes" : "no");
        if (gameTerrain_) {
            line2 << " size=" << gameTerrain_->sizeX << "x" << gameTerrain_->sizeZ;
        }
        line2 << " tree_roots=" << treeRoots_.size()
              << " car_roots=" << carRoots_.size();
        line2 << " skydome=" << (skydome_ ? "yes" : "no")
              << " ocean=" << (ocean_ ? "yes" : "no");
        e.setTextLabel(line2.str(), "times", 12.0f, 142.0f, 0.46f, {0.7f, 0.85f, 1.0f, 1.0f});

        if (gameCamera_) {
            const auto& pos = gameCamera_->getOwner()->transform().position;
            std::ostringstream line3;
            line3 << "cam=" << static_cast<int>(pos.x) << "," << static_cast<int>(pos.y) << "," << static_cast<int>(pos.z);
            if (gameTerrain_) {
                const float h = gameTerrain_->getHeight(pos.x, pos.z);
                line3 << " terrain_h=" << static_cast<int>(h);
            }
            e.setTextLabel(line3.str(), "times", 12.0f, 164.0f, 0.46f, {0.7f, 0.85f, 1.0f, 1.0f});
        }

        if (gameScene_ && !carRoots_.empty()) {
            std::ostringstream line4;
            line4 << "hierarchy children(car0)=" << gameScene_->childrenOf(carRoots_[0]).size()
                  << " parent_ok=" << parentLinkSuccessCount_
                  << " parent_fail=" << parentLinkFailCount_;
            e.setTextLabel(line4.str(), "times", 12.0f, 186.0f, 0.46f, {0.7f, 0.85f, 1.0f, 1.0f});
        }
    }

    Vec3 vec3FromJson(const nlohmann::json& node, const Vec3& fallback) {
        Vec3 result = fallback;
        if (node.is_object()) {
            result.x = node.value("x", result.x);
            result.y = node.value("y", result.y);
            result.z = node.value("z", result.z);
        }
        return result;
    }

    bool saveGameScene(const std::string& filePath) {
        if (!gameSceneReady_ || !gameScene_) {
            LOG_ERROR("Gra nie zostala jeszcze uruchomiona.");
            return false;
        }

        nlohmann::json root;
        root["worldTime"] = worldTime_;
        root["fogEnabled"] = engine_.isFogEnabled();
        root["lightingEnabled"] = engine_.isLightingEnabled();
        root["shadowsEnabled"] = engine_.shadowsEnabled();

        if (gameCamera_) {
            root["camera"] = {
                {"target", {{"x", gameCamera_->target.x}, {"y", gameCamera_->target.y}, {"z", gameCamera_->target.z}}}
            };
        }

        auto entitiesVal = nlohmann::json::array();
        for (const auto& entity : gameScene_->getEntities()) {
            nlohmann::json e;
            e["name"] = entity->getName();
            e["active"] = entity->isActive();

            const auto& t = entity->transform();
            e["transform"] = {
                {"position", {{"x", t.position.x}, {"y", t.position.y}, {"z", t.position.z}}},
                {"rotation", {{"x", t.rotation.x}, {"y", t.rotation.y}, {"z", t.rotation.z}}},
                {"scale", {{"x", t.scale.x}, {"y", t.scale.y}, {"z", t.scale.z}}}
            };
            entitiesVal.push_back(e);
        }
        root["entities"] = entitiesVal;

        std::ofstream out(filePath);
        if (!out.is_open()) {
            LOG_ERROR("Nie udalo sie otworzyc pliku do zapisu: {}", filePath);
            return false;
        }
        out << root.dump(2);
        return out.good();
    }

    bool loadGameScene(const std::string& filePath) {
        std::ifstream in(filePath);
        if (!in.is_open()) {
            LOG_ERROR("Nie udalo sie otworzyc pliku do odczytu: {}", filePath);
            return false;
        }

        nlohmann::json root;
        try {
            in >> root;
        } catch (const std::exception& e) {
            LOG_ERROR("Blad parsowania JSON przy wczytywaniu: {}", e.what());
            return false;
        }

        if (!ensureGameScene()) {
            LOG_ERROR("Nie udalo sie zainicjalizowac sceny gry.");
            return false;
        }

        worldTime_ = root.value("worldTime", 0.0f);
        if (skydome_) skydome_->setTime(worldTime_);
        if (ocean_) ocean_->setTime(worldTime_);

        bool fogOn = root.value("fogEnabled", true);
        engine_.setFogEnabled(fogOn);
        if (checkFog_) checkFog_->setChecked(fogOn);

        bool lightOn = root.value("lightingEnabled", true);
        engine_.setLightingEnabled(lightOn);
        if (checkLighting_) checkLighting_->setChecked(lightOn);

        bool shadowsOn = root.value("shadowsEnabled", true);
        engine_.setShadowsEnabled(shadowsOn);
        if (checkShadows_) checkShadows_->setChecked(shadowsOn);

        if (root.contains("camera") && root["camera"].is_object()) {
            const auto& cam = root["camera"];
            if (gameCamera_) {
                gameCamera_->target = vec3FromJson(cam.value("target", nlohmann::json::object()), gameCamera_->target);
            }
        }

        if (root.contains("entities") && root["entities"].is_array()) {
            for (const auto& e : root["entities"]) {
                std::string name = e.value("name", "");
                if (name.empty()) continue;

                Entity* entity = gameScene_->getEntity(name);
                if (entity) {
                    entity->setActive(e.value("active", true));
                    if (e.contains("transform") && e["transform"].is_object()) {
                        const auto& t = e["transform"];
                        entity->transform().position = vec3FromJson(t.value("position", nlohmann::json::object()), entity->transform().position);
                        entity->transform().rotation = vec3FromJson(t.value("rotation", nlohmann::json::object()), entity->transform().rotation);
                        entity->transform().scale = vec3FromJson(t.value("scale", nlohmann::json::object()), entity->transform().scale);
                    }
                }
            }
        }

        LOG_INFO("Pomyslnie wczytano stan sceny z pliku: {}", filePath);
        return true;
    }

private:
    Engine engine_;

    SceneManager* sceneManager_ = nullptr;
    Renderer* renderer_ = nullptr;
    MaterialManager* materialManager_ = nullptr;
    MeshManager* meshManager_ = nullptr;
    AssetRegistry* assets_ = nullptr;

    Scene* menuScene_ = nullptr;
    Scene* gameScene_ = nullptr;

    CameraComponent* menuCamera_ = nullptr;
    CameraComponent* gameCamera_ = nullptr;

    std::shared_ptr<GuiMgr> guiMgr_;
    GuiWindow* menuMainWindow_ = nullptr;
    GuiWindow* menuPauseWindow_ = nullptr;
    GuiWindow* settingsWindow_ = nullptr;

    GuiButton* btnNewGame_ = nullptr;
    GuiButton* btnSettingsMain_ = nullptr;
    GuiButton* btnLoadMain_ = nullptr;
    GuiButton* btnExitMain_ = nullptr;
    GuiButton* btnContinue_ = nullptr;
    GuiButton* btnSettingsPause_ = nullptr;
    GuiButton* btnSavePause_ = nullptr;
    GuiButton* btnLoadPause_ = nullptr;
    GuiButton* btnEnd_ = nullptr;
    GuiButton* btnSettingsBack_ = nullptr;

    GuiComboBox* comboGraphics_ = nullptr;
    GuiComboBox* comboRefresh_ = nullptr;
    GuiComboBox* comboSync_ = nullptr;
    GuiComboBox* comboPresent_ = nullptr;
    GuiCheckBox* checkFullscreen_ = nullptr;
    GuiCheckBox* checkWireframe_ = nullptr;
    GuiCheckBox* checkFog_ = nullptr;
    GuiCheckBox* checkLighting_ = nullptr;
    GuiCheckBox* checkShadows_ = nullptr;

    std::shared_ptr<Terrain> gameTerrain_;
    std::shared_ptr<SkydomeRenderer> skydome_;
    std::shared_ptr<OceanRenderer> ocean_;

    std::shared_ptr<Md2ModelData> md2Model_;
    std::shared_ptr<Md2AnimationState> md2Anim_;
    GltfSkinnedModel brainModel_;
    std::vector<GltfSkinnedInstance> brainInstances_;
    std::vector<Entity*> treeRoots_;
    std::vector<Entity*> carRoots_;
    std::vector<Entity*> terrainRoots_;
    std::vector<float> terrainRootOffsets_;
    int parentLinkSuccessCount_ = 0;
    int parentLinkFailCount_ = 0;

    UiMode uiMode_ = UiMode::MenuMain;
    UiMode modeBeforeSettings_ = UiMode::MenuMain;
    bool debugOverlayEnabled_ = true;
    bool gameSceneReady_ = false;
    float worldTime_ = 0.0f;
    std::string statusMessage_;
    float statusMessageTimer_ = 0.0f;
    GuiTrackBar* trackTimeSpeed_ = nullptr;
    float timeSpeedMultiplier_ = 1.0f;
};

} // namespace

int main() {
    GameApp app;
    if (!app.initialize()) {
        return 1;
    }
    app.run();
    return 0;
}
