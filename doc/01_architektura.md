# Architektura silnika engine3d

## Filozofia projektowa

Silnik `engine3d` to edukacyjny silnik 3D napisany w C++20 z użyciem Vulkan API. Jego celem jest
pokazanie **jak zbudować własny silnik graficzny od zera** — od inicjalizacji Vulkan,
przez ładowanie modeli, system materiałów, shaderów HLSL, po renderowanie tekstu i cząstek.

Silnik nie używa zewnętrznych bibliotek poza:
- **Vulkan 1.1** — API graficzne niskiego poziomu
- **SDL3** — tworzenie okna, obsługa wejścia (klawiatura, mysz)
- **SDL3_ttf** — rasteryzacja czcionek TTF
- **SDL3_image** — ładowanie obrazów (zarezerwowane na przyszłość)
- **SDL3_sound** — odtwarzanie dźwięku (zarezerwowane na przyszłość)

Cała reszta — matematyka 3D, zarządzanie pamięcią, system komponentów — jest napisana od zera.

---

## Struktura katalogów

```
przykład_vulkan/
├── engine/                  # kod źródłowy silnika
│   ├── core/                # pętla główna, encje, komponenty, typy, matematyka
│   ├── render/              # system renderowania Vulkan
│   │   ├── backend/         # kontekst Vulkan, swapchain, bufory GPU
│   │   ├── material_system  # system materiałów i pipeline'ów
│   │   ├── model_3ds        # loader modeli 3DS
│   │   └── ...              # shadery, tekstury, czcionki, cząstki
│   ├── camera/              # kamera i kontroler FPS
│   ├── platform/            # okno SDL3 + obsługa wejścia
│   ├── scene/               # zarządzanie sceną
│   └── input/               # (usunięte — input jest w platform.cpp)
├── examples/                # przykładowe aplikacje
│   ├── flyaround/           # latanie po scenie z modelami 3DS
│   ├── rotating_cube/       # obracające się kostki
│   ├── rotating_triangle/   # obracający się trójkąt
│   └── particles/           # system cząstek
├── assets/                  # zasoby: modele 3DS, tekstury DDS/BMP/TGA, shadery HLSL
├── doc/                     # dokumentacja (ten katalog)
└── cmake/                   # skrypty CMake (w tym kompilacja shaderów)
```

---

## Przepływ danych — od pliku do piksela

Oto jak silnik przetwarza dane w każdej klatce:

```
1. ZAŁADUJ ZASOBY (raz, przy starcie)
   Model 3DS (.3DS) ──► Model3ds::load() ──► MeshData (wierzchołki + indeksy)
   Tekstura (.DDS/.BMP) ──► TextureAssetLoader ──► VkImage + VkImageView
   Shader (.hlsl) ──► DXC ──► SPIR-V (.spv) ──► VkShaderModule

2. GŁÓWNA PĘTLA (co klatkę)
   ┌─ Engine::run()
   │
   ├─ calculateDeltaTimeMs()    # oblicz delta time i FPS
   ├─ update(dt)               # aktualizuj logikę
   │  ├─ updateCameraControls() # ruchy kamery (mysz + klawisze)
   │  ├─ onUpdate(dt)          # callback użytkownika
   │  └─ entity->update(dt)    # aktualizuj wszystkie encje
   │
   └─ renderFrame()            # renderuj scenę
      ├─ zbierz RenderPacket z MeshComponent
      ├─ oblicz odległość od kamery (do sortowania przezroczystości)
      ├─ renderer->render()
      │  ├─ vkBeginRenderPass
      │  ├─ OpaquePass   – nieprzezroczyste obiekty
      │  ├─ TransparentPass – przezroczyste (sortowane od tyłu)
      │  ├─ Skybox       – tło nieba
      │  ├─ Grid         – siatka pomocnicza
      │  ├─ TextOverlay  – tekst (FPS, UI)
      │  └─ vkEndRenderPass
      └─ swapchain->present()
```

---

## Warstwy abstrakcji

Silnik ma 3 główne warstwy:

| Warstwa | Odpowiedzialność | Przykład |
|---------|-----------------|----------|
| **Core** | Pętla gry, encje/komponenty, typy | `Engine`, `Entity`, `MeshComponent` |
| **Render** | Wszystko związane z GPU | `MaterialSystem`, `Model3ds`, `VulkanSwapchain` |
| **Platform** | Okno SDL3, wejście, czcionki | `Platform`, `FontAtlasLoader` |

Każda warstwa zależy tylko od warstw poniżej:
```
Core ──► Render ──► Platform
```

---

## Kluczowe koncepcje

### Entity + Component (wzorzec ECS light)
Encja (`Entity`) to kontener na komponenty. Komponenty (`MeshComponent`, `TransformComponent`, `CameraComponent`) definiują zachowanie. Nie ma systemów — aktualizacja jest bezpośrednia.

### Material + Pipeline
`Material` definiuje wygląd obiektu (tekstura, shader, przezroczystość, culling).
`MaterialSystem` tworzy potoki Vulkan (`VkPipeline`) na podstawie właściwości materiału.
Pipeline'y są cache'owane — ten sam zestaw parametrów = ten sam pipeline.

### RenderPacket
`RenderPacket` to jednostka rysowania: bufor wierzchołków + indeksów, macierz modelu,
materiał. Pakiety są zbierane z komponentów, sortowane (nieprzezroczyste → przezroczyste)
i wysyłane do GPU.

### Push Constants
Dane per-obiekt (macierz modelu, kolor) są przesyłane przez `vkCmdPushConstants` —
szybka ścieżka w Vulkan, szybsza niż osobny bufor uniform dla każdego obiektu.

### Descriptor Sets
Globalne dane (macierze widoku/projekcji, tekstury) są w `VkDescriptorSet`.
Binding 0 = UBO (view + proj), Binding 1 = sampler tekstury.
Każda tekstura ma własny descriptor set per-klatka (double buffering).

---

## Wątek główny vs GPU

Silnik jest jednowątkowy na CPU (oprócz systemu cząstek, który używa `std::async`).

```
CPU: Update() ──► Render() ──► Present()
GPU:            ──► [renderuje poprzednią klatkę] ──►

Synchronizacja przez VkFence (CPU czeka na GPU co 2 klatki przez double buffering).
```

---

## Format współrzędnych

- **Świat 3D**: prawoskrętny, Y=góra, X=prawo, Z=przód (OpenGL-style)
- **Vulkan NDC**: X=[-1,1] lewo→prawo, Y=[-1,1] góra→dół, Z=[0,1]
- **Tekstury**: UV [0,1], (0,0)=góra-lewo
- **3DS → engine**: współrzędne są konwertowane: Y_3ds→Z_engine, Z_3ds→-Y_engine
