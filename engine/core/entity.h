#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "core/component.h"
#include "core/transform_component.h"

namespace engine3d {

class Entity {
public:
    explicit Entity(const std::string& entityName);
    ~Entity() = default;

    const std::string& getName() const { return name_; }
    bool isActive() const { return active_; }
    void setActive(bool isActive) { active_ = isActive; }

    TransformComponent& transform() { return *transform_; }
    const TransformComponent& transform() const { return *transform_; }

    void initialize();
    void update(std::chrono::milliseconds deltaTime);
    void render();

    template <typename T, typename... Args>
    T* addComponent(Args&&... args) {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* componentPtr = component.get();
        componentPtr->setOwner(this);
        components_.push_back(std::move(component));
        componentPtr->initialize();
        return componentPtr;
    }

    template <typename T>
    T* getComponent() const {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        for (auto it = components_.rbegin(); it != components_.rend(); ++it) {
            if (auto* casted = dynamic_cast<T*>(it->get())) {
                return casted;
            }
        }
        return nullptr;
    }

    template <typename T>
    bool removeComponent() {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        for (auto it = components_.rbegin(); it != components_.rend(); ++it) {
            if (dynamic_cast<T*>(it->get()) != nullptr) {
                components_.erase(std::next(it).base());
                return true;
            }
        }
        return false;
    }

    template <typename T>
    bool hasComponent() const {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        return getComponent<T>() != nullptr;
    }

private:
    std::string name_;
    bool active_ = true;
    TransformComponent* transform_ = nullptr;
    std::vector<std::unique_ptr<Component>> components_;
};

} // namespace engine3d
