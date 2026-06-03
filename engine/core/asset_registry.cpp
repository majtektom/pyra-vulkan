#include "core/asset_registry.h"

namespace engine3d {

template <typename Handle>
Handle AssetRegistry::registerAsset(const std::string& name,
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

MaterialHandle AssetRegistry::registerMaterial(const std::string& name) {
    return registerAsset(name, materialNameToHandle_, materialHandleToName_, nextMaterialId_);
}

TextureHandle AssetRegistry::registerTexture(const std::string& name) {
    return registerAsset(name, textureNameToHandle_, textureHandleToName_, nextTextureId_);
}

MeshHandle AssetRegistry::registerMesh(const std::string& name) {
    return registerAsset(name, meshNameToHandle_, meshHandleToName_, nextMeshId_);
}

std::optional<std::string> AssetRegistry::materialName(MaterialHandle handle) const {
    return getNameByHandle(handle, materialHandleToName_);
}

std::optional<std::string> AssetRegistry::textureName(TextureHandle handle) const {
    return getNameByHandle(handle, textureHandleToName_);
}

std::optional<std::string> AssetRegistry::meshName(MeshHandle handle) const {
    return getNameByHandle(handle, meshHandleToName_);
}

std::optional<MaterialHandle> AssetRegistry::materialHandle(const std::string& name) const {
    return getHandleByName(name, materialNameToHandle_);
}

std::optional<TextureHandle> AssetRegistry::textureHandle(const std::string& name) const {
    return getHandleByName(name, textureNameToHandle_);
}

std::optional<MeshHandle> AssetRegistry::meshHandle(const std::string& name) const {
    return getHandleByName(name, meshNameToHandle_);
}

} // namespace engine3d
