#pragma once

#include <memory>

#include "engine/Types.hpp"

namespace engine3d {

class MaterialManager;
class VulkanRendererImpl;

// Cienki wrapper nad prywatna implementacja renderera Vulkan.
class VulkanRenderer {
public:
    // Tworzy renderer dla podanej konfiguracji sceny i opcjonalnego menedzera materialow.
    explicit VulkanRenderer(AppConfig config, MaterialManager* materialManager = nullptr);

    // Destruktor zwalnia implementacje, a ta po kolei niszczy zasoby GPU i okna.
    ~VulkanRenderer();

    // Przenoszenie jest bezpieczne, bo caly stan siedzi wewnatrz unique_ptr.
    VulkanRenderer(VulkanRenderer&&) noexcept;
    VulkanRenderer& operator=(VulkanRenderer&&) noexcept;

    // Kopiowanie blokujemy, bo renderer trzyma uchwyty systemowe i Vulkanowe.
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    // Startuje cala petle renderowania.
    void run();

private:
    // Prywatna implementacja ukrywa ogrom szczegolow przed publicznym API.
    std::unique_ptr<VulkanRendererImpl> impl_;
};

}  // namespace engine3d