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

const VulkanPipeline& MaterialCompiler::getOrCreatePipeline(const Material& materialData) {
    // Generowanie unikalnego klucza na podstawie stanu materiału
    PipelineKey key{materialData.shaderProgram, materialData.blendMode, materialData.depthTest, materialData.depthWrite, materialData.cullMode};
    
    // Zwrócenie potoku, jeśli jest już w pamięci podręcznej
    if (pipelineCache_.count(key)) return pipelineCache_[key];

    VkDevice device = context_->device();
    std::string shaderName = materialData.shaderProgram.empty() ? "default" : materialData.shaderProgram;
    std::string shaderDir = "bin/assets/shaders/" + shaderName + "/";
    
    std::string tescPath = shaderDir + shaderName + ".tesc.spv";
    std::string tesePath = shaderDir + shaderName + ".tese.spv";
    bool hasTessellation = false;
    std::vector<char> tescCode, teseCode;
    
    bool tryTessellation = tessellationEnabled_ && context_->isTessellationSupported();
    if (tryTessellation) {
        try {
            tescCode = vk::readFile(tescPath);
            teseCode = vk::readFile(tesePath);
            hasTessellation = true;
        } catch (...) {}
    }

    std::string vertShaderFile = shaderName;
    if (shaderName == "terrain" && !hasTessellation) {
        vertShaderFile = "terrain_no_tess";
    }
    
    // Odczyt binarnych plików SPIR-V shaderów skompilowanych przez offline-owy kompilator
    auto vertCode = vk::readFile(shaderDir + vertShaderFile + ".vert.spv");
    auto fragCode = vk::readFile(shaderDir + shaderName + ".frag.spv");
    
    // Tworzenie modułów shaderów (Shader Modules) w Vulkanie
    VkShaderModule vertModule = vk::createShaderModule(device, vertCode);
    VkShaderModule fragModule = vk::createShaderModule(device, fragCode);

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    
    // Przechowujemy punkt wejścia w zmiennej o czasie życia równym funkcji
    std::string vertEntryPoint = materialData.vertexEntryPoint;
    if (shaderName == "terrain" && !hasTessellation) {
        vertEntryPoint = "vertexMainNoTess";
    }
    
    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.pName = vertEntryPoint.c_str(); 
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    shaderStages.push_back(vertStage);

    VkShaderModule tescModule = VK_NULL_HANDLE;
    VkShaderModule teseModule = VK_NULL_HANDLE;
    if (hasTessellation) {
        tescModule = vk::createShaderModule(device, tescCode);
        VkPipelineShaderStageCreateInfo tescStage{};
        tescStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        tescStage.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        tescStage.module = tescModule;
        tescStage.pName = "hullMain";
        shaderStages.push_back(tescStage);

        teseModule = vk::createShaderModule(device, teseCode);
        VkPipelineShaderStageCreateInfo teseStage{};
        teseStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        teseStage.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        teseStage.module = teseModule;
        teseStage.pName = "domainMain";
        shaderStages.push_back(teseStage);
    }

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = materialData.fragmentEntryPoint.c_str();
    shaderStages.push_back(fragStage);

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
    // Tylko atrybuty czytane przez vertex shader (reszta to ostrzeżenie wydajnościowe walidacji).
    allAttrs = vk::filterVertexAttributes(vertCode, allAttrs);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(allAttrs.size());
    vertexInput.pVertexAttributeDescriptions = allAttrs.data();

    // Definiowanie topologii prymitywów
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    if (hasTessellation) {
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    } else {
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }

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
    rasterizer.polygonMode = wireframe_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
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
    // Materiały z blendingiem trafiają do TransparentPass, gdzie depth jest w layoucie READ_ONLY
    // (równocześnie próbkowany przez ocean) — zapis głębi jest tam niedozwolony.
    const bool transparent = materialData.blendMode != BlendMode::Disabled;
    depthStencil.depthWriteEnable = (materialData.depthWrite && !transparent) ? VK_TRUE : VK_FALSE;
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
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                              VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;

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
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    
    VkPipelineTessellationStateCreateInfo tessellationState{};
    tessellationState.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tessellationState.patchControlPoints = 3;
    if (hasTessellation) {
        pipelineInfo.pTessellationState = &tessellationState;
    }
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
    if (tescModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, tescModule, nullptr);
    if (teseModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, teseModule, nullptr);

    // Zapisanie nowo utworzonego potoku do pamięci podręcznej i jego zwrócenie
    pipelineCache_[key] = pipeline;
    return pipelineCache_[key];
}

void MaterialCompiler::invalidatePipelinesForShader(const std::string& shaderName) {
    VkDevice device = context_->device();
    vkDeviceWaitIdle(device); // Wait for GPU to finish using resources
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
    vkDeviceWaitIdle(device); // Wait for GPU to finish using resources
    for (auto& pair : pipelineCache_) {
        vkDestroyPipeline(device, pair.second.pipeline, nullptr);
        vkDestroyPipelineLayout(device, pair.second.layout, nullptr);
    }
    pipelineCache_.clear();
}

} // namespace engine3d
