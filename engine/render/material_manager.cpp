#include "material_manager.h"

#include <stdexcept>

namespace engine3d {

void MaterialManager::add(const Material& material) {
    materials_[material.name] = material;
}

const Material* MaterialManager::get(const std::string& name) const {
    const auto it = materials_.find(name);
    return it != materials_.end() ? &it->second : nullptr;
}

const Material& MaterialManager::require(const std::string& name) const {
    const auto it = materials_.find(name);
    if (it == materials_.end()) {
        throw std::runtime_error("material not found: " + name);
    }
    return it->second;
}

bool MaterialManager::has(const std::string& name) const {
    return materials_.find(name) != materials_.end();
}

}  // namespace engine3d
