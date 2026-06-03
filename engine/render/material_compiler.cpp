#include "render/material_compiler.h"

#include "render/vulkan_util.h"

#include <array>
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
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 1: Tablica samplerów tekstur (tzw. "Bindless Texturing").
    // Alokujemy dużą tablicę deskryptorów (1024), co pozwala na indeksowanie tekstur bezpośrednio 
    // z poziomu kodu shadera za pomocą identyfikatorów przekazywanych np. w Push Constants.
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1024;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

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
    VkDescriptorBindingFlags bindingFlags[3] = {0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, 0};
    
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
        throw std::runtime_error("failed to create descriptor set layout");
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

VulkanPipeline MaterialCompiler::getOrCreatePipeline(const Material& materialData) {
    // Generowanie unikalnego klucza na podstawie stanu materiału
    PipelineKey key{materialData.shaderProgram, materialData.blendMode, materialData.depthTest, materialData.depthWrite, materialData.cullMode};
    
    // Zwrócenie potoku, jeśli jest już w pamięci podręcznej
    if (pipelineCache_.count(key)) return pipelineCache_[key];

    VkDevice device = context_->device();
    std::string shaderName = materialData.shaderProgram.empty() ? "default" : materialData.shaderProgram;
    std::string shaderDir = "bin/assets/shaders/" + shaderName + "/";
    
    // Odczyt binarnych plików SPIR-V shaderów skompilowanych przez offline-owy kompilator
    auto vertCode = vk::readFile(shaderDir + shaderName + ".vert.spv");
    auto fragCode = vk::readFile(shaderDir + shaderName + ".frag.spv");
    
    // Tworzenie modułów shaderów (Shader Modules) w Vulkanie
    VkShaderModule vertModule = vk::createShaderModule(device, vertCode);
    VkShaderModule fragModule = vk::createShaderModule(device, fragCode);

    // Definiowanie etapów shadera (Shader Stages) w potoku graficznym
    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].pName = materialData.vertexEntryPoint.c_str(); // Zwykle "main"
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = materialData.fragmentEntryPoint.c_str();

    // Pobranie układu formatu wierzchołków statycznych (pozycja, normalna, UV, kolor wierzchołka)
    auto bindingDesc = Vertex::bindingDescription();
    auto attrDescs = Vertex::attributeDescriptions();

    // Rozróżnienie konfiguracji danych wejściowych w zależności od typu shadera
    bool isParticle = (materialData.shaderProgram == "particle");
    bool isSprite = (materialData.shaderProgram == "sprite");

    VkVertexInputBindingDescription binding2{};
    std::vector<VkVertexInputAttributeDescription> binding2Attrs;

    if (isSprite) {
        // Dla sprite'ów stosujemy Instancing (rysowanie wielu kopii jednym wywołaniem draw).
        // Bufor 1 dostarcza unikalne dane dla każdej instancji (pozycję, rozmiar, kolor i klatkę UV).
        binding2.binding = 1;
        binding2.stride = sizeof(SpriteInstanceData);
        binding2.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE; // Krok o 1 element na instancję, a nie na wierzchołek

        // Atrybut instancji: Pozycja, rotacja i skala (X, Y, Z, Scale)
        VkVertexInputAttributeDescription attrPosSize{};
        attrPosSize.binding = 1;
        attrPosSize.location = 4;
        attrPosSize.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrPosSize.offset = offsetof(SpriteInstanceData, posX);
        binding2Attrs.push_back(attrPosSize);

        // Atrybut instancji: Kolor modulacji (R, G, B, A)
        VkVertexInputAttributeDescription attrColor{};
        attrColor.binding = 1;
        attrColor.location = 5;
        attrColor.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrColor.offset = offsetof(SpriteInstanceData, r);
        binding2Attrs.push_back(attrColor);

        // Atrybut instancji: Przesunięcie i wymiary kafelka UV w atlasie
        VkVertexInputAttributeDescription attrUV{};
        attrUV.binding = 1;
        attrUV.location = 6;
        attrUV.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrUV.offset = offsetof(SpriteInstanceData, uvOffsetU);
        binding2Attrs.push_back(attrUV);
    } else if (isParticle) {
        // Cząsteczki również rysowane są instancjonowanie.
        binding2.binding = 1;
        binding2.stride = sizeof(ParticleInstanceData);
        binding2.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        // Pozycja i rozmiar cząsteczki
        VkVertexInputAttributeDescription attrPosSize{};
        attrPosSize.binding = 1;
        attrPosSize.location = 4;
        attrPosSize.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrPosSize.offset = offsetof(ParticleInstanceData, posX);
        binding2Attrs.push_back(attrPosSize);

        // Kolor cząsteczki (z przejściem alfy)
        VkVertexInputAttributeDescription attrColor{};
        attrColor.binding = 1;
        attrColor.location = 5;
        attrColor.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrColor.offset = offsetof(ParticleInstanceData, r);
        binding2Attrs.push_back(attrColor);
    } else {
        // Domyślna ścieżka dla modeli 3D z wagami i indeksami kości do animacji szkieletowej.
        // Bufor 1 dostarcza dane kości na każdy wierzchołek.
        binding2.binding = 1;
        binding2.stride = sizeof(uint32_t) * 4 + sizeof(float) * 4;
        binding2.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        // Identyfikatory 4 najbardziej wpływających kości (jako wektor uint4)
        VkVertexInputAttributeDescription skinAttrs[2]{};
        skinAttrs[0].binding = 1;
        skinAttrs[0].location = 4;
        skinAttrs[0].format = VK_FORMAT_R32G32B32A32_UINT;
        skinAttrs[0].offset = 0;
        
        // Wagi wpływów poszczególnych kości (jako wektor float4 o sumie równej 1.0)
        skinAttrs[1].binding = 1;
        skinAttrs[1].location = 5;
        skinAttrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        skinAttrs[1].offset = sizeof(uint32_t) * 4;
        
        binding2Attrs.push_back(skinAttrs[0]);
        binding2Attrs.push_back(skinAttrs[1]);
    }

    // Łączenie opisów buforów wejściowych wierzchołków
    std::vector<VkVertexInputBindingDescription> bindings = {bindingDesc, binding2};
    std::vector<VkVertexInputAttributeDescription> allAttrs(attrDescs.begin(), attrDescs.end());
    allAttrs.insert(allAttrs.end(), binding2Attrs.begin(), binding2Attrs.end());

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(allAttrs.size());
    vertexInput.pVertexAttributeDescriptions = allAttrs.data();

    // Definiowanie topologii prymitywów (lista trójkątów)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // viewport i scissor są deklarowane jako dynamiczne, więc nie musimy ich tu konfigurować na sztywno
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Dynamiczny stan potoku pozwala na zmianę wymiarów rzutni i obszaru przycinania bez konieczności
    // kosztownej rekompilacji całego obiektu potoku graficznego przy zmianie rozmiaru okna.
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Konfiguracja rasteryzatora (sposób rysowania wielokątów, culling)
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // Standard w OpenGL/Vulkan w układzie prawoskrętnym
    
    // Ustawienie usuwania niewidocznych ścianek (Backface Culling) na podstawie właściwości materiału
    if (materialData.cullMode == CullMode::Disabled) {
        rasterizer.cullMode = VK_CULL_MODE_NONE;
    } else if (materialData.cullMode == CullMode::Front) {
        rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    } else if (materialData.cullMode == CullMode::FrontAndBack) {
        rasterizer.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
    } else {
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; // Domyślnie: odrzucanie tyłów trójkątów
    }

    // Wyłączenie Multisamplingu (MSAA) na poziomie potoku materiału (brak antyaliasingu MSAA)
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Test głębi (Depth Test) i zapis do bufora głębi (Depth Write)
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = materialData.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = materialData.depthWrite ? VK_TRUE : VK_FALSE;
    // W przypadku obiektów przezroczystych (blendMode != Disabled) używamy operacji LESS_OR_EQUAL
    // aby zapobiec artefaktom z-fightingu na stykających się krawędziach.
    depthStencil.depthCompareOp = (materialData.blendMode != BlendMode::Disabled) ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Konfiguracja mieszania kolorów (Color Blending) dla docelowego bufora ramki
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    if (materialData.blendMode == BlendMode::Additive) {
        // Mieszanie addytywne: kolor_wyjsciowy = kolor_zrodlowy * alfa_zrodlowa + kolor_tla
        // Przydatne przy cząsteczkach ognia, wybuchów itp.
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else if (materialData.blendMode == BlendMode::Alpha) {
        // Standardowe mieszanie alfa (interpolacja liniowa): kolor_wyjsciowy = kolor_zrodlowy * alfa_zrodlowa + kolor_tla * (1 - alfa_zrodlowa)
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else {
        // Mieszanie wyłączone - obiekty nieprzezroczyste (nadpisywanie koloru bufora)
        colorBlendAttachment.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Push Constants - mały, szybki blok pamięci (256 bajtów) przekazywany w linii komend renderowania.
    // Służy do przesyłania macierzy modelu, indeksów materiału itp.
    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = 256;
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Tworzenie Layoutu Potoku (Pipeline Layout) określającego układ wejść
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout setLayouts[2] = {descriptorSetLayout_, boneDescriptorSetLayout_};
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;

    VulkanPipeline pipeline;
    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipeline.layout);

    // Konfiguracja potoku graficznego
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipeline.layout;

    // Dynamic Rendering (Vulkan 1.3): Zamiast referencji do przestarzałego VkRenderPass, podajemy 
    // struktury opisujące formaty obrazów docelowych wprost podczas tworzenia potoku.
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat_;
    renderingInfo.depthAttachmentFormat = depthFormat_;
    pipelineInfo.pNext = &renderingInfo;

    // Rzeczywista kompilacja potoku graficznego przez sterownik Vulkan
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline.pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline");
    }
    
    // Moduły shaderów można bezpiecznie skasować po utworzeniu potoku (Vulkan kopiuje wewnętrznie kod SPIR-V)
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    // Zapisanie nowo utworzonego potoku do pamięci podręcznej i jego zwrócenie
    pipelineCache_[key] = pipeline;
    return pipeline;
}

void MaterialCompiler::invalidatePipelinesForShader(const std::string& shaderName) {
    auto it = pipelineCache_.begin();
    while (it != pipelineCache_.end()) {
        // Jeśli nazwa shadera zgadza się ze zmienioną, usuwamy go z cache, zmuszając do rekompilacji przy następnym rysowaniu
        if (it->first.shaderName == shaderName || (shaderName.empty() && it->first.shaderName.empty())) {
            it = pipelineCache_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace engine3d
