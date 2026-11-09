#pragma once

#include <array>
#include <string>
#include <vector>

namespace engine3d {

class Engine;
class AudioSystem;

/**
 * @brief Ustawienia gracza wspólne dla gier: dźwięk, okno, synchronizacja, limit FPS, cienie.
 *
 * Zapis/odczyt w JSON (brakujące pola zostają domyślne, wartości są przycinane do zakresu),
 * stosowanie w silniku i w systemie dźwięku:
 *
 *   GameSettings settings;
 *   settings.load("save/moja_gra.json");
 *   settings.applyDisplay(engine, true);       // okno, pełny ekran, VSync, synchronizacja, FPS, cienie
 *   settings.applyAudio(engine.getAudio());    // głośności
 *   ...
 *   settings.save("save/moja_gra.json");
 *
 * Wartości trybów: presentMode 0 FIFO (VSync) / 1 Immediate / 2 Mailbox,
 * syncMode 0 Standard / 1 Timeline / 2 wyłączona, fpsLimit 0 = bez limitu.
 */
struct GameSettings {
    // Dźwięk (głośności w procentach)
    bool musicOn = true;
    bool sfxOn = true;
    int musicVolume = 55;
    int sfxVolume = 90;
    int masterVolume = 100;
    // Ekran
    int windowW = 1440;
    int windowH = 900;
    bool fullscreen = false;
    int presentMode = 0;
    int syncMode = 0;
    int fpsLimit = 0;
    bool shadows = true;

    // false = brak pliku albo błąd (ustawienia pozostają bez zmian).
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    // Głośności i wyciszenia (muzyka wyłączona = stopMusic).
    void applyAudio(AudioSystem* audio) const;
    // Okno i grafika. startup = true: tryby prezentacji/synchronizacji zmieniane tylko, gdy różnią się
    // od domyślnych (przebudowa swapchaina przy starcie jest zbędna).
    void applyDisplay(Engine& engine, bool startup) const;
    void applyPresentMode(Engine& engine) const;
    void applySyncMode(Engine& engine) const;

    // Typowe rozdzielczości okna, które mieszczą się na ekranie.
    static std::vector<std::array<int, 2>> resolutionList(const Engine& engine);
    // Nazwy do list wyboru w oknie ustawień.
    static const std::vector<std::string>& presentModeNames();
    static const std::vector<std::string>& syncModeNames();
};

} // namespace engine3d
