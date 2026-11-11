#include <array>
#include <stdexcept>
#include <vector>
#include "render/renderer.h"
#include "render/pipeline_builder.h"
#include "render/vulkan_util.h"
#include "core/math.h"
#include "core/types.h"
#include "object/ocean_renderer.h"

namespace engine3d {

namespace {

struct PushConstants {
    float cameraPosition[4];
    float time;
    float waterLevel;
    float waveAmplitude;
    float waveSpeed;
    float waveFrequency;
    float foamThreshold;
    float sceneColorIndex;   // Bindless index for ocean_scene_color texture
    float sceneDepthIndex;   // Bindless index for ocean_scene_depth texture
    float dudvMapIndex;      // Bindless index for water_dudv texture
    float _padding[3];
    float lightDir[4];
};
static_assert(sizeof(PushConstants) == 80);

void generateOceanMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    constexpr int n = 512;
    constexpr float size = 6500.0f;
    constexpr float half = size * 0.5f;
    constexpr float y = -8.0f;

    vertices.resize((n + 1) * (n + 1));
    for (int iz = 0; iz <= n; ++iz) {
        for (int ix = 0; ix <= n; ++ix) {
            float px = -half + (float(ix) / n) * size;
            float pz = -half + (float(iz) / n) * size;
            float u = float(ix) / n;
            float v = float(iz) / n;
            vertices[iz * (n + 1) + ix] = {
                {{px, y, pz}}, {{0, 1, 0}}, {{1, 1, 1}}, {{u, v}}
            };
        }
    }

    indices.resize(n * n * 6);
    uint32_t* idx = indices.data();
    for (int iz = 0; iz < n; ++iz) {
        for (int ix = 0; ix < n; ++ix) {
            uint32_t a = iz * (n + 1) + ix;
            uint32_t b = a + 1;
            uint32_t c = a + (n + 1);
            uint32_t d = c + 1;
            *idx++ = a; *idx++ = c; *idx++ = b;
            *idx++ = b; *idx++ = c; *idx++ = d;
        }
    }
}

}  // namespace

OceanRenderer::OceanRenderer() : name_("OceanRenderer") {}
OceanRenderer::~OceanRenderer() {
    if (renderer_) {
        cleanup(renderer_->device());
    }
}

void OceanRenderer::configure(const OceanConfig& config, const std::string& shaderDir, const std::string& descName) {
    config_ = config;
    config_.enabled = true;
    shaderDir_ = shaderDir;
    descName_ = descName;
}

void OceanRenderer::init(Renderer* renderer) {
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

    // Woda: głębokość tylko testowana (bez zapisu), bez sprzętowego mieszania - shader oceanu sam składa
    // refrakcję z kopią koloru sceny; dodatkowe mieszanie rozcieńczyłoby odbicie drugi raz.
    const vk::ShaderModule vert(device, shaderDir_ + "/ocean.vert.spv");
    const vk::ShaderModule frag(device, shaderDir_ + "/ocean.frag.spv");

    pipelineLayout_ = vk::createPipelineLayout(device, {descriptorSetLayout}, sizeof(PushConstants),
                                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, "ocean");
    pipeline_ = vk::GraphicsPipelineBuilder()
        .shaders(vert, frag)
        .vertexInput(Vertex::bindingDescription(), vk::filterVertexAttributes(vert.code(), Vertex::attributeDescriptions()))
        .polygonMode(renderer_->isWireframeEnabled() ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL)
        .depth(true, false)
        .colorFormat(colorFormat)
        .depthFormat(depthFormat)
        .build(device, pipelineLayout_, "ocean");

    // Tekstury wody do zniekształcania odbić i załamań
    renderer_->loadTexture("water_dudv", "assets/woda/water_dudv.bmp", true);
    renderer_->loadTexture("water_bump", "assets/woda/water_bump.bmp", true);

    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
    generateOceanMesh(verts, inds);
    indexCount_ = static_cast<uint32_t>(inds.size());

    VkDeviceSize vbSize = sizeof(Vertex) * verts.size();
    vk::createDeviceLocalBufferWithStaging(
        device, physicalDevice, commandPool, graphicsQueue,
        verts.data(), vbSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertexBuffer_, vertexBufferMemory_);

    VkDeviceSize ibSize = sizeof(uint32_t) * inds.size();
    vk::createDeviceLocalBufferWithStaging(
        device, physicalDevice, commandPool, graphicsQueue,
        inds.data(), ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        indexBuffer_, indexBufferMemory_);

    ready_ = true;
}

void OceanRenderer::render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) {
    if (!enabled_ || !renderer_) return;

    bool currentWireframe = renderer_->isWireframeEnabled();
    if (currentWireframe != lastWireframe_) {
        lastWireframe_ = currentWireframe;
        if (ready_) {
            renderer_->waitIdle();
            cleanup(renderer_->device());
        }
        init(renderer_);
    }

    if (!ready_) return;

    VkDescriptorSet descSet = renderer_->getTextureDescriptorSet(descName_, frameIndex);
    recordDrawCommands(cmd, view.cameraPosition, time_, descSet, config_);
}

void OceanRenderer::cleanup(VkDevice device) {
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

void OceanRenderer::recordDrawCommands(
    VkCommandBuffer commandBuffer,
    const Vec3& cameraPosition,
    float time,
    VkDescriptorSet descriptorSet,
    const OceanConfig& config
) const {
    if (!ready_ || !enabled_) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_, 0, 1, &descriptorSet, 0, nullptr);

    PushConstants push{};
    push.cameraPosition[0] = cameraPosition.x;
    push.cameraPosition[1] = cameraPosition.y;
    push.cameraPosition[2] = cameraPosition.z;
    push.cameraPosition[3] = 0.0f;
    push.time = time;
    push.waterLevel = config.waterLevel;
    push.waveAmplitude = config.waveAmplitude;
    push.waveSpeed = config.waveSpeed;
    push.waveFrequency = config.waveFrequency;
    push.foamThreshold = config.foamThreshold;
    push.sceneColorIndex = static_cast<float>(renderer_->getTextureIndex("ocean_scene_color"));
    push.sceneDepthIndex = static_cast<float>(renderer_->getTextureIndex("ocean_scene_depth"));
    push.dudvMapIndex = static_cast<float>(renderer_->getTextureIndex("water_dudv"));
    push.lightDir[0] = 0.5f;
    push.lightDir[1] = 1.0f;
    push.lightDir[2] = 0.3f;
    push.lightDir[3] = 0.25f;

    vkCmdPushConstants(commandBuffer, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &push);

    VkBuffer vb[] = {vertexBuffer_};
    VkDeviceSize offset[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vb, offset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);
}

}  // namespace engine3d
