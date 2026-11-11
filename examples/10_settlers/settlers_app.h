#pragma once
/**
 * settlers_app.h - aplikacja gry "Osadnicy" (warstwa prezentacji i sterowania).
 *
 * PODZIAŁ ODPOWIEDZIALNOŚCI
 * -------------------------
 * Gra jest podzielona na dwie warstwy:
 *
 *   World (settlers_world.h)   - SYMULACJA: teren, jednostki, budynki, ekonomia, walka, AI.
 *                                Nie wie nic o myszy, klawiaturze ani o oknach GUI.
 *
 *   SettlersApp (ten plik)     - PREZENTACJA I STEROWANIE: silnik, kamera, wejście gracza,
 *                                menu, HUD, dźwięk, tryb debug i testy automatyczne.
 *                                Czyta stan świata i wydaje mu rozkazy.
 *
 * Metody klasy SettlersApp są rozdzielone na pliki wg tematu:
 *
 *   app_core.cpp   - start aplikacji, tworzenie świata, tryby gry (menu/gra/pauza), pętla klatki
 *   app_menus.cpp  - okna GUI: menu główne, pauza, ustawienia, sterowanie; zapis ustawień
 *   app_input.cpp  - kamera, zaznaczanie jednostek, rozkazy, stawianie budynków
 *   app_hud.cpp    - rysowanie HUD: górny pasek, komunikaty, paski życia, minimapa, panel, pomoc
 *   app_buildbar.cpp - dolny pasek budowy: ikony budynków (pixel-art), karta informacji, kliknięcia
 *   app_audio.cpp  - efekty dźwiękowe i muzyka zależna od sytuacji w grze
 *   app_debug.cpp  - tryb debug (F3): ścieżki, stany, siatka kolizji; zrzut stanu (F9)
 *   app_save.cpp   - zapis / wczytanie gry: plik, kamera, przyciski i klawisze (F5 / F8)
 *   app_tests.cpp  - testy automatyczne (SETTLERS_UITEST) i rozgrywka AI z logami (SETTLERS_AUTOPLAY)
 *
 * PRZEPŁYW JEDNEJ KLATKI (SettlersApp::update)
 * --------------------------------------------
 *   1. GUI odczytuje mysz (kliknięcia w przyciski).
 *   2. Zależnie od trybu: obsługa kamery i wejścia gracza, krok symulacji świata (World::update).
 *   3. Kamera, niebo, dźwięk.
 *   4. Rysowanie HUD i (opcjonalnie) informacji debug.
 * Renderowanie sceny 3D wykonuje silnik po zakończeniu update.
 */

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "camera/camera_component.h"
#include "camera/camera_picking.h"
#include "camera/rts_camera_controller.h"
#include "core/engine.h"
#include "core/game_settings.h"
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
#include "feature/gui/gui_toolbar.h"
#include "feature/gui/gui_track_bar.h"
#include "object/ocean_renderer.h"
#include "object/skydome_renderer.h"
#include "object/terrain/terrain_system.h"
#include "render/renderer.h"
#include "settlers_assets.h"
#include "settlers_world.h"

namespace settlers {

using namespace engine3d;

// ======================================================================================
// Stałe interfejsu
// ======================================================================================

inline constexpr const char* kFont = "times";      // jedyna czcionka używana przez HUD
inline constexpr float kHudDepth = 0.93f;          // głębokość napisów HUD (okna GUI leżą w 0.1..0.9)
inline constexpr float kTopBarH = 34.0f;           // wysokość górnego paska surowców (px)
inline constexpr const char* kSoundDir = "assets/sound/settlers/";
inline constexpr const char* kSettingsPath = "save/settlers_settings.json";
inline constexpr const char* kSavePath = "save/settlers_save.json";   // zapis gry (F5 / F8)

// Nazwy akcji wejścia. Klawisze przypisuje SettlersApp::setupInput, a sprawdza się je
// przez engine.actionDown/actionPressed - dzięki temu kod gry nie zna kodów klawiszy.
inline constexpr const char* kActPanLeft = "cam.left";
inline constexpr const char* kActPanRight = "cam.right";
inline constexpr const char* kActPanUp = "cam.up";
inline constexpr const char* kActPanDown = "cam.down";
inline constexpr const char* kActRotL = "cam.rot_left";
inline constexpr const char* kActRotR = "cam.rot_right";
inline constexpr const char* kActHome = "cam.home";
inline constexpr const char* kActZoomIn = "cam.zoom_in";
inline constexpr const char* kActZoomOut = "cam.zoom_out";
inline constexpr const char* kActEsc = "menu.back";
inline constexpr const char* kActRotateBuilding = "build.rotate";
inline constexpr const char* kActDemolish = "build.demolish";
inline constexpr const char* kActHalt = "unit.halt";
inline constexpr const char* kActHelp = "ui.help";
inline constexpr const char* kActSelectArmy = "unit.select_army";
inline constexpr const char* kActMuteMusic = "audio.mute_music";
inline constexpr const char* kActDebug = "debug.cycle";
inline constexpr const char* kActDebugDump = "debug.dump";
inline constexpr const char* kActSlower = "time.slower";
inline constexpr const char* kActFaster = "time.faster";
inline constexpr const char* kActRoad = "build.road";
inline constexpr const char* kActEconomy = "ui.economy";
inline constexpr const char* kActSaveGame = "game.save";
inline constexpr const char* kActLoadGame = "game.load";

// Budynki w panelu budowy. Pierwsze 9 ma klawisze 1..9, pozostałe stawia się z paska.
inline constexpr BuildingType kBuildMenu[] = {
    BuildingType::House, BuildingType::Woodcutter, BuildingType::Forester, BuildingType::Quarry,
    BuildingType::Mine, BuildingType::Farm, BuildingType::Barracks, BuildingType::Tower, BuildingType::Warehouse,
    // Łańcuchy produkcji (bez klawiszy)
    BuildingType::Sawmill, BuildingType::Mill, BuildingType::Bakery, BuildingType::Fisher, BuildingType::Hunter,
    BuildingType::CoalMine, BuildingType::OreMine, BuildingType::Smelter, BuildingType::Smithy,
};
inline constexpr int kHotkeyBuildings = 9;
inline constexpr int kBuildMenuSize = static_cast<int>(std::size(kBuildMenu));

// Kolor RGBA używany przez GUI i napisy.
using Color = std::array<float, 4>;

// Tryb aplikacji - decyduje, co dzieje się w każdej klatce (patrz SettlersApp::update).
enum class Mode {
    Menu,      // menu główne; w tle świat żyje (AI przeciwnika wyłączone)
    Game,      // właściwa rozgrywka
    Pause,     // pauza - symulacja zatrzymana
    GameOver,  // koniec partii - okno z wynikiem
};

// ======================================================================================
// Sterowanie - jedna lista dla okna "Sterowanie" i pomocy F1
// ======================================================================================

struct ControlLine {
    const char* key;   // pusty napis oznacza nagłówek sekcji
    const char* desc;
};

inline const ControlLine kControls[] = {
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
    {"PPM na własnej wieży", "obsadzenie wieży (załoga do 3, wieża strzela tylko z załogą)"},
    {"PPM na wrogiej wieży", "szturm: obrońcy wychodzą, pustą wieżę zdobywa żołnierz"},
    {"", "BUDOWA"},
    {"1 - 9", "Dom, Drwal, Leśniczy, Kamieniarz, Kopalnia, Farma, Koszary, Wieża, Magazyn"},
    {"pasek na dole", "pozostałe: Tartak, Młyn, Piekarnia, Rybak, Myśliwy, kopalnie, Huta, Kuźnia"},
    {"LPM / PPM", "postawienie budynku / anulowanie"},
    {"R", "obrót budynku przed postawieniem"},
    {"Shift + LPM", "postawienie kilku budynków z rzędu"},
    {"T", "droga: LPM na fladze/budynku, potem LPM kolejne punkty, PPM koniec"},
    {"Del", "zburzenie budynku (zwrot 50%) / usunięcie flagi z drogami"},
    {"", "INNE"},
    {"I", "gospodarka: zapasy, magazyny, tragarze"},
    {"ESC", "pauza / powrót z okna"},
    {"F1", "pomoc na ekranie gry"},
    {"M", "muzyka wł. / wył."},
    {"[  /  ]", "wolniej / szybciej (x1, x2, x4, x8, x16, x24)"},
    {"F5 / F8", "zapis gry / wczytanie zapisu"},
    {"F3 / F9", "tryb debug / zrzut stanu do konsoli"},
};

// ======================================================================================
// Ustawienia gracza (zapisywane w save/settlers_settings.json)
// ======================================================================================

// Ustawienia gracza: engine3d::GameSettings (core/game_settings.h) - wspólne dla gier na silniku.
using Settings = GameSettings;

// ======================================================================================
// Funkcje pomocnicze
// ======================================================================================

// Wysyła do SDL zdarzenie zamknięcia programu (silnik zakończy pętlę główną).
inline void pushQuitEvent() {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}

// Opis stanu jednostki dla gracza (np. "Ścina drzewo"), wyświetlany w panelu zaznaczenia.
inline const char* stateText(const Unit& u, const World& w) {
    switch (u.state) {
    case UnitState::Idle: return u.type == UnitType::Settler ? "Czeka na zadanie" : "Na posterunku";
    case UnitState::Moving: return u.attackMove ? "Marsz bojowy" : "W marszu";
    case UnitState::GoBuild: return "Idzie na budowę";
    case UnitState::Building: return "Buduje";
    case UnitState::GoToWork: return "Idzie do pracy";
    case UnitState::WorkWait: return "Odpoczywa w pracy";
    case UnitState::GoGather:
    case UnitState::Gathering: {
        if (u.building < 0) return "Pracuje";
        const bool going = u.state == UnitState::GoGather;
        const ProductionDef& p = productionDef(w.buildings[u.building].type);
        switch (p.kind) {
        case WorkKind::Gather:
            if (p.output == CarryType::Wood) return going ? "Idzie do drzewa" : "Ścina drzewo";
            return going ? "Idzie do skały" : "Kuje kamień";
        case WorkKind::Farm: return going ? "Idzie na pole" : "Pracuje w polu";
        case WorkKind::Forester: return going ? "Idzie sadzić drzewo" : "Sadzi drzewo";
        case WorkKind::Fisher: return going ? "Idzie nad wodę" : "Łowi ryby";
        case WorkKind::Hunter: return going ? "Idzie na polowanie" : "Poluje";
        case WorkKind::Mine: return "Pracuje w kopalni";
        case WorkKind::Process: return "Pracuje w warsztacie";
        case WorkKind::None: break;
        }
        return "Pracuje";
    }
    case UnitState::ReturnHome: return "Niesie surowiec";
    case UnitState::Attack: return u.homeTower >= 0 ? "Broni wieży" : "Walczy";
    case UnitState::GoGarrison: return "Idzie obsadzić wieżę";
    case UnitState::Garrison: return "W załodze wieży";
    case UnitState::GoToRoad:
        if (u.jobFlag >= 0) return "Tragarz - idzie po towar do warsztatu";
        if (u.jobOrder >= 0) return "Tragarz - idzie do magazynu po materiały";
        return "Tragarz - wraca do magazynu";
    case UnitState::CarrierWait: return "Tragarz - czeka na zlecenie";
    case UnitState::CarrierWalk: return u.jobOrder >= 0 ? "Tragarz - niesie materiały" : "Tragarz - niesie towar do magazynu";
    }
    return "";
}

// Kolor paska życia: zielony przy pełnym zdrowiu, przez żółty do czerwonego.
inline Color hpColor(float frac) {
    frac = std::clamp(frac, 0.0f, 1.0f);
    return {std::min(1.0f, 2.0f * (1.0f - frac)), std::min(1.0f, 2.0f * frac), 0.15f, 1.0f};
}

// ======================================================================================
// Aplikacja
// ======================================================================================

/**
 * Aplikacja gry: łączy silnik, świat gry, kamerę, sterowanie, interfejs i dźwięk.
 */
class SettlersApp {
public:
    // Inicjalizuje silnik, zasoby, GUI i dźwięk oraz tworzy pierwszy świat (tło menu).
    bool initialize();
    // Uruchamia pętlę główną silnika (wraca po zamknięciu okna).
    void run() { engine_.run(); }

private:
    // ----------------------------------------------------------------------------------
    // app_core.cpp - start, świat, tryby gry, pętla klatki
    // ----------------------------------------------------------------------------------

    // Przypisuje klawisze do nazwanych akcji (kAct...).
    void setupInput();
    // Tworzy niebo (skydome) i wodę (ocean) - efekty renderera wspólne dla wszystkich partii.
    void setupSkyAndWater();
    // Tworzy od nowa scenę gry: kamerę, słońce, świat (teren, bazy, surowce) i podgląd budynku.
    // Nowy świat z ziarna albo (save != nullptr) odtworzony z zapisu; false = zapisu nie dało się odtworzyć.
    bool newWorld(uint32_t seed, Difficulty diff, const nlohmann::json* save = nullptr, std::string* error = nullptr);
    // app_save.cpp - zapis i wczytanie gry (F5 / F8, przyciski w menu i pauzie)
    bool saveGame(const std::string& path = kSavePath);
    // Okno końca gry: po zwycięstwie - wolna gra (dalsza rozbudowa osady), po porażce - zamyka okno (oglądanie mapy).
    void continueAfterGameOver();
    // Czy symulacja biegnie: gra trwa albo wolna gra po zwycięstwie.
    [[nodiscard]] bool simulationRunning() const { return world_ && (world_->winner < 0 || freePlay_); }
    // Krok gry w trakcie partii: symulacja w krótkich krokach (prędkość gry) + prezentacja raz na klatkę.
    void advanceSimulation(float dt);
    void freePlayTest();
    void fogOfWarTest();
    void spatialGridTest();
    void slotReuseTest();
    void aiFogTest();
    void staleRefTest();
    bool loadGame(const std::string& path = kSavePath);
    [[nodiscard]] bool saveExists(const std::string& path = kSavePath) const;
    // Rozpoczyna partię z poziomem trudności z menu. spectator = pokaz "AI kontra AI".
    void startNewGame(bool spectator = false);

    // Dostępne prędkości czasu (kroków symulacji na klatkę).
    static constexpr int kSpeeds[] = {1, 2, 4, 8, 16, 24};
    // Zmienia prędkość czasu o jeden stopień (dir = -1 wolniej, +1 szybciej).
    void changeGameSpeed(int dir);
    // Przełącza prędkość w kółko x1 -> x2 -> ... -> x24 -> x1 (przycisk w pauzie).
    void cycleGameSpeed();
    // Aktualizuje napis na przycisku prędkości w menu pauzy.
    void updateSpeedButton();

    // Przejścia między trybami - każde ustawia mode_ i pokazuje odpowiednie okna.
    void hideAllWindows();
    void showMenu();
    void enterGame();
    void showPause();
    void showGameOver();

    // Główna funkcja klatki wywoływana przez silnik (engine.onUpdate).
    void update(float dt, Engine& e);
    // Klawisze działające w każdym trybie (F1, M, F3, F9).
    void handleGlobalKeys(Engine& e);
    // Logika bieżącego trybu (menu / gra / pauza / koniec gry).
    void updateMode(float dt, Engine& e, const InputSnapshot& in);
    // Pora dnia, kierunek słońca, światło otoczenia i mgła.
    void updateSky(float dt);
    // W menu kamera powoli krąży nad mapą.
    void updateMenuCamera(float dt);
    // Zakres kaskad cieni dopasowany do widoku kamery RTS.
    void updateShadowRange();

    SceneManager* sceneManager() { return engine_.getSceneManager(); }

    // ----------------------------------------------------------------------------------
    // app_menus.cpp - okna GUI i ustawienia
    // ----------------------------------------------------------------------------------

    // Tworzy wszystkie okna GUI (menu, pauza, koniec gry, panel budowy, koszary, akcje).
    void setupGui();
    // Ustawia pozycje okien względem rozmiaru ekranu (wołane też po zmianie rozdzielczości).
    void layoutGui();
    // Tworzy przycisk; kliknięcie odtwarza dźwięk i wywołuje fn.
    GuiButton* makeButton(GuiWindow* w, const std::string& name, int x, int y, int bw, int bh,
                          const std::string& text, std::function<void()> fn);
    // Tworzy napis w oknie (y = linia bazowa tekstu).
    GuiStaticText* label(GuiWindow* w, int x, int y, const std::string& t, const Color& c = {1, 1, 1, 1});

    // Okna "Ustawienia" i "Sterowanie" otwierane z menu głównego lub pauzy.
    void openSubWindow(GuiWindow* window, GuiWindow* returnTo);
    // Zamyka okno podrzędne i wraca do okna, z którego je otwarto.
    void closeSubWindow();
    void setupSettingsWindow();
    void setupControlsWindow();
    // Lista rozdzielczości, które mieszczą się na ekranie.
    void buildResolutionList();
    // Ustawia widgety okna ustawień zgodnie z settings_.
    void syncSettingsWidgets();

    // Stosowanie ustawień w silniku i systemie dźwięku.
    void applyAudioSettings();
    void applyPresentMode();
    void applySyncMode();
    // Stosuje wszystkie ustawienia (startup = true przy starcie gry).
    void applySettings(bool startup);
    void loadSettings();
    void saveSettings();

    // ----------------------------------------------------------------------------------
    // app_input.cpp - kamera, zaznaczanie, rozkazy, budowa
    // ----------------------------------------------------------------------------------

    // Przesuwanie (klawisze, krawędź ekranu), obrót, pochylenie i zoom kamery.
    void updateCameraInput(float dt, Engine& e, const InputSnapshot& in);
    // Obsługa myszy i skrótów w trakcie gry (zaznaczanie, rozkazy, budowa).
    void handleGameInput(Engine& e, const InputSnapshot& in);
    // Promień z kamery przez kursor myszy (do wskazywania obiektów w świecie).
    Ray mouseRay(const InputSnapshot& in) const;

    // Minimapa w prawym dolnym rogu ekranu.
    struct MinimapRect {
        float x, y, size;
    };
    MinimapRect minimapRect() const;
    // Czy kursor jest nad minimapą; opcjonalnie zwraca wskazany punkt świata.
    bool mouseInMinimap(int mx, int my, Vec3* worldPos = nullptr) const;
    // Czy kursor jest nad jakimkolwiek elementem interfejsu (wtedy klik nie trafia w świat).
    bool mouseOverUi(int mx, int my) const;
    bool hasSelectionPanel() const { return !selectedUnits_.empty() || selectedBuilding_ >= 0 || selectedFlag_ >= 0; }

    // Zaznaczanie
    void clickSelect(const InputSnapshot& in, bool additive);
    void boxSelect(int x0, int y0, int x1, int y1, bool additive);
    void selectAllArmy();
    void clearSelection();
    // Usuwa z zaznaczenia jednostki i budynki, które zginęły.
    void cleanupSelection();
    // Zaznaczeni żołnierze gracza (osadnicy nie przyjmują rozkazów).
    std::vector<int> selectedSoldiers() const;

    // Rozkazy dla wojska
    // PPM: atak na wroga pod kursorem albo marsz w wskazane miejsce.
    void issueOrderAtCursor(const InputSnapshot& in, bool attackMove);
    void issueMoveOrder(const Vec3& p, bool attackMove);
    void haltSelected();

    // Stawianie budynków: półprzezroczysty podgląd podąża za kursorem.
    void beginPlacement(BuildingType type);
    void cancelPlacement();
    // Zielony podgląd = można budować, czerwony = nie można.
    void setGhostMaterial(bool ok);
    void updatePlacement(Engine& e, const InputSnapshot& in, bool overUi, bool shift);

    // Budowa dróg (klawisz T): pierwszy klik - flaga/budynek początkowy, kolejne - punkty końcowe.
    void beginRoad(int fromFlag = -1);
    void cancelRoad();
    void updateRoadMode(Engine& e, const InputSnapshot& in, bool overUi, bool shift);
    // Flaga pod kursorem: flaga budynku pod kursorem albo flaga w pobliżu punktu terenu.
    int flagUnderCursor(const InputSnapshot& in, const Vec3& groundHit) const;
    // Przycisk "Połącz drogą": automatyczna droga od zaznaczonego budynku do sieci magazynu.
    void connectSelected();

    // Akcje zaznaczonego budynku
    void trainSelected(UnitType type);
    void demolishSelected();
    // Przycisk "Wyprowadź załogę" (zaznaczona własna wieża).
    void releaseGarrisonSelected();
    // Koszary: włączenie rekrutacji automatycznej i zmiana udziału mieczników (krok 10%).
    void toggleAutoTrainSelected();
    void changeSwordShareSelected(int delta);
    // Okna przy lewej krawędzi (koszary, akcje) jedno nad drugim, nad paskiem budowy.
    void layoutSideWindows();
    // Pokazuje okna koszar/akcji tylko dla zaznaczonego budynku gracza; koloruje przyciski budowy.
    void updateContextWindows();

    // ----------------------------------------------------------------------------------
    // app_hud.cpp - rysowanie HUD (każdą klatkę od nowa, w trybie "immediate")
    // ----------------------------------------------------------------------------------

    void text(const std::string& s, float x, float y, float scale, const Color& c, float depth = kHudDepth);
    void textCentered(const std::string& s, float cx, float y, float scale, const Color& c, float depth = kHudDepth);
    // Pasek postępu/życia z czarną obwódką.
    void bar(float x, float y, float w, float h, float frac, const Color& fill);
    // Rysuje cały HUD (wywołuje funkcje poniżej).
    void drawHud(Engine& e, const InputSnapshot& in);
    // Górny pasek: surowce, ludność, wojsko, czas, FPS.
    void drawTopBar(Engine& e);
    // Komunikaty gry wyświetlane na środku u góry, zanikające po kilku sekundach.
    void drawMessages();
    // Paski życia nad jednostkami/budynkami i znaczniki problemów ("! brak drzew").
    void drawWorldBars();
    void drawMinimap();
    // Panel informacji o zaznaczonej jednostce/budynku (dół ekranu).
    void drawSelectionPanel();
    // Linia tekstu panelu z kolorem (stan budynku, podpowiedź).
    struct PanelText {
        std::string text;
        Color color{0.9f, 0.9f, 0.88f, 1.0f};
    };
    void drawFlagPanel(float px, float py);
    void drawBuildingPanel(float px, float py);
    void drawUnitPanel(float px, float py);
    void drawGroupPanel(float px, float py);
    void describeBuildingState(const Building& b, PanelText& status, PanelText& hint) const;
    void describeBuildingRepair(const Building& b, PanelText& status) const;
    void describeBuildingGoods(const Building& b, PanelText& status, PanelText& hint) const;
    // Pomoc F1: sterowanie i zasady gry.
    void drawHelp();

    // ----------------------------------------------------------------------------------
    // app_buildbar.cpp - dolny pasek budowy
    // ----------------------------------------------------------------------------------

    // Pasek to GuiToolbar z silnika (feature/gui/gui_toolbar.h); gra dostarcza ikony i treść kart.
    // Identyfikatory przycisków: indeks w kBuildMenu (0-8, jak klawisze 1-9) albo kBarRoad.
    static constexpr int kBarRoad = 100;
    // Czy pasek jest widoczny (gra, nie pokaz AI).
    [[nodiscard]] bool buildBarVisible() const { return mode_ == Mode::Game && !spectator_; }
    // Tworzy przyciski paska (ikony, opisy) - raz, w setupGui.
    void setupBuildBar();
    // Co klatkę: układ, stan przycisków (stać nas? aktywny?) i karty z kosztem.
    void refreshBuildBar();
    [[nodiscard]] bool mouseOverBuildBar(int mx, int my) const;
    // Środek przycisku (dla testów UI i zrzutów).
    bool buildBarSlotCenter(int id, float& x, float& y) const;
    // Klik w pasek: true = kliknięcie obsłużone (nie trafia w świat).
    bool handleBuildBar(Engine& e, const InputSnapshot& in);
    void drawBuildBar();
    // Okno gospodarki (I): towary w magazynach / na flagach / w drodze, magazyny, tragarze, przestoje.
    void drawEconomy();
    void economyPanelRect(float& x, float& y, float& w, float& h) const;
    static std::string stockSummary(const Resources& r, int maxItems);
    // Przyciski liczby tragarzy (okienko pod panelem gospodarki).
    void changeCarriers(int delta);
    void toggleCarrierAuto();
    // Limit zapasu towaru: krok kStockLimitStep, powyżej kStockLimitMax - "bez limitu".
    void changeStockLimit(CarryType good, int delta);
    // Kliknięcie w przycisk limitu (true = obsłużone).
    bool clickLimitButton(int mx, int my);
    // Podgląd trasy budowanej drogi.
    void drawRoadPreview(const InputSnapshot& in);
    static float length2(const Vec3& v) { return dot(v, v); }

    // ----------------------------------------------------------------------------------
    // app_audio.cpp - dźwięk
    // ----------------------------------------------------------------------------------

    // Wczytuje efekty dźwiękowe i ustawia limity ich jednoczesnego odtwarzania.
    void setupAudio();
    // Odtwarza efekt interfejsu (bez położenia w świecie).
    void sfx(const char* name, float volume = 1.0f);
    // Uruchamia utwór (z przenikaniem), jeśli nie gra już ten sam.
    void playMusicTrack(const std::string& file, bool loop, float fade = 2.5f);
    // Odtwarza dźwięki zgłoszone przez symulację i wybiera muzykę (menu / spokój / bitwa).
    void updateAudio();
    // Klawisz M - włącza/wyłącza muzykę.
    void toggleMusic();

    // ----------------------------------------------------------------------------------
    // app_debug.cpp - tryb debug (F3) i zrzut stanu (F9)
    // ----------------------------------------------------------------------------------

    // Linia na ekranie złożona z małych prostokątów (GUI rysuje tylko prostokąty).
    void debugLine(float x0, float y0, float x1, float y1, const Color& c, float thick = 2.0f, float gap = 0.0f);
    // Punkt świata -> piksel ekranu (false, gdy za kamerą lub daleko poza ekranem).
    bool project(const Vec3& p, float& sx, float& sy) const;
    // Punkt na powierzchni terenu (z podniesieniem o lift).
    Vec3 onGround(const Vec3& p, float lift = 1.0f) const { return {p.x, world_->heightAt(p.x, p.z) + lift, p.z}; }
    // Kolor stanu jednostki w trybie debug.
    static Color stateColor(UnitState s);
    // Ścieżki, cele, etykiety jednostek i budynków, panel statystyk.
    void drawDebug(const InputSnapshot& in);
    // Siatka kolizji wokół kamery (tryb 2) jako półprzezroczysta siatka 3D.
    void updateDebugNavMesh(float dt);
    // F9: pełny opis zaznaczonych i zawieszonych jednostek w konsoli.
    void dumpDebug();

    // ----------------------------------------------------------------------------------
    // app_tests.cpp - testy automatyczne
    // ----------------------------------------------------------------------------------

    // Wstrzykiwanie zdarzeń SDL - trafiają do silnika tak samo jak prawdziwa mysz/klawiatura.
    static void pushMouseMotion(float x, float y);
    static void pushMouseButton(Uint8 button, bool down);
    static void pushKey(SDL_Scancode sc, bool down);
    // Zapisuje wynik pojedynczego sprawdzenia w logu (OK / BŁĄD).
    void uiCheck(const char* name, bool ok);
    // Kolejny krok scenariusza testu UI (SETTLERS_UITEST=1); jeden krok na klatkę.
    void runUiTestStep();
    // Testy symulacji wywoływane ze scenariusza UI.
    void crowdAndAccessTest();
    void repairHealResourceTest();
    void mountainBuildingsTest();
    void territoryCaptureTest();
    void towerAssaultTest();
    void economyRoadTest();
    void productionChainTest();
    void storageJamTest();
    void autoRecruitTest();
    void aiPlannerTest();
    void saveLoadTest();
    void showcaseBuildings();
    bool constructionStressTest();
    // Log stanu partii w trybie SETTLERS_AUTOPLAY i zakończenie po zadanym czasie.
    void updateAutoplay();

    // ==================================================================================
    // Dane
    // ==================================================================================

    // --- Silnik i scena ---
    Engine engine_;
    Renderer* renderer_ = nullptr;
    Scene* scene_ = nullptr;                  // scena "game" (tworzona od nowa przy każdej partii)
    CameraComponent* camera_ = nullptr;
    RtsCameraController cam_;                 // sterowanie kamerą strategiczną
    Entity* sun_ = nullptr;                   // światło kierunkowe słońca
    std::shared_ptr<SkydomeRenderer> skydome_;
    std::shared_ptr<OceanRenderer> ocean_;
    float skyTime_ = 0.0f;
    float shadowStart_ = -1.0f, shadowEnd_ = -1.0f;
    float screenW_ = 1440.0f, screenH_ = 900.0f;
    int lastW_ = 0, lastH_ = 0;               // rozmiar ekranu przy ostatnim layoutGui

    // --- Świat gry ---
    std::unique_ptr<World> world_;
    GameMaterials materials_;
    Mode mode_ = Mode::Menu;
    bool gameStarted_ = false;                // trwa partia (można ją kontynuować z menu)
    bool spectator_ = false;                  // pokaz "AI kontra AI"
    int gameSpeed_ = 1;                       // kroków symulacji na klatkę (prędkość czasu)

    // --- Dźwięk ---
    AudioSystem* audio_ = nullptr;
    bool musicEnabled_ = true;
    bool battleMusic_ = false;
    int peaceTrack_ = 0;                      // który ze spokojnych utworów gra

    // --- Zaznaczenie i stawianie budynków ---
    std::vector<int> selectedUnits_;
    int selectedBuilding_ = -1;
    bool dragging_ = false;                   // trwa przeciąganie ramki zaznaczenia
    int dragStartX_ = 0, dragStartY_ = 0;
    int selPanelX_ = 0, selPanelY_ = 0, selPanelW_ = 0, selPanelH_ = 0;
    GuiToolbar buildBar_;                     // dolny pasek budowy (komponent GUI silnika)
    bool placing_ = false;                    // tryb stawiania budynku
    BuildingType placeType_ = BuildingType::House;
    float placeRot_ = 0.0f;
    Vec3 ghostPos_{};
    std::string placeReason_;                 // dlaczego nie można tu budować
    Entity* ghost_ = nullptr;                 // półprzezroczysty podgląd budynku
    bool mouseMoved_ = false;                 // przewijanie krawędzią dopiero po ruchu myszy
    bool showHelp_ = false;
    bool showEconomy_ = false;                // okno gospodarki (I)
    // Przyciski -/+ limitów zapasu w oknie gospodarki (prostokąty z ostatniego rysowania).
    struct LimitButton {
        float x = 0, y = 0, size = 0;
        CarryType good = CarryType::None;
        int delta = 0;
    };
    std::vector<LimitButton> limitButtons_;
    float slowAt_ = 0.0f;   // SETTLERS_SLOWAT: czas gry, od którego autoplay biegnie x1
    bool autoplaySaved_ = false;   // SETTLERS_SAVEAT: zapis już wykonany
    // Wolna gra po zwycięstwie ("Graj dalej"): symulacja biegnie dalej, okno końca gry się nie pokazuje.
    bool freePlay_ = false;
    uint64_t fogVersionSent_ = ~0ull;   // wersja tekstury mgły wojny wysłana do renderera
    // Minimapa jako obrazek GUI: przeliczany po zmianie świata / mgły wojny.
    int minimapImage_ = -1;
    uint64_t minimapFogVersion_ = ~0ull;   // ~0 = do przeliczenia (nowy świat)
    GuiButton* btnGameOverContinue_ = nullptr;
    int selectedFlag_ = -1;

    // --- Budowa dróg ---
    bool roadMode_ = false;
    int roadStart_ = -1;                      // flaga, od której biegnie budowana droga (-1 = jeszcze nie wybrana)
    std::vector<Vec3> roadPreview_;           // podgląd trasy (planRoad)
    bool roadPreviewOk_ = false;
    std::string roadReason_;                  // dlaczego nie można tu poprowadzić drogi
    Vec3 roadPreviewTarget_{1e9f, 0.0f, 1e9f};

    // --- Okna GUI ---
    std::shared_ptr<GuiMgr> gui_;
    GuiWindow* menuWindow_ = nullptr;
    GuiWindow* pauseWindow_ = nullptr;
    GuiWindow* gameOverWindow_ = nullptr;
    GuiWindow* barracksWindow_ = nullptr;
    GuiWindow* actionsWindow_ = nullptr;
    GuiWindow* settingsWindow_ = nullptr;
    GuiWindow* controlsWindow_ = nullptr;
    GuiWindow* subWindow_ = nullptr;          // otwarte okno ustawień / sterowania
    GuiWindow* subReturn_ = nullptr;          // okno, do którego wracamy (menu lub pauza)
    GuiComboBox* comboDifficulty_ = nullptr;
    GuiButton* btnSpeed_ = nullptr;
    GuiStaticText* gameOverText_ = nullptr;
    GuiStaticText* gameOverStats_ = nullptr;
    GuiButton* btnAutoTrain_ = nullptr;       // "Auto-rekrutacja" w oknie koszar
    GuiButton* btnTrainRatio_ = nullptr;      // napis z proporcją mieczników / łuczników
    GuiButton* btnConnect_ = nullptr;         // "Połącz drogą" w oknie akcji budynku
    GuiWindow* carriersWindow_ = nullptr;     // przyciski -/+/Auto liczby tragarzy (przy oknie gospodarki)
    GuiButton* btnCarrierAuto_ = nullptr;
    std::string hoverMissing_;                // brakujące surowce budynku pod kursorem (karta na pasku budowy)

    // --- Okno ustawień ---
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

    // --- Debug ---
    int debugMode_ = 0;                       // 0 wył., 1 ścieżki/stany, 2 + siatka kolizji
    Entity* debugNavEntity_ = nullptr;
    float debugNavTimer_ = 0.0f;

    // --- Testy automatyczne ---
    float autoplaySeconds_ = 0.0f;            // > 0: tryb SETTLERS_AUTOPLAY
    int autoplaySpeed_ = 1;
    float nextAutoplayLog_ = 0.0f;
    bool uiTest_ = false;                     // tryb SETTLERS_UITEST
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
};

} // namespace settlers
