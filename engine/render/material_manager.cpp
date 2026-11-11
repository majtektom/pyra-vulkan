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
        MaterialHandle handle = assetRegistry_->registerMaterial(material.name);
        if (handle.isValid()) {
            if (handle.value >= handleToMaterial_.size()) {
                handleToMaterial_.resize(handle.value + 1);
            }
            handleToMaterial_[handle.value] = material;
        }
        return handle;
    }
    
    LOG_WARN("[MaterialManager] brak AssetRegistry - rejestracja materiału może być niepełna: {}", material.name);
    return {};
}

const Material* MaterialManager::get(const std::string& name) const {
    const auto it = materials_.find(name);
    if (it == materials_.end()) {
        LOG_WARN("[MaterialManager] nie znaleziono materiału o nazwie: {}", name);
        return nullptr;
    }
    return &it->second;
}

const Material* MaterialManager::get(MaterialHandle handle) const {
    if (!handle.isValid()) {
        return nullptr;
    }
    if (handle.value < handleToMaterial_.size()) {
        const auto& mat = handleToMaterial_[handle.value];
        if (!mat.name.empty()) {
            return &mat;
        }
    }

    if (!assetRegistry_) {
        LOG_ERROR("[MaterialManager] brak AssetRegistry przy pobieraniu materiału po uchwycie");
        return nullptr;
    }

    // Tłumaczymy uchwyt MaterialHandle z powrotem na nazwę materiału za pomocą rejestru zasobów.
    const auto name = assetRegistry_->materialName(handle);
    if (!name.has_value()) {
        LOG_WARN("[MaterialManager] brak nazwy materiału w rejestrze dla uchwytu: {}", handle.value);
        return nullptr;
    }
    
    // Wyszukujemy parametry materiału po odczytanej nazwie.
    return get(*name);
}

const Material& MaterialManager::require(const std::string& name) const {
    const auto it = materials_.find(name);
    if (it == materials_.end()) {
        throw std::runtime_error("nie znaleziono materiału: " + name);
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
