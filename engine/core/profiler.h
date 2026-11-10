#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace engine3d {

/**
 * @brief Prosty profiler czasu klatki (CPU) - pokazuje, gdzie idzie czas: logika gry, przygotowanie sceny,
 * nagrywanie komend Vulkan, czekanie na GPU.
 *
 * Włączany zmienną środowiskową ENGINE_PROFILE=1. Co 3 s wypisuje do logu średni czas każdej sekcji na klatkę:
 *   [profil] klatka 16.4 ms (61 fps) | update 9.1 | gra 8.3 | render.cienie 2.0 | ...
 *
 * Dwa sposoby pomiaru (tylko w głównym wątku):
 *   - PROFILE_SCOPE("nazwa");          czas od tego miejsca do końca bloku { },
 *   - Profiler::get().lap("nazwa");    czas od poprzedniego lap() / początku klatki - wygodne w długich funkcjach,
 *                                      których nie chcemy dzielić na bloki.
 */
class Profiler {
public:
    static Profiler& get();

    [[nodiscard]] bool enabled() const { return enabled_; }
    void beginFrame();
    void lap(const char* name);
    void add(const char* name, double ms);
    // Licznik (np. liczba rysowań) - w raporcie średnia na klatkę z dopiskiem "x".
    void count(const char* name, double n);
    void endFrame();

    // Ostatni raport (średnie z okna ~3 s) - np. do wyświetlenia w grze.
    [[nodiscard]] const std::string& lastReport() const { return lastReport_; }

private:
    Profiler();
    struct Section {
        const char* name;
        double totalMs = 0.0;
        bool counter = false;
    };
    Section& section(const char* name);

    bool enabled_ = false;
    std::vector<Section> sections_;   // kolejność pierwszego wystąpienia = kolejność w raporcie
    std::chrono::steady_clock::time_point frameStart_{}, lapStart_{}, windowStart_{};
    double frameTotalMs_ = 0.0;
    int frames_ = 0;
    std::string lastReport_;
};

class ProfileScope {
public:
    explicit ProfileScope(const char* name) : name_(name), start_(std::chrono::steady_clock::now()) {}
    ~ProfileScope() {
        if (!Profiler::get().enabled()) return;
        Profiler::get().add(name_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count());
    }
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    const char* name_;
    std::chrono::steady_clock::time_point start_;
};

#define ENGINE3D_PROFILE_CONCAT2(a, b) a##b
#define ENGINE3D_PROFILE_CONCAT(a, b) ENGINE3D_PROFILE_CONCAT2(a, b)
#define PROFILE_SCOPE(name) ::engine3d::ProfileScope ENGINE3D_PROFILE_CONCAT(profileScope_, __LINE__)(name)

} // namespace engine3d
