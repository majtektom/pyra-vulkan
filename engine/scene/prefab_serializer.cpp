#include "scene/prefab_serializer.h"

#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/entity.h"
#include "core/hierarchy_component.h"
#include "core/light_component.h"
#include "core/transform_component.h"
#include "scene/scene.h"

namespace engine3d {

namespace {

using json = nlohmann::json;

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

LightType lightTypeFromString(const std::string& value) {
    if (value == "directional") {
        return LightType::Directional;
    }
    if (value == "spot") {
        return LightType::Spot;
    }
    return LightType::Point;
}

json vec3ToJson(const Vec3& v) {
    return json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

Vec3 vec3FromJson(const json& node, const Vec3& fallback = Vec3{}) {
    Vec3 result = fallback;
    result.x = node.value("x", result.x);
    result.y = node.value("y", result.y);
    result.z = node.value("z", result.z);
    return result;
}

json buildEntityJson(const Entity& entity) {
    json e;
    e["sourceId"] = entity.id();
    e["name"] = entity.getName();
    e["active"] = entity.isActive();

    const auto& t = entity.transform();
    e["transform"] = {
        {"position", vec3ToJson(t.position)},
        {"rotation", vec3ToJson(t.rotation)},
        {"scale", vec3ToJson(t.scale)}
    };

    if (const auto* light = entity.getComponent<LightComponent>()) {
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

    return e;
}

void collectPrefabNodes(const Entity& root,
                        std::vector<const Entity*>& outOrder,
                        std::unordered_set<EntityId>& visited) {
    if (visited.count(root.id()) != 0) {
        return;
    }
    visited.insert(root.id());
    outOrder.push_back(&root);

    const auto* hierarchy = root.getComponent<HierarchyComponent>();
    if (!hierarchy) {
        return;
    }

    for (const Entity* child : hierarchy->children) {
        if (!child) {
            continue;
        }
        collectPrefabNodes(*child, outOrder, visited);
    }
}

void applyEntityJson(Entity& entity, const json& e) {
    entity.setActive(e.value("active", true));

    if (e.contains("transform") && e["transform"].is_object()) {
        const auto& t = e["transform"];
        entity.transform().position = vec3FromJson(t.value("position", json::object()), entity.transform().position);
        entity.transform().rotation = vec3FromJson(t.value("rotation", json::object()), entity.transform().rotation);
        entity.transform().scale = vec3FromJson(t.value("scale", json::object()), entity.transform().scale);
    }

    if (e.contains("light") && e["light"].is_object()) {
        const auto& l = e["light"];
        auto* light = entity.getComponent<LightComponent>();
        if (!light) {
            light = entity.addComponent<LightComponent>();
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

} // namespace

bool PrefabSerializer::saveEntityToFile(const Entity& entity, const std::string& filePath) {
    json root;
    root["format"] = "engine3d.prefab.v2";
    root["rootSourceId"] = entity.id();
    root["entities"] = json::array();

    std::vector<const Entity*> ordered;
    std::unordered_set<EntityId> visited;
    collectPrefabNodes(entity, ordered, visited);

    for (const Entity* node : ordered) {
        json serialized = buildEntityJson(*node);
        const auto* h = node->getComponent<HierarchyComponent>();
        if (h && h->parent) {
            serialized["parentSourceId"] = h->parent->id();
        } else {
            serialized["parentSourceId"] = nullptr;
        }
        root["entities"].push_back(std::move(serialized));
    }

    std::ofstream out(filePath);
    if (!out.is_open()) {
        return false;
    }

    out << root.dump(2);
    return out.good();
}

Entity* PrefabSerializer::instantiateFromFile(Scene& scene, const std::string& filePath,
                                              const std::string& nameOverride) {
    std::ifstream in(filePath);
    if (!in.is_open()) {
        return nullptr;
    }

    json root;
    try {
        in >> root;
    } catch (...) {
        return nullptr;
    }

    if (!root.contains("entities") || !root["entities"].is_array()) {
        return nullptr;
    }

    std::unordered_map<EntityId, Entity*> sourceToInstance;
    sourceToInstance.reserve(root["entities"].size());

    // Krok 1: tworzymy encje bez laczenia relacji, aby miec mapowanie sourceId -> instancja.
    for (const auto& e : root["entities"]) {
        EntityId sourceId = e.value("sourceId", static_cast<EntityId>(0));
        if (sourceId == 0) {
            continue;
        }

        std::string name = e.value("name", std::string("prefab_entity"));
        if (!nameOverride.empty() && sourceId == root.value("rootSourceId", static_cast<EntityId>(0))) {
            name = nameOverride;
        }

        Entity* instance = scene.createEntity(name);
        if (!instance) {
            continue;
        }
        applyEntityJson(*instance, e);
        sourceToInstance[sourceId] = instance;
    }

    // Krok 2: odtwarzamy hierarchy parent-child po sourceId.
    for (const auto& e : root["entities"]) {
        EntityId sourceId = e.value("sourceId", static_cast<EntityId>(0));
        auto childIt = sourceToInstance.find(sourceId);
        if (sourceId == 0 || childIt == sourceToInstance.end()) {
            continue;
        }

        Entity* childEntity = childIt->second;

        if (e.contains("parentSourceId") && !e["parentSourceId"].is_null()) {
            EntityId parentSourceId = e.value("parentSourceId", static_cast<EntityId>(0));
            auto parentIt = sourceToInstance.find(parentSourceId);
            if (parentIt != sourceToInstance.end()) {
                Entity* parentEntity = parentIt->second;
                scene.setParent(childEntity, parentEntity);
            }
        }
    }

    EntityId rootSourceId = root.value("rootSourceId", static_cast<EntityId>(0));
    auto rootIt = sourceToInstance.find(rootSourceId);
    if (rootIt != sourceToInstance.end()) {
        return rootIt->second;
    }

    if (!sourceToInstance.empty()) {
        return sourceToInstance.begin()->second;
    }

    return nullptr;
}

} // namespace engine3d
