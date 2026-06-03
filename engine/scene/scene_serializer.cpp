#include "scene/scene_serializer.h"
#include "core/logger.h"

#include <fstream>

#include <nlohmann/json.hpp>

#include "core/entity.h"
#include "core/light_component.h"
#include "core/transform_component.h"
#include "scene/scene.h"

namespace engine3d {

namespace {

using json = nlohmann::json;

// Konwersja enuma typu światła na czytelny dla człowieka format tekstowy (string).
const char* lightTypeToString(LightType type) {
    switch (type) {
    case LightType::Directional:
        return "directional";
    case LightType::Point:
        return "point";
    case LightType::Spot:
        return "spot";
    }
    return "point";
}

// Konwersja formatu tekstowego (string) z pliku JSON na niskopoziomowy enum typu światła.
LightType lightTypeFromString(const std::string& value) {
    if (value == "directional") {
        return LightType::Directional;
    }
    if (value == "spot") {
        return LightType::Spot;
    }
    return LightType::Point;
}

// Konwersja wektora Vec3 na obiekt nlohmann::json.
json vec3ToJson(const Vec3& v) {
    return json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

// Odczyt wektora Vec3 z węzła JSON z obsługą wartości domyślnej w razie braku pól.
Vec3 vec3FromJson(const json& node, const Vec3& fallback = Vec3{}) {
    Vec3 result = fallback;
    result.x = node.value("x", result.x);
    result.y = node.value("y", result.y);
    result.z = node.value("z", result.z);
    return result;
}

} // namespace

bool SceneSerializer::saveToFile(const Scene& scene, const std::string& filePath) {
    json root;
    root["name"] = scene.name();
    root["entities"] = json::array(); // Inicjalizujemy tablicę encji.

    // Iterujemy po wszystkich encjach znajdujących się na scenie.
    for (const auto& entity : scene.getEntities()) {
        json e;
        e["id"] = entity->id();
        e["name"] = entity->getName();
        e["active"] = entity->isActive();

        // Serializacja komponentu transformacji (pozycja, rotacja, skala):
        const auto& t = entity->transform();
        e["transform"] = {
            {"position", vec3ToJson(t.position)},
            {"rotation", vec3ToJson(t.rotation)},
            {"scale", vec3ToJson(t.scale)}
        };

        // Serializacja komponentu oświetlenia (jeśli encja go posiada):
        if (const auto* light = entity->getComponent<LightComponent>()) {
            e["light"] = {
                {"active", light->isActive()},
                {"type", lightTypeToString(light->type)},
                {"color", vec3ToJson(light->color)},
                {"intensity", light->intensity},
                {"range", light->range},
                {"direction", vec3ToJson(light->direction)},
                {"innerAngleDeg", light->innerAngleDeg},
                {"outerAngleDeg", light->outerAngleDeg}
            };
        }

        // Dodajemy zserializowaną encję do tablicy głównej.
        root["entities"].push_back(std::move(e));
    }

    // Otwieramy strumień wyjściowy do zapisu pliku.
    std::ofstream out(filePath);
    if (!out.is_open()) {
        LOG_ERROR("[SceneSerializer] Failed to open file for writing: {}", filePath);
        return false;
    }

    // Zrzucamy strukturę JSON do pliku tekstowego z wcięciami o szerokości 2 spacji (pretty printing).
    out << root.dump(2);
    if (!out.good()) {
        LOG_ERROR("[SceneSerializer] Error occurred while writing to file: {}", filePath);
        return false;
    }
    LOG_INFO("[SceneSerializer] Successfully saved scene to {}", filePath);
    return true;
}

bool SceneSerializer::loadFromFile(Scene& scene, const std::string& filePath) {
    std::ofstream out; // (zmienna pomocnicza)
    std::ifstream in(filePath);
    if (!in.is_open()) {
        LOG_ERROR("[SceneSerializer] Failed to open file for reading: {}", filePath);
        return false;
    }

    json root;
    try {
        in >> root; // Odczytujemy i parsujemy całą zawartość pliku JSON.
    } catch (const std::exception& e) {
        LOG_ERROR("[SceneSerializer] JSON parsing error for {}: {}", filePath, e.what());
        return false;
    }

    // Sprawdzamy poprawność struktury dokumentu JSON.
    if (!root.contains("entities") || !root["entities"].is_array()) {
        LOG_ERROR("[SceneSerializer] Invalid scene file format (missing 'entities' array): {}", filePath);
        return false;
    }

    // Czyszczenie aktualnych encji na scenie przed załadowaniem nowych.
    scene.clearEntities();

    // Rekonstrukcja obiektów ze zserializowanej tablicy JSON:
    for (const auto& e : root["entities"]) {
        EntityId id = e.value("id", static_cast<EntityId>(0));
        if (id == 0) {
            continue; // Pomijamy błędne wpisy.
        }

        const std::string name = e.value("name", std::string("entity_" + std::to_string(id)));
        
        // Tworzymy encję w ECS z przywróceniem jej stabilnego ID.
        Entity* entity = scene.createEntity(id, name);
        entity->setActive(e.value("active", true));

        // Deserializacja i przywracanie transformacji:
        if (e.contains("transform") && e["transform"].is_object()) {
            const auto& t = e["transform"];
            entity->transform().position = vec3FromJson(t.value("position", json::object()), entity->transform().position);
            entity->transform().rotation = vec3FromJson(t.value("rotation", json::object()), entity->transform().rotation);
            entity->transform().scale = vec3FromJson(t.value("scale", json::object()), entity->transform().scale);
        }

        // Deserializacja i przywracanie światła:
        if (e.contains("light") && e["light"].is_object()) {
            const auto& l = e["light"];
            auto* light = entity->getComponent<LightComponent>();
            if (!light) {
                light = entity->addComponent<LightComponent>();
            }
            light->setActive(l.value("active", true));
            light->type = lightTypeFromString(l.value("type", std::string("point")));
            light->color = vec3FromJson(l.value("color", json::object()), light->color);
            light->intensity = l.value("intensity", light->intensity);
            light->range = l.value("range", light->range);
            light->direction = vec3FromJson(l.value("direction", json::object()), light->direction);
            light->innerAngleDeg = l.value("innerAngleDeg", light->innerAngleDeg);
            light->outerAngleDeg = l.value("outerAngleDeg", light->outerAngleDeg);
        }
    }

    LOG_INFO("[SceneSerializer] Successfully loaded scene from {}", filePath);
    return true;
}

} // namespace engine3d
