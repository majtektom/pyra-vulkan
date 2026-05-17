#include "engine/Engine.hpp"

#include "VulkanRenderer.hpp"

#include <exception>
#include <iostream>
#include <utility>

namespace engine3d {

// Konstruktor tylko sklada publiczna fasade z konkretnym rendererem Vulkan.
Engine::Engine(AppConfig config)
    : renderer_(std::make_unique<VulkanRenderer>(std::move(config))) {}

// Definicja poza klasa trzyma szczegoly implementacyjne w pliku .cpp.
Engine::~Engine() = default;

// Przenoszenie pozostawiamy kompilatorowi, bo unique_ptr robi tutaj cala potrzebna robote.
Engine::Engine(Engine&&) noexcept = default;

// Analogicznie operator przypisania przenoszacego moze byc domyslny.
Engine& Engine::operator=(Engine&&) noexcept = default;

// Publiczne run() deleguje prace do renderera, zeby interfejs silnika byl prosty.
void Engine::run() {
    renderer_->run();
}

// Funkcja pomocnicza zamienia wyjatki na kod wyjscia procesu, co upraszcza kod przykladow.
int runEngine(AppConfig config) {
    try {
        Engine engine(std::move(config));
        engine.run();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

}  // namespace engine3d