#include "scene/transform_system.h"

#include <functional>
#include <unordered_map>

#include "core/entity.h"
#include "core/hierarchy_component.h"
#include "core/transform_component.h"
#include "core/math.h"

namespace engine3d {

void TransformSystem::update(Scene& scene, float /*deltaTime*/) {
    // Pamięć podręczna (Cache) wyliczonych macierzy świata.
    // Dzięki memoizacji zapobiegamy wielokrotnemu przeliczaniu macierzy dla tego samego rodzica,
    // gdy posiada on wiele dzieci. Złożoność algorytmu spada z wykładniczej do liniowej O(N).
    std::unordered_map<const Entity*, Matrix4> cache;

    // Funkcja lambda wywołująca się rekurencyjnie w celu wyznaczenia ostatecznej pozycji w świecie.
    std::function<Matrix4(Entity*)> resolveWorldMatrix = [&](Entity* entity) -> Matrix4 {
        // Krok 1: Sprawdzamy, czy macierz tego obiektu została już obliczona w tej klatce.
        auto found = cache.find(entity);
        if (found != cache.end()) {
            return found->second;
        }

        // Krok 2: Pobieramy lokalną macierz transformacji TRS (Translation, Rotation, Scale).
        TransformComponent* transform = entity->getComponent<TransformComponent>();
        Matrix4 local = transform ? transform->getLocalMatrix() : Matrix4::identity();
        Matrix4 world = local;

        // Krok 3: Jeśli obiekt posiada rodzica, to jego ostateczna pozycja w świecie jest iloczynem:
        // Macierz_Swiata_Rodzica * Macierz_Lokalna_Dziecka.
        // Mnożenie macierzy przenosi układ współrzędnych dziecka w układ rodzica.
        if (auto* hierarchy = entity->getComponent<HierarchyComponent>()) {
            if (hierarchy->parent && hierarchy->parent->isActive()) {
                world = multiply(resolveWorldMatrix(hierarchy->parent), local);
            }
        }

        // Krok 4: Zapisujemy wyliczoną macierz w komponencie transformacji obiektu.
        if (transform) {
            transform->setWorldMatrix(world);
        }

        // Krok 5: Zapisujemy wynik w pamięci podręcznej i zwracamy.
        cache[entity] = world;
        return world;
    };

    // Iterujemy po wszystkich encjach na scenie i wyliczamy ich transformacje.
    for (const auto& entity : scene.getEntities()) {
        if (!entity->isActive()) {
            continue; // Pomijamy nieaktywne encje oraz całe ich poddrzewa.
        }
        resolveWorldMatrix(entity.get());
    }
}

} // namespace engine3d