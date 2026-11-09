#include "core/game_settings.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/engine.h"
#include "core/logger.h"
#include "feature/audio/audio_system.h"
#include "render/renderer.h"

namespace engine3d {

bool GameSettings::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    try {
        nlohmann::json j;
        f >> j;
        GameSettings s;
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
        *this = s;
        LOG_INFO("Wczytano ustawienia z {}", path);
        return true;
    } catch (const std::exception& ex) {
        LOG_WARN("Nie udało się wczytać ustawień {}: {}", path, ex.what());
        return false;
    }
}

bool GameSettings::save(const std::string& path) const {
    const nlohmann::json j = {
        {"musicOn", musicOn},         {"sfxOn", sfxOn},           {"musicVolume", musicVolume},
        {"sfxVolume", sfxVolume},     {"masterVolume", masterVolume}, {"windowW", windowW},
        {"windowH", windowH},         {"fullscreen", fullscreen}, {"presentMode", presentMode},
        {"syncMode", syncMode},       {"fpsLimit", fpsLimit},     {"shadows", shadows},
    };
    std::error_code ec;
    const auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir, ec);
    std::ofstream f(path);
    if (!f) {
        LOG_WARN("Nie można zapisać ustawień do {}", path);
        return false;
    }
    f << j.dump(2);
    return true;
}

void GameSettings::applyAudio(AudioSystem* audio) const {
    if (!audio) return;
    audio->setMasterVolume(static_cast<float>(masterVolume) / 100.0f);
    audio->setMusicVolume(static_cast<float>(musicVolume) / 100.0f);
    audio->setSfxVolume(sfxOn ? static_cast<float>(sfxVolume) / 100.0f : 0.0f);
    if (!musicOn) audio->stopMusic(0.6f);
}

void GameSettings::applyPresentMode(Engine& engine) const {
    static const PresentMode modes[] = {PresentMode::Fifo, PresentMode::Immediate, PresentMode::Mailbox};
    engine.setPresentMode(modes[std::clamp(presentMode, 0, 2)]);
}

void GameSettings::applySyncMode(Engine& engine) const {
    static const SynchronizationMode modes[] = {SynchronizationMode::Standard, SynchronizationMode::Timeline,
                                                SynchronizationMode::Off};
    engine.setSynchronizationMode(modes[std::clamp(syncMode, 0, 2)]);
}

void GameSettings::applyDisplay(Engine& engine, bool startup) const {
    engine.setShadowsEnabled(shadows);
    engine.setFrameRateLimit(fpsLimit);
    if (!startup || presentMode != 0) applyPresentMode(engine);
    if (!startup || syncMode != 0) applySyncMode(engine);
    Renderer* r = engine.getRenderer();
    if (!fullscreen && r &&
        (static_cast<int>(r->width()) != windowW || static_cast<int>(r->height()) != windowH)) {
        engine.setWindowSize(static_cast<uint32_t>(windowW), static_cast<uint32_t>(windowH));
    }
    if (engine.isFullscreen() != fullscreen) engine.setFullscreen(fullscreen);
}

std::vector<std::array<int, 2>> GameSettings::resolutionList(const Engine& engine) {
    static const int modes[][2] = {{1024, 768}, {1280, 720}, {1280, 800}, {1366, 768}, {1440, 900}, {1600, 900},
                                   {1680, 1050}, {1920, 1080}, {1920, 1200}, {2560, 1440}, {3840, 2160}};
    uint32_t dw = 0, dh = 0;
    const bool known = engine.getDisplaySize(dw, dh);
    std::vector<std::array<int, 2>> out;
    for (const auto& m : modes) {
        if (!known || (static_cast<uint32_t>(m[0]) <= dw && static_cast<uint32_t>(m[1]) <= dh)) out.push_back({m[0], m[1]});
    }
    if (out.empty()) out.push_back({1280, 720});
    return out;
}

const std::vector<std::string>& GameSettings::presentModeNames() {
    static const std::vector<std::string> names = {"FIFO - VSync włączony", "Immediate - VSync wyłączony",
                                                    "Mailbox - potrójne buforowanie"};
    return names;
}

const std::vector<std::string>& GameSettings::syncModeNames() {
    static const std::vector<std::string> names = {"Standard (fence)", "Timeline (semafory)", "Wyłączona"};
    return names;
}

} // namespace engine3d
