#include "core/entity.h"

namespace engine3d {

Entity::Entity(const std::string& entityName) : name_(entityName) {
    transform_ = addComponent<TransformComponent>();
}

void Entity::initialize() {
    for (auto& component : components_) {
        component->initialize();
    }
}

void Entity::update(std::chrono::milliseconds deltaTime) {
    for (auto& component : components_) {
        if (component->isActive()) {
            component->update(deltaTime);
        }
    }
}

void Entity::render() {
    for (auto& component : components_) {
        if (component->isActive()) {
            component->render();
        }
    }
}

} // namespace engine3d
