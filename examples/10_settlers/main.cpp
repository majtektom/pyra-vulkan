/**
 * 10_settlers - strategia ekonomiczno-wojenna w stylu klasycznych "Osadników".
 *
 * Gracz rozbudowuje osadę (drwal, kamieniarz, farma, domy, koszary, wieże),
 * osadnicy samodzielnie budują i pracują, a wojsko walczy z komputerowym przeciwnikiem.
 * Celem jest zniszczenie wrogiego zamku.
 *
 * Elementy silnika użyte w przykładzie:
 *  - RtsCameraController (kamera strategiczna), screenPointToRay / worldToScreen (picking),
 *  - Terrain::raycast + Terrain::setHeightmap (proceduralny teren z warstwami tekstur),
 *  - NavGrid (A*), MeshBuilder (proceduralne modele low-poly),
 *  - GuiMgr::addOverlayRect (HUD, paski życia, minimapa, ramka zaznaczenia),
 *  - SkydomeRenderer, OceanRenderer, cienie kierunkowe, oświetlenie PBR.
 */
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "camera/camera_component.h"
#include "camera/camera_picking.h"
#include "camera/rts_camera_controller.h"
#include "core/engine.h"
#include "core/entity.h"
#include "core/light_component.h"
#include "core/logger.h"
#include "core/mesh_builder.h"
#include "core/mesh_component.h"
#include "core/mesh_manager.h"
#include "feature/audio/audio_system.h"
#include "feature/gui/gui.h"
#include "feature/gui/gui_button.h"
#include "feature/gui/gui_check_box.h"
#include "feature/gui/gui_combo_box.h"
#include "feature/gui/gui_static_text.h"
#include "feature/gui/gui_track_bar.h"
#include "object/ocean_renderer.h"
#include "object/skydome_renderer.h"
#include "object/terrain/terrain_system.h"
#include "render/renderer.h"
#include <nlohmann/json.hpp>
#include "settlers_assets.h"
#include "settlers_world.h"

using namespace engine3d;
using namespace settlers;

namespace {

constexpr const char* kFont = "times";
constexpr float kHudDepth = 0.93f;
constexpr float kTopBarH = 34.0f;

constexpr const char* kActPanLeft = "cam.left";
constexpr const char* kActPanRight = "cam.right";
constexpr const char* kActPanUp = "cam.up";
constexpr const char* kActPanDown = "cam.down";
constexpr const char* kActRotL = "cam.rot_left";
constexpr const char* kActRotR = "cam.rot_right";
constexpr const char* kActHome = "cam.home";
constexpr const char* kActZoomIn = "cam.zoom_in";
constexpr const char* kActZoomOut = "cam.zoom_out";
constexpr const char* kActEsc = "menu.back";
constexpr const char* kActRotateBuilding = "build.rotate";
constexpr const char* kActDemolish = "build.demolish";
constexpr const char* kActHalt = "unit.halt";
constexpr const char* kActHelp = "ui.help";
constexpr const char* kActSelectArmy = "unit.select_army";
constexpr const char* kActMuteMusic = "audio.mute_music";
constexpr const char* kActDebug = "debug.cycle";
constexpr const char* kActSlower = "time.slower";
constexpr const char* kActFaster = "time.faster";
constexpr const char* kActDebugDump = "debug.dump";
constexpr const char* kSoundDir = "assets/sound/settlers/";

using Color = std::array<float, 4>;

void pushQuitEvent() {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}

const char* stateText(const Unit& u, const World& w) {
    switch (u.state) {
    case UnitState::Idle: return u.type == UnitType::Settler ? "Czeka na zadanie" : "Na posterunku";
    case UnitState::Moving: return u.attackMove ? "Marsz bojowy" : "W marszu";
    case UnitState::GoBuild: return "Idzie na budowę";
    case UnitState::Building: return "Buduje";
    case UnitState::GoToWork: return "Idzie do pracy";
    case UnitState::WorkWait: return "Odpoczywa w pracy";
    case UnitState::GoGather: {
        if (u.building >= 0 && w.buildings[u.building].type == BuildingType::Farm) return "Idzie na pole";
        return "Idzie po surowiec";
    }
    case UnitState::Gathering: {
        if (u.building >= 0) {
            const auto t = w.buildings[u.building].type;
            if (t == BuildingType::Woodcutter) return "Ścina drzewo";
            if (t == BuildingType::Quarry) return "Kuje kamień";
            if (t == BuildingType::Farm) return "Pracuje w polu";
        }
        return "Zbiera";
    }
    case UnitState::ReturnHome: return "Niesie surowiec";
    case UnitState::Attack: return "Walczy";
    }
    return "";
}

Color hpColor(float frac) {
    frac = std::clamp(frac, 0.0f, 1.0f);
    return {std::min(1.0f, 2.0f * (1.0f - frac)), std::min(1.0f, 2.0f * frac), 0.15f, 1.0f};
}

enum class Mode { Menu, Game, Pause, GameOver };

// Pełna lista sterowania - wspólna dla okna "Sterowanie" i pomocy F1. Pusty klawisz = nagłówek sekcji.
struct ControlLine {
    const char* key;
    const char* desc;
};
const ControlLine kControls[] = {
    {"", "KAMERA"},
    {"W S A D / strzałki", "przesuwanie kamery"},
    {"kursor przy krawędzi", "przesuwanie kamery myszą"},
    {"Q / E", "obrót kamery"},
    {"środkowy przycisk", "obrót i pochylenie (przytrzymaj i ruszaj myszą)"},
    {"kółko, + / -, PgUp/PgDn", "przybliżanie i oddalanie"},
    {"Spacja", "kamera nad zamek"},
    {"LPM na minimapie", "przeskok kamery w wskazane miejsce"},
    {"", "JEDNOSTKI"},
    {"LPM", "zaznaczenie jednostki lub budynku"},
    {"LPM + przeciągnięcie", "zaznaczenie ramką (wojsko ma pierwszeństwo)"},
    {"Shift + LPM", "dodanie / usunięcie z zaznaczenia"},
    {"Tab", "zaznaczenie całego wojska"},
    {"PPM", "ruch lub atak na wskazany cel"},
    {"Ctrl + PPM", "marsz bojowy (atak na napotkanych wrogów)"},
    {"PPM na minimapie", "marsz wojska we wskazane miejsce"},
    {"H", "stój (przerwanie rozkazu)"},
    {"", "BUDOWA"},
    {"1 - 6", "Dom, Drwal, Kamieniarz, Farma, Koszary, Wieża"},
    {"LPM / PPM", "postawienie budynku / anulowanie"},
    {"R", "obrót budynku przed postawieniem"},
    {"Shift + LPM", "postawienie kilku budynków z rzędu"},
    {"Del", "zburzenie zaznaczonego budynku (zwrot 50%)"},
    {"", "INNE"},
    {"ESC", "pauza / powrót z okna"},
    {"F1", "pomoc na ekranie gry"},
    {"M", "muzyka wł. / wył."},
    {"[  /  ]", "wolniej / szybciej (x1, x2, x4, x8, x16)"},
    {"F3 / F9", "tryb debug / zrzut stanu do konsoli"},
};

// Ustawienia zapisywane w save/settlers_settings.json
struct Settings {
    bool musicOn = true;
    bool sfxOn = true;
    int musicVolume = 55;   // %
    int sfxVolume = 90;
    int masterVolume = 100;
    int windowW = 1440;
    int windowH = 900;
    bool fullscreen = false;
    int presentMode = 0;    // 0 FIFO, 1 Immediate, 2 Mailbox
    int syncMode = 0;       // 0 Standard, 1 Timeline, 2 Off
    int fpsLimit = 0;       // 0 = bez limitu
    bool shadows = true;
};
constexpr const char* kSettingsPath = "save/settlers_settings.json";

/**
 * Aplikacja gry: łączy silnik, świat gry, kamerę, sterowanie i interfejs.
 */
class SettlersApp {
public:
    bool initialize() {
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

    void run() { engine_.run(); }

private:
    // =================================================================================
    // Inicjalizacja
    // =================================================================================

    void setupInput() {
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

    void setupSkyAndWater() {
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

    // =================================================================================
    // Dźwięk
    // =================================================================================

    void setupAudio() {
        audio_ = engine_.getAudio();
        if (!audio_ || !audio_->isReady()) {
            LOG_WARN("Brak dźwięku - gra działa bez audio");
            return;
        }
        struct Spec {
            const char* name;
            int variants;      // 0 = pojedynczy plik name.ogg, N = name_0..name_{N-1}.ogg
            int maxInstances;
            float minInterval;
        };
        static const Spec specs[] = {
            {"chop", 4, 3, 0.15f},      {"mine", 5, 3, 0.15f},     {"hammer", 4, 3, 0.15f},
            {"sword", 5, 5, 0.05f},     {"hit_building", 3, 3, 0.1f}, {"bow", 2, 4, 0.08f},
            {"arrow_hit", 3, 4, 0.05f}, {"death", 3, 3, 0.1f},      {"collapse", 3, 2, 0.2f},
            {"alarm", 0, 1, 2.0f},      {"trained", 0, 2, 0.2f},    {"settler", 0, 1, 0.5f},
            {"ui_click", 0, 3, 0.03f},  {"ui_error", 0, 1, 0.25f},  {"complete", 0, 2, 0.3f},
            {"place", 0, 2, 0.1f},      {"select", 0, 2, 0.08f},    {"deliver", 0, 2, 0.3f},
            {"victory", 0, 1, 1.0f},    {"defeat", 0, 1, 1.0f},
        };
        int loaded = 0;
        for (const auto& s : specs) {
            if (s.variants == 0) {
                loaded += audio_->loadSound(s.name, std::string(kSoundDir) + "sfx/" + s.name + ".ogg") ? 1 : 0;
            } else {
                for (int i = 0; i < s.variants; ++i) {
                    loaded += audio_->loadSound(s.name, std::string(kSoundDir) + "sfx/" + s.name + "_" + std::to_string(i) + ".ogg") ? 1 : 0;
                }
            }
            audio_->setSoundLimits(s.name, s.maxInstances, s.minInterval);
        }
        audio_->setDistanceModel(70.0f, 520.0f);
        LOG_INFO("Wczytano {} plików dźwiękowych", loaded);
    }

    void sfx(const char* name, float volume = 1.0f) {
        if (audio_) audio_->play(name, volume);
    }

    void playMusicTrack(const std::string& file, bool loop, float fade = 2.5f) {
        if (!audio_ || !musicEnabled_) return;
        const std::string path = std::string(kSoundDir) + "music/" + file;
        if (audio_->currentMusic() == path && audio_->isMusicPlaying()) return;
        audio_->playMusic(path, loop, fade);
    }

    void updateAudio() {
        if (!audio_) {
            world_->soundEvents.clear();
            return;
        }
        // Słuchacz nad punktem, na który patrzy kamera - im dalej oddalona kamera, tym ciszej
        const Vec3 f = cam_.focus();
        audio_->setListener({f.x, f.y + cam_.distance() * 0.35f, f.z}, cam_.groundRight());

        // Zdarzenia z symulacji (w menu ściszone)
        const float scale = mode_ == Mode::Menu ? 0.35f : 1.0f;
        for (const auto& ev : world_->soundEvents) {
            if (ev.positional) {
                audio_->play3D(ev.name, ev.pos, ev.volume * scale);
            } else if (mode_ != Mode::Menu) {
                audio_->play(ev.name, ev.volume);
            }
        }
        world_->soundEvents.clear();

        // Muzyka zależna od sytuacji
        switch (mode_) {
        case Mode::Menu:
            playMusicTrack("menu_exploration.ogg", true);
            break;
        case Mode::Game:
        case Mode::Pause: {
            const float sinceCombat = world_->gameTime - world_->lastCombatTime[kPlayer];
            if (sinceCombat < 6.0f) {
                battleMusic_ = true;
            } else if (sinceCombat > 25.0f) {
                battleMusic_ = false;
            }
            if (battleMusic_) {
                playMusicTrack("battle.ogg", true, 1.5f);
            } else {
                // Spokojne utwory na zmianę, bez zapętlania
                static const char* peace[] = {"peace_harvest_season.ogg", "peace_market_day.ogg"};
                const std::string current = audio_->currentMusic();
                const bool peaceNow = current.find("peace_") != std::string::npos;
                if (!peaceNow || !audio_->isMusicPlaying()) {
                    if (peaceNow) peaceTrack_ = (peaceTrack_ + 1) % 2;
                    playMusicTrack(peace[peaceTrack_], false, 3.0f);
                }
            }
            break;
        }
        case Mode::GameOver:
            break;
        }
    }

    void toggleMusic() {
        musicEnabled_ = !musicEnabled_;
        settings_.musicOn = musicEnabled_;
        if (checkMusic_) checkMusic_->setChecked(musicEnabled_);
        saveSettings();
        if (!audio_) return;
        if (!musicEnabled_) {
            audio_->stopMusic(0.8f);
            world_->pushMessage("Muzyka wyłączona (M)", {0.85f, 0.85f, 0.85f, 1.0f});
        } else {
            world_->pushMessage("Muzyka włączona (M)", {0.85f, 0.85f, 0.85f, 1.0f});
        }
    }

    GuiButton* makeButton(GuiWindow* w, const std::string& name, int x, int y, int bw, int bh, const std::string& text,
                          std::function<void()> fn) {
        auto* b = new GuiButton(w, name, x, y, bw, bh);
        b->setText(text);
        b->setOnClick([this, fn = std::move(fn)]() {
            sfx("ui_click", 0.7f);
            fn();
        });
        return b;
    }

    void setupGui() {
        gui_ = std::make_shared<GuiMgr>();
        gui_->setTextDrawFn([this](const std::string& text, float x, float y, float scale, std::array<float, 4> color, float z) {
            engine_.setTextLabel(text, kFont, x, y, scale, color, z);
        });
        renderer_->addFeature(gui_);

        // Menu główne
        menuWindow_ = gui_->addWindow("Osadnicy", 0, 0, 340, 420);
        auto* title = new GuiStaticText(menuWindow_, "title", 10, 14);
        title->setText("Nowa rozgrywka");
        auto* diffLabel = new GuiStaticText(menuWindow_, "diff_label", 10, 52);
        diffLabel->setText("Poziom trudności:");
        comboDifficulty_ = new GuiComboBox(menuWindow_, "difficulty", 10, 64, 220);
        comboDifficulty_->addItem("Łatwy");
        comboDifficulty_->addItem("Normalny");
        comboDifficulty_->addItem("Trudny");
        comboDifficulty_->setSelected(1);
        makeButton(menuWindow_, "start", 10, 110, 220, 36, "Rozpocznij grę", [this]() { startNewGame(); });
        makeButton(menuWindow_, "demo", 10, 160, 220, 36, "Pokaz: AI kontra AI", [this]() { startNewGame(true); });
        makeButton(menuWindow_, "continue", 10, 210, 220, 36, "Kontynuuj", [this]() {
            if (gameStarted_ && world_->winner < 0) enterGame();
        });
        makeButton(menuWindow_, "settings", 10, 260, 220, 36, "Ustawienia", [this]() { openSubWindow(settingsWindow_, menuWindow_); });
        makeButton(menuWindow_, "controls", 10, 310, 220, 36, "Sterowanie", [this]() { openSubWindow(controlsWindow_, menuWindow_); });
        makeButton(menuWindow_, "exit", 10, 360, 220, 36, "Wyjście", []() { pushQuitEvent(); });

        // Pauza
        pauseWindow_ = gui_->addWindow("Pauza", 0, 0, 300, 320);
        makeButton(pauseWindow_, "resume", 10, 20, 260, 36, "Wróć do gry", [this]() { enterGame(); });
        btnSpeed_ = makeButton(pauseWindow_, "speed", 10, 70, 260, 36, "", [this]() { cycleGameSpeed(); });
        makeButton(pauseWindow_, "settings", 10, 120, 260, 36, "Ustawienia", [this]() { openSubWindow(settingsWindow_, pauseWindow_); });
        makeButton(pauseWindow_, "controls", 10, 170, 260, 36, "Sterowanie", [this]() { openSubWindow(controlsWindow_, pauseWindow_); });
        makeButton(pauseWindow_, "menu", 10, 220, 260, 36, "Menu główne", [this]() { showMenu(); });
        makeButton(pauseWindow_, "quit", 10, 270, 260, 36, "Wyjście z gry", []() { pushQuitEvent(); });
        updateSpeedButton();

        setupSettingsWindow();
        setupControlsWindow();

        // Koniec gry
        gameOverWindow_ = gui_->addWindow("Koniec gry", 0, 0, 380, 200);
        gameOverText_ = new GuiStaticText(gameOverWindow_, "result", 14, 30);
        gameOverStats_ = new GuiStaticText(gameOverWindow_, "stats", 14, 64);
        makeButton(gameOverWindow_, "again", 14, 100, 160, 36, "Nowa gra", [this]() { showMenu(); });
        makeButton(gameOverWindow_, "quit", 196, 100, 160, 36, "Wyjście", []() { pushQuitEvent(); });

        // Panel budowy
        static const BuildingType buildable[] = {BuildingType::House, BuildingType::Woodcutter, BuildingType::Quarry,
                                                 BuildingType::Farm, BuildingType::Barracks, BuildingType::Tower};
        buildWindow_ = gui_->addWindow("Budowa", 0, 0, 250, 6 * 40 + 8);
        for (int i = 0; i < 6; ++i) {
            const BuildingType t = buildable[i];
            const BuildingDef& def = buildingDef(t);
            const std::string label = std::to_string(i + 1) + ". " + def.name + "  (" + costText(def.cost) + ")";
            buildButtons_[i] = makeButton(buildWindow_, "b" + std::to_string(i), 8, 8 + i * 40, 234, 32, label,
                                          [this, t]() { beginPlacement(t); });
            buildTypes_[i] = t;
        }

        // Koszary
        barracksWindow_ = gui_->addWindow("Koszary", 0, 0, 250, 96);
        makeButton(barracksWindow_, "sword", 8, 8, 234, 34,
                   std::string("Miecznik (") + costText(unitDef(UnitType::Swordsman).cost) + ")",
                   [this]() { trainSelected(UnitType::Swordsman); });
        makeButton(barracksWindow_, "archer", 8, 52, 234, 34,
                   std::string("Łucznik (") + costText(unitDef(UnitType::Archer).cost) + ")",
                   [this]() { trainSelected(UnitType::Archer); });

        // Akcje budynku
        actionsWindow_ = gui_->addWindow("Akcje", 0, 0, 250, 50);
        makeButton(actionsWindow_, "demolish", 8, 8, 234, 34, "Zburz (Del, zwrot 50%)", [this]() { demolishSelected(); });

        layoutGui();
    }

    // =================================================================================
    // Ustawienia i sterowanie
    // =================================================================================

    void openSubWindow(GuiWindow* window, GuiWindow* returnTo) {
        if (returnTo) returnTo->setVisible(false);
        subReturn_ = returnTo;
        subWindow_ = window;
        window->setVisible(true);
        gui_->setActive(window->name());
        if (window == settingsWindow_) syncSettingsWidgets();
    }

    void closeSubWindow() {
        if (!subWindow_) return;
        if (subWindow_ == settingsWindow_) saveSettings();
        subWindow_->setVisible(false);
        subWindow_ = nullptr;
        if (subReturn_) {
            subReturn_->setVisible(true);
            gui_->setActive(subReturn_->name());
        }
    }

    GuiStaticText* label(GuiWindow* w, int x, int y, const std::string& t, const Color& c = {1, 1, 1, 1}) {
        static int counter = 0;
        auto* s = new GuiStaticText(w, "lbl" + std::to_string(++counter), x, y);
        s->setText(t);
        s->color_ = c;
        return s;
    }

    // Lista rozdzielczości mieszczących się na ekranie
    void buildResolutionList() {
        static const int modes[][2] = {{1024, 768}, {1280, 720}, {1280, 800}, {1366, 768}, {1440, 900}, {1600, 900},
                                       {1680, 1050}, {1920, 1080}, {1920, 1200}, {2560, 1440}, {3840, 2160}};
        uint32_t dw = 0, dh = 0;
        const bool known = engine_.getDisplaySize(dw, dh);
        resolutions_.clear();
        for (const auto& m : modes) {
            if (!known || (static_cast<uint32_t>(m[0]) <= dw && static_cast<uint32_t>(m[1]) <= dh)) {
                resolutions_.push_back({m[0], m[1]});
            }
        }
        if (resolutions_.empty()) resolutions_.push_back({1280, 720});
    }

    void setupSettingsWindow() {
        settingsWindow_ = gui_->addWindow("Ustawienia", 0, 0, 520, 600);
        GuiWindow* w = settingsWindow_;
        const Color header{1.0f, 0.9f, 0.6f, 1.0f};
        int y = 22;

        label(w, 12, y, "DŹWIĘK", header);
        y += 14;
        checkMusic_ = new GuiCheckBox(w, "music_on", 12, y);
        checkMusic_->setText("Muzyka");
        checkMusic_->setOnToggle([this](bool on) {
            settings_.musicOn = on;
            applyAudioSettings();
        });
        checkSfx_ = new GuiCheckBox(w, "sfx_on", 262, y);
        checkSfx_->setText("Efekty dźwiękowe");
        checkSfx_->setOnToggle([this](bool on) {
            settings_.sfxOn = on;
            applyAudioSettings();
        });
        y += 46;
        auto slider = [&](const char* name, int& value, GuiStaticText*& lbl, GuiTrackBar*& bar, const char* caption) {
            lbl = label(w, 12, y, "");
            bar = new GuiTrackBar(w, name, 250, y - 15, 250, 0, 100);
            bar->setOnChange([this, &value, lbl, caption](int v) {
                value = v;
                lbl->setText(std::string(caption) + ": " + std::to_string(v) + "%");
                applyAudioSettings();
            });
            y += 34;
        };
        slider("vol_music", settings_.musicVolume, lblMusicVol_, barMusicVol_, "Głośność muzyki");
        slider("vol_sfx", settings_.sfxVolume, lblSfxVol_, barSfxVol_, "Głośność efektów");
        slider("vol_master", settings_.masterVolume, lblMasterVol_, barMasterVol_, "Głośność ogólna");

        y += 10;
        label(w, 12, y, "GRAFIKA", header);
        y += 14;
        checkFullscreen_ = new GuiCheckBox(w, "fullscreen", 12, y);
        checkFullscreen_->setText("Pełny ekran");
        checkFullscreen_->setOnToggle([this](bool on) {
            settings_.fullscreen = on;
            engine_.setFullscreen(on);
        });
        checkShadows_ = new GuiCheckBox(w, "shadows", 262, y);
        checkShadows_->setText("Cienie");
        checkShadows_->setOnToggle([this](bool on) {
            settings_.shadows = on;
            engine_.setShadowsEnabled(on);
        });
        y += 44;

        // Combo boxy od dołu do góry w kolejności tworzenia, żeby rozwinięta lista przykrywała niższe
        const int comboY[4] = {y + 4, y + 60, y + 116, y + 172};
        label(w, 12, comboY[0] - 6, "Rozdzielczość okna");
        label(w, 12, comboY[1] - 6, "Synchronizacja pionowa");
        label(w, 12, comboY[2] - 6, "Synchronizacja GPU-CPU");
        label(w, 12, comboY[3] - 6, "Limit klatek (FPS)");

        comboFps_ = new GuiComboBox(w, "fps", 12, comboY[3], 300);
        for (const char* s : {"Bez limitu", "30 FPS", "60 FPS", "120 FPS", "144 FPS"}) comboFps_->addItem(s);
        comboFps_->setOnSelect([this](const std::string&) {
            static const int limits[] = {0, 30, 60, 120, 144};
            settings_.fpsLimit = limits[std::clamp(comboFps_->selectedIdx(), 0, 4)];
            engine_.setFrameRateLimit(settings_.fpsLimit);
        });
        comboSync_ = new GuiComboBox(w, "sync", 12, comboY[2], 300);
        for (const char* s : {"Standard (fence)", "Timeline (semafory)", "Wyłączona"}) comboSync_->addItem(s);
        comboSync_->setOnSelect([this](const std::string&) {
            settings_.syncMode = comboSync_->selectedIdx();
            applySyncMode();
        });
        comboPresent_ = new GuiComboBox(w, "present", 12, comboY[1], 300);
        for (const char* s : {"FIFO - VSync włączony", "Immediate - VSync wyłączony", "Mailbox - potrójne buforowanie"})
            comboPresent_->addItem(s);
        comboPresent_->setOnSelect([this](const std::string&) {
            settings_.presentMode = comboPresent_->selectedIdx();
            applyPresentMode();
        });
        buildResolutionList();
        comboResolution_ = new GuiComboBox(w, "resolution", 12, comboY[0], 300);
        for (const auto& r : resolutions_) comboResolution_->addItem(std::to_string(r[0]) + " x " + std::to_string(r[1]));
        comboResolution_->setOnSelect([this](const std::string&) {
            const int i = std::clamp(comboResolution_->selectedIdx(), 0, static_cast<int>(resolutions_.size()) - 1);
            settings_.windowW = resolutions_[i][0];
            settings_.windowH = resolutions_[i][1];
            if (engine_.isFullscreen()) {
                world_->pushMessage("Rozdzielczość okna zadziała po wyłączeniu pełnego ekranu", {0.9f, 0.9f, 0.6f, 1.0f});
            }
            engine_.setWindowSize(static_cast<uint32_t>(settings_.windowW), static_cast<uint32_t>(settings_.windowH));
        });

        makeButton(w, "defaults", 12, 548, 200, 36, "Domyślne", [this]() {
            settings_ = Settings{};
            applySettings(false);
            syncSettingsWidgets();
        });
        makeButton(w, "back", 300, 548, 200, 36, "Wróć (ESC)", [this]() { closeSubWindow(); });
        w->setVisible(false);
    }

    void syncSettingsWidgets() {
        if (!settingsWindow_) return;
        checkMusic_->setChecked(settings_.musicOn);
        checkSfx_->setChecked(settings_.sfxOn);
        barMusicVol_->setValue(settings_.musicVolume);
        barSfxVol_->setValue(settings_.sfxVolume);
        barMasterVol_->setValue(settings_.masterVolume);
        lblMusicVol_->setText("Głośność muzyki: " + std::to_string(settings_.musicVolume) + "%");
        lblSfxVol_->setText("Głośność efektów: " + std::to_string(settings_.sfxVolume) + "%");
        lblMasterVol_->setText("Głośność ogólna: " + std::to_string(settings_.masterVolume) + "%");
        checkFullscreen_->setChecked(engine_.isFullscreen());
        checkShadows_->setChecked(settings_.shadows);
        comboPresent_->setSelected(settings_.presentMode);
        comboSync_->setSelected(settings_.syncMode);
        static const int limits[] = {0, 30, 60, 120, 144};
        for (int i = 0; i < 5; ++i) {
            if (limits[i] == settings_.fpsLimit) comboFps_->setSelected(i);
        }
        // Rozdzielczość: najbliższa z listy względem aktualnego okna
        int best = 0;
        long bestDiff = -1;
        for (size_t i = 0; i < resolutions_.size(); ++i) {
            const long d = std::labs(resolutions_[i][0] - static_cast<long>(renderer_->width())) +
                           std::labs(resolutions_[i][1] - static_cast<long>(renderer_->height()));
            if (bestDiff < 0 || d < bestDiff) {
                bestDiff = d;
                best = static_cast<int>(i);
            }
        }
        comboResolution_->setSelected(best);
    }

    void setupControlsWindow() {
        controlsWindow_ = gui_->addWindow("Sterowanie", 0, 0, 640, 640);
        GuiWindow* w = controlsWindow_;
        int y = 22;
        for (const auto& c : kControls) {
            if (c.key[0] == '\0') {
                if (y > 22) y += 6;
                label(w, 12, y, c.desc, {1.0f, 0.9f, 0.6f, 1.0f});
            } else {
                label(w, 24, y, c.key, {0.75f, 0.9f, 1.0f, 1.0f});
                label(w, 250, y, c.desc);
            }
            y += 20;
        }
        makeButton(w, "back", 220, 592, 200, 36, "Wróć (ESC)", [this]() { closeSubWindow(); });
        w->setVisible(false);
    }

    void applyAudioSettings() {
        musicEnabled_ = settings_.musicOn;
        if (!audio_) return;
        audio_->setMasterVolume(settings_.masterVolume / 100.0f);
        audio_->setMusicVolume(settings_.musicVolume / 100.0f);
        audio_->setSfxVolume(settings_.sfxOn ? settings_.sfxVolume / 100.0f : 0.0f);
        if (!settings_.musicOn) audio_->stopMusic(0.6f);
    }

    void applyPresentMode() {
        static const PresentMode modes[] = {PresentMode::Fifo, PresentMode::Immediate, PresentMode::Mailbox};
        engine_.setPresentMode(modes[std::clamp(settings_.presentMode, 0, 2)]);
    }

    void applySyncMode() {
        static const SynchronizationMode modes[] = {SynchronizationMode::Standard, SynchronizationMode::Timeline,
                                                    SynchronizationMode::Off};
        engine_.setSynchronizationMode(modes[std::clamp(settings_.syncMode, 0, 2)]);
    }

    void applySettings(bool startup) {
        applyAudioSettings();
        engine_.setShadowsEnabled(settings_.shadows);
        engine_.setFrameRateLimit(settings_.fpsLimit);
        if (settings_.presentMode != 0) applyPresentMode();
        if (settings_.syncMode != 0) applySyncMode();
        if (!startup) {
            applyPresentMode();
            applySyncMode();
        }
        if (!settings_.fullscreen &&
            (static_cast<int>(renderer_->width()) != settings_.windowW || static_cast<int>(renderer_->height()) != settings_.windowH)) {
            engine_.setWindowSize(static_cast<uint32_t>(settings_.windowW), static_cast<uint32_t>(settings_.windowH));
        }
        if (engine_.isFullscreen() != settings_.fullscreen) engine_.setFullscreen(settings_.fullscreen);
    }

    void loadSettings() {
        // Testy automatyczne startują zawsze na ustawieniach domyślnych (niezależnie od pliku gracza)
        if (std::getenv("SETTLERS_UITEST") || std::getenv("SETTLERS_AUTOPLAY")) return;
        std::ifstream f(kSettingsPath);
        if (!f) return;
        try {
            nlohmann::json j;
            f >> j;
            Settings s;
            s.musicOn = j.value("musicOn", s.musicOn);
            s.sfxOn = j.value("sfxOn", s.sfxOn);
            s.musicVolume = std::clamp(j.value("musicVolume", s.musicVolume), 0, 100);
            s.sfxVolume = std::clamp(j.value("sfxVolume", s.sfxVolume), 0, 100);
            s.masterVolume = std::clamp(j.value("masterVolume", s.masterVolume), 0, 100);
            s.windowW = std::clamp(j.value("windowW", s.windowW), 640, 7680);
            s.windowH = std::clamp(j.value("windowH", s.windowH), 480, 4320);
            s.fullscreen = j.value("fullscreen", s.fullscreen);
            s.presentMode = std::clamp(j.value("presentMode", s.presentMode), 0, 2);
            s.syncMode = std::clamp(j.value("syncMode", s.syncMode), 0, 2);
            s.fpsLimit = std::clamp(j.value("fpsLimit", s.fpsLimit), 0, 1000);
            s.shadows = j.value("shadows", s.shadows);
            settings_ = s;
            LOG_INFO("Wczytano ustawienia z {}", kSettingsPath);
        } catch (const std::exception& ex) {
            LOG_WARN("Nie udało się wczytać ustawień {}: {}", kSettingsPath, ex.what());
        }
    }

    void saveSettings() {
        if (uiTest_ || autoplaySeconds_ > 0.0f) return;   // testy nie nadpisują ustawień gracza
        settings_.fullscreen = engine_.isFullscreen();
        nlohmann::json j = {
            {"musicOn", settings_.musicOn},       {"sfxOn", settings_.sfxOn},
            {"musicVolume", settings_.musicVolume}, {"sfxVolume", settings_.sfxVolume},
            {"masterVolume", settings_.masterVolume}, {"windowW", settings_.windowW},
            {"windowH", settings_.windowH},       {"fullscreen", settings_.fullscreen},
            {"presentMode", settings_.presentMode}, {"syncMode", settings_.syncMode},
            {"fpsLimit", settings_.fpsLimit},     {"shadows", settings_.shadows},
        };
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(kSettingsPath).parent_path(), ec);
        std::ofstream f(kSettingsPath);
        if (f) {
            f << j.dump(2);
        } else {
            LOG_WARN("Nie można zapisać ustawień do {}", kSettingsPath);
        }
    }

    void layoutGui() {
        const int w = static_cast<int>(renderer_->width());
        const int h = static_cast<int>(renderer_->height());
        lastW_ = w;
        lastH_ = h;
        menuWindow_->setPosition(w / 2 - 170, h / 2 - 175);
        pauseWindow_->setPosition(w / 2 - 150, h / 2 - 160);
        settingsWindow_->setPosition(w / 2 - settingsWindow_->width() / 2, std::max(40, h / 2 - settingsWindow_->height() / 2));
        controlsWindow_->setPosition(w / 2 - controlsWindow_->width() / 2, std::max(40, h / 2 - controlsWindow_->height() / 2));
        gameOverWindow_->setPosition(w / 2 - 190, h / 2 - 100);
        buildWindow_->setPosition(14, h - buildWindow_->height() - 14);
        barracksWindow_->setPosition(14 + 250 + 20, h - 96 - 14 - 64);
        actionsWindow_->setPosition(14 + 250 + 20, h - 50 - 14);
    }

    // =================================================================================
    // Cykl życia świata
    // =================================================================================

    void newWorld(uint32_t seed, Difficulty diff) {
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
    void startNewGame(bool spectator = false) {
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

    // --- Prędkość czasu ---
    static constexpr int kSpeeds[] = {1, 2, 4, 8, 16};

    void changeGameSpeed(int dir) {
        int idx = 0;
        for (int i = 0; i < 5; ++i) {
            if (kSpeeds[i] <= gameSpeed_) idx = i;
        }
        idx = std::clamp(idx + dir, 0, 4);
        gameSpeed_ = kSpeeds[idx];
        updateSpeedButton();
        if (world_) world_->pushMessage("Prędkość gry: x" + std::to_string(gameSpeed_), {0.8f, 0.95f, 1.0f, 1.0f});
    }

    void cycleGameSpeed() {
        if (gameSpeed_ >= 16) {
            gameSpeed_ = 1;
            updateSpeedButton();
            if (world_) world_->pushMessage("Prędkość gry: x1", {0.8f, 0.95f, 1.0f, 1.0f});
        } else {
            changeGameSpeed(+1);
        }
    }

    void updateSpeedButton() {
        if (btnSpeed_) btnSpeed_->setText("Prędkość gry: x" + std::to_string(gameSpeed_) + "  ([ / ])");
    }

    SceneManager* sceneManager() { return engine_.getSceneManager(); }

    void hideAllWindows() {
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

    void showMenu() {
        mode_ = Mode::Menu;
        hideAllWindows();
        menuWindow_->setVisible(true);
        gui_->setActive(menuWindow_->name());
        gui_->enableCursor(true);
        if (world_ && gameStarted_ && world_->winner >= 0) {
            gameStarted_ = false;
        }
    }

    void enterGame() {
        mode_ = Mode::Game;
        hideAllWindows();
        buildWindow_->setVisible(!spectator_);
        gui_->enableCursor(true);
    }

    void showPause() {
        mode_ = Mode::Pause;
        cancelPlacement();
        hideAllWindows();
        pauseWindow_->setVisible(true);
        gui_->setActive(pauseWindow_->name());
    }

    void showGameOver() {
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

    // =================================================================================
    // Pętla gry
    // =================================================================================

    void update(float dt, Engine& e) {
        const auto& in = e.input();
        if (static_cast<int>(renderer_->width()) != lastW_ || static_cast<int>(renderer_->height()) != lastH_) {
            layoutGui();
        }
        screenW_ = static_cast<float>(renderer_->width());
        screenH_ = static_cast<float>(renderer_->height());

        if (e.actionPressed(kActHelp)) showHelp_ = !showHelp_;
        if (e.actionPressed(kActMuteMusic)) toggleMusic();
        if (e.actionPressed(kActDebug)) {
            debugMode_ = (debugMode_ + 1) % 3;
            static const char* names[] = {"Debug: wyłączony", "Debug: ścieżki, stany, wejścia (F3)", "Debug: + siatka kolizji (F3)"};
            world_->pushMessage(names[debugMode_], {0.7f, 0.9f, 1.0f, 1.0f});
        }
        if (e.actionPressed(kActDebugDump)) dumpDebug();

        gui_->update(in, *e.getPlatform(), screenW_, screenH_);

        // ESC w oknie ustawień / sterowania wraca do menu, z którego je otwarto
        const bool escConsumed = subWindow_ && e.actionPressed(kActEsc);
        if (escConsumed) closeSubWindow();

        switch (mode_) {
        case Mode::Menu:
            updateMenuCamera(dt);
            world_->update(dt);
            if (e.actionPressed(kActEsc) && !escConsumed && !subWindow_) {
                if (gameStarted_) enterGame(); else pushQuitEvent();
            }
            break;
        case Mode::Game: {
            const bool wasPlacing = placing_;
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
            if (e.actionPressed(kActEsc) && !escConsumed && !subWindow_) enterGame();
            break;
        case Mode::GameOver:
            updateCameraInput(dt, e, in);
            world_->update(dt);
            break;
        }

        cam_.update(dt, *camera_);
        if (uiTest_) runUiTestStep();
        updateSky(dt);
        updateAudio();
        cleanupSelection();
        updateContextWindows();
        drawHud(e, in);
        updateDebugNavMesh(dt);
        if (debugMode_ > 0 && mode_ != Mode::Menu) drawDebug(in);
        gui_->renderGui();
    }

    // --- Test interfejsu (zdarzenia SDL trafiają do Platform jak prawdziwe wejście) ---
    static void pushMouseMotion(float x, float y) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.x = x;
        ev.motion.y = y;
        SDL_PushEvent(&ev);
    }
    static void pushMouseButton(Uint8 button, bool down) {
        SDL_Event ev{};
        ev.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        ev.button.button = button;
        SDL_PushEvent(&ev);
    }
    static void pushKey(SDL_Scancode sc, bool down) {
        SDL_Event ev{};
        ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        ev.key.scancode = sc;
        SDL_PushEvent(&ev);
    }
    void uiCheck(const char* name, bool ok) {
        LOG_INFO("[uitest] {}: {}", name, ok ? "OK" : "BŁĄD");
        if (!ok) ++uiFailures_;
    }

    // Tłum wojska na drodze budowniczych, plac bez dojścia, budynek postawiony na jednostkach.
    void crowdAndAccessTest() {
        World& w = *world_;
        w.aiEnabled = false;
        w.factions[kPlayer].res = {2000, 2000, 2000};
        const int castle = w.factions[kPlayer].castle;
        const Vec3 door = w.buildings[castle].door();

        // 1) Plac budowy ~45 jednostek przed bramą, a między nimi zwarty blok 20 żołnierzy
        Vec3 site{};
        bool found = false;
        for (int i = 0; i < 200 && !found; ++i) {
            const float a = i * 0.31f;
            const Vec3 p = w.snapToGrid(add(door, {std::sin(a) * 45.0f, 0.0f, std::cos(a) * 45.0f}));
            if (w.canPlace(BuildingType::House, p, kPlayer)) {
                site = p;
                found = true;
            }
        }
        uiCheck("znaleziono miejsce na test tłumu", found);
        if (found) {
            const Vec3 mid = lerp(door, site, 0.55f);
            std::vector<int> soldiers;
            for (int i = 0; i < 20; ++i) {
                soldiers.push_back(w.debugSpawnUnit(UnitType::Swordsman, kPlayer,
                                                    {mid.x + (i % 5 - 2) * 3.5f, 0.0f, mid.z + (i / 5 - 2) * 3.5f}));
            }
            const int b = w.placeBuilding(BuildingType::House, site, kPlayer, false);
            float t = 0.0f;
            while (t < 120.0f && w.buildings[b].alive && !w.buildings[b].constructed) {
                w.update(0.1f);
                t += 0.1f;
            }
            LOG_INFO("[uitest] dom za tłumem wojska ukończony po {:.0f} s", t);
            uiCheck("budowniczowie przechodzą przez tłum wojska", w.buildings[b].constructed && t < 60.0f);
        }

        // 2) Teren bez dojścia z zamku: szukamy przechodniej komórki w innym obszarze niż brama zamku
        const auto& nav = w.nav();
        const int castleRegion = nav.regionAt(door.x, door.z);
        Vec3 isolated{};
        bool haveIsolated = false;
        for (int cz = 10; cz < nav.height() - 10 && !haveIsolated; cz += 2) {
            for (int cx = 10; cx < nav.width() - 10 && !haveIsolated; cx += 2) {
                const Vec3 c = nav.cellCenter(cx, cz);
                const int r = nav.regionAt(c.x, c.z);
                if (r < 0 || r == castleRegion) continue;
                // obszar musi pomieścić dom z wejściem
                if (!nav.isAreaFree(c.x - 9, c.z - 9, c.x + 9, c.z + 13) || nav.regionAt(c.x, c.z + 10) != r) continue;
                isolated = w.snapToGrid(c);
                haveIsolated = true;
            }
        }
        if (!haveIsolated) {
            LOG_INFO("[uitest] na tej mapie nie ma odciętych obszarów - test dojścia pominięty");
        } else {
            std::string reason;
            // canPlace odrzuca też "poza terytorium" - sprawdzamy sam warunek dojścia
            const bool sameRegion = nav.sameRegion(door, add(isolated, {0.0f, 0.0f, 10.0f}));
            uiCheck("obszar odcięty od zamku wykryty", !sameRegion);
            const int b = w.placeBuilding(BuildingType::House, isolated, kPlayer, false);
            for (int i = 0; i < 400 && !w.buildings[b].unreachable; ++i) w.update(0.1f);
            uiCheck("plac budowy bez dojścia jest oznaczony", w.buildings[b].unreachable);
            w.demolish(b);
        }
        // canPlace z powodem "Brak dojścia" (dla obszaru odciętego w zasięgu terytorium)
        {
            std::string reason;
            bool sawAccess = false;
            const Vec3 c = w.buildings[castle].pos;
            for (int i = 0; i < 4000 && !sawAccess; ++i) {
                const float a = i * 0.0731f, r = 40.0f + (i % 37) * 4.0f;
                const Vec3 p = w.snapToGrid(add(c, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
                if (!w.canPlace(BuildingType::House, p, kPlayer, &reason)) sawAccess = sawAccess || reason.find("dojścia") != std::string::npos;
            }
            LOG_INFO("[uitest] canPlace zgłosił 'Brak dojścia' w terytorium: {}", sawAccess ? "tak" : "nie (brak odciętych miejsc w zasięgu)");
        }

        // 3) Budynek postawiony na stojącej jednostce - jednostka wychodzi na zewnątrz
        Vec3 spot{};
        bool ok = false;
        for (int i = 0; i < 300 && !ok; ++i) {
            const float a = i * 0.53f, r = 50.0f + (i % 9) * 8.0f;
            const Vec3 p = w.snapToGrid(add(w.buildings[castle].pos, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
            if (w.canPlace(BuildingType::Barracks, p, kPlayer)) {
                spot = p;
                ok = true;
            }
        }
        if (ok) {
            const int u = w.debugSpawnUnit(UnitType::Archer, kPlayer, spot);
            w.placeBuilding(BuildingType::Barracks, spot, kPlayer, true);
            for (int i = 0; i < 40; ++i) w.update(0.1f);
            uiCheck("jednostka wychodzi z budynku postawionego na niej", w.nav().isWalkableWorld(w.units[u].pos.x, w.units[u].pos.z));
        }
    }

    // Naprawa budynku, leczenie jednostki i sygnalizacja braku surowca.
    void repairHealResourceTest() {
        World& w = *world_;
        // Naprawa: uszkodzony dom
        int house = -1;
        for (size_t i = 0; i < w.buildings.size(); ++i) {
            const Building& b = w.buildings[i];
            if (b.alive && b.faction == kPlayer && b.constructed && b.type == BuildingType::House) house = static_cast<int>(i);
        }
        int soldier = -1;
        for (size_t i = 0; i < w.units.size(); ++i) {
            if (w.units[i].alive && w.units[i].faction == kPlayer && w.units[i].type == UnitType::Swordsman) soldier = static_cast<int>(i);
        }
        const float houseMax = buildingDef(BuildingType::House).maxHp;
        w.buildings[house].hp = houseMax * 0.35f;
        w.buildings[house].lastDamagedTime = w.gameTime;
        w.units[soldier].hp = 20.0f;
        w.units[soldier].lastDamagedTime = w.gameTime;
        const int woodBefore = w.factions[kPlayer].res.wood;
        bool sawRepairer = false;
        for (int i = 0; i < 1500; ++i) {
            w.update(0.1f);
            sawRepairer = sawRepairer || w.repairersOf(house) > 0;
        }
        LOG_INFO("[uitest] dom: {:.0f}/{:.0f} PŻ, zużyte drewno: {}, żołnierz: {:.0f} PŻ", w.buildings[house].hp, houseMax,
                 woodBefore - w.factions[kPlayer].res.wood, w.units[soldier].hp);
        uiCheck("osadnicy naprawiają uszkodzony budynek", sawRepairer && w.buildings[house].hp >= houseMax * 0.99f);
        uiCheck("jednostka poza walką się leczy", w.units[soldier].hp >= unitDef(UnitType::Swordsman).maxHp * 0.99f);

        // Brak surowca: drwal bez drzew w zasięgu
        int woodcutter = -1;
        for (size_t i = 0; i < w.buildings.size(); ++i) {
            const Building& b = w.buildings[i];
            if (b.alive && b.faction == kPlayer && b.constructed && b.type == BuildingType::Woodcutter) woodcutter = static_cast<int>(i);
        }
        for (auto& n : w.nodes) {
            if (n.kind == NodeKind::Tree) n.amount = 0;
        }
        bool msg = false;
        for (int i = 0; i < 600; ++i) {
            w.update(0.1f);
            for (const auto& m : w.messages) msg = msg || m.text.find("BRAK DRZEW") != std::string::npos;
        }
        uiCheck("drwal sygnalizuje brak drzew", woodcutter >= 0 && w.buildings[woodcutter].lacksResources);
        uiCheck("komunikat o braku drzew", msg);
    }

    // Stawia po kilka placów budowy każdego typu i symuluje świat - wszystkie muszą zostać ukończone.
    bool constructionStressTest() {
        World& w = *world_;
        w.aiEnabled = false;
        w.factions[kPlayer].res = {5000, 5000, 5000};
        const BuildingType types[] = {BuildingType::Tower, BuildingType::House, BuildingType::Woodcutter, BuildingType::Quarry,
                                      BuildingType::Farm, BuildingType::Barracks};
        std::vector<int> sites;
        const Vec3 c = w.buildings[w.factions[kPlayer].castle].pos;
        int attempt = 0;
        for (int round = 0; round < 3; ++round) {
            for (BuildingType t : types) {
                for (int i = 0; i < 300; ++i, ++attempt) {
                    const float a = attempt * 2.399f, r = 40.0f + static_cast<float>((attempt * 7) % 120);
                    const Vec3 p = w.snapToGrid(add(c, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
                    if (w.canPlace(t, p, kPlayer)) {
                        sites.push_back(w.placeBuilding(t, p, kPlayer, false, (attempt % 4) * PI * 0.5f));
                        break;
                    }
                }
            }
        }
        for (int step = 0; step < 12000; ++step) {
            w.update(0.1f);
            bool done = true;
            for (int id : sites) done = done && (!w.buildings[id].alive || w.buildings[id].constructed);
            if (done) break;
        }
        int unfinished = 0;
        for (int id : sites) {
            const Building& b = w.buildings[id];
            if (b.alive && !b.constructed) {
                ++unfinished;
                LOG_INFO("[uitest]   nieukończony: {} postęp={}%", buildingDef(b.type).name, static_cast<int>(b.progress * 100));
            }
        }
        LOG_INFO("[uitest] place budowy: {}, nieukończone po {} s gry: {}", sites.size(), static_cast<int>(w.gameTime), unfinished);
        return !sites.empty() && unfinished == 0;
    }

    void runUiTestStep() {
        const int f = uiFrame_++;
        World& w = *world_;
        const int castle = w.factions[kPlayer].castle;
        auto project = [&](const Vec3& p, float& sx, float& sy) { return worldToScreen(*camera_, p, screenW_, screenH_, sx, sy); };

        if (f == 20) {
            // Picking: promień z ekranowej pozycji zamku musi trafić w zamek i w teren obok niego
            float sx, sy;
            const Vec3 c = add(w.buildings[castle].pos, {0.0f, 8.0f, 0.0f});
            const bool vis = project(c, sx, sy);
            const Ray ray = screenPointToRay(*camera_, sx, sy, screenW_, screenH_);
            uiCheck("worldToScreen/screenPointToRay -> pickBuilding(zamek)", vis && w.pickBuilding(ray) == castle);
            Vec3 hit;
            float gx, gy;
            const Vec3 g = add(w.buildings[castle].pos, {40.0f, 0.0f, 40.0f});
            project({g.x, w.heightAt(g.x, g.z), g.z}, gx, gy);
            const bool ground = w.pickGround(screenPointToRay(*camera_, gx, gy, screenW_, screenH_), hit);
            uiCheck("Terrain::raycast trafia w punkt terenu", ground && std::abs(hit.x - g.x) < 3.0f && std::abs(hit.z - g.z) < 3.0f);

            // Szukamy miejsca na Dom i wchodzimy w tryb budowy klawiszem "1"
            uiTarget_ = {};
            for (int i = 0; i < 400 && uiTarget_.y == 0.0f; ++i) {
                const float a = i * 0.37f, r = 35.0f + (i % 10) * 6.0f;
                const Vec3 p = w.snapToGrid(add(w.buildings[castle].pos, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
                float px, py;
                if (w.canPlace(BuildingType::House, p, kPlayer) && project(p, px, py) && px > 300 && px < screenW_ - 300 &&
                    py > 100 && py < screenH_ - 250) {
                    uiTarget_ = p;
                    uiTarget_.y = std::max(0.01f, p.y);
                }
            }
            uiSitesBefore_ = 0;
            for (const auto& b : w.buildings) uiSitesBefore_ += (b.alive && b.faction == kPlayer) ? 1 : 0;
            pushKey(SDL_SCANCODE_1, true);
        } else if (f == 22) {
            pushKey(SDL_SCANCODE_1, false);
        } else if (f == 24) {
            uiCheck("klawisz 1 włącza tryb budowy", placing_ && placeType_ == BuildingType::House);
            float sx, sy;
            project(uiTarget_, sx, sy);
            pushMouseMotion(sx, sy);
        } else if (f == 28) {
            pushMouseButton(SDL_BUTTON_LEFT, true);
        } else if (f == 30) {
            pushMouseButton(SDL_BUTTON_LEFT, false);
        } else if (f == 34) {
            int sites = 0;
            for (const auto& b : w.buildings) sites += (b.alive && b.faction == kPlayer) ? 1 : 0;
            uiCheck("LPM stawia plac budowy", sites == uiSitesBefore_ + 1 && !placing_);
        } else if (f == 40) {
            // Ramka zaznaczenia wokół żołnierzy gracza
            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
            for (const auto& u : w.units) {
                if (!u.alive || u.faction != kPlayer || u.type == UnitType::Settler) continue;
                float sx, sy;
                if (!project(add(u.pos, {0.0f, 3.0f, 0.0f}), sx, sy)) continue;
                minX = std::min(minX, sx); minY = std::min(minY, sy);
                maxX = std::max(maxX, sx); maxY = std::max(maxY, sy);
            }
            uiBox_ = {minX - 15.0f, minY - 15.0f, maxX + 15.0f, maxY + 15.0f};
            pushMouseMotion(uiBox_[0], uiBox_[1]);
        } else if (f == 43) {
            pushMouseButton(SDL_BUTTON_LEFT, true);
        } else if (f == 46) {
            pushMouseMotion(uiBox_[2], uiBox_[3]);
        } else if (f == 49) {
            pushMouseButton(SDL_BUTTON_LEFT, false);
        } else if (f == 53) {
            uiCheck("ramka zaznacza wojsko", !selectedSoldiers().empty());
            // Rozkaz ruchu PPM
            uiMoveTarget_ = add(w.buildings[castle].pos, {0.0f, 0.0f, -45.0f});
            uiMoveTarget_.y = w.heightAt(uiMoveTarget_.x, uiMoveTarget_.z);
            float sx, sy;
            project(uiMoveTarget_, sx, sy);
            pushMouseMotion(sx, sy);
        } else if (f == 56) {
            pushMouseButton(SDL_BUTTON_RIGHT, true);
        } else if (f == 58) {
            pushMouseButton(SDL_BUTTON_RIGHT, false);
        } else if (f == 62) {
            bool ordered = !selectedSoldiers().empty();
            for (int id : selectedSoldiers()) {
                const Unit& u = w.units[id];
                if (std::abs(u.guardPos.x - uiMoveTarget_.x) > 25.0f || std::abs(u.guardPos.z - uiMoveTarget_.z) > 25.0f) ordered = false;
            }
            uiCheck("PPM wydaje rozkaz marszu", ordered);
            // Kliknięcie w zamek zaznacza budynek
            float sx, sy;
            project(add(w.buildings[castle].pos, {0.0f, 8.0f, 0.0f}), sx, sy);
            pushMouseMotion(sx, sy);
        } else if (f == 65) {
            pushMouseButton(SDL_BUTTON_LEFT, true);
        } else if (f == 67) {
            pushMouseButton(SDL_BUTTON_LEFT, false);
        } else if (f == 70) {
            uiCheck("LPM na zamku zaznacza budynek", selectedBuilding_ == castle);
            // Kółko myszy z ułamkowymi wartościami (precyzyjna mysz / touchpad)
            float sx, sy;
            project(add(w.buildings[castle].pos, {60.0f, 0.0f, 60.0f}), sx, sy);
            pushMouseMotion(std::clamp(sx, 400.0f, screenW_ - 400.0f), std::clamp(sy, 200.0f, screenH_ - 300.0f));
            uiDistBefore_ = cam_.distance();
            for (int i = 0; i < 4; ++i) {
                SDL_Event ev{};
                ev.type = SDL_EVENT_MOUSE_WHEEL;
                ev.wheel.y = 0.4f;
                SDL_PushEvent(&ev);
            }
        } else if (f == 90) {
            uiCheck("kółko myszy przybliża kamerę", cam_.distance() < uiDistBefore_ * 0.9f);
            // Podpowiedź brakujących surowców: zerujemy drewno i najeżdżamy na przycisk "Dom"
            world_->factions[kPlayer].res.wood = 0;
            pushMouseMotion(static_cast<float>(buildWindow_->x() + 60), static_cast<float>(buildWindow_->y() + 20));
        } else if (f == 94) {
            uiCheck("podpowiedź pokazuje brakujące surowce", hoverMissing_.find("20 drewna") != std::string::npos);
            LOG_INFO("[uitest] podpowiedź: \"{}\"", hoverMissing_);
            crowdAndAccessTest();
            uiCheck("każdy typ budynku zostaje ukończony przez osadników", constructionStressTest());
            repairHealResourceTest();
            showPause();
            uiOrigW_ = static_cast<int>(renderer_->width());
            uiOrigH_ = static_cast<int>(renderer_->height());
        } else if (f == 98) {
            // Pauza -> "Ustawienia"
            pushMouseMotion(static_cast<float>(pauseWindow_->x() + 140), static_cast<float>(pauseWindow_->y() + 138));
        } else if (f == 100) {
            pushMouseButton(SDL_BUTTON_LEFT, true);
        } else if (f == 102) {
            pushMouseButton(SDL_BUTTON_LEFT, false);
        } else if (f == 105) {
            uiCheck("przycisk Ustawienia otwiera okno ustawień", subWindow_ == settingsWindow_ && settingsWindow_->isVisible());
            uiMusicBefore_ = settings_.musicOn;
            // checkbox "Muzyka"
            pushMouseMotion(static_cast<float>(settingsWindow_->x() + 20), static_cast<float>(settingsWindow_->y() + 45));
        } else if (f == 107) {
            pushMouseButton(SDL_BUTTON_LEFT, true);
        } else if (f == 109) {
            pushMouseButton(SDL_BUTTON_LEFT, false);
        } else if (f == 112) {
            uiCheck("checkbox wyłącza/włącza muzykę", settings_.musicOn != uiMusicBefore_ && musicEnabled_ == settings_.musicOn);
            // suwak głośności muzyki na ~20%
            pushMouseMotion(static_cast<float>(settingsWindow_->x() + 300), static_cast<float>(settingsWindow_->y() + 77));
        } else if (f == 114) {
            pushMouseButton(SDL_BUTTON_LEFT, true);
        } else if (f == 116) {
            pushMouseButton(SDL_BUTTON_LEFT, false);
        } else if (f == 119) {
            const bool ok = settings_.musicVolume >= 15 && settings_.musicVolume <= 25 &&
                            (!audio_ || std::abs(audio_->musicVolume() - settings_.musicVolume / 100.0f) < 0.001f);
            LOG_INFO("[uitest] głośność muzyki po kliknięciu suwaka: {}%", settings_.musicVolume);
            uiCheck("suwak zmienia głośność muzyki", ok);
            // combobox rozdzielczości - otwarcie
            pushMouseMotion(static_cast<float>(settingsWindow_->x() + 60), static_cast<float>(settingsWindow_->y() + 266));
        } else if (f == 121) {
            pushMouseButton(SDL_BUTTON_LEFT, true);
        } else if (f == 123) {
            pushMouseButton(SDL_BUTTON_LEFT, false);
        } else if (f == 126) {
            // wybór innej rozdzielczości niż obecna
            uiResIdx_ = -1;
            for (size_t i = 0; i < resolutions_.size(); ++i) {
                if (resolutions_[i][0] != uiOrigW_ && resolutions_[i][0] <= 1280) uiResIdx_ = static_cast<int>(i);
            }
            if (uiResIdx_ < 0) uiResIdx_ = 0;
            pushMouseMotion(static_cast<float>(settingsWindow_->x() + 60),
                            static_cast<float>(settingsWindow_->y() + 256 + 25 + uiResIdx_ * 25 + 12));
        } else if (f == 128) {
            pushMouseButton(SDL_BUTTON_LEFT, true);
        } else if (f == 130) {
            pushMouseButton(SDL_BUTTON_LEFT, false);
        } else if (f == 170) {
            const auto want = resolutions_[uiResIdx_];
            LOG_INFO("[uitest] rozdzielczość: wybrano {}x{}, okno {}x{}", want[0], want[1], renderer_->width(), renderer_->height());
            uiCheck("wybór rozdzielczości zmienia rozmiar okna",
                    settings_.windowW == want[0] && static_cast<int>(renderer_->width()) == want[0] &&
                        static_cast<int>(renderer_->height()) == want[1]);
            pushKey(SDL_SCANCODE_ESCAPE, true);
        } else if (f == 172) {
            pushKey(SDL_SCANCODE_ESCAPE, false);
        } else if (f == 175) {
            uiCheck("ESC wraca z ustawień do pauzy", subWindow_ == nullptr && pauseWindow_->isVisible() && mode_ == Mode::Pause);
            // "Sterowanie" (wiersze okna pauzy przesunęły się po zmianie rozdzielczości - liczymy od okna)
            pushMouseMotion(static_cast<float>(pauseWindow_->x() + 140), static_cast<float>(pauseWindow_->y() + 188));
        } else if (f == 177) {
            pushMouseButton(SDL_BUTTON_LEFT, true);
        } else if (f == 179) {
            pushMouseButton(SDL_BUTTON_LEFT, false);
        } else if (f == 182) {
            uiCheck("przycisk Sterowanie otwiera listę sterowania", subWindow_ == controlsWindow_ && controlsWindow_->isVisible());
            pushKey(SDL_SCANCODE_ESCAPE, true);
        } else if (f == 184) {
            pushKey(SDL_SCANCODE_ESCAPE, false);
        } else if (f == 187) {
            uiCheck("ESC zamyka sterowanie", subWindow_ == nullptr && mode_ == Mode::Pause);
            engine_.setWindowSize(static_cast<uint32_t>(uiOrigW_), static_cast<uint32_t>(uiOrigH_));
            // Przycisk prędkości w pauzie
            uiSpeedBefore_ = gameSpeed_;
        } else if (f == 215) {
            // po zmianie rozmiaru okna pozycje okien GUI są już przeliczone
            pushMouseMotion(static_cast<float>(pauseWindow_->x() + 140), static_cast<float>(pauseWindow_->y() + 88));
        } else if (f == 220) {
            pushMouseButton(SDL_BUTTON_LEFT, true);
        } else if (f == 222) {
            pushMouseButton(SDL_BUTTON_LEFT, false);
        } else if (f == 225) {
            uiCheck("przycisk prędkości w pauzie zmienia prędkość", gameSpeed_ == uiSpeedBefore_ * 2);
            enterGame();
            gameSpeed_ = 1;
            pushKey(SDL_SCANCODE_RIGHTBRACKET, true);
        } else if (f == 227) {
            pushKey(SDL_SCANCODE_RIGHTBRACKET, false);
        } else if (f == 229) {
            pushKey(SDL_SCANCODE_RIGHTBRACKET, true);
        } else if (f == 231) {
            pushKey(SDL_SCANCODE_RIGHTBRACKET, false);
        } else if (f == 233) {
            uiCheck("klawisz ] przyspiesza czas (x1 -> x4)", gameSpeed_ == 4);
            uiTimeBefore_ = world_->gameTime;
            uiRealBefore_ = std::chrono::steady_clock::now();
        } else if (f == 293) {
            const float simDt = world_->gameTime - uiTimeBefore_;
            const float realDt = std::chrono::duration<float>(std::chrono::steady_clock::now() - uiRealBefore_).count();
            LOG_INFO("[uitest] przy x4: {:.1f} s gry w {:.1f} s rzeczywistych", simDt, realDt);
            uiCheck("czas gry płynie ~4x szybciej", simDt > realDt * 3.0f);
            pushKey(SDL_SCANCODE_LEFTBRACKET, true);
        } else if (f == 295) {
            pushKey(SDL_SCANCODE_LEFTBRACKET, false);
        } else if (f == 297) {
            uiCheck("klawisz [ zwalnia czas", gameSpeed_ == 2);
            // Pokaz AI kontra AI z menu głównego
            showMenu();
            pushMouseMotion(static_cast<float>(menuWindow_->x() + 110), static_cast<float>(menuWindow_->y() + 178));
        } else if (f == 300) {
            pushMouseButton(SDL_BUTTON_LEFT, true);
        } else if (f == 302) {
            pushMouseButton(SDL_BUTTON_LEFT, false);
        } else if (f == 306) {
            uiCheck("przycisk Pokaz uruchamia AI kontra AI",
                    mode_ == Mode::Game && spectator_ && world_->aiControlsPlayer && !buildWindow_->isVisible() && gameSpeed_ == 4);
        } else if (f == 600) {
            int playerSites = 0;
            for (const auto& b : world_->buildings) playerSites += (b.alive && b.faction == kPlayer) ? 1 : 0;
            LOG_INFO("[uitest] pokaz: {:.0f} s gry, budynki AI gracza: {}", world_->gameTime, playerSites);
            uiCheck("w pokazie AI rozbudowuje stronę gracza", playerSites > 1);
            LOG_INFO("[uitest] koniec, błędów: {}", uiFailures_);
            uiTest_ = false;
            if (!std::getenv("SETTLERS_UITEST_KEEP")) pushQuitEvent();
        }
    }

    void updateAutoplay() {
        const World& w = *world_;
        if (w.gameTime >= nextAutoplayLog_) {
            nextAutoplayLog_ += 30.0f;
            for (int f = 0; f < kFactionCount; ++f) {
                const FactionState& fs = w.factions[f];
                int built = 0, sites = 0, soldiers = 0, workers = 0;
                for (const auto& b : w.buildings) {
                    if (!b.alive || b.faction != f) continue;
                    (b.constructed ? built : sites)++;
                }
                for (const auto& u : w.units) {
                    if (!u.alive || u.faction != f) continue;
                    if (u.type != UnitType::Settler) ++soldiers;
                    else if (u.building >= 0) ++workers;
                }
                LOG_INFO("[autoplay] t={}s {}: D={} K={} Z={} pop={}/{} bud={} plac={} wojsko={} pracuje={} zabici={} straty={} fale={}",
                         static_cast<int>(w.gameTime), factionName(f), fs.res.wood, fs.res.stone, fs.res.food,
                         fs.population, fs.popCap, built, sites, soldiers, workers, fs.enemiesKilled, fs.unitsLost,
                         fs.waveNumber);
            }
            for (int f = 0; f < kFactionCount; ++f) {
                int states[10] = {};
                int delivered = 0;
                for (const auto& b : w.buildings) {
                    if (b.alive && b.faction == f && b.type == BuildingType::Quarry) delivered += b.produced;
                }
                for (const auto& u : w.units) {
                    if (u.alive && u.faction == f && u.building >= 0 && w.buildings[u.building].type == BuildingType::Quarry)
                        states[static_cast<int>(u.state)]++;
                }
                if (f == 0 && audio_) LOG_INFO("[autoplay] audio: muzyka={} grająca={} szczyt={:.3f} głosy={}",
                                            audio_->currentMusic(), audio_->isMusicPlaying(), audio_->peakLevel(), audio_->activeVoices());
            LOG_INFO("[autoplay] kamieniarze {}: idzie={} kuje={} niesie={} czeka={} dostarczono={}", factionName(f),
                         states[static_cast<int>(UnitState::GoGather)], states[static_cast<int>(UnitState::Gathering)],
                         states[static_cast<int>(UnitState::ReturnHome)], states[static_cast<int>(UnitState::WorkWait)], delivered);
            }
        }
        if (w.winner >= 0 || w.gameTime >= autoplaySeconds_) {
            LOG_INFO("[autoplay] koniec: t={}s zwycięzca={}", static_cast<int>(w.gameTime),
                     w.winner >= 0 ? factionName(w.winner) : "brak");
            autoplaySeconds_ = 0.0f;
            pushQuitEvent();
        }
    }

    void updateSky(float dt) {
        (void)dt;
        // Pora dnia waha się łagodnie między porankiem a popołudniem (bez nocy - to gra strategiczna)
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

    void updateMenuCamera(float dt) {
        cam_.rotate(dt * 4.0f);
    }

    void updateCameraInput(float dt, Engine& e, const InputSnapshot& in) {
        const float speed = cam_.distance() * 1.3f * dt;
        float right = 0.0f, forward = 0.0f;
        if (e.actionDown(kActPanLeft)) right -= speed;
        if (e.actionDown(kActPanRight)) right += speed;
        if (e.actionDown(kActPanUp)) forward += speed;
        if (e.actionDown(kActPanDown)) forward -= speed;

        // Przewijanie przy krawędzi ekranu - dopiero po pierwszym ruchu myszy
        // (przed nim SDL raportuje pozycję 0,0, co przesuwałoby kamerę od startu).
        if (in.mouseDeltaX != 0 || in.mouseDeltaY != 0) mouseMoved_ = true;
        if (mouseMoved_ && autoplaySeconds_ <= 0.0f) {
            const float margin = 4.0f;
            if (in.mouseX <= margin) right -= speed;
            if (in.mouseX >= screenW_ - margin - 1) right += speed;
            if (in.mouseY <= margin) forward += speed;
            if (in.mouseY >= screenH_ - margin - 1) forward -= speed;
        }
        if (right != 0.0f || forward != 0.0f) cam_.pan(right, forward);

        if (e.actionDown(kActRotL)) cam_.rotate(90.0f * dt);
        if (e.actionDown(kActRotR)) cam_.rotate(-90.0f * dt);
        if (e.mouseDown(2)) {
            cam_.rotate(-static_cast<float>(in.mouseDeltaX) * 0.3f);
            cam_.tilt(static_cast<float>(in.mouseDeltaY) * 0.2f);
        }
        if (in.mouseWheelF != 0.0f && !gui_->isPointerOverGui()) {
            cam_.zoom(in.mouseWheelF);
        }
        // Zoom z klawiatury (+/-, PageUp/PageDown) - przydatne np. na touchpadzie
        if (e.actionDown(kActZoomIn)) cam_.zoom(4.0f * dt);
        if (e.actionDown(kActZoomOut)) cam_.zoom(-4.0f * dt);
        if (e.actionPressed(kActHome)) {
            const int castle = world_->factions[kPlayer].castle;
            if (castle >= 0 && world_->buildings[castle].alive) cam_.moveTo(world_->buildings[castle].pos);
        }
    }

    // =================================================================================
    // Sterowanie
    // =================================================================================

    struct MinimapRect {
        float x, y, size;
    };

    MinimapRect minimapRect() const {
        const float size = 200.0f;
        return {screenW_ - size - 14.0f, screenH_ - size - 14.0f, size};
    }

    bool mouseInMinimap(int mx, int my, Vec3* worldPos = nullptr) const {
        const MinimapRect r = minimapRect();
        const float fx = (static_cast<float>(mx) - r.x) / r.size;
        const float fy = (static_cast<float>(my) - r.y) / r.size;
        if (fx < 0.0f || fy < 0.0f || fx > 1.0f || fy > 1.0f) return false;
        if (worldPos) {
            *worldPos = {-kWorldHalf + fx * kWorldHalf * 2.0f, 0.0f, -kWorldHalf + fy * kWorldHalf * 2.0f};
        }
        return true;
    }

    bool mouseOverUi(int mx, int my) const {
        return gui_->isPointerOverGui() || mouseInMinimap(mx, my) || static_cast<float>(my) < kTopBarH ||
               (hasSelectionPanel() && mx > selPanelX_ && mx < selPanelX_ + selPanelW_ && my > selPanelY_);
    }

    bool hasSelectionPanel() const { return !selectedUnits_.empty() || selectedBuilding_ >= 0; }

    Ray mouseRay(const InputSnapshot& in) const {
        return screenPointToRay(*camera_, static_cast<float>(in.mouseX), static_cast<float>(in.mouseY), screenW_, screenH_);
    }

    void handleGameInput(Engine& e, const InputSnapshot& in) {
        const bool shift = in.keys[SDL_SCANCODE_LSHIFT] || in.keys[SDL_SCANCODE_RSHIFT];
        const bool ctrl = in.keys[SDL_SCANCODE_LCTRL] || in.keys[SDL_SCANCODE_RCTRL];
        const bool overUi = mouseOverUi(in.mouseX, in.mouseY);

        // Skróty budowy
        for (int i = 0; i < 6; ++i) {
            if (e.actionPressed("build." + std::to_string(i + 1))) beginPlacement(buildTypes_[i]);
        }
        if (e.actionPressed(kActDemolish)) demolishSelected();
        if (e.actionPressed(kActHalt)) haltSelected();
        if (e.actionPressed(kActSelectArmy)) selectAllArmy();

        if (placing_) {
            updatePlacement(e, in, overUi, shift);
            return;
        }

        // Minimapa
        Vec3 mmWorld;
        if (mouseInMinimap(in.mouseX, in.mouseY, &mmWorld)) {
            if (e.mouseDown(0) && !dragging_) cam_.moveTo(mmWorld);
            if (e.mousePressed(1)) issueMoveOrder(mmWorld, ctrl);
        }

        // Zaznaczanie (LPM)
        if (e.mousePressed(0) && !overUi) {
            dragging_ = true;
            dragStartX_ = in.mouseX;
            dragStartY_ = in.mouseY;
        }
        if (dragging_ && e.mouseReleased(0)) {
            dragging_ = false;
            const int dx = std::abs(in.mouseX - dragStartX_), dy = std::abs(in.mouseY - dragStartY_);
            if (dx > 6 || dy > 6) {
                boxSelect(dragStartX_, dragStartY_, in.mouseX, in.mouseY, shift);
            } else {
                clickSelect(in, shift);
            }
        }
        if (!e.mouseDown(0) && dragging_) dragging_ = false;

        // Rozkazy (PPM)
        if (e.mousePressed(1) && !overUi) {
            issueOrderAtCursor(in, ctrl);
        }
    }

    void clickSelect(const InputSnapshot& in, bool additive) {
        const Ray ray = mouseRay(in);
        const int u = world_->pickUnit(ray);
        if (u >= 0) {
            if (!additive) clearSelection();
            const Unit& unit = world_->units[u];
            if (unit.faction == kPlayer && additive) {
                // Shift: przełączanie zaznaczenia
                auto it = std::find(selectedUnits_.begin(), selectedUnits_.end(), u);
                if (it != selectedUnits_.end()) {
                    world_->setSelected(u, false);
                    selectedUnits_.erase(it);
                    return;
                }
            }
            if (unit.faction != kPlayer) clearSelection();
            if (unit.faction == kPlayer) sfx("select", 0.5f);
            selectedUnits_.push_back(u);
            world_->setSelected(u, true);
            return;
        }
        const int b = world_->pickBuilding(ray);
        clearSelection();
        if (b >= 0) {
            selectedBuilding_ = b;
            world_->setBuildingSelected(b, true);
        }
    }

    void boxSelect(int x0, int y0, int x1, int y1, bool additive) {
        if (!additive) clearSelection();
        const float minX = static_cast<float>(std::min(x0, x1)), maxX = static_cast<float>(std::max(x0, x1));
        const float minY = static_cast<float>(std::min(y0, y1)), maxY = static_cast<float>(std::max(y0, y1));
        std::vector<int> soldiers, settlersSel;
        for (size_t i = 0; i < world_->units.size(); ++i) {
            const Unit& u = world_->units[i];
            if (!u.alive || u.faction != kPlayer) continue;
            float sx, sy;
            if (!worldToScreen(*camera_, add(u.pos, {0.0f, 3.0f, 0.0f}), screenW_, screenH_, sx, sy)) continue;
            if (sx < minX || sx > maxX || sy < minY || sy > maxY) continue;
            (u.type == UnitType::Settler ? settlersSel : soldiers).push_back(static_cast<int>(i));
        }
        // Wojsko ma pierwszeństwo - osadników zaznaczamy tylko, gdy w ramce nie ma żołnierzy
        const auto& pick = soldiers.empty() ? settlersSel : soldiers;
        if (selectedBuilding_ >= 0) {
            world_->setBuildingSelected(selectedBuilding_, false);
            selectedBuilding_ = -1;
        }
        for (int id : pick) {
            if (std::find(selectedUnits_.begin(), selectedUnits_.end(), id) != selectedUnits_.end()) continue;
            selectedUnits_.push_back(id);
            world_->setSelected(id, true);
        }
    }

    void selectAllArmy() {
        clearSelection();
        for (size_t i = 0; i < world_->units.size(); ++i) {
            const Unit& u = world_->units[i];
            if (u.alive && u.faction == kPlayer && u.type != UnitType::Settler) {
                selectedUnits_.push_back(static_cast<int>(i));
                world_->setSelected(static_cast<int>(i), true);
            }
        }
    }

    std::vector<int> selectedSoldiers() const {
        std::vector<int> out;
        if (spectator_) return out;   // w pokazie AI gracz tylko ogląda
        for (int id : selectedUnits_) {
            const Unit& u = world_->units[id];
            if (u.alive && u.faction == kPlayer && u.type != UnitType::Settler) out.push_back(id);
        }
        return out;
    }

    void issueOrderAtCursor(const InputSnapshot& in, bool attackMove) {
        const auto soldiers = selectedSoldiers();
        if (soldiers.empty()) return;
        const Ray ray = mouseRay(in);
        const int u = world_->pickUnit(ray);
        if (u >= 0 && world_->units[u].faction != kPlayer) {
            world_->commandAttackUnit(soldiers, u);
            world_->spawnMarker(world_->units[u].pos);
            return;
        }
        const int b = world_->pickBuilding(ray);
        if (b >= 0 && world_->buildings[b].faction != kPlayer) {
            world_->commandAttackBuilding(soldiers, b);
            world_->spawnMarker(world_->buildings[b].pos);
            return;
        }
        Vec3 hit;
        if (world_->pickGround(ray, hit)) issueMoveOrder(hit, attackMove);
    }

    void issueMoveOrder(const Vec3& p, bool attackMove) {
        const auto soldiers = selectedSoldiers();
        if (soldiers.empty()) return;
        world_->commandMove(soldiers, p, attackMove);
        world_->spawnMarker(p);
        sfx("select", 0.45f);
    }

    void haltSelected() {
        for (int id : selectedSoldiers()) {
            Unit& u = world_->units[id];
            u.state = UnitState::Idle;
            u.path.clear();
            u.targetUnit = -1;
            u.targetBuilding = -1;
            u.guardPos = u.pos;
        }
    }

    void clearSelection() {
        if (world_) {
            for (int id : selectedUnits_) world_->setSelected(id, false);
            if (selectedBuilding_ >= 0) world_->setBuildingSelected(selectedBuilding_, false);
        }
        selectedUnits_.clear();
        selectedBuilding_ = -1;
    }

    void cleanupSelection() {
        selectedUnits_.erase(std::remove_if(selectedUnits_.begin(), selectedUnits_.end(),
                                            [this](int id) { return !world_->units[id].alive; }),
                             selectedUnits_.end());
        if (selectedBuilding_ >= 0 && !world_->buildings[selectedBuilding_].alive) selectedBuilding_ = -1;
    }

    // --- Budowanie ---

    void beginPlacement(BuildingType type) {
        if (mode_ != Mode::Game || spectator_) return;
        const BuildingDef& def = buildingDef(type);
        if (!world_->factions[kPlayer].res.canAfford(def.cost)) {
            world_->pushMessage(std::string(def.name) + ": brakuje " + missingText(def.cost, world_->factions[kPlayer].res),
                                {1.0f, 0.6f, 0.4f, 1.0f});
            sfx("ui_error", 0.7f);
            return;
        }
        placing_ = true;
        placeType_ = type;
        dragging_ = false;
        auto* mc = ghost_->getComponent<MeshComponent>();
        mc->meshName = buildingMeshName(type, kPlayer);
        ghost_->setActive(true);
    }

    void cancelPlacement() {
        placing_ = false;
        if (ghost_) ghost_->setActive(false);
    }

    void setGhostMaterial(bool ok) {
        auto* mc = ghost_->getComponent<MeshComponent>();
        const MaterialHandle wanted = ok ? materials_.ghostOk : materials_.ghostBad;
        if (mc->materialHandle.value != wanted.value) {
            mc->materialHandle = wanted;
            mc->materialInstance.reset();  // wymusza ponowne rozwiązanie materiału
        }
    }

    void updatePlacement(Engine& e, const InputSnapshot& in, bool overUi, bool shift) {
        if (e.mousePressed(1) || e.actionPressed(kActEsc)) {
            cancelPlacement();
            return;
        }
        if (e.actionPressed(kActRotateBuilding)) placeRot_ += PI * 0.5f;

        Vec3 hit;
        if (!world_->pickGround(mouseRay(in), hit)) {
            ghost_->setActive(false);
            return;
        }
        ghost_->setActive(true);
        const Vec3 p = world_->snapToGrid(hit);
        std::string reason;
        const BuildingDef& def = buildingDef(placeType_);
        bool ok = world_->canPlace(placeType_, p, kPlayer, &reason, placeRot_);
        const bool affordable = world_->factions[kPlayer].res.canAfford(def.cost);
        if (ok && !affordable) {
            ok = false;
            reason = "Brakuje: " + missingText(def.cost, world_->factions[kPlayer].res);
        }
        placeReason_ = ok ? "" : reason;
        setGhostMaterial(ok);
        float y = 0.0f;
        for (int i = 0; i < 5; ++i) {
            const float h = def.halfSize;
            const float ox[] = {0, -h, h, -h, h}, oz[] = {0, -h, -h, h, h};
            y += world_->heightAt(p.x + ox[i], p.z + oz[i]);
        }
        ghost_->transform().position = {p.x, y / 5.0f, p.z};
        ghost_->transform().rotation.y = placeRot_;
        ghostPos_ = p;

        if (e.mousePressed(0) && !overUi) {
            if (ok) {
                world_->placeBuilding(placeType_, p, kPlayer, false, placeRot_);
                sfx("place", 0.9f);
                world_->pushMessage(std::string("Plac budowy: ") + def.name + " - osadnicy ruszają do pracy",
                                    {0.85f, 0.95f, 1.0f, 1.0f});
                if (!shift || !world_->factions[kPlayer].res.canAfford(def.cost)) cancelPlacement();
            } else {
                world_->pushMessage("Nie można tu budować: " + reason, {1.0f, 0.55f, 0.45f, 1.0f});
                sfx("ui_error", 0.7f);
            }
        }
    }

    void trainSelected(UnitType type) {
        if (selectedBuilding_ < 0 || spectator_) return;
        std::string reason;
        if (!world_->queueTraining(selectedBuilding_, type, &reason)) {
            world_->pushMessage("Nie można szkolić: " + reason, {1.0f, 0.6f, 0.4f, 1.0f});
            sfx("ui_error", 0.7f);
        }
    }

    void demolishSelected() {
        if (selectedBuilding_ < 0 || spectator_) return;
        const Building& b = world_->buildings[selectedBuilding_];
        if (!b.alive || b.faction != kPlayer || b.type == BuildingType::Castle) return;
        world_->demolish(selectedBuilding_);
        selectedBuilding_ = -1;
    }

    void updateContextWindows() {
        if (mode_ != Mode::Game) return;
        hoverMissing_.clear();
        bool barracks = false, actions = false;
        if (selectedBuilding_ >= 0) {
            const Building& b = world_->buildings[selectedBuilding_];
            if (b.alive && b.faction == kPlayer) {
                barracks = b.type == BuildingType::Barracks && b.constructed;
                actions = b.type != BuildingType::Castle;
            }
        }
        barracksWindow_->setVisible(barracks && !spectator_);
        actionsWindow_->setVisible(actions && !spectator_);
        // Wyszarzenie przycisków budowy, na które nie stać gracza
        // Ustawiane co klatkę: GuiButton sam zmienia kolor przy najechaniu kursorem.
        const auto& res = world_->factions[kPlayer].res;
        const int mx = gui_->mouseX() - buildWindow_->x(), my = gui_->mouseY() - buildWindow_->y();
        for (int i = 0; i < 6; ++i) {
            const bool can = res.canAfford(buildingDef(buildTypes_[i]).cost);
            const bool hover = buildButtons_[i]->isHovered(mx, my);
            buildButtons_[i]->color_ = can ? (hover ? Color{0.2f, 0.2f, 0.28f, 0.9f} : Color{0.3f, 0.3f, 0.38f, 0.9f})
                                           : (hover ? Color{0.42f, 0.18f, 0.16f, 0.9f} : Color{0.32f, 0.16f, 0.16f, 0.9f});
            if (can) continue;
            if (hover) hoverMissing_ = std::string(buildingDef(buildTypes_[i]).name) + " - brakuje: " +
                                       missingText(buildingDef(buildTypes_[i]).cost, res);
        }
    }

    // =================================================================================
    // Debug (F3 / F9)
    // =================================================================================

    // Linia ekranowa z małych prostokątów (GUI ma tylko prostokąty).
    void debugLine(float x0, float y0, float x1, float y1, const Color& c, float thick = 2.0f, float gap = 0.0f) {
        const float dx = x1 - x0, dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.5f || len > 4000.0f) return;
        const float step = std::max(2.0f, thick + gap);
        const int n = std::min(800, static_cast<int>(len / step) + 1);
        for (int i = 0; i <= n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(n);
            gui_->addOverlayRect(x0 + dx * t - thick * 0.5f, y0 + dy * t - thick * 0.5f, thick, thick, c, 0.949f);
        }
    }

    bool project(const Vec3& p, float& sx, float& sy) const {
        return worldToScreen(*camera_, p, screenW_, screenH_, sx, sy) && sx > -200 && sy > -200 && sx < screenW_ + 200 &&
               sy < screenH_ + 200;
    }

    Vec3 onGround(const Vec3& p, float lift = 1.0f) const { return {p.x, world_->heightAt(p.x, p.z) + lift, p.z}; }

    static Color stateColor(UnitState s) {
        switch (s) {
        case UnitState::Idle: return {0.75f, 0.75f, 0.75f, 1.0f};
        case UnitState::Moving: return {0.4f, 0.8f, 1.0f, 1.0f};
        case UnitState::GoBuild: case UnitState::Building: return {1.0f, 0.85f, 0.3f, 1.0f};
        case UnitState::GoToWork: case UnitState::WorkWait: return {0.6f, 1.0f, 0.6f, 1.0f};
        case UnitState::GoGather: case UnitState::Gathering: return {0.4f, 1.0f, 0.8f, 1.0f};
        case UnitState::ReturnHome: return {0.95f, 0.65f, 1.0f, 1.0f};
        case UnitState::Attack: return {1.0f, 0.35f, 0.3f, 1.0f};
        }
        return {1, 1, 1, 1};
    }

    void drawDebug(const InputSnapshot& in) {
        const World& w = *world_;
        const Vec3 focus = cam_.focus();
        const float range = cam_.distance() * 1.3f + 120.0f;
        const bool detailed = cam_.distance() < 380.0f;
        int counts[10] = {};
        int stalledNow = 0;

        // --- Jednostki: ścieżki, cele, etykiety ---
        for (size_t i = 0; i < w.units.size(); ++i) {
            const Unit& u = w.units[i];
            if (!u.alive) continue;
            counts[static_cast<int>(u.state)]++;
            if (u.stallTime > 2.0f) ++stalledNow;
            const float dx = u.pos.x - focus.x, dz = u.pos.z - focus.z;
            if (dx * dx + dz * dz > range * range) continue;
            float ux, uy;
            if (!project(onGround(u.pos, 1.0f), ux, uy)) continue;
            const Color sc = stateColor(u.state);

            // Ścieżka (od bieżącej pozycji przez kolejne punkty)
            float px = ux, py = uy;
            for (size_t k = u.pathIndex; k < u.path.size(); ++k) {
                float qx, qy;
                if (!project(onGround(u.path[k], 1.0f), qx, qy)) break;
                debugLine(px, py, qx, qy, {sc[0], sc[1], sc[2], 0.85f}, 2.0f);
                gui_->addOverlayRect(qx - 3, qy - 3, 6, 6, sc, 0.948f);
                px = qx;
                py = qy;
            }
            // Cel logiczny: zasób / budynek / atakowany
            Vec3 target{};
            bool hasTarget = false;
            if (u.resourceNode >= 0 && w.nodes[u.resourceNode].alive) {
                target = w.nodes[u.resourceNode].pos;
                hasTarget = true;
            } else if (u.targetUnit >= 0 && w.units[u.targetUnit].alive) {
                target = w.units[u.targetUnit].pos;
                hasTarget = true;
            } else if (u.targetBuilding >= 0 && w.buildings[u.targetBuilding].alive) {
                target = w.buildings[u.targetBuilding].pos;
                hasTarget = true;
            } else if (u.building >= 0 && w.buildings[u.building].alive) {
                target = u.state == UnitState::ReturnHome || u.state == UnitState::GoToWork ? w.buildings[u.building].door()
                                                                                          : w.buildings[u.building].pos;
                hasTarget = true;
            }
            if (hasTarget) {
                float tx, ty;
                if (project(onGround(target, 2.0f), tx, ty)) debugLine(ux, uy, tx, ty, {1.0f, 1.0f, 1.0f, 0.35f}, 1.5f, 5.0f);
            }

            // Etykieta
            const bool stalled = u.stallTime > 2.0f;
            if (detailed || u.selected || stalled) {
                char buf[96];
                if (stalled) {
                    std::snprintf(buf, sizeof(buf), "#%zu %s STOI %.0fs", i, World::stateName(u.state), u.stallTime);
                } else {
                    std::snprintf(buf, sizeof(buf), "#%zu %s", i, World::stateName(u.state));
                }
                const Color lc = stalled ? Color{1.0f, 0.25f, 0.2f, 1.0f} : sc;
                if (stalled) gui_->addOverlayFrame(ux - 9, uy - 30, 18, 32, 2.0f, lc, 0.947f);
                text(buf, ux + 8.0f, uy - 18.0f, 0.4f, lc);
            }
        }

        // --- Budynki: wejścia, pracownik, obszar ---
        for (size_t i = 0; i < w.buildings.size(); ++i) {
            const Building& b = w.buildings[i];
            if (!b.alive) continue;
            const float dx = b.pos.x - focus.x, dz = b.pos.z - focus.z;
            if (dx * dx + dz * dz > range * range) continue;
            const Vec3 door = b.door();
            const bool doorFree = w.nav().isWalkableWorld(door.x, door.z);
            float sx, sy;
            if (project(onGround(door, 0.5f), sx, sy)) {
                const Color dc = doorFree ? Color{0.2f, 1.0f, 0.3f, 1.0f} : Color{1.0f, 0.15f, 0.1f, 1.0f};
                gui_->addOverlayFrame(sx - 6, sy - 6, 12, 12, 2.0f, dc, 0.947f);
                if (detailed) text(doorFree ? "wejście" : "WEJŚCIE ZABL.", sx + 9, sy + 4, 0.38f, dc);
            }
            if (detailed && project(onGround(b.pos, buildingDef(b.type).height * 0.5f), sx, sy)) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "#%zu %s prac=%d obszar=%d%s%s", i, buildingDef(b.type).name, b.worker,
                              w.nav().regionAt(door.x, door.z), b.unreachable ? " BRAK DOJŚCIA" : "",
                              b.lacksResources ? " BRAK SUROWCA" : "");
                text(buf, sx - 60.0f, sy, 0.4f, {1.0f, 1.0f, 0.75f, 1.0f});
            }
        }

        // --- Surowce: rezerwacje i nieosiągalne ---
        if (detailed) {
            for (const auto& n : w.nodes) {
                if (!n.alive || (n.reserved == 0 && n.blockedUntil <= w.gameTime)) continue;
                const float dx = n.pos.x - focus.x, dz = n.pos.z - focus.z;
                if (dx * dx + dz * dz > range * range) continue;
                float sx, sy;
                if (!project(onGround(n.pos, 3.0f), sx, sy)) continue;
                if (n.blockedUntil > w.gameTime) {
                    text("nieosiągalne", sx - 20, sy, 0.38f, {1.0f, 0.4f, 0.3f, 1.0f});
                } else {
                    text("rez=" + std::to_string(n.reserved) + " zost=" + std::to_string(n.amount), sx - 20, sy, 0.38f,
                         {0.6f, 1.0f, 0.9f, 1.0f});
                }
            }
        }

        // --- Panel statystyk ---
        const float x = 14.0f;
        float y = kTopBarH + 26.0f;
        const float pw = 340.0f;
        gui_->addOverlayRect(x - 6, y - 20, pw, 222, {0.02f, 0.03f, 0.06f, 0.82f}, 0.958f);
        auto line = [&](const std::string& s, const Color& c = {0.85f, 0.95f, 1.0f, 1.0f}) {
            text(s, x, y, 0.42f, c);
            y += 18.0f;
        };
        line(std::string("DEBUG (F3: ") + (debugMode_ == 2 ? "+siatka kolizji" : "ścieżki/stany") + ", F9: zrzut)",
             {1.0f, 0.9f, 0.5f, 1.0f});
        char buf[160];
        for (int s = 0; s < 10; s += 2) {
            std::snprintf(buf, sizeof(buf), "%-11s %3d    %-11s %3d", World::stateName(static_cast<UnitState>(s)), counts[s],
                          World::stateName(static_cast<UnitState>(s + 1)), counts[s + 1]);
            line(buf);
        }
        std::snprintf(buf, sizeof(buf), "Zawieszone teraz: %d   wykryte: %d   odblokowane: %d", stalledNow,
                      w.debugStats.stallsDetected, w.debugStats.stallsRecovered);
        line(buf, stalledNow > 0 ? Color{1.0f, 0.4f, 0.3f, 1.0f} : Color{0.6f, 1.0f, 0.6f, 1.0f});
        std::snprintf(buf, sizeof(buf), "A*: %d   ścieżki proste: %d   pociski: %zu", w.debugStats.pathRequests,
                      w.debugStats.directPaths, w.projectiles.size());
        line(buf);
        std::snprintf(buf, sizeof(buf), "Encje sceny: %zu   jednostki: %zu   budynki: %zu", scene_->getEntities().size(),
                      w.units.size(), w.buildings.size());
        line(buf);
        // Komórka pod kursorem
        Vec3 hit;
        if (w.pickGround(mouseRay(in), hit)) {
            int cx, cz;
            if (w.nav().worldToCell(hit.x, hit.z, cx, cz)) {
                const bool walk = w.nav().isWalkable(cx, cz);
                const bool stat = w.nav().isStaticBlocked(cx, cz);
                std::snprintf(buf, sizeof(buf), "Kursor: (%.0f, %.0f) kom=(%d,%d) %s obszar=%d h=%.1f", hit.x, hit.z, cx, cz,
                              walk ? "wolna" : (stat ? "TEREN" : "PRZESZKODA"), w.nav().regionAt(hit.x, hit.z), hit.y);
                line(buf, walk ? Color{0.7f, 1.0f, 0.7f, 1.0f} : Color{1.0f, 0.55f, 0.4f, 1.0f});
            }
        }
        line("Zaznacz jednostkę i wciśnij F9, aby zrzucić jej stan do konsoli", {0.7f, 0.7f, 0.7f, 1.0f});
    }

    // Siatka kolizji (tryb 2): półprzezroczyste kwadraty zablokowanych komórek wokół kamery.
    void updateDebugNavMesh(float dt) {
        if (!debugNavEntity_) return;
        if (debugMode_ != 2 || mode_ == Mode::Menu) {
            debugNavEntity_->setActive(false);
            return;
        }
        debugNavTimer_ -= dt;
        if (debugNavTimer_ > 0.0f) return;
        debugNavTimer_ = 0.4f;
        const auto& nav = world_->nav();
        const Vec3 f = cam_.focus();
        const float radius = std::clamp(cam_.distance() * 0.9f, 80.0f, 260.0f);
        int c0x, c0z, c1x, c1z;
        nav.worldToCell(f.x - radius, f.z - radius, c0x, c0z);
        nav.worldToCell(f.x + radius, f.z + radius, c1x, c1z);
        c0x = std::max(0, c0x); c0z = std::max(0, c0z);
        c1x = std::min(nav.width() - 1, c1x); c1z = std::min(nav.height() - 1, c1z);
        MeshBuilder mb;
        const float hs = nav.cellSize() * 0.45f;
        for (int cz = c0z; cz <= c1z; ++cz) {
            for (int cx = c0x; cx <= c1x; ++cx) {
                if (nav.isWalkable(cx, cz)) continue;
                const Vec3 c = nav.cellCenter(cx, cz);
                const bool stat = nav.isStaticBlocked(cx, cz);
                const Vec3 col = stat ? Vec3{0.95f, 0.15f, 0.1f} : Vec3{1.0f, 0.6f, 0.05f};
                auto pt = [&](float ox, float oz) {
                    return Vec3{c.x + ox, world_->heightAt(c.x + ox, c.z + oz) + 0.6f, c.z + oz};
                };
                const Vec3 a = pt(-hs, -hs), b = pt(hs, -hs), d = pt(hs, hs), e = pt(-hs, hs);
                mb.polygon({a, b, d, e}, {c.x, a.y - 10.0f, c.z}, col);
            }
        }
        auto* mc = debugNavEntity_->getComponent<MeshComponent>();
        if (mb.data().vertices.empty()) {
            debugNavEntity_->setActive(false);
            return;
        }
        engine_.getMeshManager()->loadMesh("debug_nav_grid", mb.build());
        mc->meshName = "debug_nav_grid";
        debugNavEntity_->setActive(true);
    }

    void dumpDebug() {
        const World& w = *world_;
        LOG_INFO("===== ZRZUT DEBUG t={:.1f}s (A*={}, zawieszenia wykryte={} odblokowane={}) =====", w.gameTime,
                 w.debugStats.pathRequests, w.debugStats.stallsDetected, w.debugStats.stallsRecovered);
        int dumped = 0;
        for (int id : selectedUnits_) {
            LOG_INFO("[zaznaczona] {}", w.describeUnit(id));
            ++dumped;
        }
        for (size_t i = 0; i < w.units.size(); ++i) {
            if (w.units[i].alive && w.units[i].stallTime > 2.0f) {
                LOG_INFO("[stoi] {}", w.describeUnit(static_cast<int>(i)));
                ++dumped;
            }
        }
        if (selectedBuilding_ >= 0) {
            const Building& b = w.buildings[selectedBuilding_];
            LOG_INFO("[budynek] #{} {} poz=({:.1f},{:.1f}) hp={:.0f} prac={} brakDojscia={} brakSurowca={} wZasiegu={}",
                     selectedBuilding_, buildingDef(b.type).name, b.pos.x, b.pos.z, b.hp, b.worker, b.unreachable,
                     b.lacksResources, b.resourcesInRange);
            ++dumped;
        }
        world_->pushMessage("Zrzut debug: " + std::to_string(dumped) + " wpisów w konsoli (F9)", {0.7f, 0.9f, 1.0f, 1.0f});
    }

    // =================================================================================
    // HUD
    // =================================================================================

    void text(const std::string& s, float x, float y, float scale, const Color& c, float depth = kHudDepth) {
        engine_.setTextLabel(s, kFont, x, y, scale, c, depth);
    }

    void textCentered(const std::string& s, float cx, float y, float scale, const Color& c, float depth = kHudDepth) {
        text(s, cx - engine_.measureText(s, scale) * 0.5f, y, scale, c, depth);
    }

    void bar(float x, float y, float w, float h, float frac, const Color& fill) {
        gui_->addOverlayRect(x - 1, y - 1, w + 2, h + 2, {0.0f, 0.0f, 0.0f, 0.75f}, 0.951f);
        gui_->addOverlayRect(x, y, w * std::clamp(frac, 0.0f, 1.0f), h, fill, 0.95f);
    }

    void drawHud(Engine& e, const InputSnapshot& in) {
        const World& w = *world_;
        if (mode_ == Mode::Menu) {
            textCentered("OSADNICY", screenW_ * 0.5f, screenH_ * 0.5f - 270.0f, 1.6f, {1.0f, 0.92f, 0.6f, 1.0f}, 0.92f);
            textCentered("strategia na silniku engine3d", screenW_ * 0.5f, screenH_ * 0.5f - 237.0f, 0.6f,
                         {0.9f, 0.9f, 0.95f, 1.0f}, 0.92f);
            text("FPS: " + std::to_string(static_cast<int>(e.getCurrentFPS())), 12.0f, screenH_ - 12.0f, 0.5f,
                 {1.0f, 1.0f, 0.5f, 1.0f});
            return;
        }

        drawWorldBars();
        drawTopBar(e);
        drawMessages();
        drawMinimap();
        drawSelectionPanel();

        if (dragging_) {
            const float x0 = static_cast<float>(std::min(dragStartX_, in.mouseX));
            const float y0 = static_cast<float>(std::min(dragStartY_, in.mouseY));
            const float bw = static_cast<float>(std::abs(in.mouseX - dragStartX_));
            const float bh = static_cast<float>(std::abs(in.mouseY - dragStartY_));
            gui_->addOverlayRect(x0, y0, bw, bh, {0.3f, 1.0f, 0.4f, 0.12f}, 0.96f);
            gui_->addOverlayFrame(x0, y0, bw, bh, 1.5f, {0.4f, 1.0f, 0.5f, 0.9f}, 0.955f);
        }

        if (placing_ && mode_ == Mode::Game) {
            const BuildingDef& def = buildingDef(placeType_);
            const float x = static_cast<float>(in.mouseX) + 18.0f, y = static_cast<float>(in.mouseY) + 30.0f;
            text(std::string(def.name) + " (" + costText(def.cost) + ")", x, y, 0.55f, {1, 1, 1, 1});
            if (!placeReason_.empty()) text(placeReason_, x, y + 20.0f, 0.5f, {1.0f, 0.5f, 0.4f, 1.0f});
            text("LPM: postaw  PPM: anuluj  R: obróć  Shift: kilka", x, y + 40.0f, 0.42f, {0.85f, 0.85f, 0.85f, 1.0f});
        }

        if (!hoverMissing_.empty() && mode_ == Mode::Game) {
            const float x = static_cast<float>(in.mouseX) + 18.0f, y = static_cast<float>(in.mouseY) + 6.0f;
            const float tw = engine_.measureText(hoverMissing_, 0.52f);
            gui_->addOverlayRect(x - 6.0f, y - 18.0f, tw + 12.0f, 26.0f, {0.1f, 0.05f, 0.04f, 0.92f}, 0.05f);
            text(hoverMissing_, x, y, 0.52f, {1.0f, 0.6f, 0.45f, 1.0f}, 0.04f);
        }

        if (showHelp_) drawHelp();
    }

    void drawTopBar(Engine& e) {
        const World& w = *world_;
        const FactionState& fs = w.factions[kPlayer];
        gui_->addOverlayRect(0, 0, screenW_, kTopBarH, {0.09f, 0.075f, 0.05f, 0.88f}, 0.96f);
        gui_->addOverlayRect(0, kTopBarH, screenW_, 2, {0.55f, 0.45f, 0.25f, 0.9f}, 0.96f);

        int soldiers = 0;
        for (const auto& u : w.units) {
            if (u.alive && u.faction == kPlayer && u.type != UnitType::Settler) ++soldiers;
        }
        float x = 14.0f;
        const float y = 24.0f;
        auto item = [&](const std::string& label, const std::string& value, const Color& c) {
            text(label, x, y, 0.55f, {0.85f, 0.8f, 0.65f, 1.0f});
            x += engine_.measureText(label, 0.55f) + 6.0f;
            text(value, x, y, 0.6f, c);
            x += engine_.measureText(value, 0.6f) + 26.0f;
        };
        item("Drewno:", std::to_string(fs.res.wood), {0.95f, 0.75f, 0.45f, 1.0f});
        item("Kamień:", std::to_string(fs.res.stone), {0.85f, 0.85f, 0.9f, 1.0f});
        item("Żywność:", std::to_string(fs.res.food), {0.95f, 0.9f, 0.4f, 1.0f});
        item("Ludność:", std::to_string(fs.population) + "/" + std::to_string(fs.popCap),
             fs.population >= fs.popCap ? Color{1.0f, 0.5f, 0.4f, 1.0f} : Color{0.8f, 1.0f, 0.8f, 1.0f});
        item("Wojsko:", std::to_string(soldiers), {0.7f, 0.85f, 1.0f, 1.0f});

        const int t = static_cast<int>(w.gameTime);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", t / 60, t % 60);
        static const char* diffNames[] = {"Łatwy", "Normalny", "Trudny"};
        std::string right = std::string("Poziom: ") + diffNames[static_cast<int>(w.difficulty)] + "   Czas: " + buf;
        if (gameSpeed_ != 1) right += " (x" + std::to_string(gameSpeed_) + ")";
        right += "   FPS: " + std::to_string(static_cast<int>(e.getCurrentFPS())) + "   F1: pomoc";
        if (spectator_) {
            const std::string tag = "POKAZ: AI kontra AI";
            const float tw = engine_.measureText(tag, 0.6f);
            gui_->addOverlayRect(screenW_ * 0.5f - tw * 0.5f - 10.0f, kTopBarH + 4.0f, tw + 20.0f, 26.0f, {0.35f, 0.1f, 0.08f, 0.85f}, 0.955f);
            textCentered(tag, screenW_ * 0.5f, kTopBarH + 23.0f, 0.6f, {1.0f, 0.9f, 0.6f, 1.0f});
        }
        text(right, screenW_ - engine_.measureText(right, 0.5f) - 14.0f, y, 0.5f, {0.85f, 0.85f, 0.85f, 1.0f});
    }

    void drawMessages() {
        const World& w = *world_;
        float y = kTopBarH + (spectator_ ? 56.0f : 30.0f);
        for (const auto& m : w.messages) {
            const float age = w.gameTime - m.time;
            const float alpha = std::clamp(9.0f - age, 0.0f, 1.0f);
            Color c = m.color;
            c[3] *= alpha;
            textCentered(m.text, screenW_ * 0.5f, y, 0.58f, c);
            y += 24.0f;
        }
    }

    void drawWorldBars() {
        const World& w = *world_;
        const float maxDist = 900.0f;
        const Vec3 eye = camera_->getOwner()->transform().position;
        for (const auto& u : w.units) {
            if (!u.alive) continue;
            const UnitDef& def = unitDef(u.type);
            const bool show = u.selected || u.hp < def.maxHp || w.gameTime - u.lastDamagedTime < 4.0f;
            if (!show) continue;
            if (length2(subtract(u.pos, eye)) > maxDist * maxDist) continue;
            float sx, sy;
            if (!worldToScreen(*camera_, add(u.pos, {0.0f, def.height + 1.6f, 0.0f}), screenW_, screenH_, sx, sy)) continue;
            const float bw = 26.0f;
            const Color fill = u.faction == kPlayer ? hpColor(u.hp / def.maxHp) : Color{0.95f, 0.25f, 0.2f, 1.0f};
            bar(sx - bw * 0.5f, sy, bw, 4.0f, u.hp / def.maxHp, fill);
        }
        for (const auto& b : w.buildings) {
            if (!b.alive) continue;
            const BuildingDef& def = buildingDef(b.type);
            const bool show = b.selected || !b.constructed || b.hp < def.maxHp * 0.999f || w.gameTime - b.lastDamagedTime < 4.0f;
            if (!show) continue;
            if (length2(subtract(b.pos, eye)) > maxDist * maxDist * 1.4f) continue;
            const float top = b.constructed ? def.height + 3.0f : def.height * 0.75f + 3.0f;
            float sx, sy;
            if (!worldToScreen(*camera_, add(b.pos, {0.0f, top, 0.0f}), screenW_, screenH_, sx, sy)) continue;
            const float bw = 54.0f;
            const Color fill = b.faction == kPlayer ? hpColor(b.hp / def.maxHp) : Color{0.95f, 0.25f, 0.2f, 1.0f};
            bar(sx - bw * 0.5f, sy, bw, 5.0f, b.hp / def.maxHp, fill);
            if (!b.constructed) {
                bar(sx - bw * 0.5f, sy + 8.0f, bw, 4.0f, b.progress, {0.35f, 0.65f, 1.0f, 1.0f});
            }
        }
        // Znaczniki problemów nad budynkami gracza (widoczne zawsze)
        for (const auto& b : w.buildings) {
            if (!b.alive || b.faction != kPlayer || (!b.constructed && !b.unreachable)) continue;
            const BuildingDef& def = buildingDef(b.type);
            std::string label;
            Color color{1.0f, 0.55f, 0.3f, 1.0f};
            if (b.unreachable) {
                label = "! brak dojścia";
            } else if (b.lacksResources) {
                label = b.type == BuildingType::Woodcutter ? "! brak drzew" : "! brak kamienia";
            } else if (b.repairBlocked) {
                label = "! brak drewna na naprawę";
            } else if (w.needsRepairPublic(b) && w.gameTime - b.lastDamagedTime > 8.0f) {
                label = "naprawa";
                color = {0.6f, 0.85f, 1.0f, 1.0f};
            } else {
                continue;
            }
            float sx, sy;
            if (!worldToScreen(*camera_, add(b.pos, {0.0f, def.height + 7.0f, 0.0f}), screenW_, screenH_, sx, sy)) continue;
            const float tw = engine_.measureText(label, 0.5f);
            gui_->addOverlayRect(sx - tw * 0.5f - 5.0f, sy - 16.0f, tw + 10.0f, 21.0f, {0.08f, 0.05f, 0.03f, 0.8f}, 0.952f);
            text(label, sx - tw * 0.5f, sy, 0.5f, color);
        }
    }

    static float length2(const Vec3& v) { return dot(v, v); }

    void drawMinimap() {
        const World& w = *world_;
        const MinimapRect r = minimapRect();
        gui_->addOverlayRect(r.x - 3, r.y - 3, r.size + 6, r.size + 6, {0.55f, 0.45f, 0.25f, 0.95f}, 0.97f);
        const int n = w.minimapSize();
        const float cell = r.size / static_cast<float>(n);
        const auto& colors = w.minimapColors();
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                const Vec3& c = colors[static_cast<size_t>(y) * n + x];
                gui_->addOverlayRect(r.x + x * cell, r.y + y * cell, cell + 0.5f, cell + 0.5f, {c.x, c.y, c.z, 1.0f}, 0.965f);
            }
        }
        auto toMap = [&](const Vec3& p, float& mx, float& my) {
            mx = r.x + (p.x + kWorldHalf) / (kWorldHalf * 2.0f) * r.size;
            my = r.y + (p.z + kWorldHalf) / (kWorldHalf * 2.0f) * r.size;
        };
        for (const auto& b : w.buildings) {
            if (!b.alive) continue;
            float mx, my;
            toMap(b.pos, mx, my);
            const Vec3 fc = factionColor(b.faction);
            const float s = b.type == BuildingType::Castle ? 8.0f : 5.0f;
            gui_->addOverlayRect(mx - s * 0.5f - 1, my - s * 0.5f - 1, s + 2, s + 2, {0, 0, 0, 1}, 0.962f);
            gui_->addOverlayRect(mx - s * 0.5f, my - s * 0.5f, s, s, {fc.x, fc.y, fc.z, 1.0f}, 0.96f);
        }
        for (const auto& u : w.units) {
            if (!u.alive) continue;
            float mx, my;
            toMap(u.pos, mx, my);
            const Vec3 fc = u.faction == kPlayer ? Vec3{0.5f, 0.75f, 1.0f} : Vec3{1.0f, 0.45f, 0.4f};
            gui_->addOverlayRect(mx - 1.0f, my - 1.0f, 2.5f, 2.5f, {fc.x, fc.y, fc.z, 1.0f}, 0.958f);
        }
        // Widok kamery
        float cx, cy;
        toMap(cam_.focus(), cx, cy);
        const float vs = std::clamp(cam_.distance() / (kWorldHalf * 2.0f) * r.size * 1.4f, 10.0f, r.size * 0.8f);
        gui_->addOverlayFrame(cx - vs * 0.5f, cy - vs * 0.35f, vs, vs * 0.7f, 1.0f, {1, 1, 1, 0.9f}, 0.955f);
    }

    void drawSelectionPanel() {
        const World& w = *world_;
        selPanelW_ = 470;
        selPanelX_ = static_cast<int>(screenW_ * 0.5f) - selPanelW_ / 2;
        const int panelH = 132;
        selPanelY_ = static_cast<int>(screenH_) - panelH - 12;
        if (!hasSelectionPanel()) return;
        const float px = static_cast<float>(selPanelX_), py = static_cast<float>(selPanelY_);
        gui_->addOverlayRect(px - 3, py - 3, selPanelW_ + 6.0f, panelH + 6.0f, {0.55f, 0.45f, 0.25f, 0.95f}, 0.97f);
        gui_->addOverlayRect(px, py, static_cast<float>(selPanelW_), static_cast<float>(panelH), {0.1f, 0.085f, 0.06f, 0.92f}, 0.965f);

        const Color title{1.0f, 0.92f, 0.65f, 1.0f};
        const Color body{0.9f, 0.9f, 0.88f, 1.0f};

        if (selectedBuilding_ >= 0) {
            const Building& b = w.buildings[selectedBuilding_];
            const BuildingDef& def = buildingDef(b.type);
            text(std::string(def.name) + (b.faction == kPlayer ? "" : " (wróg)"), px + 14, py + 26, 0.7f, title);
            bar(px + 14, py + 38, 200, 8, b.hp / def.maxHp, hpColor(b.hp / def.maxHp));
            text(std::to_string(static_cast<int>(b.hp)) + " / " + std::to_string(static_cast<int>(def.maxHp)), px + 224, py + 47,
                 0.45f, body);
            std::string status;
            std::string resLine;
            Color resColor{0.8f, 0.95f, 0.8f, 1.0f};
            if (!b.constructed) {
                status = "W budowie: " + std::to_string(static_cast<int>(b.progress * 100.0f)) + "%";
            } else if (def.needsWorker) {
                status = (b.worker >= 0 && w.units[b.worker].alive) ? "Pracownik: jest" : "Pracownik: brak (czeka na osadnika)";
                if (b.produced > 0) status += "   Dostarczono: " + std::to_string(b.produced);
                if (b.type == BuildingType::Woodcutter || b.type == BuildingType::Quarry) {
                    const bool tree = b.type == BuildingType::Woodcutter;
                    if (b.lacksResources) {
                        resLine = tree ? "BRAK DRZEW w zasięgu - postaw drwala bliżej lasu" : "BRAK SKAŁ w zasięgu - postaw kamieniarza przy skałach";
                        resColor = {1.0f, 0.5f, 0.35f, 1.0f};
                    } else if (b.resourcesInRange >= 0) {
                        resLine = std::string(tree ? "Drzewa" : "Skały") + " w zasięgu pracy: " + std::to_string(b.resourcesInRange);
                        if (b.resourcesInRange <= 2) resColor = {1.0f, 0.85f, 0.45f, 1.0f};
                    }
                }
            } else if (b.type == BuildingType::Barracks) {
                status = "Kolejka: " + std::to_string(b.trainQueue.size());
                if (!b.trainQueue.empty()) {
                    const float total = unitDef(b.trainQueue.front()).trainTime;
                    status += std::string("   Szkoli: ") + unitDef(b.trainQueue.front()).name + " " +
                              std::to_string(static_cast<int>(std::min(1.0f, b.trainTimer / total) * 100.0f)) + "%";
                }
            } else if (def.popBonus > 0) {
                status = "Mieszkańcy: +" + std::to_string(def.popBonus);
            } else if (def.attackRange > 0.0f) {
                status = "Zasięg strzału: " + std::to_string(static_cast<int>(def.attackRange));
            }
            if (b.constructed && b.hp < def.maxHp * 0.97f) {
                const int rep = w.repairersOf(selectedBuilding_);
                if (b.repairBlocked) status += "   Naprawa: brak drewna";
                else if (rep > 0) status += "   Naprawia: " + std::to_string(rep);
                else if (w.gameTime - b.lastDamagedTime < 8.0f) status += "   Naprawa po walce";
                else status += "   Czeka na naprawę";
            }
            if (b.unreachable && b.faction == kPlayer) {
                resLine = "Osadnicy nie mogą tu dojść - udrożnij drogę lub zburz (Del)";
                resColor = {1.0f, 0.5f, 0.35f, 1.0f};
            }
            text(status, px + 14, py + 76, 0.52f, body);
            if (!resLine.empty()) {
                text(resLine, px + 14, py + 104, 0.5f, resColor);
            } else {
                text(def.description, px + 14, py + 104, 0.45f, {0.75f, 0.75f, 0.7f, 1.0f});
            }
            return;
        }

        if (selectedUnits_.size() == 1) {
            const Unit& u = w.units[selectedUnits_[0]];
            const UnitDef& def = unitDef(u.type);
            text(std::string(def.name) + (u.faction == kPlayer ? "" : " (wróg)"), px + 14, py + 26, 0.7f, title);
            bar(px + 14, py + 38, 200, 8, u.hp / def.maxHp, hpColor(u.hp / def.maxHp));
            text(std::to_string(static_cast<int>(u.hp)) + " / " + std::to_string(static_cast<int>(def.maxHp)), px + 224, py + 47,
                 0.45f, body);
            std::string st = stateText(u, w);
            if (u.type == UnitType::Settler && u.building >= 0 && w.buildings[u.building].alive &&
                w.buildings[u.building].constructed && (u.state == UnitState::GoBuild || u.state == UnitState::Building)) {
                st = u.state == UnitState::Building ? "Naprawia budynek" : "Idzie naprawić budynek";
            }
            if (u.hp < def.maxHp && w.gameTime - u.lastDamagedTime >= 8.0f && u.state != UnitState::Attack) st += "  (leczy się)";
            text(st, px + 14, py + 76, 0.55f, body);
            if (u.type == UnitType::Settler) {
                std::string job = "Osadnicy pracują samodzielnie - wznoszą budowle i obsługują warsztaty.";
                if (u.building >= 0 && w.buildings[u.building].alive) {
                    job = std::string("Miejsce: ") + buildingDef(w.buildings[u.building].type).name;
                }
                text(job, px + 14, py + 104, 0.45f, {0.75f, 0.75f, 0.7f, 1.0f});
            } else {
                text("Atak: " + std::to_string(static_cast<int>(def.damage)) + "   Zasięg: " +
                         std::to_string(static_cast<int>(def.range)) + "   PPM: ruch / atak, Ctrl+PPM: marsz bojowy, H: stój",
                     px + 14, py + 104, 0.45f, {0.75f, 0.75f, 0.7f, 1.0f});
            }
            return;
        }

        // Wiele jednostek
        std::map<std::string, int> counts;
        for (int id : selectedUnits_) counts[unitDef(w.units[id].type).name]++;
        std::string summary = "Zaznaczono: ";
        bool first = true;
        for (const auto& [name, c] : counts) {
            if (!first) summary += ", ";
            summary += std::to_string(c) + "x " + name;
            first = false;
        }
        text(summary, px + 14, py + 26, 0.62f, title);
        const int perRow = 18;
        for (size_t i = 0; i < selectedUnits_.size() && i < 54; ++i) {
            const Unit& u = w.units[selectedUnits_[i]];
            const UnitDef& def = unitDef(u.type);
            const float ix = px + 14 + static_cast<float>(i % perRow) * 24.0f;
            const float iy = py + 40 + static_cast<float>(i / perRow) * 22.0f;
            const Vec3 fc = u.type == UnitType::Settler ? Vec3{0.8f, 0.7f, 0.4f}
                                                         : (u.type == UnitType::Archer ? Vec3{0.4f, 0.7f, 0.3f} : Vec3{0.6f, 0.65f, 0.75f});
            gui_->addOverlayRect(ix, iy, 20, 14, {fc.x, fc.y, fc.z, 1.0f}, 0.96f);
            bar(ix, iy + 16, 20, 3, u.hp / def.maxHp, hpColor(u.hp / def.maxHp));
        }
        text("PPM: ruch / atak   Ctrl+PPM: marsz bojowy   H: stój   Tab: całe wojsko", px + 14, py + 120, 0.45f,
             {0.75f, 0.75f, 0.7f, 1.0f});
    }

    void drawHelp() {
        // Dwie kolumny sterowania (wspólna lista z oknem "Sterowanie") + zasady gry
        // Poniżej strefy komunikatów (do 6 linii pod górnym paskiem)
        const float w = std::min(screenW_ - 40.0f, 1100.0f), h = 450.0f;
        const float x = screenW_ * 0.5f - w * 0.5f, y = std::max(kTopBarH + 10.0f, std::min(kTopBarH + 180.0f, screenH_ - h - 20.0f));
        gui_->addOverlayRect(x - 3, y - 3, w + 6, h + 6, {0.55f, 0.45f, 0.25f, 0.95f}, 0.94f);
        gui_->addOverlayRect(x, y, w, h, {0.08f, 0.07f, 0.05f, 0.94f}, 0.935f);
        const Color headerC{1.0f, 0.9f, 0.6f, 1.0f};
        const size_t total = std::size(kControls);
        const size_t split = 17;   // początek sekcji BUDOWA - druga kolumna
        const float colW = w * 0.5f;
        for (int col = 0; col < 2; ++col) {
            float ly = y + 30.0f;
            const size_t from = col == 0 ? 0 : split, to = col == 0 ? split : total;
            for (size_t i = from; i < to; ++i) {
                const auto& c = kControls[i];
                const float cx = x + 18.0f + col * colW;
                if (c.key[0] == '\0') {
                    if (ly > y + 30.0f) ly += 6.0f;
                    text(c.desc, cx, ly, 0.55f, headerC, 0.93f);
                } else {
                    text(c.key, cx + 8.0f, ly, 0.46f, {0.75f, 0.9f, 1.0f, 1.0f}, 0.93f);
                    text(c.desc, cx + 185.0f, ly, 0.46f, {0.9f, 0.9f, 0.9f, 1.0f}, 0.93f);
                }
                ly += 21.0f;
            }
            if (col == 1) {
                static const char* rules[] = {
                    "Osadnicy sami budują i pracują. Zamek przyjmuje nowych za żywność.",
                    "Domy zwiększają limit ludności. Wieże poszerzają terytorium.",
                    "Osadnicy naprawiają budynki (1 drewno = 10% PŻ).",
                    "Jednostki poza walką leczą się - najszybciej przy zamku i koszarach.",
                    "Zniszcz wrogi zamek, by wygrać!",
                };
                ly += 8.0f;
                text("ROZGRYWKA", x + 18.0f + colW, ly, 0.55f, headerC, 0.93f);
                ly += 21.0f;
                for (const char* r : rules) {
                    text(r, x + 26.0f + colW, ly, 0.44f, {0.9f, 0.9f, 0.9f, 1.0f}, 0.93f);
                    ly += 20.0f;
                }
            }
        }
        text("F1 - zamknij pomoc   |   Menu (ESC) -> Sterowanie / Ustawienia", x + 18.0f, y + h - 12.0f, 0.44f,
             {0.7f, 0.7f, 0.7f, 1.0f}, 0.93f);
    }

    // =================================================================================

    Engine engine_;
    Renderer* renderer_ = nullptr;
    Scene* scene_ = nullptr;
    CameraComponent* camera_ = nullptr;
    Entity* sun_ = nullptr;
    Entity* ghost_ = nullptr;
    Entity* debugNavEntity_ = nullptr;
    float debugNavTimer_ = 0.0f;
    int debugMode_ = 0;
    RtsCameraController cam_;
    std::unique_ptr<World> world_;
    GameMaterials materials_;
    AudioSystem* audio_ = nullptr;
    bool musicEnabled_ = true;
    bool battleMusic_ = false;
    int peaceTrack_ = 0;
    std::shared_ptr<SkydomeRenderer> skydome_;
    std::shared_ptr<OceanRenderer> ocean_;
    float skyTime_ = 0.0f;
    float autoplaySeconds_ = 0.0f;
    int autoplaySpeed_ = 1;
    int gameSpeed_ = 1;          // ile kroków symulacji na klatkę (prędkość czasu)
    bool spectator_ = false;     // pokaz AI kontra AI
    GuiButton* btnSpeed_ = nullptr;
    float nextAutoplayLog_ = 0.0f;

    std::shared_ptr<GuiMgr> gui_;
    GuiWindow* menuWindow_ = nullptr;
    GuiWindow* pauseWindow_ = nullptr;
    GuiWindow* gameOverWindow_ = nullptr;
    GuiWindow* buildWindow_ = nullptr;
    GuiWindow* barracksWindow_ = nullptr;
    GuiWindow* actionsWindow_ = nullptr;
    GuiComboBox* comboDifficulty_ = nullptr;
    GuiWindow* settingsWindow_ = nullptr;
    GuiWindow* controlsWindow_ = nullptr;
    GuiWindow* subWindow_ = nullptr;     // otwarte okno ustawień / sterowania
    GuiWindow* subReturn_ = nullptr;     // okno, do którego wracamy (menu lub pauza)
    Settings settings_;
    std::vector<std::array<int, 2>> resolutions_;
    GuiCheckBox* checkMusic_ = nullptr;
    GuiCheckBox* checkSfx_ = nullptr;
    GuiCheckBox* checkFullscreen_ = nullptr;
    GuiCheckBox* checkShadows_ = nullptr;
    GuiTrackBar* barMusicVol_ = nullptr;
    GuiTrackBar* barSfxVol_ = nullptr;
    GuiTrackBar* barMasterVol_ = nullptr;
    GuiStaticText* lblMusicVol_ = nullptr;
    GuiStaticText* lblSfxVol_ = nullptr;
    GuiStaticText* lblMasterVol_ = nullptr;
    GuiComboBox* comboResolution_ = nullptr;
    GuiComboBox* comboPresent_ = nullptr;
    GuiComboBox* comboSync_ = nullptr;
    GuiComboBox* comboFps_ = nullptr;
    GuiStaticText* gameOverText_ = nullptr;
    GuiStaticText* gameOverStats_ = nullptr;
    std::array<GuiButton*, 6> buildButtons_{};
    std::array<BuildingType, 6> buildTypes_{};
    std::string hoverMissing_;   // podpowiedź brakujących surowców pod kursorem

    Mode mode_ = Mode::Menu;
    bool gameStarted_ = false;
    bool showHelp_ = false;
    bool mouseMoved_ = false;
    bool uiTest_ = false;
    int uiFrame_ = 0;
    int uiFailures_ = 0;
    int uiSitesBefore_ = 0;
    float uiDistBefore_ = 0.0f;
    bool uiMusicBefore_ = true;
    int uiOrigW_ = 0, uiOrigH_ = 0, uiResIdx_ = 0;
    int uiSpeedBefore_ = 1;
    float uiTimeBefore_ = 0.0f;
    std::chrono::steady_clock::time_point uiRealBefore_{};
    Vec3 uiTarget_{};
    Vec3 uiMoveTarget_{};
    std::array<float, 4> uiBox_{};
    float screenW_ = 1440.0f, screenH_ = 900.0f;
    int lastW_ = 0, lastH_ = 0;

    std::vector<int> selectedUnits_;
    int selectedBuilding_ = -1;
    bool dragging_ = false;
    int dragStartX_ = 0, dragStartY_ = 0;
    int selPanelX_ = 0, selPanelY_ = 0, selPanelW_ = 0;

    bool placing_ = false;
    BuildingType placeType_ = BuildingType::House;
    float placeRot_ = 0.0f;
    Vec3 ghostPos_{};
    std::string placeReason_;
};

} // namespace

int main() {
    SettlersApp app;
    if (!app.initialize()) {
        return 1;
    }
    app.run();
    return 0;
}
