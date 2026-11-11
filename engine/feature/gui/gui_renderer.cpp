#include "feature/gui/gui_renderer.h"
#include "render/vulkan_util.h"
#include <cstring>
#include <stdexcept>

namespace engine3d {

VkVertexInputBindingDescription GuiVertex::bindingDescription() {
    VkVertexInputBindingDescription d{}; d.binding = 0; d.stride = sizeof(GuiVertex); d.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return d;
}
std::array<VkVertexInputAttributeDescription, 3> GuiVertex::attributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 3> a{};
    a[0].binding = 0; a[0].location = 0; a[0].format = VK_FORMAT_R32G32B32_SFLOAT; a[0].offset = offsetof(GuiVertex, pos);
    a[1].binding = 0; a[1].location = 1; a[1].format = VK_FORMAT_R32G32_SFLOAT; a[1].offset = offsetof(GuiVertex, texCoord);
    a[2].binding = 0; a[2].location = 2; a[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; a[2].offset = offsetof(GuiVertex, color);
    return a;
}

void GuiRenderer::init(VkDevice d, VkPhysicalDevice p, VkFormat colorFormat, VkFormat depthFormat, VkCommandPool cp, VkQueue q, VkExtent2D) {
    device_ = d; pd_ = p; cp_ = cp; q_ = q;

    std::string dir = "bin/assets/shaders/text_overlay/";
    auto vc = vk::readFile(dir + "text.vert.spv");
    auto fc = vk::readFile(dir + "text.frag.spv");
    auto vm = vk::createShaderModule(d, vc);
    auto fm = vk::createShaderModule(d, fc);

    VkPipelineShaderStageCreateInfo ss[2] = {};
    ss[0].sType = ss[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ss[0].stage = VK_SHADER_STAGE_VERTEX_BIT; ss[0].module = vm; ss[0].pName = "vertexMain";
    ss[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; ss[1].module = fm; ss[1].pName = "fragmentMain";

    auto bd = GuiVertex::bindingDescription(); auto ad = GuiVertex::attributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bd;
    vi.vertexAttributeDescriptionCount = (uint32_t)ad.size(); vi.pVertexAttributeDescriptions = ad.data();

    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState ba{};
    ba.blendEnable = VK_TRUE; ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; ba.colorBlendOp = VK_BLEND_OP_ADD;
    ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; ba.alphaBlendOp = VK_BLEND_OP_ADD;
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &ba;

    std::vector<VkDynamicState> dyn = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = (uint32_t)dyn.size(); dy.pDynamicStates = dyn.data();

    VkDescriptorSetLayoutBinding lb{}; lb.binding = 0; lb.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    lb.descriptorCount = 1; lb.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo sl{}; sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;   // tekstura podawana przy rysowaniu (push descriptors)
    sl.bindingCount = 1; sl.pBindings = &lb;
    vkCreateDescriptorSetLayout(d, &sl, nullptr, &dsl_);

    VkPipelineLayoutCreateInfo pl{}; pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1; pl.pSetLayouts = &dsl_;
    vkCreatePipelineLayout(d, &pl, nullptr, &pipelineLayout_);

    VkGraphicsPipelineCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2; pi.pStages = ss; pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp; pi.pRasterizationState = &rs; pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds; pi.pColorBlendState = &cb; pi.pDynamicState = &dy;
    pi.layout = pipelineLayout_; pi.renderPass = VK_NULL_HANDLE; pi.subpass = 0;

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = depthFormat;
    pi.pNext = &renderingInfo;

    vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline_);

    vkDestroyShaderModule(d, fm, nullptr); vkDestroyShaderModule(d, vm, nullptr);

    createWhiteTexture();
    ready_ = true;
}

// Biała tekstura 1x1 (prostokąty i tekst używają tego samego potoku co obrazki) - piksel kopiowany z CPU.
void GuiRenderer::createWhiteTexture() {
    const uint32_t white = 0xFFFFFFFF;
    vk::createImage(device_, pd_, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_HOST_TRANSFER_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, whiteImage_, whiteMem_);
    vk::hostTransitionToCopyLayout(device_, whiteImage_);
    vk::hostCopyToImage(device_, whiteImage_, &white, 1, 1);
    whiteView_ = vk::createImageView(device_, whiteImage_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    sampler_ = vk::createClampSampler(device_);
}

void GuiRenderer::addRect(float x, float y, float w, float h, std::array<float,4> c, float z) {
    float actualZ = (z == -999.0f) ? currentDepth_ : z;
    GuiVertex v;
    v.texCoord[0] = 0.5f; v.texCoord[1] = 0.5f;
    v.color[0] = c[0]; v.color[1] = c[1]; v.color[2] = c[2]; v.color[3] = c[3];
    v.pos[0] = x; v.pos[1] = y; v.pos[2] = actualZ; vertices_.push_back(v);
    v.pos[0] = x+w; v.pos[1] = y; v.pos[2] = actualZ; vertices_.push_back(v);
    v.pos[0] = x+w; v.pos[1] = y+h; v.pos[2] = actualZ; vertices_.push_back(v);
    v.pos[0] = x; v.pos[1] = y; v.pos[2] = actualZ; vertices_.push_back(v);
    v.pos[0] = x+w; v.pos[1] = y+h; v.pos[2] = actualZ; vertices_.push_back(v);
    v.pos[0] = x; v.pos[1] = y+h; v.pos[2] = actualZ; vertices_.push_back(v);
}

void GuiRenderer::addText(const std::vector<GuiVertex>& verts) {
    vertices_.insert(vertices_.end(), verts.begin(), verts.end());
}

int GuiRenderer::createImage(uint32_t width, uint32_t height) {
    if (images_.size() >= kMaxGuiImages || width == 0 || height == 0) return -1;
    GuiImage img;
    img.width = width;
    img.height = height;
    img.data.assign(static_cast<size_t>(width) * height * 4, 0);
    for (uint32_t f = 0; f < kGuiMaxFrames; ++f) {
        // Obraz dla każdej klatki w locie; treść kopiowana z CPU prosto do obrazu (hostImageCopy), bez bufora pośredniego.
        vk::createImage(device_, pd_, width, height, 1, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_HOST_TRANSFER_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        img.image[f], img.memory[f]);
        img.view[f] = vk::createImageView(device_, img.image[f], VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        vk::hostTransitionToCopyLayout(device_, img.image[f]);
        vk::hostCopyToImage(device_, img.image[f], img.data.data(), width, height);   // start: czarny obraz
        img.uploaded[f] = img.version;
    }
    images_.push_back(std::move(img));
    return static_cast<int>(images_.size()) - 1;
}

void GuiRenderer::updateImage(int id, const std::vector<uint8_t>& rgba) {
    if (id < 0 || id >= static_cast<int>(images_.size())) return;
    GuiImage& img = images_[static_cast<size_t>(id)];
    if (rgba.size() != img.data.size()) return;
    img.data = rgba;
    ++img.version;
}

void GuiRenderer::addImage(int id, float x, float y, float w, float h, float z) {
    if (id < 0 || id >= static_cast<int>(images_.size())) return;
    auto& out = images_[static_cast<size_t>(id)].vertices;
    GuiVertex v;
    v.color[0] = v.color[1] = v.color[2] = 1.0f;
    v.color[3] = 2.0f;   // znacznik dla shadera (text.hlsl): obrazek - kolor RGBA z tekstury, nie maska glifu
    auto put = [&](float px, float py, float u, float t) {
        v.pos[0] = px; v.pos[1] = py; v.pos[2] = z;
        v.texCoord[0] = u; v.texCoord[1] = t;
        out.push_back(v);
    };
    put(x, y, 0, 0); put(x + w, y, 1, 0); put(x + w, y + h, 1, 1);
    put(x, y, 0, 0); put(x + w, y + h, 1, 1); put(x, y + h, 0, 1);
}

// Przed rysowaniem klatki fi (po czekaniu na jej fence - GPU nie używa jej obrazów): zmienione obrazki kopiujemy z CPU.
void GuiRenderer::uploadImages(uint32_t fi) {
    for (auto& img : images_) {
        if (img.uploaded[fi] == img.version) continue;
        vk::hostCopyToImage(device_, img.image[fi], img.data.data(), img.width, img.height);
        img.uploaded[fi] = img.version;
    }
}

void GuiRenderer::build(uint32_t fi) {
    // Obrazki za prostokątami w tym samym buforze wierzchołków; rysowane z własną teksturą (recordDrawCommands).
    const uint32_t rectCount = static_cast<uint32_t>(vertices_.size());
    for (auto& img : images_) {
        img.first[fi] = static_cast<uint32_t>(vertices_.size());
        img.count[fi] = static_cast<uint32_t>(img.vertices.size());
        vertices_.insert(vertices_.end(), img.vertices.begin(), img.vertices.end());
        img.vertices.clear();
    }
    rectCount_ = rectCount;
    if (vertices_.empty()) { vc_ = 0; return; }
    vc_ = (uint32_t)vertices_.size();
    VkDeviceSize sz = sizeof(GuiVertex) * vertices_.size();
    ensureBuffer(sz, fi);
    void* m; vkMapMemory(device_, vbm_[fi], 0, sz, 0, &m);
    std::memcpy(m, vertices_.data(), (size_t)sz);
    vkUnmapMemory(device_, vbm_[fi]);
    vertices_.clear();
}

void GuiRenderer::ensureBuffer(VkDeviceSize sz, uint32_t fi) {
    if (vb_[fi] && sz <= cap_[fi]) return;
    if (vb_[fi]) { vkDestroyBuffer(device_, vb_[fi], nullptr); vkFreeMemory(device_, vbm_[fi], nullptr); }
    cap_[fi] = sz;
    vk::createBuffer(device_, pd_, sz, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vb_[fi], vbm_[fi]);
}

void GuiRenderer::recordDrawCommands(VkCommandBuffer cmd, uint32_t fi) {
    if (vc_ == 0) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    // Prostokąty i tekst: biała tekstura (push descriptors - bez puli i setów).
    const VkDescriptorImageInfo white{sampler_, whiteView_, vk::hostCopyLayout()};
    vk::pushImageSamplers(cmd, pipelineLayout_, 0, &white, 1);
    VkBuffer b[] = {vb_[fi]}; VkDeviceSize o[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, b, o);
    if (rectCount_ > 0) vkCmdDraw(cmd, rectCount_, 1, 0, 0);
    for (auto& img : images_) {
        if (img.count[fi] == 0) continue;
        const VkDescriptorImageInfo info{sampler_, img.view[fi], vk::hostCopyLayout()};
        vk::pushImageSamplers(cmd, pipelineLayout_, 0, &info, 1);
        vkCmdDraw(cmd, img.count[fi], 1, img.first[fi], 0);
    }
}

void GuiRenderer::cleanup() {
    for (auto& img : images_) {
        for (uint32_t f = 0; f < kGuiMaxFrames; ++f) {
            if (img.view[f]) vkDestroyImageView(device_, img.view[f], nullptr);
            if (img.image[f]) vkDestroyImage(device_, img.image[f], nullptr);
            if (img.memory[f]) vkFreeMemory(device_, img.memory[f], nullptr);
        }
    }
    images_.clear();
    for (uint32_t i = 0; i < kGuiMaxFrames; i++) {
        if (vb_[i]) { vkDestroyBuffer(device_, vb_[i], nullptr); vkFreeMemory(device_, vbm_[i], nullptr); vb_[i] = VK_NULL_HANDLE; }
    }
    if (sampler_) { vkDestroySampler(device_, sampler_, nullptr); sampler_ = VK_NULL_HANDLE; }
    if (whiteView_) { vkDestroyImageView(device_, whiteView_, nullptr); whiteView_ = VK_NULL_HANDLE; }
    if (whiteImage_) { vkDestroyImage(device_, whiteImage_, nullptr); vkFreeMemory(device_, whiteMem_, nullptr); whiteImage_ = VK_NULL_HANDLE; }
    if (pipeline_) { vkDestroyPipeline(device_, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_) { vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
    if (dsl_) { vkDestroyDescriptorSetLayout(device_, dsl_, nullptr); dsl_ = VK_NULL_HANDLE; }
    ready_ = false;
}

} // namespace engine3d
