# Pipeline Vulkan — Inicjalizacja i Nowoczesny Rendering

Silnik `engine3d` wykorzystuje nowoczesne podejście do API Vulkan (wymagana wersja 1.4), rezygnując z klasycznych Render Passów i Framebufferów na rzecz **Dynamic Renderingu** oraz **Bindless Textures**.

## Inicjalizacja Vulkan 1.4

Proces startu silnika konfiguruje zaawansowane funkcje urządzenia:

```
VulkanContext::initialize()
├─ createInstance()         # Vulkan 1.4 (starszy loader -> czytelny błąd), Debug Messenger
├─ pickPhysicalDevice()     # GPU z Vulkan 1.4 i wymaganymi cechami; karta dedykowana ma pierwszeństwo
│                           # wybór układu dla obrazów kopiowanych z CPU (vk::hostCopyLayout)
└─ createLogicalDevice()    # Cechy: dynamicRendering, synchronization2, timelineSemaphore, descriptorIndexing,
                            # pushDescriptor, hostImageCopy (+ maintenance5/6 i inne z 1.4, gdy są)
```

Z Vulkana 1.4 silnik korzysta wprost, bez wersji zapasowych dla starszych sterowników:
- **Synchronizacja 2** (`vk::imageBarriers`): każda bariera ma etapy i dostępy przy swoim obrazie.
- **Push descriptors** (`vk::pushImageSamplers`): deskryptory zmieniane przy każdym rysowaniu (G-Buffer dla
  oświetlenia, obrazki GUI) podaje się wprost w buforze poleceń - bez puli i setów osobnych na każdą klatkę.
- **Kopiowanie obrazów z CPU** (`vk::hostCopyToImage`, hostImageCopy): mgła wojny i obrazki GUI (minimapa) trafiają
  do obrazu bez bufora pośredniego, poleceń GPU i barier.

### Nowoczesne Funkcje (Features)
- **Dynamic Rendering**: Pozwala na używanie `vkCmdBeginRendering` zamiast predefiniowanych obiektów `VkRenderPass`.
- **Timeline Semaphores**: Uproszczona synchronizacja CPU-GPU i między kolejkami przy użyciu wartości monotonicznych.
- **Descriptor Indexing (Bindless)**: Możliwość odwoływania się do ogromnej liczby tekstur w shaderze poprzez indeksy, bez konieczności przełączania Descriptor Setów per-obiekt.

---

## Architektura Deferred Shading (PBR)

Silnik wykorzystuje potok odroczony (Deferred Shading), co pozwala na wydajne oświetlanie sceny wieloma źródłami światła.

### 1. G-Buffer Pass
W tej fazie geometria sceny jest rysowana do zestawu tekstur pomocniczych (G-Buffer):
- **Attachment 0**: Albedo (RGB) + Occlusion (A)
- **Attachment 1**: Normalne (World Space RGB)
- **Attachment 2**: Metalness (R) + Roughness (G) + Emissive (B)
- **Attachment 3**: Pozycja / Dane specjalne

### 2. Shadow Pass
Generowanie map cieni dla aktywnych świateł. Silnik wspiera do 16 świateł z mapowaniem cieni przy użyciu tablic tekstur (`Texture Arrays`).

### 3. Lighting Pass (Deferred)
Końcowe oświetlenie obliczane jest na pełnoekranowym prostokącie (quad). Shader pobiera dane z G-Buffera i map cieni, wyliczając finalny kolor zgodnie z modelem PBR (Physically Based Rendering).

---

## System Bindless Textures

Zamiast tradycyjnego wiązania jednej tekstury do jednego slotu, silnik wykorzystuje jeden duży layout deskryptorów:
- **Binding 0**: Global UBO (View, Proj, Lights).
- **Binding 1**: Tablica samplerów i tekstur (do 1024 slotów).

W `RenderPacket` przesyłane są tylko indeksy (`uint32_t`) do tablicy bindless, co eliminuje kosztowne operacje `vkUpdateDescriptorSets` i `vkCmdBindDescriptorSets` podczas rysowania geometrii.

---

## Zarządzanie Pamięcią (VMA)

Silnik integruje **Vulkan Memory Allocator (VMA)**, co automatyzuje:
- Dobór odpowiedniego typu pamięci (Device Local, Host Visible).
- Defragmentację i podział dużych bloków pamięci GPU.
- Mapowanie buforów dla szybkiego przesyłania danych (np. przez `VulkanBuffer::upload`).

---

## Synchronizacja i Timeline Semaphores

Wykorzystanie `Timeline Semaphores` pozwala na precyzyjne śledzenie postępu GPU. Zamiast czekać na binarny stan (Fence), CPU może oczekiwać na konkretną wartość semafora, co pozwala na lepsze nakładanie pracy CPU i GPU w systemach asynchronicznych.

```cpp
// Przykład synchronizacji klatki
VkSemaphoreWaitInfo waitInfo{};
waitInfo.pSemaphores = &renderTimelineSemaphores_[currentFrame_];
waitInfo.pValues = &timelineCounters_[currentFrame_];
vkWaitSemaphores(device, &waitInfo, UINT64_MAX);
```

---

## Tworzenie potoków graficznych (`render/pipeline_builder.h`)

Potok graficzny (`VkPipeline`) to "zamrożony" komplet ustawień rysowania: shadery, układ wierzchołków, topologia,
rasteryzacja, głębokość, mieszanie kolorów i formaty celów. W czystym Vulkanie to ~120 linii struktur na potok.
Silnik ma do tego budowniczego, który podaje tylko różnice od ustawień domyślnych:

```cpp
const vk::ShaderModule vert(device, dir + "skybox.vert.spv");   // RAII: moduł zwalniany na końcu funkcji
const vk::ShaderModule frag(device, dir + "skybox.frag.spv");

layout_ = vk::createPipelineLayout(device, {descriptorSetLayout}, sizeof(PushConstants),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, "skybox");
pipeline_ = vk::GraphicsPipelineBuilder()
    .shaders(vert, frag)                                   // vertexMain / fragmentMain
    .vertexInput(Vertex::bindingDescription(),
                 vk::filterVertexAttributes(vert.code(), Vertex::attributeDescriptions()))
    .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
    .cull(VK_CULL_MODE_NONE)                               // jesteśmy wewnątrz kostki nieba
    .depth(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)       // test bez zapisu - niebo zostaje w tle
    .colorFormat(colorFormat)                              // kolejne wywołanie = kolejny cel (G-Buffer ma 4)
    .depthFormat(depthFormat)
    .build(device, layout_, "skybox");
```

Ustawienia domyślne: trójkąty, wypełnianie, cull BACK z przodem CCW, test i zapis głębokości (LESS), brak
mieszania, viewport i scissor jako stan dynamiczny. Mieszanie kolorów: `vk::BlendMode::Alpha`, `Premultiplied`
(mgła), `Additive` (ogień); nietypowe przez `customBlend`. Teselacja: `.patches(3)` + etapy hull/domain przez
`.shader(...)`. Drugi bufor wierzchołków (kości `SkinVertex`, dane instancji cząsteczek) - kolejne `vertexInput`.
Ten sam builder można zbudować dwa razy (mapy cieni: wariant światła lokalnego i słońca z `depthClamp`).

## Double Buffering i Dynamic State

Dla zapewnienia płynności, zasoby takie jak Uniform Buffery i obrazy G-Buffera są duplikowane (`kMaxFrames = 2`). Stan taki jak **Viewport** i **Scissor** jest ustawiany dynamicznie w każdej klatce, co pozwala na bezproblemową zmianę rozmiaru okna bez niszczenia pipeline'ów.
