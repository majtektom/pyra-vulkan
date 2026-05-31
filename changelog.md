# changelog

## 0.4.0

system obiektów oparty na ECS (Entity-Component-System) z domieszką Data-Oriented Design
Shadow mapping — mapa cieni 2048×2048 (depth-only pass przed głównym renderingiem), PCF 3×3 dla miękkich cieni

## 0.3.2

Dynamiczne światło — Physically Based Rendering (PBR) oraz Deferred Shading (z użyciem G-Buffera)
Przykład 07_shadows — scena z podłogą + budynkami, światło krążące wokół sceny, włączone cienie, sterowanie WASD + mysz

Flow renderowania

1. G-Buffer pass      → 4 tekstury RGBA16F + depth
2. Bariera            → G-Buffer write → read
3. Swapchain render:
   a. Skybox/features → swapchain color + depth (LOAD depth z G-Buffer)
   b. Deferred pass   → fullscreen triangle, czyta G-Buffer, liczy PBR
   c. Transparent     → forward rendering
   d. Overlay         → text/GUI
4. Present
   G-Buffer (4 tekstury RGBA16F)
   RT	Zawartość
   0	baseColor.rgb + metallic
   1	worldNormal (packed) + roughness
   2	ao + emissiveStrength + pad
   3	worldPos.xyz

## 0.3.1

Dodane funkcje

1. Timeline Semaphores (Vulkan 1.3)
   - Włączona obsługa timelineSemaphore w vulkan_context.cpp:228 (VkPhysicalDeviceVulkan12Features)
   - Tryb Timeline zastępuje binarne semafory + fence'y timeline'owymi semaforami (renderer_core.cpp):
   - renderTimelineSemaphores_[2] – semafory timeline do synchronizacji CPU-GPU (vkWaitSemaphores zamiast vkWaitForFences)
   - presentReadySemaphores_[2] – binarne semafory do prezentacji (bo vkQueuePresentKHR wymaga binarnych)
   - VkTimelineSemaphoreSubmitInfo użyte w vkQueueSubmit do sygnalizowania wartości timeline
2. Async Compute Queue
   - vulkan_context.h/cpp – wykrywanie osobnej kolejki compute (jeśli GPU ją wspiera)
   - computeQueue_, computeCommandPool_, computeQueueFamily_ dostępne przez VulkanContext
3. Kontrola synchronizacji (tryby)

engine->setSynchronizationMode(SynchronizationMode::Timeline);  // Timeline semaphores
engine->setSynchronizationMode(SynchronizationMode::Standard);  // Binary semaphores + fences (domyślnie)
engine->setSynchronizationMode(SynchronizationMode::Off);       // Brak fence'ów, maks. wydajność
engine->setPresentMode(PresentMode::Immediate);  // V-Sync OFF (tearing)
engine->setPresentMode(PresentMode::Mailbox);    // V-Sync OFF (bez tearingu)
engine->setPresentMode(PresentMode::Fifo);       // V-Sync ON (domyślnie)

## 0.3.0

Bindless Rendering, Wszystkie zasoby (tekstury, bufory) są dostępne w jednym, ogromnym "tablicowym" deskryptorze. W shaderze po prostu pobierasz indeks materiału.

## 0.2.0

dodane , skydome, sprite, parciple

## 0.1.0

dodane terenu3d, modeli 3ds, md2, gltf

## 0.0.1

namiasta silnika, obsługa fontów, skybox, grid
