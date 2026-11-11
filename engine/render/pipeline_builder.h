#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace engine3d::vk {

/**
 * @brief Moduł shadera wczytany z pliku .spv i zwalniany automatycznie (RAII).
 *
 * Moduł jest potrzebny tylko do utworzenia potoku - po build() można go zniszczyć, więc zwykle żyje
 * jako zmienna lokalna funkcji tworzącej potok. Wyjątek w trakcie tworzenia nie zostawia wycieku.
 */
class ShaderModule {
public:
    ShaderModule(VkDevice device, const std::string& spvPath);
    ~ShaderModule();
    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    operator VkShaderModule() const { return module_; }
    // Kod SPIR-V (np. dla filterVertexAttributes - które wejścia shader naprawdę czyta).
    [[nodiscard]] const std::vector<char>& code() const { return code_; }

private:
    VkDevice device_;
    std::vector<char> code_;
    VkShaderModule module_ = VK_NULL_HANDLE;
};

/**
 * @brief Tryb mieszania kolorów jednego celu renderowania.
 *
 * - Opaque:        piksel zastępuje to, co było (bez mieszania).
 * - Alpha:         klasyczna przezroczystość: src * a + dst * (1 - a).
 * - Premultiplied: kolor źródła jest już przemnożony przez alfę: src + dst * (1 - a).
 * - Additive:      dodawanie światła (ogień, iskry): src * a + dst.
 */
enum class BlendMode { Opaque, Alpha, Premultiplied, Additive };

/**
 * @brief Budowniczy potoku graficznego (VkPipeline) dla dynamic rendering.
 *
 * Potok graficzny to "zamrożony" komplet ustawień rysowania: shadery, układ wierzchołków, topologia,
 * rasteryzacja, test głębokości, mieszanie kolorów i formaty celów. W czystym Vulkanie to ~120 linii
 * struktur na każdy potok - tu podajemy tylko to, co różni się od rozsądnych ustawień domyślnych:
 *
 *  - trójkąty (TRIANGLE_LIST), wypełnianie, cull BACK, przód = przeciwnie do wskazówek zegara (CCW),
 *  - test i zapis głębokości z porównaniem LESS (gdy podano format głębokości),
 *  - bez mieszania kolorów, bez multisamplingu,
 *  - viewport i scissor ustawiane przy rysowaniu (stan dynamiczny) - potok nie zależy od rozmiaru okna.
 *
 * @code
 *   VkPipeline p = vk::GraphicsPipelineBuilder()
 *       .shaders(vert, frag)                    // vertexMain / fragmentMain
 *       .vertexInput(binding, attributes)
 *       .cull(VK_CULL_MODE_NONE)
 *       .depth(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)
 *       .colorFormat(colorFormat, vk::BlendMode::Alpha)
 *       .depthFormat(depthFormat)
 *       .build(device, layout, "skybox");
 * @endcode
 */
class GraphicsPipelineBuilder {
public:
    GraphicsPipelineBuilder();

    // Etap shadera (moduł musi żyć do wywołania build()).
    GraphicsPipelineBuilder& shader(VkShaderStageFlagBits stage, VkShaderModule module, const char* entry);
    // Najczęstsza para: vertex + fragment z punktami wejścia vertexMain / fragmentMain.
    GraphicsPipelineBuilder& shaders(VkShaderModule vert, VkShaderModule frag);

    // Opis bufora wierzchołków (kopiowany). Kolejne wywołanie dodaje następny bufor (np. binding 1 z wagami kości).
    // Brak wywołania = bez buforów wierzchołków (np. pełnoekranowy trójkąt generowany w shaderze).
    GraphicsPipelineBuilder& vertexInput(const VkVertexInputBindingDescription& binding,
                                         const VkVertexInputAttributeDescription* attributes, uint32_t count);
    // Wersja dla std::vector / std::array opisów atrybutów.
    template <typename Container>
    GraphicsPipelineBuilder& vertexInput(const VkVertexInputBindingDescription& binding, const Container& attributes) {
        return vertexInput(binding, attributes.data(), static_cast<uint32_t>(attributes.size()));
    }
    GraphicsPipelineBuilder& topology(VkPrimitiveTopology topology);
    // Teselacja: topologia PATCH_LIST z podaną liczbą punktów kontrolnych na łatę.
    GraphicsPipelineBuilder& patches(uint32_t controlPoints);

    GraphicsPipelineBuilder& cull(VkCullModeFlags mode, VkFrontFace front = VK_FRONT_FACE_COUNTER_CLOCKWISE);
    GraphicsPipelineBuilder& polygonMode(VkPolygonMode mode);
    // Przesunięcie głębokości (mapy cieni - przeciw "shadow acne"). Wartości można zmieniać przy rysowaniu,
    // gdy dodamy VK_DYNAMIC_STATE_DEPTH_BIAS.
    GraphicsPipelineBuilder& depthBias(float constantFactor, float slopeFactor);
    GraphicsPipelineBuilder& depthClamp(bool enable);

    GraphicsPipelineBuilder& depth(bool test, bool write, VkCompareOp compare = VK_COMPARE_OP_LESS);

    // Kolejny cel koloru (kolejność = kolejność w vkCmdBeginRendering i w shaderze SV_TargetN).
    GraphicsPipelineBuilder& colorFormat(VkFormat format, BlendMode blend = BlendMode::Opaque);
    // Zmienia mieszanie ostatnio dodanego celu na dowolne (gdy żaden z trybów BlendMode nie pasuje).
    GraphicsPipelineBuilder& customBlend(const VkPipelineColorBlendAttachmentState& state);
    GraphicsPipelineBuilder& depthFormat(VkFormat format);

    // Dodatkowy stan dynamiczny (viewport i scissor są zawsze).
    GraphicsPipelineBuilder& dynamic(VkDynamicState state);

    /**
     * @brief Tworzy potok. Rzuca std::runtime_error z nazwą potoku, gdy Vulkan odmówi.
     * Builder można użyć ponownie (np. ten sam potok z depthClamp i bez).
     */
    [[nodiscard]] VkPipeline build(VkDevice device, VkPipelineLayout layout, const std::string& name) const;

private:
    std::vector<VkPipelineShaderStageCreateInfo> stages_;
    std::vector<VkVertexInputBindingDescription> bindings_;
    std::vector<VkVertexInputAttributeDescription> attributes_;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly_{};
    VkPipelineTessellationStateCreateInfo tessellation_{};
    VkPipelineRasterizationStateCreateInfo rasterizer_{};
    VkPipelineDepthStencilStateCreateInfo depthStencil_{};
    std::vector<VkFormat> colorFormats_;
    std::vector<VkPipelineColorBlendAttachmentState> blends_;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    std::vector<VkDynamicState> dynamicStates_;
};

/**
 * @brief Układ potoku (pipeline layout): jakie sety deskryptorów i jaki zakres push constants widzi shader.
 * pushSize == 0 - bez push constants.
 */
[[nodiscard]] VkPipelineLayout createPipelineLayout(VkDevice device, const std::vector<VkDescriptorSetLayout>& setLayouts,
                                                    uint32_t pushSize, VkShaderStageFlags pushStages,
                                                    const std::string& name);

} // namespace engine3d::vk
