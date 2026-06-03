#include "render/material_manager.h"
#include "core/logger.h"

#include <stdexcept>

namespace engine3d {

MaterialHandle MaterialManager::add(const Material& material) {
    LOG_TRACE("[MaterialManager] Registering material: {}", material.name);
    
    // Zapisujemy parametry materiału pod jego nazwą.
    materials_[material.name] = material;
    
    // Rejestrujemy materiał w rejestrze zasobów i zwracamy unikalny uchwyt.
    if (assetRegistry_) {
        return assetRegistry_->registerMaterial(material.name);
    }
    
    LOG_WARN("[MaterialManager] AssetRegistry not set, material registration may be incomplete: {}", material.name);
    return {};
}

const Material* MaterialManager::get(const std::string& name) const {
    const auto it = materials_.find(name);
    if (it == materials_.end()) {
        LOG_WARN("[MaterialManager] Material not found by name: {}", name);
        return nullptr;
    }
    return &it->second;
}

const Material* MaterialManager::get(MaterialHandle handle) const {
    if (!assetRegistry_) {
        LOG_ERROR("[MaterialManager] AssetRegistry not set while getting material by handle");
        return nullptr;
    }
    if (!handle.isValid()) {
        LOG_WARN("[MaterialManager] Invalid MaterialHandle used");
        return nullptr;
    }

    // Tłumaczymy uchwyt MaterialHandle z powrotem na nazwę materiału za pomocą rejestru zasobów.
    const auto name = assetRegistry_->materialName(handle);
    if (!name.has_value()) {
        LOG_WARN("[MaterialManager] Material name not found in registry for handle: {}", handle.value);
        return nullptr;
    }
    
    // Wyszukujemy parametry materiału po odczytanej nazwie.
    return get(*name);
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

std::optional<MaterialHandle> MaterialManager::handle(const std::string& name) const {
    if (!assetRegistry_) {
        return std::nullopt;
    }
    return assetRegistry_->materialHandle(name);
}

}  // namespace engine3d
