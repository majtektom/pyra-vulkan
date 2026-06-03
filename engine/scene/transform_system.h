#pragma once

#include "scene/scene.h"

namespace engine3d {

/**
 * @brief System ECS odpowiedzialny za przeliczanie transformacji w świecie (Space Transform System).
 *
 * Każdy obiekt trójwymiarowy (Entity) posiada lokalną transformację (TransformComponent),
 * czyli pozycję, rotację i skalę określoną względem swojego rodzica (przestrzeń lokalna).
 * Aby GPU mogło prawidłowo narysować scenę, obiekty muszą mieć wyliczoną macierz świata 
 * (World Matrix / Model Matrix), która definiuje ich ostateczną pozycję w przestrzeni globalnej.
 *
 * TransformSystem przetwarza drzewo hierarchii (Scene Graph) i wyznacza te macierze przed renderingiem.
 */
class TransformSystem final : public ISceneSystem {
public:
    /**
     * @brief Aktualizuje macierze świata dla wszystkich aktywnych encji na scenie.
     * @param scene Referencja do przetwarzanej sceny.
     * @param deltaTime Czas od ostatniej klatki w sekundach (nieużywany w tym systemie).
     */
    void update(Scene& scene, float deltaTime) override;

    // Nazwa systemu do celów diagnostycznych.
    const char* name() const override { return "TransformSystem"; }
};

} // namespace engine3d