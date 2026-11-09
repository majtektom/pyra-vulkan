#pragma once

#include <vulkan/vulkan.h>

#include "render/render_pass.h"
#include "render/render_types.h"

namespace engine3d {

/**
 * @brief Rysuje jedną paczkę geometrii (RenderPacket) podanym potokiem.
 *
 * Wspólne dla przebiegów: nieprzezroczystego, przezroczystego i wstępnego zapisu głębokości.
 * Binduje globalny descriptor set (gdy zmienia się layout potoku - boundLayout zapamiętuje ostatni),
 * kości animacji, bufory wierzchołków/instancji/indeksów, wypełnia Push Constants i wywołuje draw.
 */
void drawRenderPacket(VkCommandBuffer commandBuffer, const RenderPacket& packet, const VulkanPipeline& pipeline,
                      VkDescriptorSet globalDescriptorSet, VkPipelineLayout& boundLayout);

} // namespace engine3d
