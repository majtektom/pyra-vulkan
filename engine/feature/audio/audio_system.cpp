#include "feature/audio/audio_system.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/logger.h"

#define STB_VORBIS_HEADER_ONLY
#include "feature/audio/stb_vorbis.c"

namespace engine3d {

// Strumieniowany utwór muzyczny: dekoder OGG + konwerter częstotliwości SDL.
struct AudioSystem::MusicTrack {
    stb_vorbis* vorbis = nullptr;
    SDL_AudioStream* converter = nullptr;
    int channels = 2;
    bool loop = true;
    bool finished = false;
    float gain = 0.0f;
    float targetGain = 1.0f;
    float fadeRate = 1.0f;      // zmiana wzmocnienia na sekundę
    std::string path;

    ~MusicTrack() {
        if (converter) SDL_DestroyAudioStream(converter);
        if (vorbis) stb_vorbis_close(vorbis);
    }
};

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
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = sampleRate_;
    device_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioSystem::audioCallback, this);
    if (!device_) {
        LOG_WARN("[Audio] Brak urządzenia audio: {}", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(device_);
    LOG_INFO("[Audio] Urządzenie audio gotowe ({} Hz, stereo)", sampleRate_);
    return true;
}

void AudioSystem::shutdown() {
    if (device_) {
        SDL_DestroyAudioStream(device_);  // zatrzymuje też wątek wywołań zwrotnych
        device_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.clear();
    music_.clear();
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
    std::lock_guard<std::mutex> lock(mutex_);
    sounds_[name].variants.push_back(std::move(buffer));
    return true;
}

void AudioSystem::setSoundLimits(const std::string& name, int maxInstances, float minIntervalSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& e = sounds_[name];
    e.maxInstances = std::max(1, maxInstances);
    e.minInterval = std::max(0.0f, minIntervalSeconds);
}

bool AudioSystem::startVoice(const std::string& name, float volume, float pan, float pitch) {
    // Wywoływane pod blokadą mutex_
    auto it = sounds_.find(name);
    if (it == sounds_.end() || it->second.variants.empty()) return false;
    SoundEntry& entry = it->second;

    const double now = nowSeconds();
    if (entry.lastPlayTime >= 0.0 && now - entry.lastPlayTime < entry.minInterval) return false;
    int instances = 0;
    for (const auto& v : voices_) instances += v.name == name ? 1 : 0;
    if (instances >= entry.maxInstances) return false;

    if (voices_.size() >= kMaxVoices) {
        // Kradzież najdłużej grającego głosu
        auto oldest = std::max_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) {
            return a.position / std::max<size_t>(1, a.buffer->frames) < b.position / std::max<size_t>(1, b.buffer->frames);
        });
        voices_.erase(oldest);
    }

    std::uniform_int_distribution<size_t> pick(0, entry.variants.size() - 1);
    Voice v;
    v.buffer = entry.variants[pick(rng_)];
    v.name = name;
    v.pitch = std::clamp(pitch, 0.25f, 4.0f);
    pan = std::clamp(pan, -1.0f, 1.0f);
    v.gainL = volume * std::min(1.0f, 1.0f - pan);
    v.gainR = volume * std::min(1.0f, 1.0f + pan);
    voices_.push_back(std::move(v));
    entry.lastPlayTime = now;
    return true;
}

bool AudioSystem::play(const std::string& name, float volume, float pan, float pitch) {
    if (!device_) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return startVoice(name, volume, pan, pitch);
}

void AudioSystem::setListener(const Vec3& position, const Vec3& right) {
    std::lock_guard<std::mutex> lock(mutex_);
    listenerPos_ = position;
    listenerRight_ = normalize(right);
}

void AudioSystem::setDistanceModel(float refDistance, float maxDistance) {
    std::lock_guard<std::mutex> lock(mutex_);
    refDistance_ = std::max(0.01f, refDistance);
    maxDistance_ = std::max(refDistance_ + 0.01f, maxDistance);
}

bool AudioSystem::play3D(const std::string& name, const Vec3& position, float volume, float pitchVariation) {
    if (!device_) return false;
    std::lock_guard<std::mutex> lock(mutex_);
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
    const float fade = std::max(0.01f, fadeSeconds);
    track->fadeRate = 1.0f / fade;
    track->gain = 0.0f;
    track->targetGain = 1.0f;
    if (!track->converter) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& m : music_) {
        m->targetGain = 0.0f;  // wygaszamy poprzednie utwory
        m->fadeRate = 1.0f / fade;
    }
    music_.push_back(std::move(track));
    currentMusicPath_ = path;
    return true;
}

void AudioSystem::stopMusic(float fadeSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& m : music_) {
        m->targetGain = 0.0f;
        m->fadeRate = 1.0f / std::max(0.01f, fadeSeconds);
    }
    currentMusicPath_.clear();
}

bool AudioSystem::isMusicPlaying() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& m : music_) {
        if (!m->finished && m->targetGain > 0.0f) return true;
    }
    return false;
}

float AudioSystem::peakLevel() {
    std::lock_guard<std::mutex> lock(mutex_);
    const float p = peak_;
    peak_ = 0.0f;
    return p;
}

int AudioSystem::activeVoices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(voices_.size());
}

void AudioSystem::setMasterVolume(float v) { masterVolume_ = std::clamp(v, 0.0f, 1.0f); }
void AudioSystem::setMusicVolume(float v) { musicVolume_ = std::clamp(v, 0.0f, 1.0f); }
void AudioSystem::setSfxVolume(float v) { sfxVolume_ = std::clamp(v, 0.0f, 1.0f); }

void AudioSystem::audioCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int) {
    auto* self = static_cast<AudioSystem*>(userdata);
    const int frames = additionalAmount / static_cast<int>(sizeof(float) * 2);
    if (frames <= 0) return;
    std::vector<float> out(static_cast<size_t>(frames) * 2, 0.0f);
    self->mix(out.data(), frames);
    SDL_PutAudioStreamData(stream, out.data(), frames * static_cast<int>(sizeof(float) * 2));
}

void AudioSystem::mixMusic(MusicTrack& t, float* out, int frames) {
    const int need = frames * 2 * static_cast<int>(sizeof(float));
    std::vector<float> decoded(4096 * static_cast<size_t>(std::max(1, t.channels)));
    while (!t.finished && SDL_GetAudioStreamAvailable(t.converter) < need) {
        const int got = stb_vorbis_get_samples_float_interleaved(t.vorbis, t.channels, decoded.data(),
                                                                 static_cast<int>(decoded.size()));
        if (got <= 0) {
            if (t.loop) {
                stb_vorbis_seek_start(t.vorbis);
                continue;
            }
            SDL_FlushAudioStream(t.converter);
            t.finished = true;
            break;
        }
        SDL_PutAudioStreamData(t.converter, decoded.data(), got * t.channels * static_cast<int>(sizeof(float)));
    }
    scratch_.assign(static_cast<size_t>(frames) * 2, 0.0f);
    const int read = SDL_GetAudioStreamData(t.converter, scratch_.data(), need);
    const int readFrames = std::max(0, read) / static_cast<int>(sizeof(float) * 2);
    if (t.finished && readFrames == 0) {
        t.targetGain = 0.0f;
        t.gain = 0.0f;
        return;
    }
    const float step = t.fadeRate / static_cast<float>(sampleRate_);
    const float vol = musicVolume_ * masterVolume_;
    for (int i = 0; i < readFrames; ++i) {
        if (t.gain < t.targetGain) t.gain = std::min(t.targetGain, t.gain + step);
        else if (t.gain > t.targetGain) t.gain = std::max(t.targetGain, t.gain - step);
        const float g = t.gain * vol;
        out[i * 2] += scratch_[i * 2] * g;
        out[i * 2 + 1] += scratch_[i * 2 + 1] * g;
    }
}

void AudioSystem::mix(float* out, int frames) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Muzyka
    for (auto& m : music_) mixMusic(*m, out, frames);
    music_.erase(std::remove_if(music_.begin(), music_.end(),
                                [](const std::unique_ptr<MusicTrack>& m) {
                                    return (m->targetGain <= 0.0f && m->gain <= 0.0001f) || (m->finished && m->gain <= 0.0001f);
                                }),
                 music_.end());

    // Efekty
    const float sfx = sfxVolume_ * masterVolume_;
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

    // Miękkie ograniczenie, żeby suma wielu dźwięków nie przesterowała
    for (int i = 0; i < frames * 2; ++i) {
        const float x = out[i];
        out[i] = x / (1.0f + std::abs(x) * 0.25f);
        peak_ = std::max(peak_, std::abs(out[i]));
    }
}

} // namespace engine3d
