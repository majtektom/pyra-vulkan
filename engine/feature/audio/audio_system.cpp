#include "feature/audio/audio_system.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "core/logger.h"

#define STB_VORBIS_HEADER_ONLY
#include "feature/audio/stb_vorbis.c"

namespace engine3d {

namespace {

// Bufor pierścieniowy próbek: jeden pisarz (wątek strumienia), jeden czytelnik (wątek audio), bez blokad.
// head/tail rosną bez końca; pozycja w tablicy = licznik % pojemność. Wolne miejsce = pojemność - (head - tail).
struct SampleRing {
    std::vector<float> data;
    std::atomic<size_t> head{0};   // ile próbek zapisano (pisze tylko wątek strumienia)
    std::atomic<size_t> tail{0};   // ile odczytano (pisze tylko wątek audio)

    size_t available() const { return head.load(std::memory_order_acquire) - tail.load(std::memory_order_relaxed); }
    size_t space() const { return data.size() - (head.load(std::memory_order_relaxed) - tail.load(std::memory_order_acquire)); }

    void write(const float* src, size_t n) {
        const size_t h = head.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i) data[(h + i) % data.size()] = src[i];
        head.store(h + n, std::memory_order_release);   // release: próbki widoczne, zanim czytelnik zobaczy nowy head
    }
    size_t read(float* dst, size_t n) {
        const size_t t = tail.load(std::memory_order_relaxed);
        n = std::min(n, head.load(std::memory_order_acquire) - t);
        for (size_t i = 0; i < n; ++i) dst[i] = data[(t + i) % data.size()];
        tail.store(t + n, std::memory_order_release);
        return n;
    }
};

} // namespace

// Strumieniowany utwór muzyczny. Dekoder i konwerter używa tylko wątek strumienia (i playMusic przed publikacją),
// gain/targetGain/fadeRate - tylko wątek audio; flagi atomowe łączą oba.
struct AudioSystem::MusicTrack {
    stb_vorbis* vorbis = nullptr;
    SDL_AudioStream* converter = nullptr;
    int channels = 2;
    bool loop = true;
    bool flushed = false;
    std::string path;
    SampleRing ring;                         // ~1 s zdekodowanej muzyki stereo w częstotliwości urządzenia
    std::atomic<bool> decodeEnded{false};    // koniec pliku (bez pętli) - w pierścieniu zostały ostatnie próbki
    std::atomic<bool> released{false};       // wątek audio już go nie używa - strumień może usunąć
    bool stopRequested = false;              // gra: wyciszony (stopMusic / nowy utwór)
    // stan wątku audio
    float gain = 0.0f;
    float targetGain = 1.0f;
    float fadeRate = 1.0f;                   // zmiana wzmocnienia na sekundę

    ~MusicTrack() {
        if (converter) SDL_DestroyAudioStream(converter);
        if (vorbis) stb_vorbis_close(vorbis);
    }
};

bool AudioSystem::CommandQueue::push(const Command& c) {
    const size_t h = head.load(std::memory_order_relaxed);
    if (h - tail.load(std::memory_order_acquire) >= kCapacity) return false;   // pełna - polecenie przepada
    items[h % kCapacity] = c;
    head.store(h + 1, std::memory_order_release);
    return true;
}

bool AudioSystem::CommandQueue::pop(Command& c) {
    const size_t t = tail.load(std::memory_order_relaxed);
    if (t == head.load(std::memory_order_acquire)) return false;
    c = items[t % kCapacity];
    tail.store(t + 1, std::memory_order_release);
    return true;
}

AudioSystem::AudioSystem() = default;

AudioSystem::~AudioSystem() {
    shutdown();
}

bool AudioSystem::init() {
    if (device_) return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LOG_WARN("[Audio] Nie udało się zainicjalizować SDL audio: {}", SDL_GetError());
        return false;
    }
    // Cała pamięć wątku audio rezerwowana tu - w trakcie gry wątek audio nie alokuje.
    voices_.reserve(kMaxVoices);
    playing_.reserve(kMaxTracks);
    mixBuffer_.assign(static_cast<size_t>(kMixChunkFrames) * 2, 0.0f);
    musicScratch_.assign(static_cast<size_t>(kMixChunkFrames) * 2, 0.0f);

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = sampleRate_;
    device_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioSystem::audioCallback, this);
    if (!device_) {
        LOG_WARN("[Audio] Brak urządzenia audio: {}", SDL_GetError());
        return false;
    }
    streamStop_ = false;
    streamThread_ = std::thread([this] { streamLoop(); });
    SDL_ResumeAudioStreamDevice(device_);
    LOG_INFO("[Audio] Urządzenie audio gotowe ({} Hz, stereo)", sampleRate_);
    return true;
}

void AudioSystem::shutdown() {
    if (device_) {
        SDL_DestroyAudioStream(device_);  // zatrzymuje też wątek wywołań zwrotnych
        device_ = nullptr;
    }
    if (streamThread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(tracksMutex_);
            streamStop_ = true;
        }
        streamWake_.notify_all();
        streamThread_.join();
    }
    voices_.clear();
    playing_.clear();
    Command c;
    while (commands_.pop(c)) {}
    {
        std::lock_guard<std::mutex> lock(tracksMutex_);
        tracks_.clear();
    }
    std::lock_guard<std::mutex> lock(mainMutex_);
    played_.clear();
    sounds_.clear();
}

double AudioSystem::nowSeconds() const {
    return static_cast<double>(SDL_GetTicks()) / 1000.0;
}

std::shared_ptr<AudioSystem::SoundBuffer> AudioSystem::decodeFile(const std::string& path) {
    SDL_AudioSpec srcSpec{};
    std::vector<float> src;
    int frames = 0;

    const bool isOgg = path.size() > 4 && (path.substr(path.size() - 4) == ".ogg" || path.substr(path.size() - 4) == ".OGG");
    if (isOgg) {
        int channels = 0, rate = 0;
        short* pcm = nullptr;
        frames = stb_vorbis_decode_filename(path.c_str(), &channels, &rate, &pcm);
        if (frames <= 0 || !pcm) {
            LOG_WARN("[Audio] Nie można zdekodować OGG: {}", path);
            return nullptr;
        }
        src.resize(static_cast<size_t>(frames) * channels);
        for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<float>(pcm[i]) / 32768.0f;
        free(pcm);
        srcSpec.format = SDL_AUDIO_F32;
        srcSpec.channels = channels;
        srcSpec.freq = rate;
    } else {
        Uint8* wav = nullptr;
        Uint32 len = 0;
        if (!SDL_LoadWAV(path.c_str(), &srcSpec, &wav, &len)) {
            LOG_WARN("[Audio] Nie można wczytać WAV {}: {}", path, SDL_GetError());
            return nullptr;
        }
        // Konwersja do float w formacie źródłowym (kanały/częstotliwość bez zmian)
        SDL_AudioSpec f32 = srcSpec;
        f32.format = SDL_AUDIO_F32;
        Uint8* conv = nullptr;
        int convLen = 0;
        const bool ok = SDL_ConvertAudioSamples(&srcSpec, wav, static_cast<int>(len), &f32, &conv, &convLen);
        SDL_free(wav);
        if (!ok) return nullptr;
        src.resize(static_cast<size_t>(convLen) / sizeof(float));
        std::memcpy(src.data(), conv, static_cast<size_t>(convLen));
        SDL_free(conv);
        srcSpec = f32;
    }

    SDL_AudioSpec dst{};
    dst.format = SDL_AUDIO_F32;
    dst.channels = 2;
    dst.freq = sampleRate_;
    Uint8* out = nullptr;
    int outLen = 0;
    if (!SDL_ConvertAudioSamples(&srcSpec, reinterpret_cast<const Uint8*>(src.data()),
                                 static_cast<int>(src.size() * sizeof(float)), &dst, &out, &outLen)) {
        LOG_WARN("[Audio] Konwersja {} nie powiodła się: {}", path, SDL_GetError());
        return nullptr;
    }
    auto buffer = std::make_shared<SoundBuffer>();
    buffer->samples.resize(static_cast<size_t>(outLen) / sizeof(float));
    std::memcpy(buffer->samples.data(), out, static_cast<size_t>(outLen));
    buffer->frames = buffer->samples.size() / 2;
    SDL_free(out);
    return buffer;
}

bool AudioSystem::loadSound(const std::string& name, const std::string& path) {
    if (!device_) return false;
    auto buffer = decodeFile(path);
    if (!buffer) return false;
    std::lock_guard<std::mutex> lock(mainMutex_);
    auto [it, inserted] = sounds_.try_emplace(name);
    if (inserted) it->second.id = static_cast<int>(sounds_.size());
    it->second.variants.push_back(std::move(buffer));
    return true;
}

void AudioSystem::setSoundLimits(const std::string& name, int maxInstances, float minIntervalSeconds) {
    std::lock_guard<std::mutex> lock(mainMutex_);
    auto [it, inserted] = sounds_.try_emplace(name);
    if (inserted) it->second.id = static_cast<int>(sounds_.size());
    it->second.maxInstances = std::max(1, maxInstances);
    it->second.minInterval = std::max(0.0f, minIntervalSeconds);
}

// Decyzja "czy grać" zapada w wątku gry: limit instancji liczymy z własnej listy played_ (czas końca każdego
// głosu znamy z długości dźwięku i wysokości), więc nie trzeba pytać wątku audio, co aktualnie gra.
bool AudioSystem::startVoice(const std::string& name, float volume, float pan, float pitch) {
    // Wywoływane pod blokadą mainMutex_
    auto it = sounds_.find(name);
    if (it == sounds_.end() || it->second.variants.empty()) return false;
    SoundEntry& entry = it->second;

    const double now = nowSeconds();
    if (entry.lastPlayTime >= 0.0 && now - entry.lastPlayTime < entry.minInterval) return false;
    played_.erase(std::remove_if(played_.begin(), played_.end(), [&](const PlayedVoice& v) { return v.endTime <= now; }),
                  played_.end());
    int instances = 0;
    for (const auto& v : played_) instances += v.soundId == entry.id ? 1 : 0;
    if (instances >= entry.maxInstances) return false;

    std::uniform_int_distribution<size_t> pick(0, entry.variants.size() - 1);
    Command c;
    c.type = Command::Type::Voice;
    c.voice.buffer = entry.variants[pick(rng_)].get();
    c.voice.pitch = std::clamp(pitch, 0.25f, 4.0f);
    pan = std::clamp(pan, -1.0f, 1.0f);
    c.voice.gainL = volume * std::min(1.0f, 1.0f - pan);
    c.voice.gainR = volume * std::min(1.0f, 1.0f + pan);
    if (!commands_.push(c)) return false;
    played_.push_back({entry.id, now + static_cast<double>(c.voice.buffer->frames) / (sampleRate_ * c.voice.pitch)});
    entry.lastPlayTime = now;
    return true;
}

bool AudioSystem::play(const std::string& name, float volume, float pan, float pitch) {
    if (!device_) return false;
    std::lock_guard<std::mutex> lock(mainMutex_);
    return startVoice(name, volume, pan, pitch);
}

void AudioSystem::setListener(const Vec3& position, const Vec3& right) {
    std::lock_guard<std::mutex> lock(mainMutex_);
    listenerPos_ = position;
    listenerRight_ = normalize(right);
}

void AudioSystem::setDistanceModel(float refDistance, float maxDistance) {
    std::lock_guard<std::mutex> lock(mainMutex_);
    refDistance_ = std::max(0.01f, refDistance);
    maxDistance_ = std::max(refDistance_ + 0.01f, maxDistance);
}

bool AudioSystem::play3D(const std::string& name, const Vec3& position, float volume, float pitchVariation) {
    if (!device_) return false;
    std::lock_guard<std::mutex> lock(mainMutex_);
    const Vec3 d = subtract(position, listenerPos_);
    const float dist = std::sqrt(dot(d, d));
    float att = 1.0f;
    if (dist > refDistance_) {
        att = 1.0f - (dist - refDistance_) / (maxDistance_ - refDistance_);
        att = std::clamp(att, 0.0f, 1.0f);
        att *= att;
    }
    if (att < 0.02f) return false;
    const float pan = dist > 0.001f ? dot(d, listenerRight_) / dist * 0.75f : 0.0f;
    std::uniform_real_distribution<float> pv(-pitchVariation, pitchVariation);
    return startVoice(name, volume * att, pan, 1.0f + pv(rng_));
}

bool AudioSystem::playMusic(const std::string& path, bool loop, float fadeSeconds) {
    if (!device_) return false;
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_filename(path.c_str(), &err, nullptr);
    if (!v) {
        LOG_WARN("[Audio] Nie można otworzyć muzyki {} (błąd {})", path, err);
        return false;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(v);
    SDL_AudioSpec src{};
    src.format = SDL_AUDIO_F32;
    src.channels = info.channels;
    src.freq = static_cast<int>(info.sample_rate);
    SDL_AudioSpec dst{};
    dst.format = SDL_AUDIO_F32;
    dst.channels = 2;
    dst.freq = sampleRate_;

    auto track = std::make_unique<MusicTrack>();
    track->vorbis = v;
    track->converter = SDL_CreateAudioStream(&src, &dst);
    track->channels = info.channels;
    track->loop = loop;
    track->path = path;
    if (!track->converter) return false;
    const float fadeRate = 1.0f / std::max(0.01f, fadeSeconds);
    track->fadeRate = fadeRate;
    track->ring.data.assign(static_cast<size_t>(sampleRate_) * 2, 0.0f);   // 1 s stereo

    std::lock_guard<std::mutex> mainLock(mainMutex_);
    MusicTrack* raw = track.get();
    {
        // Dekodowanie (także pierwszej porcji) robi wątek strumienia - wątek gry tylko dopisuje utwór do listy.
        // Do pierwszej porcji wątek audio czyta pusty pierścień (cisza), a utwór i tak wchodzi od głośności 0.
        std::lock_guard<std::mutex> lock(tracksMutex_);
        for (auto& t : tracks_) t->stopRequested = true;
        tracks_.push_back(std::move(track));
    }
    Command c;
    c.type = Command::Type::StartMusic;
    c.track = raw;
    c.fadeRate = fadeRate;
    if (!commands_.push(c)) raw->released.store(true);   // kolejka pełna - utwór nie zagra, strumień go usunie
    currentMusicPath_ = path;
    streamWake_.notify_all();
    return true;
}

void AudioSystem::stopMusic(float fadeSeconds) {
    std::lock_guard<std::mutex> mainLock(mainMutex_);
    {
        std::lock_guard<std::mutex> lock(tracksMutex_);
        for (auto& t : tracks_) t->stopRequested = true;
    }
    Command c;
    c.type = Command::Type::FadeMusic;
    c.fadeRate = 1.0f / std::max(0.01f, fadeSeconds);
    commands_.push(c);
    currentMusicPath_.clear();
}

bool AudioSystem::isMusicPlaying() const {
    std::lock_guard<std::mutex> lock(tracksMutex_);
    for (const auto& t : tracks_) {
        const bool drained = t->decodeEnded.load() && t->ring.available() == 0;
        if (!t->released.load() && !t->stopRequested && !drained) return true;
    }
    return false;
}

float AudioSystem::peakLevel() {
    return peak_.exchange(0.0f);
}

int AudioSystem::activeVoices() const {
    return activeVoices_.load();
}

void AudioSystem::setMasterVolume(float v) { masterVolume_ = std::clamp(v, 0.0f, 1.0f); }
void AudioSystem::setMusicVolume(float v) { musicVolume_ = std::clamp(v, 0.0f, 1.0f); }
void AudioSystem::setSfxVolume(float v) { sfxVolume_ = std::clamp(v, 0.0f, 1.0f); }

// ---------------------------------------------------------------------------------------------------------
// Wątek strumienia muzyki: co ~20 ms dopełnia bufory pierścieniowe utworów i usuwa utwory zwolnione przez
// wątek audio. Tu wolno alokować i czekać - to nie jest wątek czasu rzeczywistego.
// ---------------------------------------------------------------------------------------------------------
void AudioSystem::streamLoop() {
    std::unique_lock<std::mutex> lock(tracksMutex_);
    while (!streamStop_) {
        // Pod blokadą tylko porządki i kopia listy; dekodowanie bez blokady - playMusic/stopMusic/isMusicPlaying
        // nie czekają na dekoder. Utwory usuwa wyłącznie ten wątek, więc wskaźniki z kopii są ważne.
        tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                                     [](const std::unique_ptr<MusicTrack>& t) { return t->released.load(); }),
                      tracks_.end());
        streamSnapshot_.clear();
        for (auto& t : tracks_) streamSnapshot_.push_back(t.get());
        lock.unlock();
        for (MusicTrack* t : streamSnapshot_) fillTrack(*t);
        lock.lock();
        if (!streamStop_) streamWake_.wait_for(lock, std::chrono::milliseconds(20));
    }
}

// Dekoduje OGG i przepuszcza przez konwerter SDL (częstotliwość/kanały urządzenia), aż pierścień się zapełni.
void AudioSystem::fillTrack(MusicTrack& t) {
    if (t.released.load() || t.decodeEnded.load()) return;
    decodeScratch_.resize(4096 * static_cast<size_t>(std::max(1, t.channels)));
    for (;;) {
        const size_t space = t.ring.space() & ~size_t(1);   // parzyście - całe ramki stereo
        if (space < 256) return;
        const int availBytes = SDL_GetAudioStreamAvailable(t.converter);
        if (availBytes > 0) {
            const size_t want = std::min(space, static_cast<size_t>(availBytes) / sizeof(float)) & ~size_t(1);
            convertScratch_.resize(want);
            const int got = SDL_GetAudioStreamData(t.converter, convertScratch_.data(), static_cast<int>(want * sizeof(float)));
            if (got <= 0) return;
            t.ring.write(convertScratch_.data(), static_cast<size_t>(got) / sizeof(float));
            continue;
        }
        const int frames = stb_vorbis_get_samples_float_interleaved(t.vorbis, t.channels, decodeScratch_.data(),
                                                                    static_cast<int>(decodeScratch_.size()));
        if (frames > 0) {
            SDL_PutAudioStreamData(t.converter, decodeScratch_.data(), frames * t.channels * static_cast<int>(sizeof(float)));
            continue;
        }
        if (t.loop) {
            stb_vorbis_seek_start(t.vorbis);
            continue;
        }
        if (!t.flushed) {   // koniec pliku: wypychamy resztę z konwertera
            SDL_FlushAudioStream(t.converter);
            t.flushed = true;
            continue;
        }
        t.decodeEnded.store(true);
        return;
    }
}

// ---------------------------------------------------------------------------------------------------------
// Wątek audio (czas rzeczywisty): tylko polecenia z kolejki, miksowanie i odczyt z pierścieni.
// ---------------------------------------------------------------------------------------------------------
void AudioSystem::audioCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int) {
    auto* self = static_cast<AudioSystem*>(userdata);
    int frames = additionalAmount / static_cast<int>(sizeof(float) * 2);
    // Bufor przygotowany w init - SDL może poprosić o więcej, wtedy miksujemy kilkoma porcjami.
    while (frames > 0) {
        const int n = std::min(frames, kMixChunkFrames);
        std::fill_n(self->mixBuffer_.begin(), static_cast<size_t>(n) * 2, 0.0f);
        self->mix(self->mixBuffer_.data(), n);
        SDL_PutAudioStreamData(stream, self->mixBuffer_.data(), n * static_cast<int>(sizeof(float) * 2));
        frames -= n;
    }
}

void AudioSystem::applyCommand(const Command& c) {
    switch (c.type) {
    case Command::Type::Voice:
        if (voices_.size() >= kMaxVoices) {
            // Kradzież najdłużej grającego głosu (w miejsce - bez alokacji)
            auto oldest = std::max_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) {
                return a.position / std::max<size_t>(1, a.buffer->frames) < b.position / std::max<size_t>(1, b.buffer->frames);
            });
            *oldest = c.voice;
        } else {
            voices_.push_back(c.voice);   // pojemność zarezerwowana w init
        }
        break;
    case Command::Type::StartMusic:
        for (MusicTrack* t : playing_) {
            t->targetGain = 0.0f;   // wygaszamy poprzednie utwory
            t->fadeRate = c.fadeRate;
        }
        if (playing_.size() >= kMaxTracks) {
            playing_.front()->released.store(true);
            playing_.erase(playing_.begin());
        }
        c.track->gain = 0.0f;
        c.track->targetGain = 1.0f;
        playing_.push_back(c.track);
        break;
    case Command::Type::FadeMusic:
        for (MusicTrack* t : playing_) {
            t->targetGain = 0.0f;
            t->fadeRate = c.fadeRate;
        }
        break;
    }
}

void AudioSystem::mixMusic(MusicTrack& t, float* out, int frames) {
    const size_t read = t.ring.read(musicScratch_.data(), static_cast<size_t>(frames) * 2);
    const int readFrames = static_cast<int>(read / 2);
    if (readFrames == 0 && t.decodeEnded.load()) {
        t.targetGain = 0.0f;
        t.gain = 0.0f;
        return;
    }
    const float step = t.fadeRate / static_cast<float>(sampleRate_);
    const float vol = musicVolume_.load(std::memory_order_relaxed) * masterVolume_.load(std::memory_order_relaxed);
    for (int i = 0; i < readFrames; ++i) {
        if (t.gain < t.targetGain) t.gain = std::min(t.targetGain, t.gain + step);
        else if (t.gain > t.targetGain) t.gain = std::max(t.targetGain, t.gain - step);
        const float g = t.gain * vol;
        out[i * 2] += musicScratch_[i * 2] * g;
        out[i * 2 + 1] += musicScratch_[i * 2 + 1] * g;
    }
}

void AudioSystem::mix(float* out, int frames) {
    Command c;
    while (commands_.pop(c)) applyCommand(c);

    // Muzyka
    for (MusicTrack* m : playing_) mixMusic(*m, out, frames);
    playing_.erase(std::remove_if(playing_.begin(), playing_.end(),
                                  [](MusicTrack* m) {
                                      const bool done = m->targetGain <= 0.0f && m->gain <= 0.0001f;
                                      if (done) m->released.store(true);   // wątek strumienia go usunie
                                      return done;
                                  }),
                   playing_.end());

    // Efekty
    const float sfx = sfxVolume_.load(std::memory_order_relaxed) * masterVolume_.load(std::memory_order_relaxed);
    for (auto& v : voices_) {
        const auto& s = v.buffer->samples;
        const size_t n = v.buffer->frames;
        for (int i = 0; i < frames; ++i) {
            const size_t p = static_cast<size_t>(v.position);
            if (p + 1 >= n) {
                v.position = static_cast<double>(n);
                break;
            }
            const float f = static_cast<float>(v.position - static_cast<double>(p));
            const float l = s[p * 2] + (s[(p + 1) * 2] - s[p * 2]) * f;
            const float r = s[p * 2 + 1] + (s[(p + 1) * 2 + 1] - s[p * 2 + 1]) * f;
            out[i * 2] += l * v.gainL * sfx;
            out[i * 2 + 1] += r * v.gainR * sfx;
            v.position += v.pitch;
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return v.position + 1.0 >= static_cast<double>(v.buffer->frames); }),
                  voices_.end());
    activeVoices_.store(static_cast<int>(voices_.size()), std::memory_order_relaxed);

    // Miękkie ograniczenie, żeby suma wielu dźwięków nie przesterowała
    float peak = 0.0f;
    for (int i = 0; i < frames * 2; ++i) {
        const float x = out[i];
        out[i] = x / (1.0f + std::abs(x) * 0.25f);
        peak = std::max(peak, std::abs(out[i]));
    }
    if (peak > peak_.load(std::memory_order_relaxed)) peak_.store(peak, std::memory_order_relaxed);
}

} // namespace engine3d
