# Pipeline Vulkan — inicjalizacja i renderowanie

## Inicjalizacja Vulkan (co się dzieje przy starcie)

Proces inicjalizacji Vulkan składa się z następujących kroków:

```
VulkanContext::initialize()
├─ createInstance()         # VkInstance + warstwy walidacyjne
├─ createSurface()          # VkSurfaceKHR przez SDL_Vulkan_CreateSurface
├─ pickPhysicalDevice()     # wybór GPU
├─ createLogicalDevice()    # VkDevice + kolejki (graphics + present)
└─ createCommandPool()      # pula buforów komend
```

### 1. createInstance()
Tworzy instancję Vulkan z rozszerzeniami:
- `VK_EXT_DEBUG_UTILS_EXTENSION_NAME` — komunikaty walidacyjne
- Rozszerzenia powierzchni pobierane automatycznie przez `SDL_Vulkan_GetInstanceExtensions()`
  (SDL3 samo dobiera odpowiednie rozszerzenia dla platformy: X11, Wayland, Windows, macOS)

Jeśli dostępna jest warstwa `VK_LAYER_KHRONOS_validation`, jest automatycznie włączana.
Błędy walidacji są wypisywane na `stderr`.

### 2. createSurface()
Tworzy `VkSurfaceKHR` przez `SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)`.
SDL3 automatycznie obsługuje różnice między platformami — nie trzeba ręcznie
wybierać między `VK_KHR_XLIB_SURFACE` a `VK_KHR_WAYLAND_SURFACE`.

### 3. pickPhysicalDevice()
Wylicza dostępne GPU i wybiera pierwsze, które obsługuje:
- Kolejkę graficzną (`VK_QUEUE_GRAPHICS_BIT`)
- Prezentację na powierzchni (`vkGetPhysicalDeviceSurfaceSupportKHR`)

### 4. createLogicalDevice()
Tworzy `VkDevice` — logiczne urządzenie Vulkan. Żąda:
- Jednej kolejki graficznej i jednej prezentacji (mogą być tą samą)
- Rozszerzenia `VK_KHR_SWAPCHAIN_EXTENSION_NAME` (wymagane do wyświetlania)

### 5. createCommandPool()
Tworzy pulę buforów komend dla kolejki graficznej z flagą `VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT`
(umożliwia resetowanie buforów indywidualnie).

---

## Swapchain — łańcuch wymiany obrazów

`VulkanSwapchain` zarządza obrazami, które są wyświetlane na ekranie:

```
VulkanSwapchain::recreate()
├─ createSwapchain()        # VkSwapchainKHR (min 2 obrazy, format B8G8R8A8_SRGB)
├─ createImageViews()       # VkImageView dla każdego obrazu swapchain
├─ createDepthResources()   # osobny bufor głębi (D32_SFLOAT lub D24_UNORM)
├─ createRenderPass()       # VkRenderPass z 2 attachmentami (kolor + głębia)
└─ createFramebuffers()     # VkFramebuffer dla każdego obrazu
```

### Double buffering
Swapchain ma minimum 2 obrazy (`minImageCount >= 2`). Podczas gdy GPU renderuje do
jednego, drugi jest wyświetlany. Silnik używa `kMaxFrames = 2` (double buffering).

Synchronizacja między CPU a GPU:
- `VkSemaphore` — `imageAvailable` (obraz gotowy do renderowania), `renderFinished` (renderowanie zakończone)
- `VkFence` — CPU czeka na zakończenie renderowania przed rozpoczęciem nowej klatki

### Render Pass
Render pass definiuje:
- **Attachment 0**: kolor (`VK_FORMAT_B8G8R8A8_SRGB`, clear, store)
- **Attachment 1**: głębia (`VK_FORMAT_D32_SFLOAT`, clear, don't care)

Wszystkie obiekty w scenie są rysowane w tym samym render passie.

---

## Proces renderowania jednej klatki

```
Renderer::render()
│
├─ vkWaitForFences()              # czekaj aż GPU skończy poprzednią klatkę
├─ swapchain->acquireNextImage()  # pobierz indeks obrazu do renderowania
├─ vkResetFences()                # zresetuj fence dla tej klatki
├─ updateUniformBuffer(camera)    # aktualizuj UBO (view + proj)
│
├─ vkBeginCommandBuffer()
├─ vkCmdBeginRenderPass()         # rozpocznij render pass (clear kolor + głębia)
│
├─ vkCmdSetViewport() / vkCmdSetScissor()
│
├─ OpaquePass::execute()          # renderuj nieprzezroczyste
│   └─ dla każdego RenderPacket:
│       ├─ vkCmdBindPipeline()    # pipeline zależny od materiału
│       ├─ vkCmdBindDescriptorSets()  # UBO + tekstura
│       ├─ vkCmdBindVertexBuffers() / vkCmdBindIndexBuffer()
│       ├─ vkCmdPushConstants()   # macierz modelu + kolor
│       └─ vkCmdDrawIndexed()     # rysuj!
│
├─ TransparentPass::execute()     # renderuj przezroczyste (sortowane back-to-front)
│
├─ Skybox (opcjonalnie)           # tło nieba (depth=LEQUAL, depth write OFF)
├─ Grid (opcjonalnie)             # siatka pomocnicza (LINE_LIST)
├─ TextOverlay (opcjonalnie)      # tekst UI na wierzchu (depth test OFF, blend ON)
│
├─ vkCmdEndRenderPass()
├─ vkEndCommandBuffer()
│
├─ vkQueueSubmit()                # wyślij komendy do GPU + zasygnalizuj fence
└─ swapchain->present()           # wyświetl obraz
```

### Kolejność rysowania
1. **OpaquePass** — nieprzezroczyste obiekty (dowolna kolejność — depth test sortuje)
2. **TransparentPass** — przezroczyste obiekty (posortowane od najdalszego do najbliższego)
3. **Skybox** — renderowany po scenie dla optymalizacji (early depth test odrzuca ukryte piksele)
4. **Grid** — siatka pomocnicza
5. **TextOverlay** — tekst UI zawsze na wierzchu

---

## Uniform Buffer Object (UBO)

Dane globalne dla wszystkich obiektów w scenie:

```cpp
struct GlobalUBO {
    Matrix4 view;   // macierz widoku (z kamery)
    Matrix4 proj;   // macierz projekcji (perspektywiczna)
};
```

UBO jest mapowany w pamięci (`vkMapMemory`) i aktualizowany przez `memcpy` co klatkę.
Każda klatka ma własny bufor UBO (double buffering).

Macierz projekcji jest tworzona przez `perspective(fov, aspect, near, far)`:
```cpp
Matrix4 perspective(float fovYRad, float aspect, float near, float far);
```

---

## Przesyłanie danych do shaderów

Silnik używa dwóch mechanizmów przesyłania danych do GPU:

### 1. Descriptor Sets (dane globalne)
- **Binding 0**: Uniform Buffer z `view` + `proj` (VERTEX stage)
- **Binding 1**: Combined Image Sampler z teksturą (FRAGMENT stage)

Descriptor sety są tworzone per-klatka i per-tekstura. Gdy obiekt nie ma tekstury,
używana jest biała tekstura 1x1 (fallback).

### 2. Push Constants (dane per-obiekt)
Przesyłane przez `vkCmdPushConstants` przed każdym draw call:
```cpp
struct PushConstants {
    Matrix4 model;                // macierz modelu (64 bajty)
    std::array<float, 4> color;  // kolor materiału (16 bajtów)
};
```

Push constants są najszybszym sposobem przekazywania małych ilości danych w Vulkan
(nie wymagają deskryptorów ani buforów).

---

## Synchronizacja CPU-GPU

```
Klatka N:
  CPU: update() → render() → vkQueueSubmit(fence_N) → present()
  GPU:                        [renderuje klatkę N] → sygnalizuje fence_N

Klatka N+1:
  CPU: vkWaitForFences(fence_N) → update() → render() → ...
  GPU: (fence_N zasygnalizowany — CPU może kontynuować)
```

`vkWaitForFences` blokuje CPU do momentu, aż GPU zakończy renderowanie poprzedniej klatki.
Dzięki temu CPU nie wyprzedza GPU o więcej niż 2 klatki.

---

## Tworzenie pipeline'u graficznego

Pipeline Vulkan (`VkPipeline`) to "przepis" na renderowanie. Zawiera wszystkie stany:

| Stan | Ustawienie w silniku |
|------|---------------------|
| Topologia | `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST` |
| Polygon mode | `VK_POLYGON_MODE_FILL` |
| Cull mode | zależny od materiału (Back/Disabled/Front) |
| Front face | `VK_FRONT_FACE_COUNTER_CLOCKWISE` |
| Depth test | zależny od materiału (zwykle VK_TRUE) |
| Depth write | zależny od materiału |
| Blend mode | Disabled dla nieprzezroczystych, Alpha dla przezroczystych |
| Dynamic states | Viewport + Scissor (ustawiane dynamicznie) |

Pipeline'y są cache'owane w `MaterialSystem::pipelineCache_` — kluczem jest
kombinacja: shader, blend mode, depth test, depth write, cull mode.
