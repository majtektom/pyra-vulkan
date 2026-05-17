#pragma once

#include <memory>

#include "engine/Types.hpp"

namespace engine3d {

class VulkanRenderer;

// Glowna fasada silnika. Z zewnatrz widzimy tylko konfiguracje i metode run().
class Engine {
public:
    // Tworzy silnik na podstawie kompletnej konfiguracji sceny i zasobow.
    explicit Engine(AppConfig config);

    // Domyslny destruktor wystarcza, bo zwalnianiem szczegolow zajmuja sie klasy pomocnicze.
    ~Engine();

    // Pozwalamy przenosic obiekt silnika, zeby wygodnie przekazywac go miedzy funkcjami.
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    // Kopiowanie blokujemy, bo renderer posiada unikalne uchwyty do zasobow Vulkan/X11.
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Uruchamia petle glowne renderowania i obslugi okna.
    void run();

private:
    // Wlasciwa logika renderowania siedzi w osobnej klasie, zeby fasada byla mala i czytelna.
    std::unique_ptr<VulkanRenderer> renderer_;
};

// Wygodny punkt wejscia dla przykladow: tworzy silnik, uruchamia go i obsluguje bledy.
int runEngine(AppConfig config);

}  // namespace engine3d