#include "scene/light_system.h"

#include "core/entity.h"
#include "core/light_component.h"

namespace engine3d {

void LightSystem::update(Scene& scene, float /*deltaTime*/) {
    // Krok 1: Czyścimy listę wyekstrahowanych świateł z poprzedniej klatki.
    scene.clearFrameLights();
    auto& frameLights = scene.mutableFrameLights();
    
    // Optymalizacja rezerwacji pamięci w wektorze w celu uniknięcia kosztownych realokacji.
    frameLights.reserve(scene.getEntities().size());

    // Krok 2: Iterujemy po wszystkich encjach na scenie.
    for (const auto& entity : scene.getEntities()) {
        if (!entity->isActive()) {
            continue; // Pomijamy nieaktywne obiekty.
        }

        // Krok 3: Sprawdzamy, czy obiekt posiada aktywny komponent oświetlenia (LightComponent).
        const auto* light = entity->getComponent<LightComponent>();
        if (!light || !light->isActive()) {
            continue;
        }

        // Krok 4: Kopiujemy i normalizujemy parametry światła do spłaszczonej struktury SceneLight.
        SceneLight out{};
        // Pozycja światła pobierana jest z transformacji encji (przestrzeń świata).
        out.position = entity->transform().position;
        out.direction = normalize(light->direction);
        out.color = light->color;
        out.intensity = light->intensity;
        out.range = light->range;
        out.innerAngleDeg = light->innerAngleDeg;
        out.outerAngleDeg = light->outerAngleDeg;

        // Krok 5: Mapujemy typ światła z ECS-owego komponentu na typ niskopoziomowego renderera.
        switch (light->type) {
        case LightType::Directional:
            out.type = SceneLightType::Directional; // Światło kierunkowe (np. słońce) - oświetla całą scenę z jednego kierunku.
            break;
        case LightType::Point:
            out.type = SceneLightType::Point;       // Światło punktowe (np. żarówka) - emituje światło we wszystkich kierunkach w zadanym zasięgu (range).
            break;
        case LightType::Spot:
            out.type = SceneLightType::Spot;        // Reflektor (spot) - emituje stożek światła o określonym kącie wewnętrznym i zewnętrznym.
            break;
        }

        // Dodajemy światło do listy aktywnych świateł klatki.
        frameLights.push_back(out);
    }
}

} // namespace engine3d