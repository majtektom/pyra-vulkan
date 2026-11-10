/**
 * app_menus.cpp - okna interfejsu.
 *
 * Budowa wszystkich okien GUI (menu główne, pauza, koniec gry, panel budowy, koszary),
 * okna "Ustawienia" i "Sterowanie", a także wczytywanie, stosowanie i zapis ustawień gracza.
 * GUI silnika działa na wywołaniach zwrotnych: przycisk dostaje lambdę wywoływaną po kliknięciu.
 */
#include "settlers_app.h"

namespace settlers {

// Tworzy przycisk w oknie GUI. Kliknięcie odtwarza dźwięk i wywołuje przekazaną funkcję.
GuiButton* SettlersApp::makeButton(GuiWindow* w, const std::string& name, int x, int y, int bw, int bh, const std::string& text, std::function<void()> fn) {
    auto* b = new GuiButton(w, name, x, y, bw, bh);
    b->setText(text);
    b->setOnClick([this, fn = std::move(fn)]() {
        sfx("ui_click", 0.7f);
        fn();
    });
    return b;
}

// Tworzy wszystkie okna GUI. Okna istnieją przez cały czas działania programu,
// a tryby gry tylko je pokazują i ukrywają (setVisible).
void SettlersApp::setupGui() {
    gui_ = std::make_shared<GuiMgr>();
    gui_->setTextDrawFn([this](const std::string& text, float x, float y, float scale, std::array<float, 4> color, float z) {
        engine_.setTextLabel(text, kFont, x, y, scale, color, z);
    });
    gui_->setTextMeasureFn([this](const std::string& text, float scale) { return engine_.measureText(text, scale); });
    renderer_->addFeature(gui_);
    setupBuildBar();

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
    // Panel budowy to dolny pasek rysowany jako HUD (app_buildbar.cpp) - bez okna GUI.

    // Koszary
    barracksWindow_ = gui_->addWindow("Koszary", 0, 0, 250, 176);
    makeButton(barracksWindow_, "sword", 8, 8, 234, 34,
               std::string("Miecznik (") + costText(unitDef(UnitType::Swordsman).cost) + ")",
               [this]() { trainSelected(UnitType::Swordsman); });
    makeButton(barracksWindow_, "archer", 8, 52, 234, 34,
               std::string("Łucznik (") + costText(unitDef(UnitType::Archer).cost) + ")",
               [this]() { trainSelected(UnitType::Archer); });
    // Rekrutacja automatyczna: przełącznik i proporcja (- / +: udział mieczników co 10%).
    btnAutoTrain_ = makeButton(barracksWindow_, "autotrain", 8, 96, 234, 34, "Auto-rekrutacja: wył.",
                               [this]() { toggleAutoTrainSelected(); });
    makeButton(barracksWindow_, "ratio_minus", 8, 138, 40, 32, "-", [this]() { changeSwordShareSelected(-10); });
    btnTrainRatio_ = makeButton(barracksWindow_, "ratio", 52, 138, 146, 32, "", []() {});
    makeButton(barracksWindow_, "ratio_plus", 202, 138, 40, 32, "+", [this]() { changeSwordShareSelected(+10); });

    // Tragarze: liczba ustawiana przez gracza (widoczne razem z oknem gospodarki - klawisz I)
    carriersWindow_ = gui_->addWindow("Tragarze", 0, 0, 236, 44);
    makeButton(carriersWindow_, "carr_minus", 6, 6, 50, 32, "-", [this]() { changeCarriers(-1); });
    makeButton(carriersWindow_, "carr_plus", 62, 6, 50, 32, "+", [this]() { changeCarriers(+1); });
    btnCarrierAuto_ = makeButton(carriersWindow_, "carr_auto", 118, 6, 112, 32, "Auto: tak", [this]() { toggleCarrierAuto(); });
    carriersWindow_->setVisible(false);

    // Akcje budynku
    actionsWindow_ = gui_->addWindow("Akcje", 0, 0, 250, 176);
    makeButton(actionsWindow_, "demolish", 8, 8, 234, 34, "Zburz (Del, zwrot 50%)", [this]() { demolishSelected(); });
    btnConnect_ = makeButton(actionsWindow_, "connect", 8, 50, 234, 34, "Połącz drogą z magazynem",
                             [this]() { connectSelected(); });
    makeButton(actionsWindow_, "roadfrom", 8, 92, 234, 34, "Droga od budynku (T)", [this]() {
        if (selectedBuilding_ >= 0) beginRoad(world_->buildings[selectedBuilding_].flag);
    });
    makeButton(actionsWindow_, "release", 8, 134, 234, 34, "Wyprowadź załogę (wieża)", [this]() { releaseGarrisonSelected(); });

    layoutGui();
}

// Otwiera okno podrzędne (Ustawienia/Sterowanie) w miejsce menu, z którego je wywołano.
void SettlersApp::openSubWindow(GuiWindow* window, GuiWindow* returnTo) {
    if (returnTo) returnTo->setVisible(false);
    subReturn_ = returnTo;
    subWindow_ = window;
    window->setVisible(true);
    gui_->setActive(window->name());
    if (window == settingsWindow_) syncSettingsWidgets();
}

// Zamyka okno podrzędne (ESC lub 'Wróć'); ustawienia są wtedy zapisywane do pliku.
void SettlersApp::closeSubWindow() {
    if (!subWindow_) return;
    if (subWindow_ == settingsWindow_) saveSettings();
    subWindow_->setVisible(false);
    subWindow_ = nullptr;
    if (subReturn_) {
        subReturn_->setVisible(true);
        gui_->setActive(subReturn_->name());
    }
}

// Napis w oknie GUI (nazwy widgetów muszą być unikalne, stąd licznik).
GuiStaticText* SettlersApp::label(GuiWindow* w, int x, int y, const std::string& t, const Color& c) {
    static int counter = 0;
    auto* s = new GuiStaticText(w, "lbl" + std::to_string(++counter), x, y);
    s->setText(t);
    s->color_ = c;
    return s;
}

// Lista rozdzielczości mieszczących się na ekranie (GameSettings z silnika).
void SettlersApp::buildResolutionList() {
    resolutions_ = GameSettings::resolutionList(engine_);
}

// Okno ustawień: dźwięk (pola wyboru i suwaki) oraz grafika (pełny ekran, cienie,
// listy rozwijane: rozdzielczość, synchronizacja pionowa, synchronizacja GPU-CPU, limit FPS).
// Każda zmiana działa od razu (lambda w setOnToggle/setOnChange/setOnSelect).
void SettlersApp::setupSettingsWindow() {
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
    for (const auto& s : GameSettings::syncModeNames()) comboSync_->addItem(s);
    comboSync_->setOnSelect([this](const std::string&) {
        settings_.syncMode = comboSync_->selectedIdx();
        applySyncMode();
    });
    comboPresent_ = new GuiComboBox(w, "present", 12, comboY[1], 300);
    for (const auto& s : GameSettings::presentModeNames()) comboPresent_->addItem(s);
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

// Ustawia widgety okna ustawień zgodnie z bieżącymi wartościami (przy otwarciu okna).
void SettlersApp::syncSettingsWidgets() {
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

// Okno sterowania: lista klawiszy z kControls (nagłówki sekcji na żółto).
void SettlersApp::setupControlsWindow() {
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

// Ustawienia gracza to engine3d::GameSettings (core/game_settings.h): zapis/odczyt JSON i stosowanie
// w silniku są wspólne dla gier; tutaj tylko dopięcie do stanu aplikacji (muzyka, tryby testowe).

void SettlersApp::applyAudioSettings() {
    musicEnabled_ = settings_.musicOn;
    settings_.applyAudio(audio_);
}

void SettlersApp::applyPresentMode() {
    settings_.applyPresentMode(engine_);
}

void SettlersApp::applySyncMode() {
    settings_.applySyncMode(engine_);
}

// Stosuje wszystkie ustawienia (przy starcie gry i po przycisku 'Domyślne').
void SettlersApp::applySettings(bool startup) {
    applyAudioSettings();
    settings_.applyDisplay(engine_, startup);
}

// Testy automatyczne startują zawsze na ustawieniach domyślnych (niezależnie od pliku gracza).
void SettlersApp::loadSettings() {
    if (std::getenv("SETTLERS_UITEST") || std::getenv("SETTLERS_AUTOPLAY")) return;
    settings_.load(kSettingsPath);
}

// Zapis do pliku JSON (nie w trybach testowych - testy nie nadpisują ustawień gracza).
void SettlersApp::saveSettings() {
    if (uiTest_ || autoplaySeconds_ > 0.0f) return;
    settings_.fullscreen = engine_.isFullscreen();
    settings_.save(kSettingsPath);
}

// Rozmieszcza okna względem rozmiaru ekranu (menu na środku, panel budowy w lewym dolnym rogu...).
void SettlersApp::layoutGui() {
    const int w = static_cast<int>(renderer_->width());
    const int h = static_cast<int>(renderer_->height());
    lastW_ = w;
    lastH_ = h;
    menuWindow_->setPosition(w / 2 - 170, h / 2 - 175);
    pauseWindow_->setPosition(w / 2 - 150, h / 2 - 160);
    settingsWindow_->setPosition(w / 2 - settingsWindow_->width() / 2, std::max(40, h / 2 - settingsWindow_->height() / 2));
    controlsWindow_->setPosition(w / 2 - controlsWindow_->width() / 2, std::max(40, h / 2 - controlsWindow_->height() / 2));
    gameOverWindow_->setPosition(w / 2 - 190, h / 2 - 100);
    layoutSideWindows();
}

} // namespace settlers
