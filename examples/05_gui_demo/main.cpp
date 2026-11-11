/**
 * 05_gui_demo - wszystkie widgety GUI silnika w kilku oknach nad prostą sceną 3D.
 *
 * GuiMgr (feature/gui) to osobny etap rysowania: okna z widgetami (przyciski, pola wyboru, suwaki, listy,
 * listy rozwijane, paski przewijania, notatnik), nakładka HUD (paski, linie, napisy) i pasek narzędzi
 * z ikonami pixel-art (GuiToolbar - ten sam, którego używa pasek budowy w 10_settlers).
 * Sterowanie: WASD + mysz - kamera, F1 - kursor, F2 - belka okna sterowania, Esc - wyjście.
 */
#include <SDL3/SDL.h>

#include <cmath>
#include <memory>
#include <string>

#include "camera/camera_component.h"
#include "camera/fly_camera.h"
#include "core/engine.h"
#include "core/entity.h"
#include "core/mesh_component.h"
#include "feature/gui/gui.h"
#include "feature/gui/gui_button.h"
#include "feature/gui/gui_check_box.h"
#include "feature/gui/gui_combo_box.h"
#include "feature/gui/gui_list_box.h"
#include "feature/gui/gui_memo.h"
#include "feature/gui/gui_scroll_bar.h"
#include "feature/gui/gui_static_text.h"
#include "feature/gui/gui_toolbar.h"
#include "feature/gui/gui_track_bar.h"
#include "render/renderer.h"

using namespace engine3d;

namespace {

const Vec3 kCameraStart{0.0f, 3.0f, -8.0f};

// Szara podłoga i kamera - scena 3D "pod" interfejsem.
CameraComponent* createBackgroundScene(Engine& engine, Scene* scene) {
    MeshData floor;
    floor.vertices = {
        {{{-10, 0, -10}}, {{0, 1, 0}}, {{0.3f, 0.3f, 0.3f}}, {{0, 0}}},
        {{{ 10, 0, -10}}, {{0, 1, 0}}, {{0.3f, 0.3f, 0.3f}}, {{10, 0}}},
        {{{ 10, 0,  10}}, {{0, 1, 0}}, {{0.3f, 0.3f, 0.3f}}, {{10, 10}}},
        {{{-10, 0,  10}}, {{0, 1, 0}}, {{0.3f, 0.3f, 0.3f}}, {{0, 10}}},
    };
    floor.indices = {0, 2, 1, 0, 3, 2};
    engine.getMeshManager()->loadMesh("floor", floor);
    Material material;
    material.name = "floor";
    scene->createEntity("floor")->addComponent<MeshComponent>("floor")->materialHandle =
        engine.getMaterialManager()->add(material);

    auto* camera = scene->createEntity("camera")->addComponent<CameraComponent>();
    camera->getOwner()->transform().position = kCameraStart;
    camera->fieldOfViewDegrees = 50;
    camera->farPlane = 16000;
    scene->setActiveCamera(camera);
    engine.getCameraController()->setPositionAndTarget(kCameraStart, {0, 0, 0});
    return camera;
}

// GuiMgr dostaje od silnika funkcje rysowania i mierzenia tekstu (czcionka "times").
std::shared_ptr<GuiMgr> createGui(Engine& engine) {
    auto gui = std::make_shared<GuiMgr>();
    gui->setTextDrawFn([&engine](const std::string& text, float x, float y, float scale, std::array<float, 4> color, float z) {
        engine.setTextLabel(text, "times", x, y, scale, color, z);
    });
    // Pomiar szerokości napisów (wyśrodkowanie tekstu, rozmiar kart informacji na pasku narzędzi).
    gui->setTextMeasureFn([&engine](const std::string& text, float scale) { return engine.measureText(text, scale); });
    engine.getRenderer()->addFeature(gui);
    return gui;
}

// Widgety, do których sięga pętla klatki. Widgety należą do swoich okien (okno usuwa je samo).
struct DemoWidgets {
    GuiWindow* controlWindow = nullptr;
    GuiCheckBox* cursorCheck = nullptr;
    GuiCheckBox* chromeCheck = nullptr;
    GuiTrackBar* zoomTrack = nullptr;
    GuiMemo* log = nullptr;
    bool showFps = true;
    int clickCount = 0;
    int logIndex = 0;
};

// Okno 1 "Sterowanie": przycisk, pola wyboru, suwak, lista.
void createControlWindow(Engine& engine, GuiMgr& gui, CameraComponent* camera, DemoWidgets& w) {
    GuiWindow* win = gui.addWindow("Sterowanie", 880, 30, 350, 400);
    gui.setActive("Sterowanie");   // okno na wierzchu
    w.controlWindow = win;

    auto* reset = new GuiButton(win, "Resetuj", 10, 10, 150, 28);
    reset->setText("Resetuj kamerę");
    reset->setOnClick([&engine, camera]() {
        camera->getOwner()->transform().position = kCameraStart;
        engine.getCameraController()->setPositionAndTarget(kCameraStart, {0, 0, 0});
    });

    auto* fps = new GuiCheckBox(win, "FPS", 10, 55);
    fps->setChecked(true);
    fps->setText("Pokaż FPS");
    fps->setOnToggle([&w](bool on) { w.showFps = on; });

    w.cursorCheck = new GuiCheckBox(win, "Kursor", 10, 85);
    w.cursorCheck->setChecked(true);
    w.cursorCheck->setText("Kursor myszy (F1)");
    w.cursorCheck->setOnToggle([&gui](bool on) { gui.enableCursor(on); });

    // Kursor rysowany przez silnik (Custom) albo systemowy (System).
    auto* hardwareCursor = new GuiCheckBox(win, "CursorHW", 10, 115);
    hardwareCursor->setChecked(false);
    hardwareCursor->setText("Kursor sprzętowy");
    hardwareCursor->setOnToggle([&gui](bool on) {
        gui.setCursorMode(on ? GuiMgr::CursorMode::System : GuiMgr::CursorMode::Custom);
    });

    w.chromeCheck = new GuiCheckBox(win, "Chrome", 10, 145);
    w.chromeCheck->setChecked(true);
    w.chromeCheck->setText("Belka okna");
    w.chromeCheck->setOnToggle([win](bool on) { win->setChromeVisible(on); });

    // Suwak kąta widzenia kamery (odczytywany co klatkę w pętli).
    w.zoomTrack = new GuiTrackBar(win, "Zoom", 10, 215, 200, 1, 50);
    w.zoomTrack->setValue(20);
    auto* zoomLabel = new GuiStaticText(win, "", 10, 195);
    zoomLabel->setText("Zoom kamery: 20");
    w.zoomTrack->setOnChange([zoomLabel](int v) { zoomLabel->setText("Zoom kamery: " + std::to_string(v)); });

    auto* addLog = new GuiButton(win, "Log", 10, 270, 150, 28);
    addLog->setText("Dodaj do loga");
    addLog->setOnClick([&w]() {
        const char* messages[] = {"Hej!", "Test GUI", "Witaj świecie", "OK", "Anuluj"};
        w.log->addLine(messages[w.logIndex++ % 5]);
    });

    auto* list = new GuiListBox(win, "Obiekty", 180, 10, 150, 200);
    for (const char* item : {"Podłoga", "Kostka #1", "Kostka #2", "Kula", "Stożek", "Walec", "Piramida"}) {
        list->addItem(item);
    }
}

// Okno "Synchronizacja": tryb synchronizacji CPU-GPU, tryb prezentacji (V-Sync) i pełny ekran.
void createSyncWindow(Engine& engine, GuiMgr& gui) {
    GuiWindow* win = gui.addWindow("Synchronizacja", 880, 560, 430, 55);
    (new GuiStaticText(win, "", 5, 5))->setText("GPU-CPU:");

    // Standard - semafory binarne; Timeline - semafor z rosnącym licznikiem; Off - bez czekania CPU na GPU.
    auto* sync = new GuiComboBox(win, "SyncMode", 5, 28, 140);
    sync->addItem("Standard (binarne)");
    sync->addItem("Timeline");
    sync->addItem("Off");
    sync->setOnSelect([&engine](const std::string& s) {
        if (s == "Standard (binarne)") engine.setSynchronizationMode(SynchronizationMode::Standard);
        else if (s == "Timeline") engine.setSynchronizationMode(SynchronizationMode::Timeline);
        else if (s == "Off") engine.setSynchronizationMode(SynchronizationMode::Off);
    });

    (new GuiStaticText(win, "", 155, 5))->setText("V-Sync:");
    // FIFO - czeka na odświeżenie monitora (bez rwania obrazu); Immediate - od razu (rwanie);
    // Mailbox - potrójne buforowanie: bez rwania i z małym opóźnieniem.
    auto* present = new GuiComboBox(win, "PresentMode", 155, 28, 140);
    present->addItem("FIFO (ON)");
    present->addItem("Immediate (OFF)");
    present->addItem("Mailbox (OFF)");
    present->setOnSelect([&engine](const std::string& s) {
        if (s == "FIFO (ON)") engine.setPresentMode(PresentMode::Fifo);
        else if (s == "Immediate (OFF)") engine.setPresentMode(PresentMode::Immediate);
        else if (s == "Mailbox (OFF)") engine.setPresentMode(PresentMode::Mailbox);
    });

    auto* fullscreen = new GuiCheckBox(win, "Fullscreen", 305, 28);
    fullscreen->setText("Pełny ekran");
    fullscreen->setOnToggle([&engine](bool on) { engine.setFullscreen(on); });
}

// Pozostałe okna: lista rozwijana, pasek przewijania, notatnik i okno bez belki.
void createOtherWindows(GuiMgr& gui, DemoWidgets& w) {
    GuiWindow* comboWin = gui.addWindow("ComboBox", 880, 460, 200, 80);
    (new GuiStaticText(comboWin, "", 10, 5))->setText("Wybierz poziom:");
    auto* combo = new GuiComboBox(comboWin, "Poziom", 10, 30, 180);
    for (const char* level : {"Łatwy", "Średni", "Trudny", "Nocny koszmar"}) combo->addItem(level);

    GuiWindow* scrollWin = gui.addWindow("ScrollBar", 90, 90, 240, 120);
    (new GuiStaticText(scrollWin, "", 10, 5))->setText("ScrollBar (0–100):");
    auto* scrollValue = new GuiStaticText(scrollWin, "", 10, 25);
    scrollValue->setText("0");
    auto* scroll = new GuiScrollBar(scrollWin, "scroll", 210, 5, 90, 100);
    scroll->setOnChange([scrollValue](int v) { scrollValue->setText(std::to_string(v)); });

    GuiWindow* memoWin = gui.addWindow("Log zdarzeń", 30, 580, 420, 180);
    w.log = new GuiMemo(memoWin, "log", 10, 10, 400, 150);
    w.log->addLine("=== GUI Demo uruchomione ===");

    // Okno bez belki tytułowej (chrome) - widgety bez ramki.
    GuiWindow* infoWin = gui.addWindow("Info", 500, 550, 250, 110);
    infoWin->setChromeVisible(false);
    (new GuiStaticText(infoWin, "", 10, 10))->setText("Okno bez belki —\nwidgety bez ramki.");
    auto* clickMe = new GuiButton(infoWin, "Kliknij", 10, 55, 100, 28);
    clickMe->setText("Kliknij mnie!");
    clickMe->setOnClick([&w]() { w.log->addLine("Kliknięto Info (" + std::to_string(++w.clickCount) + "x)"); });
}

// Pasek narzędzi: grupy przycisków z ikonami pixel-art (wiersze znaków = piksele), skróty w rogu
// i karta informacji po najechaniu.
void createToolbar(GuiToolbar& toolbar) {
    auto tool = [](int id, const char* key, const char* title, const char* desc, std::vector<std::string> rows) {
        GuiToolbar::Item it;
        it.id = id;
        it.hotkey = key;
        it.icon.rows = std::move(rows);
        it.card.title = title;
        it.card.lines = {{desc}};
        it.card.footer = "LPM: wybierz narzędzie";
        return it;
    };
    GuiToolbar::Item sword = tool(2, "3", "Miecz", "Przykład przycisku nieaktywnego (enabled = false).",
                                  {"..........S.", ".........S..", "........S...", ".......S....", "......S.....",
                                   ".....S......", "..D.S.......", "...D........", "..D.D.......", ".D..........",
                                   "D...........", "............"});
    sword.enabled = false;
    sword.card.chipsLabel = "Koszt:";
    sword.card.chips = {{{0.75f, 0.5f, 0.25f, 1.0f}, "10 drewna", true}, {{0.72f, 0.72f, 0.75f, 1.0f}, "5 stali", false}};
    sword.card.warning = "Brakuje: 5 stali";
    toolbar.setGroups({
        {"Teren", {tool(0, "1", "Drzewo", "Sadzi drzewo w wskazanym miejscu.",
                        {".....gg.....", "....gGGg....", "...gGGGGg...", "..gGGGGGGg..", "...gGGGGg...", "..gGGGGGGg..",
                         ".gGGGGGGGGg.", "..gggggggg..", ".....DD.....", ".....DD.....", ".....DD.....", "..TTTTTTTT.."}),
                   tool(1, "2", "Kamień", "Stawia głaz.",
                        {"............", "............", "....sss.....", "...sSSSs....", "..sSSSSSs...", ".sSSSwSSSs..",
                         ".sSSSSSSSs..", "sSSSSSSSSSs.", "sSSSSSSSSSs.", ".ssssssssss.", "............", "............"})}},
        {"Wojsko", {sword}},
        {"Budynki", {tool(3, "4", "Dom", "Zwykły dom z czerwonym dachem.",
                          {"............", ".....RR.....", "....RRRR....", "...RRRRRR...", "..RRRRRRRR..", ".RRRRRRRRRR.",
                           "..WWWWWWWW..", "..WKKWWBBW..", "..WKKWWBBW..", "..WWWWWBBW..", "..WWWWWBBW..", ".gggggggggg."})}},
    });
}

} // namespace

int main() {
    std::srand(42);
    Engine engine;
    if (!engine.initialize("engine3d: gui demo — wszystkie widgety", 1280, 800)) return 1;
    Scene* scene = engine.getSceneManager()->activeScene();
    if (!scene) return 1;

    engine.getRenderer()->setClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    engine.loadFont("times", "assets/fonts/times.ttf", 28);
    FlyCamera::bindDefaultKeys(engine.inputActions());

    CameraComponent* camera = createBackgroundScene(engine, scene);
    std::shared_ptr<GuiMgr> gui = createGui(engine);
    DemoWidgets widgets;
    createControlWindow(engine, *gui, camera, widgets);
    createOtherWindows(*gui, widgets);
    createSyncWindow(engine, *gui);

    GuiToolbar toolbar;
    createToolbar(toolbar);
    int activeTool = -1;
    float progress = 0.0f;

    engine.onUpdate = [&](float dt, Engine& e) {
        if (e.keyPressed(SDL_SCANCODE_ESCAPE)) e.requestQuit();

        camera->fieldOfViewDegrees = static_cast<float>(widgets.zoomTrack->value());
        FlyCamera::update(e, dt, 2.0f);

        // F1 - kursor (swobodna praca z GUI), F2 - belka okna sterowania.
        if (e.keyPressed(SDL_SCANCODE_F1)) {
            gui->enableCursor(!gui->isCursorEnabled());
            widgets.cursorCheck->setChecked(gui->isCursorEnabled());
        }
        if (e.keyPressed(SDL_SCANCODE_F2)) {
            const bool visible = !widgets.controlWindow->isChromeVisible();
            widgets.controlWindow->setChromeVisible(visible);
            widgets.chromeCheck->setChecked(visible);
        }

        // Stan GUI: podświetlenia pod myszą, kliknięcia, przeciąganie okien.
        const float sw = static_cast<float>(e.getRenderer()->width());
        const float sh = static_cast<float>(e.getRenderer()->height());
        gui->update(e.input(), *e.getPlatform(), sw, sh);

        // Pasek narzędzi: kliknięcie wybiera narzędzie (złota ramka), ponowne - odznacza.
        toolbar.layout(sw, sh);
        const int clicked = toolbar.update(e.input().mouseX, e.input().mouseY, e.mousePressed(0));
        if (clicked != GuiToolbar::kNone && toolbar.item(clicked)->enabled) {
            if (auto* previous = toolbar.item(activeTool)) previous->active = false;
            activeTool = clicked == activeTool ? -1 : clicked;
            if (auto* current = toolbar.item(activeTool)) current->active = true;
            widgets.log->addLine("Pasek narzędzi: " + toolbar.item(clicked)->card.title);
        }
        toolbar.draw(*gui);

        // Nakładka HUD: pasek postępu, linia i wyśrodkowany napis.
        progress = std::fmod(progress + dt * 0.2f, 1.0f);
        gui->addOverlayTextCentered("Postęp (addOverlayBar)", sw * 0.5f, 26.0f, 0.5f, {1, 1, 1, 1});
        gui->addOverlayBar(sw * 0.5f - 100.0f, 34.0f, 200.0f, 8.0f, progress, {0.35f, 0.75f, 1.0f, 1.0f});
        gui->addOverlayLine(sw * 0.5f - 100.0f, 52.0f, sw * 0.5f + 100.0f, 52.0f, 2.0f, {1.0f, 0.8f, 0.3f, 0.9f});

        if (widgets.showFps) {
            e.setTextLabel("FPS: " + std::to_string(static_cast<int>(e.getCurrentFPS())), "times", 10, 20, 0.6f, {1, 1, 0.3f, 1});
        }
        gui->renderGui();
    };

    engine.run();
    return 0;
}
