# changelog

## 0.6.0

Implementacja Volumetrycznej Mgły (Froxel-based)

assets/shaders/fog/fog_volume.hlsl — Compute shader (160×90×64 froxli):
Każda grupa wątków (1,1,64) przetwarza jeden tile ekranu we wszystkich 64 wycinkach głębi
Rekonstrukcja pozycji świata z invViewProj  
Height-based density z wykładniczym zanikaniem + proceduralny noise 3D (sin)
Integracja wzdłuż osi Z (near→far) przez groupshared memory: gromadzi rozproszone światło i transmisję
Wynik: 3D tekstura R16G16B16A16_SFLOAT (RGB = światło rozproszone, A = transmisja)
assets/shaders/fog/fog_composite.hlsl — Fullscreen triangle:
Czyta depth z bindless (ocean_scene_depth)
Próbkuje volume jako Texture3D z set=1
Output: float4(światło_rozproszone, 1-transmisja) → blend ONE/ONE_MINUS_SRC_ALPHA
engine/feature/fog/volumetric_fog_renderer.h/.cpp — IRenderFeature:
Tworzy 3D image (storage + sampled), image view (3D), sampler (linear, clamp)
Compute pipeline: set=0 → storage image, push constants (124 byte: invViewProj, cameraPos, fogColor, lightDir, params)
Fullscreen pipeline: set=0 (bindless renderer) + set=1 (combined fog volume), push constant (depth tex index)
W render(): przejście volume→GENERAL → dispatch compute → barrier GENERAL→SHADER_READ → fullscreen draw

## 0.5.0

 - system oparty na ECS (Entity-Component-System)
 - Nowy  JobSystem  (job_system.h
  Prosta, lekka pula wątków. Przy starcie tworzy  N-1  wątków roboczych (N = liczba rdzeni logicznych CPU), które czekają na zadania w kolejce. Posiada dwie kluczowe metody:
  •  submit(fn)  – zwraca  std::future<> , wątek roboczy wykonuje zadanie asynchronicznie
  •  parallelFor(count, fn)  – automatycznie dzieli zakres na chunki i rozdaje je wątkom, blokując wywołującego aż do zakończenia 
 2.  RenderExtractionSystem  – równoległe zbieranie  RenderPacket  (render_extraction_system.cpp)   To jest największy potencjalny zysk przy 200+ encjach drzew i trawy. Teraz:
  • Faza 1 (single-thread): rozwiązanie materiałów (modyfikuje stan komponentu)
  • Faza 2 (multi-thread):  collectRenderPackets()  +  resolvePacketAssetHandles()  na każdym wątku we własnym lokalnym buforze  std::vector<RenderPacket>  → zero rywalizacji o mutex      
  • Faza 3 (single-thread): scalenie buforów lokalnych do wektora sceny przez  std::move 
 
 3.  RenderFramePrepSystem  – równoległe obliczanie odległości do kamery (render_frame_prep_system.cpp)
  
  Obliczanie  distanceToCamera  dla każdej z 200+ paczek podzielone na chunki i wykonywane równolegle. Brak współdzielenia danych → brak muteksów.
  
 4. MD2 + GLTF animacje – równoległe obliczanie kości i interpolacji (engine.cpp)
  
  Każda instancja goblina MD2 (interpolacja wierzchołków CPU) i każda instancja GLTF BrainStem (ewaluacja animacji + upload macierzy kości) teraz startuje jako osobne zadanie na  JobSystem
  . Wyniki zbierane przez  std::future::get()  a upload do GPU ( updateMesh ) wykonywany jest po zebraniu wyników na głównym wątku.

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
