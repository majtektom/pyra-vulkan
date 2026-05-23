#include "entity.h"

namespace engine3d {

Entity::Entity(const std::string& entityName) : name(entityName) {}

void Entity::initialize() {
    for (auto& component : components) {
        component->initialize();
    }
}

void Entity::update(std::chrono::milliseconds deltaTime) {
    for (auto& component : components) {
        if (component->isActive()) {
            component->update(deltaTime);
        }
    }
}

void Entity::render() {
    for (auto& component : components) {
        if (component->isActive()) {
            component->render();
        }
    }
}

} // namespace engine3d
