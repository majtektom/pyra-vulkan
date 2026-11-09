/**
 * app_core.cpp - rdzeń aplikacji.
 *
 * Start programu (initialize), tworzenie świata dla nowej partii (newWorld),
 * przełączanie trybów (menu / gra / pauza / koniec gry), prędkość czasu
 * oraz główna funkcja klatki update(), która decyduje, co dzieje się w danym trybie.
 */
#include "settlers_app.h"

namespace settlers {

// Start aplikacji: okno i silnik, zasoby (modele, materiały), GUI, ustawienia, dźwięk,
// pierwszy świat (widoczny w tle menu) oraz obsługa zmiennych środowiskowych trybów testowych.
bool SettlersApp::initialize() {
    if (!engine_.initialize("engine3d: Osadnicy", 1440, 900)) {
        return false;
    }
    setupInput();
    renderer_ = engine_.getRenderer();
    renderer_->setClearColor(0.5f, 0.65f, 0.85f, 1.0f);
    engine_.loadFont(kFont, "assets/fonts/times.ttf", 28);

    engine_.setLightingEnabled(true);
    engine_.setShadowsEnabled(true);
    engine_.configureDirectionalShadow(450.0f);
    engine_.setAmbientStrength(0.35f);
    engine_.setAmbientColor(0.42f, 0.45f, 0.52f);
    engine_.setTessellationEnabled(false);
    engine_.setFogEnabled(true);
    engine_.setFogStart(900.0f);
    engine_.setFogEnd(2600.0f);

    setupSkyAndWater();
    buildGameMeshes(engine_);
    materials_ = createGameMaterials(engine_);
    loadSettings();
    setupGui();
    setupAudio();
    applySettings(true);

    newWorld(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()), Difficulty::Normal);
    world_->aiEnabled = false;
    showMenu();

    // Tryb autotestu: SETTLERS_AUTOPLAY=<sekundy gry>, SETTLERS_SPEED=<mnożnik czasu>
    if (const char* autoplay = std::getenv("SETTLERS_AUTOPLAY")) {
        autoplaySeconds_ = static_cast<float>(std::atof(autoplay));
        if (const char* sp = std::getenv("SETTLERS_SPEED")) autoplaySpeed_ = std::max(1, std::atoi(sp));
        startNewGame();
        world_->aiControlsPlayer = true;
        gameSpeed_ = autoplaySpeed_;
        LOG_INFO("[autoplay] start: {} s czasu gry, przyspieszenie x{}", autoplaySeconds_, autoplaySpeed_);
    }

    // Pomocnicze: SETTLERS_OPEN=settings|controls|help otwiera okno od razu po starcie
    if (const char* open = std::getenv("SETTLERS_OPEN")) {
        const std::string what = open;
        if (what == "settings") openSubWindow(settingsWindow_, menuWindow_);
        if (what == "controls") openSubWindow(controlsWindow_, menuWindow_);
        if (what == "help") showHelp_ = true;
        if (what == "demo") startNewGame(true);
    }
    if (const char* dbg = std::getenv("SETTLERS_DEBUG")) debugMode_ = std::clamp(std::atoi(dbg), 0, 2);

    // Test interfejsu: SETTLERS_UITEST=1 - wstrzykuje zdarzenia SDL (klawiatura/mysz) i sprawdza efekty.
    if (std::getenv("SETTLERS_UITEST")) {
        startNewGame();
        uiTest_ = true;
    }

    engine_.onUpdate = [this](float dt, Engine& e) { update(dt, e); };
    return true;
}

// Przypisuje klawisze do nazwanych akcji. Jedna akcja może mieć kilka klawiszy (np. W i strzałka w górę).
void SettlersApp::setupInput() {
    auto& map = engine_.inputActions();
    map.clearAll();
    map.bindKey(kActPanLeft, SDL_SCANCODE_A);
    map.bindKey(kActPanLeft, SDL_SCANCODE_LEFT);
    map.bindKey(kActPanRight, SDL_SCANCODE_D);
    map.bindKey(kActPanRight, SDL_SCANCODE_RIGHT);
    map.bindKey(kActPanUp, SDL_SCANCODE_W);
    map.bindKey(kActPanUp, SDL_SCANCODE_UP);
    map.bindKey(kActPanDown, SDL_SCANCODE_S);
    map.bindKey(kActPanDown, SDL_SCANCODE_DOWN);
    map.bindKey(kActRotL, SDL_SCANCODE_Q);
    map.bindKey(kActRotR, SDL_SCANCODE_E);
    map.bindKey(kActHome, SDL_SCANCODE_SPACE);
    map.bindKey(kActZoomIn, SDL_SCANCODE_EQUALS);
    map.bindKey(kActZoomIn, SDL_SCANCODE_KP_PLUS);
    map.bindKey(kActZoomIn, SDL_SCANCODE_PAGEUP);
    map.bindKey(kActZoomOut, SDL_SCANCODE_MINUS);
    map.bindKey(kActZoomOut, SDL_SCANCODE_KP_MINUS);
    map.bindKey(kActZoomOut, SDL_SCANCODE_PAGEDOWN);
    map.bindKey(kActEsc, SDL_SCANCODE_ESCAPE);
    map.bindKey(kActRotateBuilding, SDL_SCANCODE_R);
    map.bindKey(kActDemolish, SDL_SCANCODE_DELETE);
    map.bindKey(kActHalt, SDL_SCANCODE_H);
    map.bindKey(kActHelp, SDL_SCANCODE_F1);
    map.bindKey(kActSelectArmy, SDL_SCANCODE_TAB);
    map.bindKey(kActMuteMusic, SDL_SCANCODE_M);
    map.bindKey(kActDebug, SDL_SCANCODE_F3);
    map.bindKey(kActSlower, SDL_SCANCODE_LEFTBRACKET);
    map.bindKey(kActFaster, SDL_SCANCODE_RIGHTBRACKET);
    map.bindKey(kActDebugDump, SDL_SCANCODE_F9);
    static const SDL_Scancode numKeys[] = {SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
                                            SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6};
    for (int i = 0; i < 6; ++i) {
        map.bindKey("build." + std::to_string(i + 1), numKeys[i]);
    }
}

// Niebo z chmurami i słońcem (SkydomeRenderer) oraz powierzchnia wody (OceanRenderer).
// Są to efekty renderera, a nie encje sceny - tworzymy je raz na cały czas działania programu.
void SettlersApp::setupSkyAndWater() {
    skydome_ = std::make_shared<SkydomeRenderer>();
    SkydomeConfig sky;
    sky.enabled = true;
    sky.domeScale = 1.6f;
    sky.timeOfDay = 13.0f;
    sky.cloudCoverage = 0.35f;
    sky.sunElevation = 50.0f;
    sky.enableSunLight = true;
    skydome_->configure(sky, "bin/assets/shaders/skydome", "skydome");
    skydome_->setEnabled(true);
    renderer_->addFeature(skydome_);

    ocean_ = std::make_shared<OceanRenderer>();
    OceanConfig oc;
    oc.enabled = true;
    oc.waterLevel = kWaterLevel;
    oc.waveAmplitude = 0.6f;
    oc.waveSpeed = 0.8f;
    oc.waveFrequency = 8.0f;
    oc.foamThreshold = 2.5f;
    ocean_->configure(oc, "bin/assets/shaders/ocean", "ocean");
    ocean_->setEnabled(true);
    renderer_->addFeature(ocean_);
}

// Tworzy nową partię: usuwa starą scenę, tworzy nową z kamerą i słońcem,
// generuje świat (World::create) i ustawia kamerę nad zamkiem gracza.
void SettlersApp::newWorld(uint32_t seed, Difficulty diff) {
    clearSelection();
    placing_ = false;
    ghost_ = nullptr;
    world_.reset();
    if (sceneManager()->getScene("game")) {
        renderer_->waitIdle();
        sceneManager()->removeScene("game");
    }
    scene_ = sceneManager()->createScene("game");
    engine_.configureScene(*scene_);
    scene_->addSystem(std::make_unique<TerrainSystem>(*engine_.getMeshManager(), *engine_.getMaterialManager(),
                                                      *engine_.getMaterialSystem()));

    auto* camEntity = scene_->createEntity("rts_camera");
    camera_ = camEntity->addComponent<CameraComponent>();
    camera_->fieldOfViewDegrees = 45.0f;
    camera_->nearPlane = 1.0f;
    camera_->farPlane = 6000.0f;
    scene_->setActiveCamera(camera_);

    sun_ = scene_->createDirectionalLight("sun", normalize(Vec3{-0.4f, -1.0f, -0.3f}), {1.0f, 0.96f, 0.88f}, 2.2f);

    world_ = std::make_unique<World>(engine_, materials_);
    world_->create(scene_, seed, diff);

    debugNavEntity_ = scene_->createEntity("debug_nav");
    {
        auto* dm = debugNavEntity_->addComponent<MeshComponent>("");
        dm->materialHandle = materials_.debug;
        dm->castShadows = false;
        debugNavEntity_->setActive(false);
    }
    debugNavTimer_ = 0.0f;

    ghost_ = scene_->createEntity("build_ghost");
    auto* gm = ghost_->addComponent<MeshComponent>(buildingMeshName(BuildingType::House, kPlayer));
    gm->materialHandle = materials_.ghostOk;
    gm->castShadows = false;
    ghost_->setActive(false);

    cam_ = RtsCameraController{};
    cam_.boundsMin = {-kWorldHalf + 60.0f, 0.0f, -kWorldHalf + 60.0f};
    cam_.boundsMax = {kWorldHalf - 60.0f, 0.0f, kWorldHalf - 60.0f};
    cam_.minDistance = 45.0f;
    cam_.maxDistance = 750.0f;
    World* w = world_.get();
    cam_.groundHeight = [w](float x, float z) { return std::max(w->heightAt(x, z), kWaterLevel); };
    cam_.setFocus(world_->startPos[kPlayer]);
    cam_.setDistance(260.0f);
    cam_.setPitch(52.0f);
    // Kamera patrzy od strony gracza w stronę środka mapy
    const Vec3 sp = world_->startPos[kPlayer];
    cam_.setYaw(std::atan2(sp.x, sp.z) * 180.0f / PI);
    cam_.update(0.0f, *camera_);

    sceneManager()->setActiveScene("game");
}

// spectator = true: pokaz "AI kontra AI" - komputer gra za obie strony, gracz tylko ogląda.
void SettlersApp::startNewGame(bool spectator) {
    const int idx = comboDifficulty_->selectedIdx();
    const Difficulty diff = idx == 0 ? Difficulty::Easy : (idx == 2 ? Difficulty::Hard : Difficulty::Normal);
    newWorld(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()), diff);
    world_->aiEnabled = true;
    world_->aiControlsPlayer = spectator;
    spectator_ = spectator;
    gameSpeed_ = spectator ? 4 : 1;
    updateSpeedButton();
    gameStarted_ = true;
    if (spectator) {
        world_->pushMessage("Pokaz: AI kontra AI. Prędkość: [ i ], ESC - menu", {1.0f, 0.95f, 0.6f, 1.0f});
    }
    enterGame();
}

// Klawisze [ i ]: zmiana prędkości czasu o jeden stopień z listy kSpeeds.
void SettlersApp::changeGameSpeed(int dir) {
    int idx = 0;
    for (int i = 0; i < 5; ++i) {
        if (kSpeeds[i] <= gameSpeed_) idx = i;
    }
    idx = std::clamp(idx + dir, 0, 4);
    gameSpeed_ = kSpeeds[idx];
    updateSpeedButton();
    if (world_) world_->pushMessage("Prędkość gry: x" + std::to_string(gameSpeed_), {0.8f, 0.95f, 1.0f, 1.0f});
}

// Przycisk w pauzie: kolejna prędkość, po x16 wraca do x1.
void SettlersApp::cycleGameSpeed() {
    if (gameSpeed_ >= 16) {
        gameSpeed_ = 1;
        updateSpeedButton();
        if (world_) world_->pushMessage("Prędkość gry: x1", {0.8f, 0.95f, 1.0f, 1.0f});
    } else {
        changeGameSpeed(+1);
    }
}

// Aktualizuje napis na przycisku prędkości w menu pauzy.
void SettlersApp::updateSpeedButton() {
    if (btnSpeed_) btnSpeed_->setText("Prędkość gry: x" + std::to_string(gameSpeed_) + "  ([ / ])");
}

// Ukrywa wszystkie okna GUI - każda funkcja show...() zaczyna od czystego ekranu.
void SettlersApp::hideAllWindows() {
    menuWindow_->setVisible(false);
    pauseWindow_->setVisible(false);
    gameOverWindow_->setVisible(false);
    settingsWindow_->setVisible(false);
    controlsWindow_->setVisible(false);
    subWindow_ = nullptr;
    buildWindow_->setVisible(false);
    barracksWindow_->setVisible(false);
    actionsWindow_->setVisible(false);
}

// Menu główne (w tle świat dalej żyje, kamera krąży).
void SettlersApp::showMenu() {
    mode_ = Mode::Menu;
    hideAllWindows();
    menuWindow_->setVisible(true);
    gui_->setActive(menuWindow_->name());
    gui_->enableCursor(true);
    if (world_ && gameStarted_ && world_->winner >= 0) {
        gameStarted_ = false;
    }
}

// Rozgrywka: widoczny panel budowy (w pokazie AI - ukryty).
void SettlersApp::enterGame() {
    mode_ = Mode::Game;
    hideAllWindows();
    buildWindow_->setVisible(!spectator_);
    gui_->enableCursor(true);
}

// Pauza: symulacja stoi, widoczne menu pauzy.
void SettlersApp::showPause() {
    mode_ = Mode::Pause;
    cancelPlacement();
    hideAllWindows();
    pauseWindow_->setVisible(true);
    gui_->setActive(pauseWindow_->name());
}

// Koniec partii: melodia wygranej/przegranej, okno z wynikiem i statystykami.
void SettlersApp::showGameOver() {
    mode_ = Mode::GameOver;
    cancelPlacement();
    hideAllWindows();
    gameOverWindow_->setVisible(true);
    gui_->setActive(gameOverWindow_->name());
    const bool won = world_->winner == kPlayer;
    if (audio_) {
        audio_->stopMusic(1.0f);
        sfx(won ? "victory" : "defeat", 1.0f);
    }
    gameOverText_->setText(won ? "ZWYCIĘSTWO! Zamek wroga upadł." : "PORAŻKA... Twój zamek został zdobyty.");
    gameOverText_->color_ = won ? Color{0.5f, 1.0f, 0.5f, 1.0f} : Color{1.0f, 0.45f, 0.4f, 1.0f};
    std::ostringstream ss;
    const int minutes = static_cast<int>(world_->gameTime) / 60;
    ss << "Czas: " << minutes << " min   Pokonani wrogowie: " << world_->factions[kPlayer].enemiesKilled
       << "   Straty: " << world_->factions[kPlayer].unitsLost;
    gameOverStats_->setText(ss.str());
    gameStarted_ = false;
}

// GŁÓWNA FUNKCJA KLATKI - wywoływana przez silnik przed renderowaniem sceny.
void SettlersApp::update(float dt, Engine& e) {
    const auto& in = e.input();

    // 1. Rozmiar okna mógł się zmienić (zmiana rozdzielczości, pełny ekran) - przestawiamy okna GUI.
    if (static_cast<int>(renderer_->width()) != lastW_ || static_cast<int>(renderer_->height()) != lastH_) {
        layoutGui();
    }
    screenW_ = static_cast<float>(renderer_->width());
    screenH_ = static_cast<float>(renderer_->height());

    // 2. Klawisze działające zawsze (pomoc, muzyka, debug) i GUI (kliknięcia w przyciski).
    handleGlobalKeys(e);
    gui_->update(in, *e.getPlatform(), screenW_, screenH_);

    // 3. Logika bieżącego trybu: wejście gracza i krok symulacji.
    updateMode(dt, e, in);

    // 4. Kamera, niebo, dźwięk.
    cam_.update(dt, *camera_);
    if (uiTest_) runUiTestStep();
    updateSky(dt);
    updateAudio();

    // 5. Interfejs rysowany od nowa w każdej klatce.
    cleanupSelection();
    updateContextWindows();
    drawHud(e, in);
    updateDebugNavMesh(dt);
    if (debugMode_ > 0 && mode_ != Mode::Menu) drawDebug(in);
    gui_->renderGui();
}

// Klawisze działające w każdym trybie: F1 pomoc, M muzyka, F3 tryb debug, F9 zrzut stanu.
void SettlersApp::handleGlobalKeys(Engine& e) {
    if (e.actionPressed(kActHelp)) showHelp_ = !showHelp_;
    if (e.actionPressed(kActMuteMusic)) toggleMusic();
    if (e.actionPressed(kActDebug)) {
        debugMode_ = (debugMode_ + 1) % 3;
        static const char* names[] = {"Debug: wyłączony", "Debug: ścieżki, stany, wejścia (F3)", "Debug: + siatka kolizji (F3)"};
        world_->pushMessage(names[debugMode_], {0.7f, 0.9f, 1.0f, 1.0f});
    }
    if (e.actionPressed(kActDebugDump)) dumpDebug();
}

// Co dzieje się w danym trybie:
//   Menu     - kamera krąży, świat żyje w tle; ESC wraca do gry albo zamyka program,
//   Game     - kamera, wejście gracza, prędkość czasu, gameSpeed_ kroków symulacji; ESC = pauza,
//   Pause    - symulacja stoi; ESC wraca do gry,
//   GameOver - można oglądać mapę, świat dalej się animuje.
// ESC w otwartym oknie Ustawienia/Sterowanie najpierw zamyka to okno.
void SettlersApp::updateMode(float dt, Engine& e, const InputSnapshot& in) {
    const bool escConsumed = subWindow_ && e.actionPressed(kActEsc);
    if (escConsumed) closeSubWindow();
    const bool escPressed = e.actionPressed(kActEsc) && !escConsumed && !subWindow_;

    switch (mode_) {
    case Mode::Menu:
        updateMenuCamera(dt);
        world_->update(dt);
        if (escPressed) {
            if (gameStarted_) enterGame(); else pushQuitEvent();
        }
        break;

    case Mode::Game: {
        const bool wasPlacing = placing_;   // ESC przy stawianiu budynku anuluje budowę, a nie pauzuje
        updateCameraInput(dt, e, in);
        handleGameInput(e, in);
        if (e.actionPressed(kActSlower)) changeGameSpeed(-1);
        if (e.actionPressed(kActFaster)) changeGameSpeed(+1);
        for (int i = 0; i < gameSpeed_ && world_->winner < 0; ++i) world_->update(dt);
        if (autoplaySeconds_ > 0.0f) updateAutoplay();
        if (world_->winner >= 0) {
            showGameOver();
        } else if (e.actionPressed(kActEsc) && !wasPlacing) {
            showPause();
        }
        break;
    }

    case Mode::Pause:
        if (escPressed) enterGame();
        break;

    case Mode::GameOver:
        updateCameraInput(dt, e, in);
        world_->update(dt);
        break;
    }
}

// Pora dnia waha się łagodnie między porankiem a popołudniem (bez nocy - to gra strategiczna).
// Kierunek i kolor słońca oraz światło otoczenia bierzemy z modelu nieba (skydome).
void SettlersApp::updateSky(float dt) {
    (void)dt;
    const float tod = 12.5f + 3.2f * std::sin(world_->gameTime * 0.004f);
    skyTime_ = (tod - 11.0f) / 0.05f;
    skydome_->setTime(skyTime_);
    ocean_->setTime(world_->gameTime);
    if (sun_) {
        if (auto* light = sun_->getComponent<LightComponent>()) {
            light->direction = skydome_->getSunDirection();
            light->color = skydome_->getSunColor();
            light->intensity = std::max(1.4f, skydome_->getSunIntensity());
        }
    }
    const float amb = std::max(0.3f, skydome_->getAmbientStrength());
    const Vec3 ac = skydome_->getAmbientColor();
    engine_.setAmbientStrength(amb);
    engine_.setAmbientColor(ac.x, ac.y, ac.z);
    engine_.setFogColor(0.62f, 0.72f, 0.86f);
}

// W menu kamera powoli obraca się wokół punktu na mapie.
void SettlersApp::updateMenuCamera(float dt) {
    cam_.rotate(dt * 4.0f);
}

} // namespace settlers
