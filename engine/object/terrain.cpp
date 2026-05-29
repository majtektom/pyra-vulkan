#include "terrain.h"
#include "math.h"
#include "mesh_component.h"
#include "engine.h"
#include "mesh_manager.h"
#include "renderer.h"
#include "vulkan_util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <stb_image.h>

namespace engine3d {

static float rangedRandom(float v1, float v2) {
    return v1 + (v2 - v1) * ((float)rand() / (float)RAND_MAX);
}

static std::vector<float> loadHeightmap(const std::string& path, int& w, int& h, int downsample) {
    int ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 1);
    if (!data) throw std::runtime_error("Cannot load heightmap: " + path);
    int dw = w / downsample, dh = h / downsample;
    std::vector<float> hm(dw * dh);
    for (int z = 0; z < dh; ++z)
        for (int x = 0; x < dw; ++x)
            hm[z * dw + x] = (float)data[(z * downsample) * w + (x * downsample)];
    stbi_image_free(data);
    w = dw; h = dh;
    return hm;
}

// --- Heightmap loading & XML ---

bool Terrain::loadFromImage(const std::string& path, float scaleXZ, float scaleY,
                             float texScale, int maxLodLevels) {
    int w, h;
    int downsample = 4;
    heightmap_ = loadHeightmap(path, w, h, downsample);
    hmSize_ = w;
    sizeX = w; sizeZ = h;
    scaleXZ_ = scaleXZ;
    scaleY_ = scaleY;
    texScale_ = texScale;
    maxLodLevels_ = maxLodLevels;

    float halfWorld = (w / 2.0f) * scaleXZ;
    root_ = new Node{};
    root_->cx = halfWorld;
    root_->cz = halfWorld;
    root_->sizeXZ = halfWorld;
    root_->minY = 0; root_->maxY = 256.0f * scaleY;
    root_->lodLevel = 0;

    gridRes_ = 16;
    buildTree(root_);
    buildNodeVertices(root_);
    findNeighbors(root_);
    nodeCount = 0;
    return true;
}

void Terrain::buildMesh(float scaleXZ, float scaleY, float texScale) {
    int w = hmSize_;
    scaleXZ_ = scaleXZ; scaleY_ = scaleY; texScale_ = texScale;
    sizeX = w; sizeZ = w;
    float halfWorld = (w / 2.0f) * scaleXZ;
    mesh.vertices.clear();
    mesh.indices.clear();
    if (!root_) {
        root_ = new Node{};
        root_->cx = halfWorld;
        root_->cz = halfWorld;
        root_->sizeXZ = halfWorld;
        root_->minY = 0; root_->maxY = 256.0f * scaleY;
    } else {
        releaseTree(root_);
        *root_ = Node{};
        root_->cx = halfWorld;
        root_->cz = halfWorld;
        root_->sizeXZ = halfWorld;
        root_->minY = 0; root_->maxY = 256.0f * scaleY;
    }
    gridRes_ = 16;
    buildTree(root_);
    buildNodeVertices(root_);
    findNeighbors(root_);
    nodeCount = 0;
}

void Terrain::smoothEdges() {
    int S = hmSize_;
    if (S < 4) return;
    for (int y = 0; y < S; ++y) {
        heightmap_[y * S] = 0;
        heightmap_[y * S + 1] = heightmap_[y * S + 3] / 3.0f;
        heightmap_[y * S + 2] = heightmap_[y * S + 3] / 1.5f;

        heightmap_[S - 1 + y * S] = 0;
        heightmap_[S - 1 + y * S - 1] = heightmap_[S - 1 + y * S - 3] / 3.0f;
        heightmap_[S - 1 + y * S - 2] = heightmap_[S - 1 + y * S - 3] / 1.5f;

        heightmap_[y] = 0;
        heightmap_[y + S] = heightmap_[y + S * 3] / 3.0f;
        heightmap_[y + S * 2] = heightmap_[y + S * 3] / 1.5f;

        heightmap_[S * (S - 1) + y] = 0;
        heightmap_[S * (S - 1) + y - S] = heightmap_[S * (S - 1) + y - S * 3] / 3.0f;
        heightmap_[S * (S - 1) + y - S * 2] = heightmap_[S * (S - 1) + y - S * 3] / 1.5f;
    }
}

void Terrain::uploadTo(MeshManager& mgr) {
    if (meshName_.empty()) {
        static int id = 0;
        meshName_ = "terrain_" + std::to_string(++id);
    }
    mgr.loadMesh(meshName_, mesh);
}

void Terrain::setupMultiTexture(Engine& engine) {
    if (layers.empty() || !renderer_) return;
    numLayers_ = std::min((int)layers.size(), 12);
    engine_ = &engine;

    VkDevice device = renderer_->device();

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings.push_back(uboBinding);

    for (int i = 0; i < numLayers_; i++) {
        VkDescriptorSetLayoutBinding texBinding{};
        texBinding.binding = 1 + i;
        texBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texBinding.descriptorCount = 1;
        texBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(texBinding);
    }
    for (int i = 0; i < numLayers_; i++) {
        VkDescriptorSetLayoutBinding maskBinding{};
        maskBinding.binding = 13 + i;
        maskBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        maskBinding.descriptorCount = 1;
        maskBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(maskBinding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = (uint32_t)bindings.size();
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout_);

    std::vector<VkDescriptorPoolSize> poolSizes;
    VkDescriptorPoolSize ubPool{};
    ubPool.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubPool.descriptorCount = 1;
    poolSizes.push_back(ubPool);
    VkDescriptorPoolSize texPool{};
    texPool.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texPool.descriptorCount = (uint32_t)(numLayers_ * 2);
    poolSizes.push_back(texPool);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = (uint32_t)poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool_);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &setLayout_;
    vkAllocateDescriptorSets(device, &allocInfo, &set_);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(Matrix4) + sizeof(float) * 4 + sizeof(float) * 4 + sizeof(float) + sizeof(int) + sizeof(float) * 3;

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &setLayout_;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(device, &plInfo, nullptr, &pipelineLayout_);

    std::string shaderDir = "bin/assets/shaders/terrain/";
    auto vertCode = vk::readFile(shaderDir + "terrain.vert.spv");
    auto fragCode = vk::readFile(shaderDir + "terrain.frag.spv");
    VkShaderModule vertMod = vk::createShaderModule(device, vertCode);
    VkShaderModule fragMod = vk::createShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName = "vertexMain";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName = "fragmentMain";

    auto bindDesc = Vertex::bindingDescription();
    auto attrDescs = Vertex::attributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindDesc;
    vertexInput.vertexAttributeDescriptionCount = (uint32_t)attrDescs.size();
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttach;

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    std::vector<VkDynamicState> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicState.dynamicStateCount = (uint32_t)dynStates.size();
    dynamicState.pDynamicStates = dynStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    VkFormat colorFormat = renderer_->colorFormat();
    VkFormat depthFormat = renderer_->depthFormat();
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = depthFormat;
    pipelineInfo.pNext = &renderingInfo;

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);
    vkDestroyShaderModule(device, fragMod, nullptr);
    vkDestroyShaderModule(device, vertMod, nullptr);

    auto* renderer = engine.getRenderer();
    for (int i = 0; i < numLayers_; i++) {
        layers[i].texName = "tl_t" + std::to_string(i);
        layers[i].maskName = "tl_m" + std::to_string(i);
        renderer->loadTexture(layers[i].texName, layers[i].texturePath);
        if (i > 0) renderer->loadTexture(layers[i].maskName, layers[i].maskPath);
    }

    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(1 + numLayers_ * 2);
    VkBuffer uboBuf = renderer->getUniformBuffer(0);
    VkDescriptorBufferInfo uboBufInfo{uboBuf, 0, sizeof(Matrix4)*2 + sizeof(float)*8};
    VkWriteDescriptorSet uboWrite{};
    uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    uboWrite.dstSet = set_;
    uboWrite.dstBinding = 0;
    uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboWrite.descriptorCount = 1;
    uboWrite.pBufferInfo = &uboBufInfo;
    writes.push_back(uboWrite);

    std::vector<VkDescriptorImageInfo> imgInfos;
    imgInfos.resize(numLayers_ * 2);
    for (int i = 0; i < numLayers_; i++) {
        VkDescriptorImageInfo& dimg = imgInfos[i];
        dimg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        renderer->getTextureInfo(layers[i].texName, dimg.imageView, dimg.sampler);

        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set_;
        w.dstBinding = 1 + i;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.descriptorCount = 1;
        w.pImageInfo = &imgInfos[i];
        writes.push_back(w);
    }
    for (int i = 0; i < numLayers_; i++) {
        VkDescriptorImageInfo& mimg = imgInfos[numLayers_ + i];
        mimg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (i > 0) {
            renderer->getTextureInfo(layers[i].maskName, mimg.imageView, mimg.sampler);
        } else {
            mimg.imageView = imgInfos[0].imageView;
            mimg.sampler = imgInfos[0].sampler;
        }

        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set_;
        w.dstBinding = 13 + i;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.descriptorCount = 1;
        w.pImageInfo = &imgInfos[numLayers_ + i];
        writes.push_back(w);
    }
    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);

    ready_ = true;
}

// --- QuadTree construction ---

void Terrain::buildTree(Node* node) {
    float half = node->sizeXZ / 2.0f;
    int halfHm = (int)(half / scaleXZ_);
    if (halfHm < gridRes_ || node->lodLevel >= maxLodLevels_) return;

    for (int i = 0; i < 4; ++i) {
        auto* child = new Node{};
        child->cx = node->cx + ((i == 1 || i == 3) ? half : -half);
        child->cz = node->cz + ((i < 2) ? half : -half);
        child->sizeXZ = half;
        child->lodLevel = node->lodLevel + 1;
        child->parent = node;

        int cxHm = (int)(child->cx / scaleXZ_);
        int czHm = (int)(child->cz / scaleXZ_);
        int hsHm = (int)(half / scaleXZ_);
        child->minY = 1e9f; child->maxY = -1e9f;
        int z0 = std::max(0, czHm - hsHm), z1 = std::min(hmSize_ - 1, czHm + hsHm);
        int x0 = std::max(0, cxHm - hsHm), x1 = std::min(hmSize_ - 1, cxHm + hsHm);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                float h = heightmap_[z * hmSize_ + x] * scaleY_;
                if (h < child->minY) child->minY = h;
                if (h > child->maxY) child->maxY = h;
            }
        node->children[i] = child;
        buildTree(child);
    }
    nodeCount++;
}

void Terrain::buildNodeVertices(Node* node) {
    if (node->children[0]) {
        for (int i = 0; i < 4; ++i) buildNodeVertices(node->children[i]);
        return;
    }
    // Leaf node - generate vertices and indices into global mesh
    float hs = node->sizeXZ;
    int cxHm = (int)(node->cx / scaleXZ_);
    int czHm = (int)(node->cz / scaleXZ_);
    int hsHm = (int)(hs / scaleXZ_);

    int vtxBase = (int)mesh.vertices.size();
    node->vertexOffset = vtxBase;
    // Wierzchołki
    for (int z = 0; z <= gridRes_; ++z) {
        for (int x = 0; x <= gridRes_; ++x) {
            int hx = cxHm - hsHm + (x * hsHm * 2) / gridRes_;
            int hz = czHm - hsHm + (z * hsHm * 2) / gridRes_;
            hx = std::max(0, std::min(hmSize_ - 1, hx));
            hz = std::max(0, std::min(hmSize_ - 1, hz));
            float h = heightmap_[hz * hmSize_ + hx] * scaleY_;
            float px = hx * scaleXZ_, pz = hz * scaleXZ_;
            mesh.vertices.push_back({
                {px, h, pz}, {0,0,1}, {1,1,1},
                {hx * texScale_ / hmSize_, hz * texScale_ / hmSize_}
            });
        }
    }
    // Normale
    for (int z = 0; z <= gridRes_; ++z)
        for (int x = 0; x <= gridRes_; ++x) {
            int idx = vtxBase + z * (gridRes_ + 1) + x;
            float hL = (x > 0) ? mesh.vertices[idx - 1].position[1] : mesh.vertices[idx].position[1];
            float hR = (x < gridRes_) ? mesh.vertices[idx + 1].position[1] : mesh.vertices[idx].position[1];
            float hD = (z > 0) ? mesh.vertices[idx - (gridRes_ + 1)].position[1] : mesh.vertices[idx].position[1];
            float hU = (z < gridRes_) ? mesh.vertices[idx + (gridRes_ + 1)].position[1] : mesh.vertices[idx].position[1];
            Vec3 n{hL - hR, 2.0f * scaleXZ_, hD - hU}; n = normalize(n);
            mesh.vertices[idx].normal = {n.x, n.y, n.z};
        }
    // Indeksy
    node->indexOffset = (int)mesh.indices.size();
    for (int z = 0; z < gridRes_; ++z) {
        for (int x = 0; x < gridRes_; ++x) {
            int i0 = vtxBase + z * (gridRes_ + 1) + x;
            int i1 = vtxBase + (z + 1) * (gridRes_ + 1) + x;
            int i2 = vtxBase + (z + 1) * (gridRes_ + 1) + (x + 1);
            int i3 = vtxBase + z * (gridRes_ + 1) + (x + 1);
            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i3);
            mesh.indices.push_back(i0);
        }
    }
    node->indexCount = (int)mesh.indices.size() - node->indexOffset;
}

// --- Neighbor finding ---

void Terrain::findNeighbors(Node* node) {
    if (!node) return;
    float hs = node->sizeXZ;
    float cx = node->cx, cz = node->cz;
    Node* n = findNodeAt(root_, cx - hs * 2, cz); if (n) node->neighbor[0] = n;
    n = findNodeAt(root_, cx + hs * 2, cz); if (n) node->neighbor[1] = n;
    n = findNodeAt(root_, cx, cz - hs * 2); if (n) node->neighbor[2] = n;
    n = findNodeAt(root_, cx, cz + hs * 2); if (n) node->neighbor[3] = n;
    for (int i = 0; i < 4; ++i)
        if (node->children[i]) findNeighbors(node->children[i]);
}

Terrain::Node* Terrain::findNodeAt(Node* node, float cx, float cz) {
    if (!node) return nullptr;
    float eps = node->sizeXZ * 0.1f;
    if (std::abs(node->cx - cx) < eps && std::abs(node->cz - cz) < eps && !node->children[0])
        return node;
    if (node->children[0]) {
        float hs = node->sizeXZ / 4.0f;
        int i = (cx > node->cx ? 1 : 0) + (cz < node->cz ? 0 : 2); // which quadrant
        if (i >= 0 && i < 4) return findNodeAt(node->children[i], cx, cz);
    }
    if (!node->children[0]) return node;
    return nullptr;
}

// --- Per-frame update ---

void Terrain::update(const CameraComponent* cam, MeshManager& meshManager, const std::string& meshName) {
    if (!root_ || !cam) return;
    visibleNodes_.clear();
    updateNodes(root_, cam);
    visibleCount = (int)visibleNodes_.size();
    for (auto* node : visibleNodes_)
        generateStitchingIndices(node);
}

void Terrain::updateNodes(Node* node, const CameraComponent* cam) {
    if (!node) return;
    Vec3 camPos = cam->position;
    float dist = std::sqrt((camPos.x - node->cx) * (camPos.x - node->cx) +
                           (camPos.z - node->cz) * (camPos.z - node->cz));
    float detailThresh = node->sizeXZ * 1.2f;

    bool shouldSplit = (dist < detailThresh) && (node->children[0] != nullptr);

    // Simple frustum check: sphere test
    float radius = node->sizeXZ * 1.5f;
    bool inFrustum = true;
    {
        Vec3 toCenter{node->cx - camPos.x, (node->minY + node->maxY) * 0.5f - camPos.y, node->cz - camPos.z};
        if (std::abs(toCenter.x) > radius + 5000 || std::abs(toCenter.z) > radius + 5000)
            inFrustum = false;
    }

    if (!inFrustum) {
        node->visible = false;
        return;
    }

    if (shouldSplit) {
        node->visible = false;
        for (int i = 0; i < 4; ++i)
            updateNodes(node->children[i], cam);
    } else {
        node->visible = true;
        visibleNodes_.push_back(node);

        // Compute LOD differences with neighbors
        for (int i = 0; i < 4; ++i) {
            node->neighborLodDiff[i] = 0;
            if (node->neighbor[i]) {
                int nLod = node->neighbor[i]->lodLevel;
                if (node->neighbor[i]->children[0]) {
                    bool anyChildVisible = false;
                    for (int j = 0; j < 4; ++j)
                        if (node->neighbor[i]->children[j]->visible) { anyChildVisible = true; break; }
                    if (anyChildVisible) nLod = node->neighbor[i]->lodLevel + 1;
                }
                if (nLod > node->lodLevel) node->neighborLodDiff[i] = 1;
            }
        }
    }
}

// --- LOD Stitching ---

void Terrain::generateStitchingIndices(Node* node) {
    // Stitching per-node is not used with global mesh; function intentionally left empty.
    // (całe ciało usunięte)
    // Function intentionally left empty: stitching per-node is not used with global mesh.
}

void Terrain::collectAndUpload(Node* node, MeshManager& mgr, const std::string& prefix, int& slot) {
    // No longer used: per-node upload removed (global mesh only)
}

void Terrain::uploadMeshes(MeshManager& mgr, const std::string& prefix) {
    // Upload only one mesh for the whole terrain
    mgr.loadMesh(prefix + "_full", mesh);
}

// --- IRenderFeature ---

void Terrain::init(Renderer* renderer) {
    renderer_ = renderer;
}

void Terrain::render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView&) {
    if (!ready_ || !renderer_ || !engine_) return;
    VkDevice device = renderer_->device();

    VkBuffer uboBuf = renderer_->getUniformBuffer(frameIndex);
    if (uboBuf != VK_NULL_HANDLE) {
        VkDescriptorBufferInfo bi{uboBuf, 0, sizeof(Matrix4)*2 + sizeof(float)*8};
        VkWriteDescriptorSet uw{};
        uw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uw.dstSet = set_;
        uw.dstBinding = 0;
        uw.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uw.descriptorCount = 1;
        uw.pBufferInfo = &bi;
        vkUpdateDescriptorSets(device, 1, &uw, 0, nullptr);
    }

    VkExtent2D extent = renderer_->extent();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &set_, 0, nullptr);

    VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0, 1};
    VkRect2D scissor{{0,0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    struct Push { Matrix4 model; float color[4]; float lightDir[4]; float bump; int numLayers; float pad[3]; } push;
    push.model = Matrix4::identity();
    push.color[0] = 1; push.color[1] = 1; push.color[2] = 1; push.color[3] = 1;
    push.lightDir[0] = 0.5f; push.lightDir[1] = 1.0f; push.lightDir[2] = 0.3f; push.lightDir[3] = 0.25f;
    push.bump = 1.0f;
    push.numLayers = numLayers_;
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

    auto gpuMesh = engine_->getMeshManager()->getMesh(meshName_);
    if (!gpuMesh) return;
    VkBuffer vb = gpuMesh->vertexBuffer ? gpuMesh->vertexBuffer->handle() : VK_NULL_HANDLE;
    VkBuffer ib = gpuMesh->indexBuffer ? gpuMesh->indexBuffer->handle() : VK_NULL_HANDLE;
    if (vb == VK_NULL_HANDLE || ib == VK_NULL_HANDLE) return;
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
    vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, gpuMesh->indexCount, 1, 0, 0, 0);
}

void Terrain::cleanupPipeline() {
    if (!renderer_) return;
    VkDevice d = renderer_->device();
    if (pipeline_) { vkDestroyPipeline(d, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_) { vkDestroyPipelineLayout(d, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
    if (pool_) { vkDestroyDescriptorPool(d, pool_, nullptr); pool_ = VK_NULL_HANDLE; }
    if (setLayout_) { vkDestroyDescriptorSetLayout(d, setLayout_, nullptr); setLayout_ = VK_NULL_HANDLE; }
    ready_ = false;
}

// --- Cleanup ---

Terrain::~Terrain() {
    cleanupPipeline();
    deleteTree(root_);
    root_ = nullptr;
}

void Terrain::releaseTree(Node* node) {
    if (!node) return;
    for (int i = 0; i < 4; ++i)
        if (node->children[i]) releaseTree(node->children[i]);
}

void Terrain::deleteTree(Node* node) {
    if (!node) return;
    for (int i = 0; i < 4; ++i) {
        if (node->children[i]) {
            deleteTree(node->children[i]);
            delete node->children[i];
            node->children[i] = nullptr;
        }
    }
    delete node;
}

// --- Height query ---

float Terrain::getHeight(float x, float z) const {
    if (hmSize_ == 0) return 0;
    float gx = (x / scaleXZ_) + hmSize_ / 2.0f;
    float gz = (z / scaleXZ_) + hmSize_ / 2.0f;
    int ix = (int)gx, iz = (int)gz;
    if (ix < 0 || ix >= hmSize_ - 1 || iz < 0 || iz >= hmSize_ - 1) return 0;
    float fx = gx - ix, fz = gz - iz;
    float h00 = heightmap_[iz * hmSize_ + ix], h10 = heightmap_[iz * hmSize_ + ix + 1];
    float h01 = heightmap_[(iz+1) * hmSize_ + ix], h11 = heightmap_[(iz+1) * hmSize_ + ix + 1];
    return ((h00 * (1-fx) + h10 * fx) * (1-fz) + (h01 * (1-fx) + h11 * fx) * fz) * scaleY_;
}

// --- Generators (unchanged) ---

void Terrain::generateFault(int size, int iterations, int maxDelta, int minDelta, int itersPerFilter, float filter) {
    hmSize_ = size; heightmap_.assign(size * size, 10.0f);
    srand((unsigned)time(nullptr));
    for (int i = 0; i < iterations; ++i) {
        int dHeight = maxDelta - ((maxDelta - minDelta) * i) / iterations;
        int x1 = rand() % size, y1 = rand() % size, x2, y2;
        do { x2 = rand() % size; y2 = rand() % size; } while (x2 == x1 && y2 == y1);
        int dx1 = x2 - x1, dy1 = y2 - y1;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                if ((x - x1) * dy1 - dx1 * (y - y1) > 0)
                    heightmap_[x + size * y] += (float)dHeight;
        if (itersPerFilter && (i % itersPerFilter) == 0) {
            auto fb = [&](float* band, int stride, int count, float f) {
                float v = band[0];
                for (int j = stride; j < count * stride; j += stride) { band[j] = f * v + (1-f) * band[j]; v = band[j]; }
            };
            for (int r = 0; r < size; ++r) { fb(&heightmap_[size*r], 1, size, filter); fb(&heightmap_[size*r+size-1], -1, size, filter); fb(&heightmap_[r], size, size, filter); fb(&heightmap_[size*(size-1)+r], -size, size, filter); }
        }
    }
    float minV = heightmap_[0], maxV = heightmap_[0];
    for (auto h : heightmap_) { if (h < minV) minV = h; if (h > maxV) maxV = h; }
    if (maxV > minV) { float dh = maxV - minV; for (auto& h : heightmap_) h = ((h - minV) / dh) * 300.0f; }
}

void Terrain::generatePlasma(int size, float roughness) {
    hmSize_ = size; heightmap_.assign(size * size, 0.0f);
    srand((unsigned)time(nullptr));
    int rectSize = size; float dh = (float)rectSize / 2.0f, r = (float)pow(2.0, -roughness);
    while (rectSize > 0) {
        for (int i = 0; i < size; i += rectSize) for (int j = 0; j < size; j += rectSize) {
            int ni = (i + rectSize) % size, nj = (j + rectSize) % size, mi = i + rectSize / 2, mj = j + rectSize / 2;
            heightmap_[mi + mj * size] = (heightmap_[i+j*size] + heightmap_[ni+j*size] + heightmap_[i+nj*size] + heightmap_[ni+nj*size]) / 4 + rangedRandom(-dh/2, dh/2) * r;
        }
        for (int i = 0; i < size; i += rectSize) for (int j = 0; j < size; j += rectSize) {
            int ni = (i + rectSize) % size, nj = (j + rectSize) % size, mi = i + rectSize / 2, mj = j + rectSize / 2, pmi = (i - rectSize / 2 + size) % size, pmj = (j - rectSize / 2 + size) % size;
            heightmap_[mi + j * size] = (heightmap_[i+j*size] + heightmap_[ni+j*size] + heightmap_[mi+mj*size] + heightmap_[mi+pmj*size]) / 4 + rangedRandom(-dh/2, dh/2) * r;
            heightmap_[i + mj * size] = (heightmap_[i+j*size] + heightmap_[i+nj*size] + heightmap_[mi+mj*size] + heightmap_[pmi+mj*size]) / 4 + rangedRandom(-dh/2, dh/2) * r;
        }
        rectSize /= 2; dh *= (float)pow(2.0, -roughness);
    }
}

void Terrain::generateDeposition(int size, int jumps, int peakWalk, int minPart, int maxPart, float caldera) {
    hmSize_ = size; heightmap_.assign(size * size, 0.0f);
    srand((unsigned)time(nullptr));
    for (int i = 0; i < jumps; ++i) {
        int px = rand() % size, pz = rand() % size;
        int particles = minPart + rand() % (maxPart - minPart + 1);
        for (int j = 0; j < particles; ++j) {
            if (rand() % 2) px = (px + 1) % size; else px = (px - 1 + size) % size;
            if (rand() % 2) pz = (pz + 1) % size; else pz = (pz - 1 + size) % size;
            heightmap_[px + pz * size] += 1.0f;
            for (int w = 0; w < peakWalk; ++w) {
                int nx = px, nz = pz;
                if (rand() % 2) nx = (nx + 1) % size; else nx = (nx - 1 + size) % size;
                if (rand() % 2) nz = (nz + 1) % size; else nz = (nz - 1 + size) % size;
                if (heightmap_[nx + nz * size] < heightmap_[px + pz * size]) { px = nx; pz = nz; }
                heightmap_[px + pz * size] += 1.0f;
            }
        }
    }
    float maxV = 0; for (auto h : heightmap_) if (h > maxV) maxV = h;
    if (maxV > 0 && caldera > 0.0001f) { for (auto& h : heightmap_) h = maxV - h; float v = maxV * caldera; for (auto& h : heightmap_) if (h < v) h = v; }
}

bool Terrain::loadFromJson(const std::string& path) {
    std::ifstream f(path); if (!f) return false;
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    auto strVal = [&](const std::string& key) -> std::string {
        auto p = json.find("\"" + key + "\"");
        if (p == std::string::npos) return "";
        p = json.find(":", p); if (p == std::string::npos) return "";
        p++;
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n')) p++;
        if (json[p] == '"') {
            p++; auto e = json.find("\"", p); if (e == std::string::npos) return "";
            return json.substr(p, e - p);
        }
        if (json[p] == '-' || (json[p] >= '0' && json[p] <= '9')) {
            auto e = p; while (e < json.size() && (json[e] == '-' || json[e] == '.' || (json[e] >= '0' && json[e] <= '9'))) e++;
            return json.substr(p, e - p);
        }
        return "";
    };

    std::string hmFile = strVal("heightMap"); if (hmFile.empty()) return false;
    std::string baseDir = path;
    auto slash = baseDir.find_last_of("/\\"); if (slash != std::string::npos) baseDir = baseDir.substr(0, slash + 1);
    hmFile = baseDir + hmFile;

    float sxz = 10, sy = 2, ts = 20;
    std::string v = strVal("scaleMap"); if (!v.empty()) sxz = std::stof(v);
    v = strVal("scaleHeight"); if (!v.empty()) sy = std::stof(v);
    v = strVal("scaleTexture"); if (!v.empty()) ts = std::stof(v);

    auto layersStart = json.find("\"layers\"");
    if (layersStart != std::string::npos) {
        layersStart = json.find("[", layersStart); if (layersStart == std::string::npos) goto done;
        auto layersEnd = json.find("]", layersStart); if (layersEnd == std::string::npos) goto done;
        std::string layersStr = json.substr(layersStart + 1, layersEnd - layersStart - 1);

        size_t objStart = 0;
        while ((objStart = layersStr.find("{", objStart)) != std::string::npos) {
            auto objEnd = layersStr.find("}", objStart); if (objEnd == std::string::npos) break;
            std::string obj = layersStr.substr(objStart, objEnd - objStart + 1);

            auto getObjVal = [&](const std::string& o, const std::string& key) -> std::string {
                auto p = o.find("\"" + key + "\""); if (p == std::string::npos) return "";
                p = o.find(":", p); if (p == std::string::npos) return "";
                p++; while (p < o.size() && (o[p] == ' ' || o[p] == '\t')) p++;
                if (o[p] == '"') { p++; auto e = o.find("\"", p); if (e == std::string::npos) return ""; return o.substr(p, e - p); }
                return "";
            };

            TerrainLayer layer;
            layer.maskPath = baseDir + getObjVal(obj, "mask");
            layer.texturePath = baseDir + getObjVal(obj, "texture");
            if (!layer.maskPath.empty() && layer.texturePath != baseDir)
                layers.push_back(layer);
            objStart = objEnd + 1;
        }
    }
done:
    return loadFromImage(hmFile, sxz, sy, ts, 5);
}

} // namespace engine3d
