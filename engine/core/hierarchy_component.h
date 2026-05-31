#pragma once

#include <vector>
#include "core/component.h"

namespace engine3d {

class Entity;

struct HierarchyComponent : public Component {
    HierarchyComponent() : Component("Hierarchy") {}

    Entity* parent = nullptr;
    std::vector<Entity*> children;
};

} // namespace engine3d
