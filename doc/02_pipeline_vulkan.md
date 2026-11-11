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

## Double Buffering i Dynamic State

Dla zapewnienia płynności, zasoby takie jak Uniform Buffery i obrazy G-Buffera są duplikowane (`kMaxFrames = 2`). Stan taki jak **Viewport** i **Scissor** jest ustawiany dynamicznie w każdej klatce, co pozwala na bezproblemową zmianę rozmiaru okna bez niszczenia pipeline'ów.
