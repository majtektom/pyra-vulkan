#include "core/asset_registry.h"
#include <mutex>
#include <shared_mutex>

namespace engine3d {

// -----------------------------------------------------------------------
// Wewnętrzna metoda rejestracji (wywołuj tylko gdy trzymasz unique_lock).
// Fast path: jeśli zasób już istnieje, zwraca jego uchwyt bez alokacji.
// -----------------------------------------------------------------------
template <typename Handle>
Handle AssetRegistry::registerAssetLocked(const std::string& name,
                                    std::unordered_map<std::string, Handle>& nameToHandle,
                                    std::unordered_map<uint32_t, std::string>& handleToName,
                                    uint32_t& nextId) {
    if (name.empty()) {
        return {};
    }

    auto found = nameToHandle.find(name);
    if (found != nameToHandle.end()) {
        return found->second;
    }

    Handle handle{nextId++};
    nameToHandle[name] = handle;
    handleToName[handle.value] = name;
    return handle;
}

template <typename Handle>
std::optional<std::string> AssetRegistry::getNameByHandle(
    Handle handle,
    const std::unordered_map<uint32_t, std::string>& handleToName) const {
    if (!handle.isValid()) {
        return std::nullopt;
    }

    auto it = handleToName.find(handle.value);
    if (it == handleToName.end()) {
        return std::nullopt;
    }
    return it->second;
}

template <typename Handle>
std::optional<Handle> AssetRegistry::getHandleByName(
    const std::string& name,
    const std::unordered_map<std::string, Handle>& nameToHandle) const {
    auto it = nameToHandle.find(name);
    if (it == nameToHandle.end()) {
        return std::nullopt;
    }
    return it->second;
}

// -----------------------------------------------------------------------
// Publiczne API - w pełni thread-safe dzięki shared_mutex.
//
// registerXxx:  unique_lock  (wyłączny zapis – tylko jeden wątek naraz)
//   - Najpierw sprawdza fast path ze zwykłym find() pod write-lock,
//     bo po shared read-lock i tak potrzebowałby upgrade do write-lock.
//
// xxxHandle / xxxName: shared_lock (wielu czytelników jednocześnie)
// -----------------------------------------------------------------------

MaterialHandle AssetRegistry::registerMaterial(const std::string& name) {
    std::unique_lock lock(mutex_);
    return registerAssetLocked(name, materialNameToHandle_, materialHandleToName_, nextMaterialId_);
}

TextureHandle AssetRegistry::registerTexture(const std::string& name) {
    // Szybka ścieżka odczytu: jeśli tekstura istnieje, wystarczy shared_lock.
    {
        std::shared_lock rlock(mutex_);
        auto found = textureNameToHandle_.find(name);
        if (found != textureNameToHandle_.end()) {
            return found->second;
        }
    }
    // Tekstury nie ma – potrzebny write lock.
    std::unique_lock wlock(mutex_);
    // Sprawdzamy ponownie (inny wątek mógł ją wstawić między rlock a wlock).
    return registerAssetLocked(name, textureNameToHandle_, textureHandleToName_, nextTextureId_);
}

MeshHandle AssetRegistry::registerMesh(const std::string& name) {
    std::unique_lock lock(mutex_);
    return registerAssetLocked(name, meshNameToHandle_, meshHandleToName_, nextMeshId_);
}

std::optional<std::string> AssetRegistry::materialName(MaterialHandle handle) const {
    std::shared_lock lock(mutex_);
    return getNameByHandle(handle, materialHandleToName_);
}

std::optional<std::string> AssetRegistry::textureName(TextureHandle handle) const {
    std::shared_lock lock(mutex_);
    return getNameByHandle(handle, textureHandleToName_);
}

std::optional<std::string> AssetRegistry::meshName(MeshHandle handle) const {
    std::shared_lock lock(mutex_);
    return getNameByHandle(handle, meshHandleToName_);
}

std::optional<MaterialHandle> AssetRegistry::materialHandle(const std::string& name) const {
    std::shared_lock lock(mutex_);
    return getHandleByName(name, materialNameToHandle_);
}

std::optional<TextureHandle> AssetRegistry::textureHandle(const std::string& name) const {
    std::shared_lock lock(mutex_);
    return getHandleByName(name, textureNameToHandle_);
}

std::optional<MeshHandle> AssetRegistry::meshHandle(const std::string& name) const {
    std::shared_lock lock(mutex_);
    return getHandleByName(name, meshNameToHandle_);
}

} // namespace engine3d
