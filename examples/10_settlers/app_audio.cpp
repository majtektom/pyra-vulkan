/**
 * app_audio.cpp - dźwięk w grze.
 *
 * Symulacja (World) nie odtwarza dźwięków sama - zgłasza je jako SoundEvent (nazwa + pozycja).
 * Tutaj zdarzenia trafiają do AudioSystem silnika jako dźwięk przestrzenny (słuchacz nad kamerą),
 * a muzyka jest dobierana do sytuacji: menu, spokojna gra, bitwa.
 */
#include "settlers_app.h"

namespace settlers {

// Wczytuje wszystkie efekty dźwiękowe (warianty name_0..N są losowane przy odtwarzaniu)
// i ustawia limity, ile kopii danego dźwięku może grać naraz i jak często.
void SettlersApp::setupAudio() {
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

// Efekt interfejsu (kliknięcie, błąd) - grany bez położenia w świecie.
void SettlersApp::sfx(const char* name, float volume) {
    if (audio_) audio_->play(name, volume);
}

// Uruchamia utwór z płynnym przenikaniem; nic nie robi, gdy muzyka jest wyłączona albo już gra ten sam.
void SettlersApp::playMusicTrack(const std::string& file, bool loop, float fade) {
    if (!audio_ || !musicEnabled_) return;
    const std::string path = std::string(kSoundDir) + "music/" + file;
    if (audio_->currentMusic() == path && audio_->isMusicPlaying()) return;
    audio_->playMusic(path, loop, fade);
}

// Wywoływane co klatkę:
//  1. słuchacz dźwięku 3D wędruje razem z kamerą (wyżej, gdy kamera oddalona = ciszej),
//  2. zdarzenia dźwiękowe zgłoszone przez symulację trafiają do miksera,
//  3. dobór muzyki: menu, spokojna gra (dwa utwory na zmianę) albo bitwa.
void SettlersApp::updateAudio() {
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

// Klawisz M: włącza/wyłącza muzykę i zapisuje wybór w ustawieniach.
void SettlersApp::toggleMusic() {
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

} // namespace settlers
