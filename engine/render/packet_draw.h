#pragma once

#include <vulkan/vulkan.h>

#include "render/render_pass.h"
#include "render/render_types.h"

namespace engine3d {

/**
 * @brief Pamięć "co jest teraz zbindowane" w buforze poleceń - pomija powtórne bindowanie tego samego.
 *
 * Każde vkCmdBind* kosztuje czas CPU (sterownik zapisuje polecenie i oznacza stan do ponownego sprawdzenia),
 * nawet gdy podajemy to samo co przed chwilą. Przy tysiącu paczek posortowanych po potoku i siatce większość
 * bindowań się powtarza - ta struktura je pomija.
 *
 * Zasady Vulkana, na których to polega:
 *  - zbindowanie potoku NIE kasuje descriptor setów ani buforów wierzchołków/indeksów,
 *  - descriptor set pozostaje ważny dla potoku o zgodnym layoucie; ostrożnie traktujemy każdą zmianę
 *    VkPipelineLayout jak niezgodną: set 0 bindujemy wtedy od nowa, a set 1 (kości) zapominamy.
 * Tworzymy ją na początku przebiegu (stan bufora poleceń jest wtedy nieznany) - reset() robi to samo.
 */
struct DrawBindCache {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;   // layout, z którym zbindowano set 0
    VkDescriptorSet set0 = VK_NULL_HANDLE;
    VkDescriptorSet set1 = VK_NULL_HANDLE;
    VkBuffer vertex[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkBuffer index = VK_NULL_HANDLE;
    uint32_t skipped = 0;                       // ile bindowań pominięto (do profilera)

    void reset() { *this = DrawBindCache{}; }
    void bindPipeline(VkCommandBuffer cmd, VkPipeline p);
    // Set 0 (UBO klatki + tekstury bindless) w layoucie potoku, którym zaraz rysujemy.
    void bindGlobalSet(VkCommandBuffer cmd, VkPipelineLayout l, VkDescriptorSet set);
    // Set 1 (kości animacji) - wołać po bindGlobalSet z tym samym layoutem.
    void bindSet1(VkCommandBuffer cmd, VkPipelineLayout l, VkDescriptorSet set);
    void bindVertexBuffer(VkCommandBuffer cmd, uint32_t slot, VkBuffer buffer);
    void bindIndexBuffer(VkCommandBuffer cmd, VkBuffer buffer);
};

/**
 * @brief Rysuje jedną paczkę geometrii (RenderPacket) podanym potokiem.
 *
 * Wspólne dla przebiegów: nieprzezroczystego, przezroczystego i wstępnego zapisu głębokości.
 * Binduje (przez DrawBindCache - tylko to, co się zmieniło) globalny descriptor set,
 * kości animacji, bufory wierzchołków/instancji/indeksów, wypełnia Push Constants i wywołuje draw.
 */
void drawRenderPacket(VkCommandBuffer commandBuffer, const RenderPacket& packet, const VulkanPipeline& pipeline,
                      VkDescriptorSet globalDescriptorSet, DrawBindCache& bound);

} // namespace engine3d
