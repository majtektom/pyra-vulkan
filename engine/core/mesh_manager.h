#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <optional>
#include <vector>
#include "core/asset_registry.h"
#include "render/backend/vulkan_context.h"
#include "render/backend/vulkan_resources.h"
#include "core/types.h"

namespace engine3d {

struct GpuMesh {
    std::unique_ptr<VulkanBuffer> vertexBuffer;
    std::unique_ptr<VulkanBuffer> indexBuffer;
    std::unique_ptr<VulkanBuffer> instanceBuffer;
    uint32_t indexCount;
    uint32_t instanceCount = 0;
    // Prostopadłościan otaczający wierzchołki w układzie lokalnym siatki (do dopasowania cieni).
    bool hasBounds = false;
    Vec3 boundsMin{}, boundsMax{};
};

class MeshManager {
public:
    // Ile klatek bufor musi przeleżeć po zastąpieniu, zanim można go ponownie użyć:
    // GPU może jeszcze czytać Renderer::kMaxFrames poprzednich klatek.
    static constexpr uint64_t kBufferRetireFrames = 4; // Renderer::kMaxFrames + 1

    MeshManager(std::shared_ptr<VulkanContext> context);

    // Wywoływane raz na klatkę, przed aktualizacjami siatek. Odlicza wiek zastąpionych buforów.
    void beginFrame();

    // Podpina rejestr typowanych uchwytow zasobow.
    void setAssetRegistry(AssetRegistry* registry) { assetRegistry_ = registry; }
    
    void loadMesh(const std::string& name, const MeshData& meshData);
    void updateMesh(const std::string& name, const MeshData& meshData);
    void loadInstanceBuffer(const std::string& name, VkDeviceSize size);
    void updateInstanceBuffer(const std::string& name, const void* data, VkDeviceSize size, uint32_t instanceCount);
    std::shared_ptr<GpuMesh> getMesh(const std::string& name);

    // Zwraca uchwyt mesha po nazwie, jesli mesh byl zarejestrowany.
    std::optional<MeshHandle> handle(const std::string& name) const;

private:
    // Bufor, z którego GPU może jeszcze czytać — wolny do ponownego użycia od releaseFrame.
    struct RetiredBuffer {
        uint64_t releaseFrame;
        std::unique_ptr<VulkanBuffer> buffer;
    };

    // Zwraca bufor do zapisu: zwolniony bufor o tym samym rozmiarze i usage albo nowy.
    std::unique_ptr<VulkanBuffer> acquireBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
    // Odkłada bufor do kolejki — nie wolno go niszczyć ani nadpisywać, dopóki GPU może go czytać.
    void retire(std::unique_ptr<VulkanBuffer> buffer);

    std::shared_ptr<VulkanContext> context_;
    std::vector<RetiredBuffer> retired_;
    uint64_t frameIndex_ = 0;
    std::unordered_map<std::string, std::shared_ptr<GpuMesh>> meshes_;
    AssetRegistry* assetRegistry_ = nullptr;
};

} // namespace engine3d
