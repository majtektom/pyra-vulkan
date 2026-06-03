#pragma once

#include "scene/scene.h"

namespace engine3d {

/**
 * @brief System ECS odpowiedzialny za zbieranie i ekstrakcję świateł (Light System).
 *
 * Światła w silniku są reprezentowane przez encje z komponentem LightComponent.
 * LightSystem przeszukuje scenę w poszukiwaniu takich encji, pobiera ich parametry 
 * (np. pozycję z TransformComponent, typ światła, kolor, intensywność) i tworzy listę
 * struktur SceneLight przekazywanych do renderera w bieżącej klatce.
 */
class LightSystem final : public ISceneSystem {
public:
    /**
     * @brief Zbiera parametry wszystkich aktywnych świateł ze sceny i zapisuje je w strukturach klatki.
     */
    void update(Scene& scene, float deltaTime) override;

    // Nazwa systemu dla logowania/debugowania.
    const char* name() const override { return "LightSystem"; }
};

} // namespace engine3d