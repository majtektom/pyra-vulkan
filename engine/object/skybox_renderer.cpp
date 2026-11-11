#include <array>
#include <vector>
#include <stdexcept>
#include "render/renderer.h"
#include "render/pipeline_builder.h"
#include "render/vulkan_util.h"
#include "object/skybox_renderer.h"

namespace engine3d {

SkyboxRenderer::SkyboxRenderer() : name_("SkyboxRenderer") {}
SkyboxRenderer::~SkyboxRenderer() {
    if (renderer_) {
        cleanup(renderer_->device());
    }
}

void SkyboxRenderer::configure(const SkyboxConfig& config, const std::string& shaderDir) {
    config_ = config;
    config_.enabled = true;
    shaderDir_ = shaderDir;
}

void SkyboxRenderer::setTextureHandles(TextureHandle fb, TextureHandle lr, TextureHandle top) {
    textureFb_ = fb;
    textureLr_ = lr;
    textureTop_ = top;
}

void SkyboxRenderer::init(Renderer* renderer) {
    renderer_ = renderer;
    if (!enabled_) {
        ready_ = false;
        return;
    }

    VkDevice device = renderer_->device();
    VkPhysicalDevice physicalDevice = renderer_->physicalDevice();
    VkCommandPool commandPool = renderer_->commandPool();
    VkQueue graphicsQueue = renderer_->graphicsQueue();
    VkDescriptorSetLayout descriptorSetLayout = renderer_->descriptorSetLayout();
    VkFormat colorFormat = renderer_->colorFormat();
    VkFormat depthFormat = renderer_->depthFormat();

    // ---------------------------------------------------------------
    // 1. POTOK: kostka nieba rysowana od środka (bez cullingu), paski TRIANGLE_STRIP (4 wierzchołki na ścianę),
    //    głębokość tylko testowana LEQUAL - niebo zawsze zostaje w tle.
    // ---------------------------------------------------------------
    const vk::ShaderModule vert(device, shaderDir_ + "/skybox.vert.spv");
    const vk::ShaderModule frag(device, shaderDir_ + "/skybox.frag.spv");

    pipelineLayout_ = vk::createPipelineLayout(device, {descriptorSetLayout}, sizeof(PushConstants),
                                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, "skybox");
    pipeline_ = vk::GraphicsPipelineBuilder()
        .shaders(vert, frag)
        .vertexInput(Vertex::bindingDescription(), vk::filterVertexAttributes(vert.code(), Vertex::attributeDescriptions()))
        .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
        .cull(VK_CULL_MODE_NONE)
        .depth(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)
        .colorFormat(colorFormat)
        .depthFormat(depthFormat)
        .build(device, pipelineLayout_, "skybox");

    // ---------------------------------------------------------------
    // 2. GEOMETRIA: 5 ścian kostki (bez dna), każda to 4 wierzchołki rysowane jako TRIANGLE_STRIP.
    //    Tekstura nieba to atlas: ściany Z biorą dolną połowę (v 0.5..1), ściany X górną (v 0..0.5),
    //    sufit cały obraz.
    // ---------------------------------------------------------------
    const float half = static_cast<float>(config_.size);             // pół boku kostki
    const float bottom = -static_cast<float>(config_.verticalOffset); // dolna krawędź (pod horyzontem)
    const float top = half + bottom;
    const float inset = 0.002f;   // lekkie odsunięcie UV od krawędzi atlasu - bez prześwitów sąsiednich pikseli
    const float uMin = inset, uMax = 1.0f - inset;
    const float lowerVMin = 0.5f + inset, lowerVMax = 1.0f - inset;   // dolna połowa atlasu
    const float upperVMin = inset, upperVMax = 0.5f - inset;          // górna połowa atlasu

    std::vector<Vertex> vertices;
    vertices.reserve(20);
    auto vertex = [&](float x, float y, float z, float u, float v) {
        vertices.push_back({{{x, y, z}}, {{0.0f, 0.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{u, v}}});
    };

    // Ściana z = -half
    vertex(-half, bottom, -half, uMin, lowerVMin);
    vertex( half, bottom, -half, uMax, lowerVMin);
    vertex( half, top,    -half, uMax, lowerVMax);
    vertex(-half, top,    -half, uMin, lowerVMax);
    // Ściana z = +half
    vertex(-half, bottom,  half, uMax, lowerVMin);
    vertex( half, bottom,  half, uMin, lowerVMin);
    vertex( half, top,     half, uMin, lowerVMax);
    vertex(-half, top,     half, uMax, lowerVMax);
    // Ściana x = -half
    vertex(-half, bottom,  half, uMin, upperVMax);
    vertex(-half, bottom, -half, uMax, upperVMax);
    vertex(-half, top,    -half, uMax, upperVMin);
    vertex(-half, top,     half, uMin, upperVMin);
    // Ściana x = +half
    vertex( half, bottom,  half, uMax, upperVMax);
    vertex( half, bottom, -half, uMin, upperVMax);
    vertex( half, top,    -half, uMin, upperVMin);
    vertex( half, top,     half, uMax, upperVMin);
    // Sufit y = top
    vertex( half, top,    -half, uMax, upperVMin);
    vertex(-half, top,    -half, uMax, lowerVMax);
    vertex(-half, top,     half, uMin, lowerVMax);
    vertex( half, top,     half, uMin, upperVMin);

    // Każda ściana to pasek 4 wierzchołków (kolejność dla TRIANGLE_STRIP).
    std::vector<uint32_t> indices = {
        1, 2, 0, 3,
        5, 4, 6, 7,
        9, 10, 8, 11,
        13, 12, 14, 15,
        17, 16, 18, 19,
    };

    VkDeviceSize vbSize = sizeof(Vertex) * vertices.size();
    vk::createDeviceLocalBufferWithStaging(
        device, physicalDevice, commandPool, graphicsQueue,
        vertices.data(), vbSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertexBuffer_, vertexBufferMemory_
    );

    VkDeviceSize ibSize = sizeof(uint32_t) * indices.size();
    vk::createDeviceLocalBufferWithStaging(
        device, physicalDevice, commandPool, graphicsQueue,
        indices.data(), ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        indexBuffer_, indexBufferMemory_
    );

    ready_ = true;
}

void SkyboxRenderer::cleanup(VkDevice device) {
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertexBuffer_, nullptr);
        vertexBuffer_ = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertexBufferMemory_, nullptr);
        vertexBufferMemory_ = VK_NULL_HANDLE;
    }
    if (indexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, indexBuffer_, nullptr);
        indexBuffer_ = VK_NULL_HANDLE;
    }
    if (indexBufferMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, indexBufferMemory_, nullptr);
        indexBufferMemory_ = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    ready_ = false;
}

void SkyboxRenderer::render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) {
    if (!ready_ || !enabled_ || !renderer_) return;

    uint32_t indices[3] = {};
    indices[0] = renderer_->getTextureIndex(textureFb_);
    indices[1] = renderer_->getTextureIndex(textureLr_);
    indices[2] = renderer_->getTextureIndex(textureTop_);

    VkDescriptorSet globalSet = renderer_->getTextureDescriptorSet("", frameIndex);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &globalSet, 0, nullptr);

    recordDrawCommands(cmd, pipelineLayout_, view.cameraPosition, indices);
}

void SkyboxRenderer::recordDrawCommands(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout skyboxPipelineLayout,
    const Vec3& cameraPosition,
    const uint32_t indices[3]
) const {
    if (!ready_ || !enabled_) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    struct PushConstants {
        float cameraPosition[4];
        float exposure;
        uint32_t skyboxIndex;
        uint32_t pad[2];
    };

    PushConstants push{};
    push.cameraPosition[0] = cameraPosition.x;
    push.cameraPosition[1] = cameraPosition.y;
    push.cameraPosition[2] = cameraPosition.z;
    push.exposure = exposure_;

    const VkBuffer vb[] = {vertexBuffer_};
    const VkDeviceSize offset[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vb, offset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);

    for (int face = 0; face < 5; ++face) {
        const int texIdx = kFaceMap[face];
        push.skyboxIndex = indices[texIdx];
        
        vkCmdPushConstants(
            commandBuffer,
            skyboxPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(PushConstants),
            &push
        );
        
        vkCmdDrawIndexed(commandBuffer, 4, 1, face * 4, 0, 0);
    }
}

} // namespace engine3d
