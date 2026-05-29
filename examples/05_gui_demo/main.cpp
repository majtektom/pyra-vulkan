#include "engine.h"
#include "renderer.h"
#include "types.h"
#include "math.h"
#include "mesh_component.h"
#include "transform_component.h"
#include "camera_component.h"
#include "gui.h"
#include "gui_button.h"
#include "gui_check_box.h"
#include "gui_list_box.h"
#include "gui_track_bar.h"
#include "gui_static_text.h"
#include "gui_combo_box.h"
#include "gui_scroll_bar.h"
#include "gui_memo.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <sstream>

using namespace engine3d;

int main() {
    std::srand(42);
    Engine engine;
    if (!engine.initialize("engine3d: gui demo — wszystkie widgety", 1280, 800)) return 1;

    auto* renderer = engine.getRenderer();
    renderer->setClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    engine.loadFont("times", "assets/fonts/times.ttf", 28);

    MeshData floor;
    floor.vertices = {
        {{{-10,0,-10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{0,0}}},
        {{{ 10,0,-10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{10,0}}},
        {{{ 10,0, 10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{10,10}}},
        {{{-10,0, 10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{0,10}}},
    };
    floor.indices = {0,2,1,0,3,2};
    engine.getMeshManager()->loadMesh("floor", floor);
    Material mat; mat.name = "floor"; engine.getMaterialManager()->add(mat);
    auto* entity = engine.createEntity("floor");
    entity->addComponent<MeshComponent>("floor")->materialInstance =
        engine.getMaterialSystem()->getOrCreateMaterialInstance(mat);

    auto* camera = engine.createEntity("camera")->addComponent<CameraComponent>();
    camera->position = {0, 3, -8};
    camera->fieldOfViewDegrees = 50;
    camera->farPlane = 1000;
    engine.setActiveCamera(camera);
    engine.getCameraController()->setPositionAndTarget(camera->position, {0, 0, 0});

    auto guiMgr = std::make_shared<GuiMgr>();
    auto drawText = [&engine](const std::string& text, float x, float y, float scale, std::array<float, 4> color) {
        engine.setTextLabel(text, "times", x, y, scale, color);
    };
    guiMgr->setTextDrawFn(drawText);
    engine.getRenderer()->addFeature(guiMgr);

    // ── Okno 1: Sterowanie (Button, CheckBox, ListBox, TrackBar, StaticText) ──
    auto* winCtrl = guiMgr->addWindow("Sterowanie", 880, 30, 350, 400);
    guiMgr->setActive("Sterowanie");

    auto* btnReset = new GuiButton(winCtrl, "Resetuj", 10, 10, 150, 28);
    btnReset->setText("Resetuj kamerę");
    btnReset->setOnClick([&]() {
        camera->position = {0, 3, -8};
        engine.getCameraController()->setPositionAndTarget({0, 3, -8}, {0, 0, 0});
    });

    auto* chkFPS = new GuiCheckBox(winCtrl, "FPS", 10, 55);
    chkFPS->setChecked(true);
    chkFPS->setText("Pokaż FPS");
    bool showFPS = true;
    chkFPS->setOnToggle([&showFPS](bool on) { showFPS = on; });

    auto* chkCursor = new GuiCheckBox(winCtrl, "Kursor", 10, 85);
    chkCursor->setChecked(true);
    chkCursor->setText("Kursor myszy (F1)");
    chkCursor->setOnToggle([&](bool on) { guiMgr->enableCursor(on); });

    auto* chkCursorMode = new GuiCheckBox(winCtrl, "CursorHW", 10, 115);
    chkCursorMode->setChecked(false);
    chkCursorMode->setText("Kursor sprzętowy");
    chkCursorMode->setOnToggle([&](bool on) {
        guiMgr->setCursorMode(on ? GuiMgr::CursorMode::System : GuiMgr::CursorMode::Custom);
    });

    auto* chkChrome = new GuiCheckBox(winCtrl, "Chrome", 10, 145);
    chkChrome->setChecked(true);
    chkChrome->setText("Belka okna");
    chkChrome->setOnToggle([&](bool on) { winCtrl->setChromeVisible(on); });

    auto* trackZoom = new GuiTrackBar(winCtrl, "Zoom", 10, 215, 200, 1, 50);
    trackZoom->setValue(20);
    auto* labelZoom = new GuiStaticText(winCtrl, "", 10, 195);
    labelZoom->setText("Zoom kamery: 20");
    trackZoom->setOnChange([labelZoom](int v) {
        labelZoom->setText("Zoom kamery: " + std::to_string(v));
    });

    auto* btnLog = new GuiButton(winCtrl, "Log", 10, 270, 150, 28);
    btnLog->setText("Dodaj do loga");

    auto* listBox = new GuiListBox(winCtrl, "Obiekty", 180, 10, 150, 200);
    listBox->addItem("Podłoga");
    listBox->addItem("Kostka #1");
    listBox->addItem("Kostka #2");
    listBox->addItem("Kula");
    listBox->addItem("Stożek");
    listBox->addItem("Walec");
    listBox->addItem("Piramida");

    // ── Okno 2: ComboBox ──
    auto* winCombo = guiMgr->addWindow("ComboBox", 880, 460, 200, 80);
    auto* comboLabel = new GuiStaticText(winCombo, "", 10, 5);
    comboLabel->setText("Wybierz poziom:");
    auto* comboBox = new GuiComboBox(winCombo, "Poziom", 10, 30, 180);
    comboBox->addItem("Łatwy");
    comboBox->addItem("Średni");
    comboBox->addItem("Trudny");
    comboBox->addItem("Nocny koszmar");
    comboBox->setOnSelect([](const std::string& s) {
        (void)s;
    });
    
    // ── Okno 3: Synchronizacja ──
    auto* winSync = guiMgr->addWindow("Synchronizacja", 880, 560, 430, 55);

    auto* syncLabel = new GuiStaticText(winSync, "", 5, 5);
    syncLabel->setText("GPU-CPU:");

    auto* syncCombo = new GuiComboBox(winSync, "SyncMode", 5, 28, 140);
    syncCombo->addItem("Standard (binarne)");
    syncCombo->addItem("Timeline (1.3)");
    syncCombo->addItem("Off");
    syncCombo->setOnSelect([&engine](const std::string& s) {
        if (s == "Standard (binarne)")
            engine.setSynchronizationMode(SynchronizationMode::Standard);
        else if (s == "Timeline (1.3)")
            engine.setSynchronizationMode(SynchronizationMode::Timeline);
        else if (s == "Off")
            engine.setSynchronizationMode(SynchronizationMode::Off);
    });

    auto* presentLabel = new GuiStaticText(winSync, "", 155, 5);
    presentLabel->setText("V-Sync:");

    auto* presentCombo = new GuiComboBox(winSync, "PresentMode", 155, 28, 140);
    presentCombo->addItem("FIFO (ON)");
    presentCombo->addItem("Immediate (OFF)");
    presentCombo->addItem("Mailbox (OFF)");
    presentCombo->setOnSelect([&engine](const std::string& s) {
        if (s == "FIFO (ON)")
            engine.setPresentMode(PresentMode::Fifo);
        else if (s == "Immediate (OFF)")
            engine.setPresentMode(PresentMode::Immediate);
        else if (s == "Mailbox (OFF)")
            engine.setPresentMode(PresentMode::Mailbox);
    });

    auto* chkFullscreen = new GuiCheckBox(winSync, "Fullscreen", 305, 28);
    chkFullscreen->setText("Pełny ekran");
    chkFullscreen->setOnToggle([&engine](bool on) { engine.setFullscreen(on); });

    // ── Okno 4: ScrollBar ──
    auto* winScroll = guiMgr->addWindow("ScrollBar", 90, 90, 240, 120);
    auto* scrollLabel = new GuiStaticText(winScroll, "", 10, 5);
    scrollLabel->setText("ScrollBar (0–100):");
    auto* labelScroll = new GuiStaticText(winScroll, "", 10, 25);
    labelScroll->setText("0");
    auto* scrollBar = new GuiScrollBar(winScroll, "scroll", 210, 5, 90, 100);
    scrollBar->setOnChange([labelScroll](int v) {
        labelScroll->setText(std::to_string(v));
    });

    // ── Okno 5: Memo (log) ──
    auto* winMemo = guiMgr->addWindow("Log zdarzeń", 30, 580, 420, 180);
    auto* memo = new GuiMemo(winMemo, "log", 10, 10, 400, 150);
    memo->addLine("=== GUI Demo uruchomione ===");

    // ── Okno 6: Info (bez chrome) ──
    auto* winInfo = guiMgr->addWindow("Info", 500, 550, 250, 110);
    winInfo->setChromeVisible(false);
    auto* infoLabel2 = new GuiStaticText(winInfo, "", 10, 10);
    infoLabel2->setText("Okno bez belki —\nwidgety bez ramki.");
    auto* infoBtn = new GuiButton(winInfo, "Kliknij", 10, 55, 100, 28);
    infoBtn->setText("Kliknij mnie!");

    // ── Licznik kliknięć w infoBtn ──
    int clickCount = 0;
    infoBtn->setOnClick([&, memo]() {
        clickCount++;
        std::ostringstream ss;
        ss << "Kliknięto Info (" << clickCount << "x)";
        memo->addLine(ss.str());
    });

    // ── btnLog → memo ──
    static int logIdx = 0;
    btnLog->setOnClick([memo]() {
        const char* msgs[] = {"Hej!", "Test GUI", "Witaj świecie", "OK", "Anuluj"};
        memo->addLine(msgs[logIdx % 5]);
        logIdx++;
    });

    // ── Główna pętla ──
    engine.onUpdate = [&](float dt, Engine& e) {
        auto snap = e.getPlatform()->pollInput();
        auto* cc = e.getCameraController();
        auto* cam = const_cast<CameraComponent*>(e.getActiveCamera());
        if (cc && cam) {
            float zoom = float(trackZoom->value());
            cam->fieldOfViewDegrees = zoom;
            float ts = cc->mouseSensitivity;
            cc->rotateYaw(-static_cast<float>(snap.mouseDeltaX) * ts);
            cc->rotatePitch(-static_cast<float>(snap.mouseDeltaY) * ts);
            float vel = cc->moveSpeed * dt;
            if (snap.keys[SDL_SCANCODE_W]) cc->moveForward(vel);
            if (snap.keys[SDL_SCANCODE_S]) cc->moveForward(-vel);
            if (snap.keys[SDL_SCANCODE_A]) cc->moveRight(-vel);
            if (snap.keys[SDL_SCANCODE_D]) cc->moveRight(vel);
            cc->update(dt, *cam);
        }

        // Klawisze globalne
        static bool prevF1 = false, prevF2 = false;
        if (snap.keys[SDL_SCANCODE_F1] && !prevF1) {
            guiMgr->enableCursor(!guiMgr->isCursorEnabled());
            chkCursor->setChecked(guiMgr->isCursorEnabled());
        }
        prevF1 = snap.keys[SDL_SCANCODE_F1];
        if (snap.keys[SDL_SCANCODE_F2] && !prevF2) {
            bool cur = winCtrl->isChromeVisible();
            winCtrl->setChromeVisible(!cur);
            chkChrome->setChecked(!cur);
        }
        prevF2 = snap.keys[SDL_SCANCODE_F2];

        // Aktualizacja GUI (obsługuje automatycznie wszystkie widgety)
        guiMgr->update(*e.getPlatform(), (float)e.getRenderer()->width(), (float)e.getRenderer()->height());

        // Rysowanie
        // FPS
        if (showFPS) {
            std::ostringstream fpsStr;
            fpsStr << "FPS: " << int(e.getCurrentFPS());
            e.setTextLabel(fpsStr.str(), "times", 10, 20, 0.6f, {1, 1, 0.3f, 1});
        }

        guiMgr->renderGui();
    };

    engine.run();
    return 0;
}
