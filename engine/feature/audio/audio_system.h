#pragma once

#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/math.h"

struct SDL_AudioStream;

namespace engine3d {

/**
 * @brief System dźwięku silnika: efekty (SFX), dźwięk przestrzenny i strumieniowana muzyka.
 *
 * Oparty o SDL3 (urządzenie wyjściowe float32 stereo) z własnym mikserem w wątku audio:
 * - Efekty ładowane w całości do pamięci (WAV przez SDL, OGG przez stb_vorbis) i konwertowane
 *   do formatu urządzenia. Pod jedną nazwą można zarejestrować kilka wariantów - play()
 *   losuje jeden z nich (np. 5 różnych odgłosów kroków).
 * - Limit jednoczesnych instancji danego dźwięku i minimalny odstęp między odtworzeniami,
 *   żeby 30 drwali nie zagłuszyło gry.
 * - Dźwięk przestrzenny: głośność maleje z odległością od słuchacza, panorama L/P wg kierunku.
 * - Muzyka OGG dekodowana strumieniowo (mała pamięć) z płynnym przenikaniem utworów.
 *
 * Gdy urządzenie audio nie jest dostępne, wszystkie wywołania są bezpiecznymi no-op.
 */
class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    bool init();
    void shutdown();
    [[nodiscard]] bool isReady() const { return device_ != nullptr; }

    /**
     * @brief Ładuje efekt (WAV/OGG). Kolejne wywołania z tą samą nazwą dodają warianty.
     */
    bool loadSound(const std::string& name, const std::string& path);

    // Ograniczenia odtwarzania danego dźwięku.
    void setSoundLimits(const std::string& name, int maxInstances, float minIntervalSeconds);

    /**
     * @brief Odtwarza efekt 2D (interfejs). pan: -1 lewo .. 1 prawo. pitch: 1 = oryginalna wysokość.
     * @return true, jeśli dźwięk został uruchomiony.
     */
    bool play(const std::string& name, float volume = 1.0f, float pan = 0.0f, float pitch = 1.0f);

    /**
     * @brief Odtwarza efekt w punkcie świata (głośność i panorama względem słuchacza).
     */
    bool play3D(const std::string& name, const Vec3& position, float volume = 1.0f, float pitchVariation = 0.08f);

    // Słuchacz dla dźwięku 3D: pozycja i wektor "w prawo" (do panoramy).
    void setListener(const Vec3& position, const Vec3& right);
    // Zasięg słyszalności dźwięku 3D (pełna głośność do refDistance, cisza od maxDistance).
    void setDistanceModel(float refDistance, float maxDistance);

    /**
     * @brief Uruchamia muzykę (OGG) z przenikaniem z aktualnym utworem.
     */
    bool playMusic(const std::string& path, bool loop = true, float fadeSeconds = 2.0f);
    void stopMusic(float fadeSeconds = 1.5f);
    [[nodiscard]] bool isMusicPlaying() const;
    [[nodiscard]] const std::string& currentMusic() const { return currentMusicPath_; }

    void setMasterVolume(float v);
    void setMusicVolume(float v);
    void setSfxVolume(float v);
    [[nodiscard]] float musicVolume() const { return musicVolume_; }
    [[nodiscard]] float sfxVolume() const { return sfxVolume_; }

    // Diagnostyka: szczytowy poziom wyjścia od ostatniego odczytu (0..1) i liczba grających efektów.
    float peakLevel();
    [[nodiscard]] int activeVoices() const;

private:
    struct SoundBuffer {
        std::vector<float> samples;  // przeplatane stereo, częstotliwość urządzenia
        size_t frames = 0;
    };
    struct SoundEntry {
        std::vector<std::shared_ptr<SoundBuffer>> variants;
        int maxInstances = 4;
        float minInterval = 0.04f;
        double lastPlayTime = -1.0;
    };
    struct Voice {
        std::shared_ptr<SoundBuffer> buffer;
        std::string name;
        double position = 0.0;  // w ramkach (ułamkowo przy zmianie wysokości)
        float pitch = 1.0f;
        float gainL = 1.0f;
        float gainR = 1.0f;
    };
    struct MusicTrack;

    static void audioCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount);
    void mix(float* out, int frames);
    void mixMusic(MusicTrack& track, float* out, int frames);
    std::shared_ptr<SoundBuffer> decodeFile(const std::string& path);
    bool startVoice(const std::string& name, float volume, float pan, float pitch);
    double nowSeconds() const;

    SDL_AudioStream* device_ = nullptr;
    int sampleRate_ = 48000;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SoundEntry> sounds_;
    std::vector<Voice> voices_;
    std::vector<std::unique_ptr<MusicTrack>> music_;
    std::vector<float> scratch_;
    std::string currentMusicPath_;
    std::mt19937 rng_{1234};

    Vec3 listenerPos_{};
    Vec3 listenerRight_{1.0f, 0.0f, 0.0f};
    float refDistance_ = 60.0f;
    float maxDistance_ = 600.0f;

    float masterVolume_ = 1.0f;
    float musicVolume_ = 0.6f;
    float sfxVolume_ = 0.9f;
    float peak_ = 0.0f;

    static constexpr size_t kMaxVoices = 48;
};

} // namespace engine3d
