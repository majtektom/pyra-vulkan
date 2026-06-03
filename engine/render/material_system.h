#pragma once

#include "render/backend/vulkan_context.h"
#include "render/render_types.h"
#include "core/resource_manager.h"

namespace engine3d {

class MaterialCompiler;

/**
 * @brief System kompilacji i cache'owania materiałów na GPU (Material System).
 *
 * MaterialSystem zarządza cyklem życia i procesem tworzenia instancji materiałów (MaterialInstance).
 * Aby obiekt mógł zostać narysowany, jego właściwości (Material) muszą zostać przetłumaczone 
 * na konkretny potok graficzny Vulkan (VulkanPipeline), który zawiera shadery i stan GPU (blending, test głębokości).
 *
 * Główne zadania:
 * - Kompilacja potoków graficznych przy użyciu MaterialCompiler.
 * - Cache'owanie skompilowanych materiałów (materialCache_), aby uniknąć ponownej, kosztownej kompilacji potoków.
 * - Obsługa przeładowywania shaderów na gorąco (Hot Reload) poprzez unieważnianie cache'u (invalidatePipelinesForShader).
 */
class MaterialSystem {
public:
    MaterialSystem(std::shared_ptr<VulkanContext> context, VkFormat colorFormat, VkFormat depthFormat);
    ~MaterialSystem();

    /**
     * @brief Zwraca istniejącą lub tworzy nową instancję materiału powiązaną z potokiem GPU.
     */
    std::shared_ptr<MaterialInstance> getOrCreateMaterialInstance(const Material& materialData);

    /**
     * @brief Aktualizuje formaty buforów ramki (Color/Depth Target formats) po zmianie rozdzielczości okna.
     */
    void setFormats(VkFormat colorFormat, VkFormat depthFormat);
    
    /**
     * @brief Unieważnia cache dla danego shadera (wymagane przy hot-reloadingu kodu shaderów w locie).
     */
    void invalidatePipelinesForShader(const std::string& shaderName);

private:
    std::unique_ptr<MaterialCompiler> compiler_;             // Kompilator potoków graficznych.
    ResourceManager<MaterialInstance> materialCache_;        // Cache przechowujący skompilowane instancje.
};

} // namespace engine3d
