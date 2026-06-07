#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "render/backend/vulkan_context.h"
#include "render/render_types.h"

namespace engine3d {

/**
 * @brief Kompilator materiałów (Material Compiler).
 * 
 * Odpowiada za tłumaczenie wysokopoziomowych stanów materiałów (klas Material)
 * na niskopoziomowe obiekty potoków graficznych Vulkan (VkPipeline). 
 * 
 * Klasa implementuje wzorzec pamięci podręcznej (Pipeline Cache), aby zapobiec 
 * nadmiarowemu i kosztownemu tworzeniu tych samych potoków (które w Vulkanie 
 * wymagają kompilacji kodu SPIR-V przez sterownik karty graficznej). Kluczem cache 
 * jest unikalny stan renderowania (np. użyty shader, tryb blendingu, test głębi, culling).
 */
class MaterialCompiler {
public:
    /**
     * @brief Inicjalizuje kompilator materiałów.
     * @param context Wskaźnik do kontekstu Vulkana (zarządzanie urządzeniem).
     * @param colorFormat Format bufora koloru (niezbędny przy tworzeniu potoku bez RenderPass w Vulkan 1.3/Dynamic Rendering).
     * @param depthFormat Format bufora głębi.
     */
    MaterialCompiler(std::shared_ptr<VulkanContext> context, VkFormat colorFormat, VkFormat depthFormat);
    
    /**
     * @brief Niszczy kompilator i zwalnia wszystkie skompilowane potoki oraz układy deskryptorów.
     */
    ~MaterialCompiler();

    /**
     * @brief Aktualizuje formaty buforów docelowych (używane np. przy zmianie rozmiaru okna lub renderowania do tekstury).
     */
    void setFormats(VkFormat colorFormat, VkFormat depthFormat) {
        colorFormat_ = colorFormat;
        depthFormat_ = depthFormat;
    }

    /**
     * @brief Zwraca potok (VkPipeline i VkPipelineLayout) dla zadanego materiału.
     * 
     * Jeśli potok o danej konfiguracji stanów (blend mode, test głębi itp.) 
     * już istnieje w pamięci podręcznej, zostanie natychmiast zwrócony. 
     * W przeciwnym razie następuje wczytanie shaderów z dysku i kompilacja nowego potoku.
     */
    VulkanPipeline getOrCreatePipeline(const Material& materialData);

    /**
     * @brief Usuwa z pamięci podręcznej potoki korzystające ze wskazanego shadera.
     * 
     * Wywoływane głównie przez system automatycznego przeładowywania shaderów (Hot Reloading).
     * Wymusza ponowną kompilację potoku przy kolejnym zapytaniu.
     */
    void invalidatePipelinesForShader(const std::string& shaderName);
    void setWireframe(bool enabled);
    bool isWireframe() const { return wireframe_; }
    void invalidateAllPipelines();
    
    void setTessellationEnabled(bool enabled) {
        if (tessellationEnabled_ != enabled) {
            tessellationEnabled_ = enabled;
            invalidateAllPipelines();
        }
    }
    bool isTessellationEnabled() const { return tessellationEnabled_; }

private:
    /**
     * @brief Klucz jednoznacznie identyfikujący konfigurację potoku w cache.
     */
    struct PipelineKey {
        std::string shaderName;
        BlendMode blendMode;
        bool depthTest;
        bool depthWrite;
        CullMode cullMode;
        
        bool operator==(const PipelineKey& other) const {
            return shaderName == other.shaderName &&
                   blendMode == other.blendMode &&
                   depthTest == other.depthTest &&
                   depthWrite == other.depthWrite &&
                   cullMode == other.cullMode;
        }
    };

    /**
     * @brief Haszer dla klucza potoku, umożliwiający stosowanie std::unordered_map.
     */
    struct PipelineKeyHasher {
        std::size_t operator()(const PipelineKey& k) const {
            return std::hash<std::string>{}(k.shaderName) ^
                   (static_cast<std::size_t>(k.blendMode) << 16) ^
                   (k.depthTest ? 1u : 0u) ^
                   (k.depthWrite ? 2u : 0u) ^
                   (static_cast<std::size_t>(k.cullMode) << 8);
        }
    };

    /**
     * @brief Tworzy układ zestawu deskryptorów dla standardowych danych (kamery, oświetlenia i tekstur).
     */
    void createDescriptorSetLayout();

    /**
     * @brief Tworzy układ zestawu deskryptorów dla macierzy kości (animacja szkieletowa).
     */
    VkDescriptorSetLayout createBoneDescriptorSetLayout();

    std::shared_ptr<VulkanContext> context_; // Kontekst renderowania
    VkFormat colorFormat_;                  // Format renderowania koloru
    VkFormat depthFormat_;                  // Format renderowania głębi

    // Cache przechowujący powiązanie klucza stanu potoku z gotowym obiektem VulkanPipeline
    std::unordered_map<PipelineKey, VulkanPipeline, PipelineKeyHasher> pipelineCache_;

    // Układy deskryptorów (Descriptor Set Layouts) określające zasoby przekazywane do shaderów
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;     // Set 0: UBO + Bindless Samplers + Shadow Map
    VkDescriptorSetLayout boneDescriptorSetLayout_ = VK_NULL_HANDLE; // Set 1: Bone Matrices UBO (opcjonalny)
    bool wireframe_ = false;
    bool tessellationEnabled_ = true;
};

} // namespace engine3d
