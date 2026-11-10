/**
 * app_save.cpp - zapis i wczytanie gry od strony aplikacji: plik, kamera, prędkość, przyciski i klawisze.
 *
 * Plik (save/settlers_save.json):
 *   { "game": "osadnicy", "world": { ...stan świata (World::saveState) }, "app": { kamera, prędkość, pokaz } }
 * Zapis idzie najpierw do pliku tymczasowego, potem podmienia stary - przerwany zapis nie niszczy poprzedniego.
 * Wczytanie najpierw czyta i sprawdza plik, dopiero potem usuwa bieżącą partię.
 */
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "settlers_app.h"

namespace settlers {

using nlohmann::json;

bool SettlersApp::saveExists(const std::string& path) const {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool SettlersApp::saveGame(const std::string& path) {
    if (!world_ || !gameStarted_ || mode_ == Mode::Menu) {
        if (world_) world_->pushMessage("Nie ma czego zapisać - rozpocznij grę", {1.0f, 0.6f, 0.4f, 1.0f});
        return false;
    }
    json root;
    root["game"] = "osadnicy";
    world_->saveState(root["world"]);
    const Vec3 focus = cam_.focus();
    root["app"] = {{"camFocus", {focus.x, focus.y, focus.z}}, {"camDistance", cam_.distance()},
                   {"camYaw", cam_.yaw()},                   {"camPitch", cam_.pitch()},
                   {"gameSpeed", gameSpeed_},                {"spectator", spectator_},
                   {"freePlay", freePlay_}};
    try {
        const std::filesystem::path p(path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
        const std::string tmp = path + ".tmp";
        {
            std::ofstream out(tmp);
            if (!out) throw std::runtime_error("nie można otworzyć " + tmp);
            out << root.dump();   // bez wcięć - plik dużej osady ma kilka MB
            if (!out) throw std::runtime_error("błąd zapisu na dysk");
        }
        std::filesystem::rename(tmp, path);
    } catch (const std::exception& e) {
        world_->pushMessage(std::string("Zapis nie powiódł się: ") + e.what(), {1.0f, 0.5f, 0.4f, 1.0f});
        sfx("ui_error", 0.7f);
        return false;
    }
    const int t = static_cast<int>(world_->gameTime);
    world_->pushMessage("Zapisano grę (czas " + std::to_string(t / 60) + ":" + (t % 60 < 10 ? "0" : "") + std::to_string(t % 60) +
                            ") - F8 / menu: wczytaj",
                        {0.7f, 1.0f, 0.7f, 1.0f});
    LOG_INFO("Zapisano grę: {} (czas gry {} s)", path, t);
    return true;
}

bool SettlersApp::loadGame(const std::string& path) {
    json root;
    try {
        std::ifstream in(path);
        if (!in) throw std::runtime_error("brak pliku zapisu");
        in >> root;
        if (root.value("game", "") != "osadnicy" || !root.contains("world")) throw std::runtime_error("to nie jest zapis Osadników");
    } catch (const std::exception& e) {
        if (world_) world_->pushMessage(std::string("Nie można wczytać gry: ") + e.what(), {1.0f, 0.5f, 0.4f, 1.0f});
        sfx("ui_error", 0.7f);
        return false;
    }

    std::string error;
    if (!newWorld(0, Difficulty::Normal, &root["world"], &error)) {
        // Plik był poprawnym JSON-em, ale świata nie dało się odtworzyć - zaczynamy nową grę, żeby nie zostać z niczym.
        LOG_ERROR("Wczytanie gry nie powiodło się: {}", error);
        newWorld(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()), Difficulty::Normal);
        world_->pushMessage("Nie można wczytać gry: " + error + " - rozpoczęto nową", {1.0f, 0.5f, 0.4f, 1.0f});
    } else if (root.contains("app")) {
        const json& a = root["app"];
        if (a.contains("camFocus")) cam_.setFocus({a["camFocus"][0].get<float>(), a["camFocus"][1].get<float>(), a["camFocus"][2].get<float>()});
        cam_.setDistance(a.value("camDistance", cam_.distance()));
        cam_.setYaw(a.value("camYaw", cam_.yaw()));
        cam_.setPitch(a.value("camPitch", cam_.pitch()));
        cam_.update(0.0f, *camera_);
        gameSpeed_ = a.value("gameSpeed", 1);
        spectator_ = a.value("spectator", false);
        freePlay_ = a.value("freePlay", false) && world_->winner == kPlayer;
    }
    world_->aiEnabled = true;
    world_->aiControlsPlayer = spectator_;
    updateSpeedButton();
    gameStarted_ = true;
    LOG_INFO("Wczytano grę: {} (czas gry {} s)", path, static_cast<int>(world_->gameTime));
    enterGame();
    return true;
}

} // namespace settlers
