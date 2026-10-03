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
#include "feature/gui/gui_list_box.h"
#include <filesystem>
#include "object/gltf/model_gltf.h"
#include "object/model_3ds.h"
#include "object/model_md2.h"
#include "object/ocean_renderer.h"
#include "object/skydome_renderer.h"
#include "object/terrain/terrain.h"
#include "object/terrain/terrain_component.h"
#include "object/terrain/terrain_system.h"
#include "object/terrain/terrain_placement_system.h"
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
        engine_.configureDirectionalShadow(1200.0f);
        engine_.setAmbientStrength(0.28f);
        engine_.setAmbientColor(0.35f, 0.38f, 0.45f);
        engine_.setTessellationEnabled(true);
        // engine_.setTessellationLevel(6.0f);

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
        menuCamera_->farPlane = 18000.0f;
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

        // Okno wyboru mapy przy nowej grze
        mapSelectionWindow_ = guiMgr_->addWindow("Wybierz mape", 520, 180, 400, 420);
        if (mapSelectionWindow_) {
            auto* listLabel = new GuiStaticText(mapSelectionWindow_, "list_label", 10, 10);
            listLabel->setText("Dostepne mapy:");
            
            listMaps_ = new GuiListBox(mapSelectionWindow_, "maps_list", 10, 40, 380, 280);
            
            btnLoadSelectedMap_ = new GuiButton(mapSelectionWindow_, "load_map_btn", 10, 330, 180, 34);
            btnLoadSelectedMap_->setText("Wczytaj");
            
            btnMapSelectionBack_ = new GuiButton(mapSelectionWindow_, "back_map_btn", 210, 330, 180, 34);
            btnMapSelectionBack_->setText("Anuluj");

            mapSelectionWindow_->setVisible(false);
        }

        wireGuiCallbacks();
        return true;
    }

    /**
     * @brief Podpina wywołania zwrotne (callbacks) pod kliknięcia widgetów.
     */
    void wireGuiCallbacks() {
        btnNewGame_->setOnClick([this]() {
            showMapSelection();
        });
        btnMapSelectionBack_->setOnClick([this]() {
            showMainMenu();
        });
        btnLoadSelectedMap_->setOnClick([this]() {
            std::string selected = listMaps_->selectedText();
            if (selected.empty()) {
                statusMessage_ = "Wybierz mape z listy!";
                statusMessageTimer_ = 3.0f;
                return;
            }
            currentMapPath_ = "assets/maps/" + selected;
            if (!ensureGameScene(currentMapPath_)) {
                LOG_ERROR("Nie udalo sie wczytac mapy: {}", currentMapPath_);
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
            engine_.configureDirectionalShadow(650.0f);
            engine_.setAmbientStrength(0.40f);
        } else {
            engine_.setShadowsEnabled(true);
            if (checkShadows_) checkShadows_->setChecked(true);
            engine_.configureDirectionalShadow(1200.0f);
            engine_.setAmbientStrength(0.28f);
        }
    }

    /**
     * @brief Leniwa alokacja i konfiguracja sceny gry (Lazy Initialization).
     * 
     * Scena gry jest tworzona dopiero przy kliknięciu "Nowa gra", dzięki czemu 
     * menu ładuje się natychmiast bez blokowania programu wczytywaniem ciężkich modeli 3D.
     */
    bool ensureGameScene(const std::string& mapPath = "assets/maps/demo.map") {
        if (gameSceneReady_) {
            sceneManager_->removeScene("game");
            gameSceneReady_ = false;
        }

        gameScene_ = sceneManager_->createScene("game");
        if (!gameScene_) {
            return false;
        }
        engine_.configureScene(*gameScene_);

        // Register ECS Systems for Terrain
        gameScene_->addSystem(std::make_unique<TerrainSystem>(*engine_.getMeshManager(), *engine_.getMaterialManager(), *engine_.getMaterialSystem()));
        gameScene_->addSystem(std::make_unique<TerrainPlacementSystem>());

        gameTerrain_ = nullptr;

        // Manually load Terrain from the map config before loading other map elements
        std::ifstream file(mapPath);
        if (file.is_open()) {
            nlohmann::json root;
            try {
                file >> root;
                if (root.contains("terrain")) {
                    auto tnode = root["terrain"];
                    if (tnode.value("enabled", false)) {
                        std::string tpath = tnode.value("path", "");
                        auto terrain = std::make_shared<Terrain>();
                        bool loaded = terrain->loadFromJson(tpath);
                        if (loaded) {
                            // terrain->setScale(15.0f, 2.0f, 20.0f, 6); // Skala w płaszczyźnie XZ - ile jednostek świata na 1 piksel heightmapy
                            terrain->buildMesh();
                        } else {
                            LOG_WARN("Could not load terrain, generating plasma");
                            terrain->generatePlasma(256, 1.2f);
                            terrain->setScale(15.0f, 2.0f, 20.0f, 6); // Skala w płaszczyźnie XZ - ile jednostek świata na 1 piksel heightmapy
                            terrain->smoothEdges();
                            terrain->buildMesh();
                        }
                        terrain->uploadTo(*engine_.getMeshManager());

                        Material terrainMaterial;
                        terrainMaterial.name = "map_terrain_mat";
                        terrainMaterial.shaderProgram = "terrain";
                        terrainMaterial.cullMode = CullMode::Back;
                        terrainMaterial.depthWrite = true;
                        terrainMaterial.depthTest = true;
                        if (!terrain->layers.empty() && !terrain->layers[0].texturePath.empty()) {
                            TextureHandle texture = engine_.getRenderer()->loadTexture("map_terrain_mat_tex", terrain->layers[0].texturePath);
                            terrainMaterial.textureHandle = texture;
                        }
                        MaterialHandle terrainMaterialHandle = engine_.getMaterialManager()->add(terrainMaterial);

                        Entity* terrainEntity = gameScene_->createEntity("game_terrain_entity");
                        terrainEntity->transform().position = terrain->worldOffset();
                        auto* terrainComp = terrainEntity->addComponent<TerrainComponent>(terrain);
                        terrainComp->materialHandle = terrainMaterialHandle;
                        terrainComp->castShadows = true;
                        terrainComp->layerBlendEnabled = true;
                        terrainComp->layerCount = std::min<uint32_t>(
                            static_cast<uint32_t>(terrain->layers.size()), kMaxTerrainLayers);
                        terrainComp->layerWorldSize = terrain->worldSizeXZ();

                        for (uint32_t i = 0; i < terrainComp->layerCount; ++i) {
                            std::string texName = "map_terrain_layer_tex_" + std::to_string(i);
                            terrainComp->layerTextureHandles[i] = engine_.getRenderer()->loadTexture(texName, terrain->layers[i].texturePath);
                            if (i > 0 && !terrain->layers[i].maskPath.empty()) {
                                std::string maskName = "map_terrain_layer_mask_" + std::to_string(i);
                                terrainComp->layerMaskHandles[i] = engine_.getRenderer()->loadTexture(maskName, terrain->layers[i].maskPath);
                            }
                        }
                        terrainComp->heightmapTextureHandle = engine_.getRenderer()->loadTextureFromMemory("map_terrain_heightmap", terrain->getHeightmapData(), terrain->getHeightmapSize(), terrain->getHeightmapSize());
                        terrainComp->heightmapEnabled = true;
                        gameTerrain_ = terrain;
                    }
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to parse map file JSON for terrain in ensureGameScene: {}", e.what());
            }
        }

        if (!engine_.loadMap(mapPath, gameScene_)) {
            LOG_ERROR("Nie udalo sie wczytac mapy: {}", mapPath);
            return false;
        }

        skydome_ = engine_.getSkydome();
        ocean_ = engine_.getOcean();
        md2Model_ = engine_.getMd2Model();
        md2Anim_ = engine_.getMd2Anim();
        gameCamera_ = gameScene_->activeCamera();

        gameSceneReady_ = true;
        return true;
    }

    void resnapTerrainRoots() {
        // Przyklejanie do terenu jest teraz automatycznie wykonywane przez silnik (Engine::update)
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
        if (mapSelectionWindow_) mapSelectionWindow_->setVisible(false);

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
        if (mapSelectionWindow_) mapSelectionWindow_->setVisible(false);

        guiMgr_->setActive("Menu");
        guiMgr_->enableCursor(true);
    }

    void showPauseMenu() {
        uiMode_ = UiMode::MenuPause;
        sceneManager_->setActiveScene("menu");

        menuMainWindow_->setVisible(false);
        menuPauseWindow_->setVisible(true);
        settingsWindow_->setVisible(false);
        if (mapSelectionWindow_) mapSelectionWindow_->setVisible(false);

        guiMgr_->setActive("Pauza");
        guiMgr_->enableCursor(true);
    }

    void showSettings() {
        modeBeforeSettings_ = uiMode_;
        uiMode_ = UiMode::Settings;

        menuMainWindow_->setVisible(false);
        menuPauseWindow_->setVisible(false);
        settingsWindow_->setVisible(true);
        if (mapSelectionWindow_) mapSelectionWindow_->setVisible(false);

        guiMgr_->setActive("Ustawienia");
        guiMgr_->enableCursor(true);
    }

    void showMapSelection() {
        menuMainWindow_->setVisible(false);
        menuPauseWindow_->setVisible(false);
        settingsWindow_->setVisible(false);
        if (mapSelectionWindow_) mapSelectionWindow_->setVisible(true);

        guiMgr_->setActive("Wybierz mape");
        guiMgr_->enableCursor(true);

        if (listMaps_) {
            listMaps_->clear();
            namespace fs = std::filesystem;
            if (fs::exists("assets/maps")) {
                for (const auto& entry : fs::directory_iterator("assets/maps")) {
                    if (entry.is_regular_file() && entry.path().extension() == ".map") {
                        listMaps_->addItem(entry.path().filename().string());
                    }
                }
            }
        }
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
            controller->moveSpeed = 152.0f;
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

        worldTime_ += dt * timeSpeedMultiplier_;

        if (skydome_) {
            skydome_->setTime(worldTime_);

            // Dynamiczna zmiana zachmurzenia (pogody)
            auto cfg = skydome_->getConfig();
            static float cloudDir = 1.0f;
            const float weatherSpeed = 0.015f; // powolne zmiany pogody
            cfg.cloudCoverage += dt * weatherSpeed * cloudDir;
            if (cfg.cloudCoverage >= 0.72f) { cfg.cloudCoverage = 0.72f; cloudDir = -1.0f; }
            else if (cfg.cloudCoverage <= 0.12f) { cfg.cloudCoverage = 0.12f; cloudDir = 1.0f; }
            skydome_->setConfig(cfg);

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

        if(gameTerrain_) {
            // Obliczamy liczbę węzłów na każdym poziomie LOD
            std::array<int, 5> lodCounts{};
            for (const auto* node : gameTerrain_->getVisibleNodes()) {
                int lod = std::max(0, std::min(4, node->lodLevel));
                lodCounts[lod]++;
            }
            
            std::ostringstream mode;
            mode << " Total Boxes: " << gameTerrain_->getTotalLeafCount()
                    << "  Visible: " << gameTerrain_->visibleCount
                    << " (LOD0:" << lodCounts[0] << " LOD1:" << lodCounts[1]
                    << " LOD2:" << lodCounts[2] << " LOD3:" << lodCounts[3]
                    << " LOD4:" << lodCounts[4] << ")";
            e.setTextLabel(mode.str(), "times", 10, 180, 0.45f, {0.7f, 0.7f, 1.0f, 1.0f});
        }

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
        root["mapPath"] = currentMapPath_;
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

        currentMapPath_ = root.value("mapPath", "assets/maps/demo.map");
        if (!ensureGameScene(currentMapPath_)) {
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
    GuiWindow* mapSelectionWindow_ = nullptr;
    GuiListBox* listMaps_ = nullptr;
    GuiButton* btnLoadSelectedMap_ = nullptr;
    GuiButton* btnMapSelectionBack_ = nullptr;
    std::string currentMapPath_ = "assets/maps/demo.map";
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
