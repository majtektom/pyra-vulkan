#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace engine3d {

// Typowany uchwyt zasobu: zapobiega mieszaniu id roznych kategorii.
template <typename Tag>
struct AssetHandle {
    uint32_t value = 0;

    bool isValid() const { return value != 0; }
    bool operator==(const AssetHandle& other) const { return value == other.value; }
    bool operator!=(const AssetHandle& other) const { return value != other.value; }
};

struct MaterialTag {};
struct TextureTag {};
struct MeshTag {};

using MaterialHandle = AssetHandle<MaterialTag>;
using TextureHandle = AssetHandle<TextureTag>;
using MeshHandle = AssetHandle<MeshTag>;

// Rejestr mapujacy nazwy zasobow na typowane uchwyty i odwrotnie.
class AssetRegistry {
public:
    MaterialHandle registerMaterial(const std::string& name);
    TextureHandle registerTexture(const std::string& name);
    MeshHandle registerMesh(const std::string& name);

    std::optional<std::string> materialName(MaterialHandle handle) const;
    std::optional<std::string> textureName(TextureHandle handle) const;
    std::optional<std::string> meshName(MeshHandle handle) const;

    std::optional<MaterialHandle> materialHandle(const std::string& name) const;
    std::optional<TextureHandle> textureHandle(const std::string& name) const;
    std::optional<MeshHandle> meshHandle(const std::string& name) const;

private:
    template <typename Handle>
    Handle registerAsset(const std::string& name,
                         std::unordered_map<std::string, Handle>& nameToHandle,
                         std::unordered_map<uint32_t, std::string>& handleToName,
                         uint32_t& nextId);

    template <typename Handle>
    std::optional<std::string> getNameByHandle(Handle handle,
                                               const std::unordered_map<uint32_t, std::string>& handleToName) const;

    template <typename Handle>
    std::optional<Handle> getHandleByName(const std::string& name,
                                          const std::unordered_map<std::string, Handle>& nameToHandle) const;

    uint32_t nextMaterialId_ = 1;
    uint32_t nextTextureId_ = 1;
    uint32_t nextMeshId_ = 1;

    std::unordered_map<std::string, MaterialHandle> materialNameToHandle_;
    std::unordered_map<uint32_t, std::string> materialHandleToName_;

    std::unordered_map<std::string, TextureHandle> textureNameToHandle_;
    std::unordered_map<uint32_t, std::string> textureHandleToName_;

    std::unordered_map<std::string, MeshHandle> meshNameToHandle_;
    std::unordered_map<uint32_t, std::string> meshHandleToName_;
};

} // namespace engine3d
