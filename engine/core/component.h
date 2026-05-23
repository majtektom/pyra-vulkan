#pragma once

#include <chrono>
#include <string>

namespace engine3d {

class Entity;

class Component {
public:
    explicit Component(const std::string& componentName = "Component");
    virtual ~Component() = default;

    virtual void initialize();
    virtual void update(std::chrono::milliseconds deltaTime);
    virtual void render();

    void setOwner(Entity* entity) { owner = entity; }
    Entity* getOwner() const { return owner; }

    const std::string& getName() const { return name; }
    bool isActive() const { return active; }
    void setActive(bool isActive) { active = isActive; }

protected:
    Entity* owner = nullptr;
    std::string name;
    bool active = true;
};

} // namespace engine3d
