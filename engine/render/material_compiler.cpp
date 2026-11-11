#include "render/material_compiler.h"

#include "render/pipeline_builder.h"
#include "render/vulkan_util.h"

#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <vector>

namespace engine3d {

MaterialCompiler::MaterialCompiler(std::shared_ptr<VulkanContext> context, VkFormat colorFormat, VkFormat depthFormat)
    : context_(std::move(context)), colorFormat_(colorFormat), depthFormat_(depthFormat) {
    // Przygotowanie układów zestawów deskryptorów, które definiują interfejs danych wejściowych shaderów
    createDescriptorSetLayout();
    boneDescriptorSetLayout_ = createBoneDescriptorSetLayout();
}

MaterialCompiler::~MaterialCompiler() {
    VkDevice device = context_->device();
    // Zwolnienie wszystkich skompilowanych potoków z pamięci podręcznej
    for (auto& pair : pipelineCache_) {
        vkDestroyPipeline(device, pair.second.pipeline, nullptr);
        vkDestroyPipelineLayout(device, pair.second.layout, nullptr);
    }
    // Zwolnienie układów deskryptorów
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
    }
    if (boneDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, boneDescriptorSetLayout_, nullptr);
    }
}

void MaterialCompiler::createDescriptorSetLayout() {
    // Binding 0: Globalny bufor jednolity (UBO) zawierający macierze kamery, mgłę itp.
    // Dostępny zarówno w shaderze wierzchołków, jak i pikseli.
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                  VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;

    // Binding 1: Tablica samplerów tekstur (tzw. "Bindless Texturing").
    // Alokujemy dużą tablicę deskryptorów (1024), co pozwala na indeksowanie tekstur bezpośrednio 
    // z poziomu kodu shadera za pomocą identyfikatorów przekazywanych np. w Push Constants.
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1024;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;

    // Binding 2: Tekstura mapy cieni (Shadow Map) z samplerem porównującym.
    VkDescriptorSetLayoutBinding shadowLayoutBinding{};
    shadowLayoutBinding.binding = 2;
    shadowLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowLayoutBinding.descriptorCount = 1;
    shadowLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {uboLayoutBinding, samplerLayoutBinding, shadowLayoutBinding};

    // Konfiguracja flag dla Bindless Texturing:
    // - VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT: Pozwala, aby nie wszystkie 1024 sloty były zajęte (zbindowane).
    // - VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT: Pozwala na modyfikację deskryptorów w locie, po ich przypisaniu do Command Buffera.
    // - VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT: Pozwala dopisywać nowe tekstury, gdy klatki są w locie.
    // Flagi muszą być identyczne jak w layoucie set 0 renderera (renderer_core.cpp), inaczej layouty są niezgodne.
    VkDescriptorBindingFlags bindingFlags[3] = {0,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
        0};
    
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsInfo.bindingCount = 3;
    flagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &flagsInfo;
    // Flaga wymagana, aby móc aktualizować deskryptory po ich powiązaniu
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(context_->device(), &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("nie udało się utworzyć układu zestawu deskryptorów");
    }
}

VkDescriptorSetLayout MaterialCompiler::createBoneDescriptorSetLayout() {
    // Układ deskryptorów zestawu 1: przechowuje bufor z macierzami transformacji kości dla animacji szkieletowej.
    VkDescriptorSetLayoutBinding boneBinding{};
    boneBinding.binding = 0;
    boneBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    boneBinding.descriptorCount = 1;
    boneBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // Dane kości są potrzebne tylko w shaderze wierzchołków

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &boneBinding;

    VkDescriptorSetLayout boneLayout;
    vkCreateDescriptorSetLayout(context_->device(), &layoutInfo, nullptr, &boneLayout);
    return boneLayout;
}

const VulkanPipeline& MaterialCompiler::getOrCreatePipeline(const Material& materialData) {
    // Generowanie unikalnego klucza na podstawie stanu materiału
    PipelineKey key{materialData.shaderProgram, materialData.blendMode, materialData.depthTest, materialData.depthWrite,
                    materialData.cullMode, materialData.colorWrite};
    
    // Zwrócenie potoku, jeśli jest już w pamięci podręcznej
    if (pipelineCache_.count(key)) return pipelineCache_[key];

    VkDevice device = context_->device();
    const std::string shaderName = materialData.shaderProgram.empty() ? "default" : materialData.shaderProgram;
    const std::string shaderDir = "bin/assets/shaders/" + shaderName + "/";

    // Teselacja (teren): używana, gdy GPU ją obsługuje, jest włączona i shader ma etapy hull/domain.
    const std::string tescPath = shaderDir + shaderName + ".tesc.spv";
    const std::string tesePath = shaderDir + shaderName + ".tese.spv";
    const bool hasTessellation = tessellationEnabled_ && context_->isTessellationSupported() &&
                                 std::filesystem::exists(tescPath) && std::filesystem::exists(tesePath);
    // Teren bez teselacji ma osobny vertex shader (wysokość liczona na wierzchołkach siatki).
    const bool terrainNoTess = shaderName == "terrain" && !hasTessellation;
    const std::string vertShaderFile = terrainNoTess ? "terrain_no_tess" : shaderName;
    const std::string vertEntryPoint = terrainNoTess ? "vertexMainNoTess" : materialData.vertexEntryPoint;

    // Binarne pliki SPIR-V skompilowane przy budowaniu (glslangValidator); moduły żyją do końca funkcji.
    const vk::ShaderModule vert(device, shaderDir + vertShaderFile + ".vert.spv");
    const vk::ShaderModule frag(device, shaderDir + shaderName + ".frag.spv");
    std::optional<vk::ShaderModule> tesc, tese;

    vk::GraphicsPipelineBuilder builder;
    builder.shader(VK_SHADER_STAGE_VERTEX_BIT, vert, vertEntryPoint.c_str());
    if (hasTessellation) {
        tesc.emplace(device, tescPath);
        tese.emplace(device, tesePath);
        builder.shader(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, *tesc, "hullMain")
            .shader(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, *tese, "domainMain")
            .patches(3);
    }
    builder.shader(VK_SHADER_STAGE_FRAGMENT_BIT, frag, materialData.fragmentEntryPoint.c_str());

    // Bufor 0: zwykłe wierzchołki (pozycja, normalna, kolor, UV). Bufor 1 zależy od rodzaju shadera:
    // - sprite / cząsteczki: dane NA INSTANCJĘ (instancing - wiele kopii jednym poleceniem rysowania),
    // - pozostałe: dane skórowania na wierzchołek (kości animacji szkieletowej).
    // Wszędzie tylko atrybuty czytane przez vertex shader (reszta to ostrzeżenie wydajnościowe walidacji).
    builder.vertexInput(Vertex::bindingDescription(), vk::filterVertexAttributes(vert.code(), Vertex::attributeDescriptions()));
    if (materialData.shaderProgram == "sprite") {
        const VkVertexInputAttributeDescription attrs[] = {
            {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SpriteInstanceData, posX)},       // pozycja + skala
            {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SpriteInstanceData, r)},          // kolor modulacji
            {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SpriteInstanceData, uvOffsetU)},  // kafelek UV w atlasie
        };
        builder.vertexInput({1, sizeof(SpriteInstanceData), VK_VERTEX_INPUT_RATE_INSTANCE},
                            vk::filterVertexAttributes(vert.code(), attrs));
    } else if (materialData.shaderProgram == "particle") {
        const VkVertexInputAttributeDescription attrs[] = {
            {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ParticleInstanceData, posX)},     // pozycja + rozmiar
            {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ParticleInstanceData, r)},        // kolor (z alfą)
        };
        builder.vertexInput({1, sizeof(ParticleInstanceData), VK_VERTEX_INPUT_RATE_INSTANCE},
                            vk::filterVertexAttributes(vert.code(), attrs));
    } else {
        builder.vertexInput(SkinVertex::bindingDescription(),
                            vk::filterVertexAttributes(vert.code(), SkinVertex::attributeDescriptions()));
    }

    // Rasteryzacja: wypełnianie (albo siatka w trybie wireframe) i odrzucanie ścian wg materiału.
    VkCullModeFlags cull = VK_CULL_MODE_BACK_BIT;   // domyślnie: odrzucanie tyłów trójkątów
    if (materialData.cullMode == CullMode::Disabled) cull = VK_CULL_MODE_NONE;
    else if (materialData.cullMode == CullMode::Front) cull = VK_CULL_MODE_FRONT_BIT;
    else if (materialData.cullMode == CullMode::FrontAndBack) cull = VK_CULL_MODE_FRONT_AND_BACK;
    builder.polygonMode(wireframe_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL).cull(cull);

    // Głębokość. Materiały z mieszaniem trafiają do TransparentPass, gdzie bufor głębokości jest tylko
    // do odczytu (równocześnie próbkuje go ocean) - zapis jest tam niedozwolony. LESS_OR_EQUAL zapobiega
    // migotaniu (z-fighting) na stykających się krawędziach przezroczystych obiektów.
    const bool transparent = materialData.blendMode != BlendMode::Disabled;
    builder.depth(materialData.depthTest, materialData.depthWrite && !transparent,
                  transparent ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS)
        .depthFormat(depthFormat_);

    // Cele koloru. Shader piszący G-Buffer (np. teren): 4 tekstury bez mieszania; inaczej jeden kolor (ekran).
    if (writesGBuffer(shaderName) && !transparent) {
        for (int a = 0; a < kGBufferAttachmentCount; ++a) builder.colorFormat(gbufferAttachmentFormat(a));
    } else if (materialData.blendMode == BlendMode::Additive) {
        builder.colorFormat(colorFormat_, vk::BlendMode::Additive);        // ogień, wybuchy
    } else if (materialData.blendMode == BlendMode::Alpha) {
        builder.colorFormat(colorFormat_, vk::BlendMode::Alpha);
    } else if (!materialData.colorWrite) {
        // Potok "tylko głębokość" (wstępny zapis głębokości przezroczystych obiektów): mieszanie ZERO/ONE
        // zostawia kolor bufora bez zmian. Celowo nie colorWriteMask = 0 - sterownik NVIDIA 615 wysypuje się
        // przy kompilacji takiego potoku (SEGV w libnvidia-gpucomp).
        VkPipelineColorBlendAttachmentState keep{};
        keep.blendEnable = VK_TRUE;
        keep.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        keep.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        keep.colorBlendOp = VK_BLEND_OP_ADD;
        keep.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        keep.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        keep.alphaBlendOp = VK_BLEND_OP_ADD;
        keep.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT;
        builder.colorFormat(colorFormat_).customBlend(keep);
    } else {
        builder.colorFormat(colorFormat_);   // nieprzezroczyste: nadpisanie koloru
    }

    // Układ: set 0 = dane klatki, set 1 = macierze kości; push constants materiału (256 B, wszystkie etapy).
    VulkanPipeline pipeline;
    pipeline.layout = vk::createPipelineLayout(
        device, {descriptorSetLayout_, boneDescriptorSetLayout_}, 256,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
            VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        "material " + shaderName);
    pipeline.pipeline = builder.build(device, pipeline.layout, "material " + shaderName);

    // Zapisanie nowo utworzonego potoku do pamięci podręcznej i jego zwrócenie
    pipelineCache_[key] = pipeline;
    return pipelineCache_[key];
}

void MaterialCompiler::invalidatePipelinesForShader(const std::string& shaderName) {
    VkDevice device = context_->device();
    vkDeviceWaitIdle(device); // czekamy, aż GPU przestanie używać starych potoków
    auto it = pipelineCache_.begin();
    while (it != pipelineCache_.end()) {
        // Jeśli nazwa shadera zgadza się ze zmienioną, usuwamy go z cache, zmuszając do rekompilacji przy następnym rysowaniu
        if (it->first.shaderName == shaderName || (shaderName.empty() && it->first.shaderName.empty())) {
            vkDestroyPipeline(device, it->second.pipeline, nullptr);
            vkDestroyPipelineLayout(device, it->second.layout, nullptr);
            it = pipelineCache_.erase(it);
        } else {
            ++it;
        }
    }
}

void MaterialCompiler::setWireframe(bool enabled) {
    if (wireframe_ != enabled) {
        wireframe_ = enabled;
        invalidateAllPipelines();
    }
}

void MaterialCompiler::invalidateAllPipelines() {
    VkDevice device = context_->device();
    vkDeviceWaitIdle(device); // czekamy, aż GPU przestanie używać starych potoków
    for (auto& pair : pipelineCache_) {
        vkDestroyPipeline(device, pair.second.pipeline, nullptr);
        vkDestroyPipelineLayout(device, pair.second.layout, nullptr);
    }
    pipelineCache_.clear();
}

} // namespace engine3d
