# Słownik pojęć i struktur

Ten dokument odpowiada na pytanie **„czym jest ta struktura i co robi w silniku?”**
Dokumenty modułów pokazują użycie i działanie; tutaj jest słownik nazw, które
pojawiają się w kodzie i w pozostałych rozdziałach.

Legenda:

* **struktura** — dane (np. `RenderPacket`, `Material`, `TransformComponent`),
* **klasa** — obiekt z zachowaniem (np. `MeshManager`, `Renderer`),
* **wewnętrzna** — detal implementacji, nie część publicznego API,
* **helper** — obiekt tworzony na chwilę, żeby uprościć kod (np.
  `GraphicsPipelineBuilder`, `MeshBuilder`).

---

## 1. Fundament: pętla, wejście, narzędzia

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `Engine` | Fasada i orkiestrator całego silnika | Tworzy podsystemy, prowadzi pętlę `run()`, woła `onUpdate`, aktualizuje scenę i renderuje klatkę | `core/engine.h` |
| `Platform` | Warstwa nad SDL3 | Okno, zdarzenia, migawka wejścia, pełny ekran, przechwytywanie kursora | `platform/platform.h` |
| `InputSnapshot` | Zamrożony stan klawiszy i myszy na jedną klatkę | Logika czyta spójny stan przez całą klatkę; `previousInput_` pozwala wykryć „wciśnięto teraz” | `platform/platform.h` |
| `InputActionMap` | Mapa nazwa akcji → lista scancodów | Gra pyta o intencję (`move.forward`), a nie o konkretny klawisz | `core/input_actions.h` |
| `GameSettings` | Ustawienia gracza (dźwięk, okno, VSync, FPS, cienie) | Zapis/odczyt JSON i zastosowanie do `Engine`/`AudioSystem` | `core/game_settings.h` |
| `JobSystem` | Pula wątków roboczych | `parallelFor` dzieli pracę (ekstrakcja paczek, animacje, cząsteczki); `currentSlot()` daje indeks bufora „na wątek” | `core/job_system.h` |
| `Profiler` / `ProfileScope` | Licznik czasu sekcji klatki | `ENGINE_PROFILE=1` pokazuje, gdzie idzie czas CPU | `core/profiler.h` |
| `Logger` / `LogLevel` | System logowania z backendem | Komunikaty `INFO/WARN/ERROR`; ostatnie wpisy trafiają do raportu awarii | `core/logger.h` |
| `ValueNoise` | Szum wartości 2D + fbm | Proceduralny teren, rozmieszczanie lasów, chmury; ten sam seed = ten sam wynik | `core/noise.h` |
| `Vec3`, `Quaternion`, `Matrix4` | Podstawowa matematyka 3D | Pozycje, obroty, macierze widoku/projekcji; `Matrix4` w układzie kolumnowym | `core/math.h` |

---

## 2. Scena i ECS

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `EntityId` | Stabilny `uint64_t` | Identyfikuje encję niezależnie od realokacji wektora; używaj go zamiast długo trzymanych `Entity*` | `core/types.h` |
| `Entity` | Pojemnik na komponenty + nazwa + ID | Sam nie ma logiki; jest „haczykiem” na dane i komponenty | `core/entity.h` |
| `Component` | Bazowa klasa komponentu | `initialize/update`, właściciel (`owner`), flaga `active`; kompozycja zamiast dziedziczenia | `core/component.h` |
| `TransformComponent` | Pozycja, rotacja, skala + macierz świata | `TransformSystem` liczy `world = parent.world * local` | `core/transform_component.h` |
| `MeshComponent` | Siatka + materiał + warstwy terenu | Mówi rendererowi, co narysować; przechowuje `MaterialInstance` i bufory kości | `core/mesh_component.h` |
| `LightComponent` | Typ, kolor, intensywność, zasięg, kąty | `LightSystem` zamienia go na `SceneLight` dla renderera | `core/light_component.h` |
| `CameraComponent` | FOV, near/far, target, up, aspect | `CameraExtractionSystem` liczy z niego `RenderView` | `camera/camera_component.h` |
| `HierarchyComponent` | `parent` + `children` | Relacja rodzic-dziecko; transformacje i usuwanie drzewa | `core/hierarchy_component.h` |
| `TerrainComponent` | `shared_ptr<Terrain>` + warstwy tekstur | Encja terenu; `RenderExtractionSystem` tworzy z niej paczki węzłów | `object/terrain/terrain_component.h` |
| `TerrainPlacementComponent` | Offset Y względem terenu | `TerrainPlacementSystem` ustawia wysokość obiektu na powierzchni | `object/terrain/terrain_placement_component.h` |
| `ISceneSystem` | Interfejs systemu ECS | `update(Scene&, dt)`; systemy dodaje się w kolejności wykonania | `scene/scene.h` |
| `Scene` | Jeden świat: encje + systemy + paczki klatki | Właściciel encji, hierarchii, `frameLights`, `frameView`, `renderPackets` | `scene/scene.h` |
| `SceneManager` | Rejestr scen | Tworzy/usuwa sceny i wskazuje aktywną; tylko aktywna jest aktualizowana i rysowana | `scene/scene_manager.h` |
| `SceneLight` | Spłaszczone światło dla GPU | Kopia danych z `LightComponent`; pozycja i typ w formacie zrozumiałym dla shaderów | `core/types.h` |
| `RenderView` | Macierze i baza kamery na klatkę | `view`, `projection`, `cameraPosition`, `cameraRight`, `cameraUp` — wejście renderera | `render/render_types.h` |
| `Transform`, `CameraConfig`, `SkyboxConfig`, `SkydomeConfig`, `OceanConfig`, `GridConfig` | Struktury konfiguracyjne | Wygodne zestawy parametrów przekazywane do loaderów/sub-rendererów | `core/types.h` |

### Systemy wbudowane

| System | Co robi |
|---|---|
| `TransformSystem` | Liczy macierze świata z hierarchii (dodawany przez `Scene` jako pierwszy) |
| `RenderExtractionSystem` | Materiały + równoległe zbieranie `RenderPacket` z encji |
| `LightSystem` | Zbiera `LightComponent` do `SceneLight[]` |
| `CameraExtractionSystem` | Tworzy `RenderView` z aktywnej kamery |
| `RenderFramePrepSystem` | Liczy `distanceToCamera` paczek (sortowanie) |
| `TerrainSystem` | Aktualizuje LOD i widoczność węzłów terenu |
| `TerrainPlacementSystem` | Dopasowuje Y obiektów do terenu |
| `VegetationSystem` | Generuje instancje roślin i aktualizuje wiatr |

---

## 3. Zasoby

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `AssetHandle<Tag>` | Typowany uchwyt (`uint32_t` + tag) | Bezpieczne ID zasobu; tag blokuje pomylenie materiału z teksturą | `core/asset_registry.h` |
| `MaterialHandle`, `TextureHandle`, `MeshHandle` | Konkretne uchwyty | Przekazywane w komponentach i paczkach zamiast nazw/wskaźników | `core/asset_registry.h` |
| `AssetRegistry` | Słownik nazwa ↔ uchwyt | Rejestruje i wyszukuje zasoby; bezpieczny wątkowo (`shared_mutex`) | `core/asset_registry.h` |
| `Material` | Dane materiału (kolor, mapy, blending, głębokość) | Opis wyglądu powierzchni; `MaterialSystem` zamienia go na potok | `core/types.h` |
| `MaterialManager` | Rejestr materiałów | `add`, `get`, `require`, `handle`; przechowuje `Material` po nazwie i uchwycie | `render/material_manager.h` |
| `TextureManager` | Rejestr opisów tekstur | Nazwa → ścieżka/parametry; GPU ładuje `Renderer` | `render/texture_manager.h` |
| `FontManager` | Rejestr czcionek | Nazwa → `FontAsset` (ścieżka, wysokość, glify) | `feature/font/font_manager.h` |
| `ResourceManager<T>` | Cache `shared_ptr` z `weak_ptr` | Współdzielenie zasobów; `load(name, loader)` tworzy raz | `core/resource_manager.h` |
| `MeshData` | Wektory `Vertex` i indeksów w RAM | Wynik loaderów/`MeshBuilder`; wejście `MeshManager::loadMesh` | `core/types.h` |
| `Vertex` | Pozycja, normalna, kolor, UV | Format wierzchołka siatek statycznych (binding 0) | `core/types.h` |
| `SkinVertex` | 4 indeksy kości + 4 wagi | Drugi bufor wierzchołków (binding 1) dla animacji szkieletowej | `core/types.h` |
| `GpuMesh` | Bufory GPU + liczniki + granice | To, co `MeshManager` zwraca do rysowania; dane są `DEVICE_LOCAL` | `core/mesh_manager.h` |
| `MeshManager` | Właściciel siatek GPU | `loadMesh`, `updateMesh`, `flushUploads`, `removeMesh`, bufory instancji | `core/mesh_manager.h` |
| `MeshBuilder` | Helper do składania siatek z brył | Box, walec, stożek, sfera, dach, pierścień; kolor w wierzchołkach | `core/mesh_builder.h` |

---

## 4. Renderer: dane i rozszerzenia

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `Renderer` | Fasada renderera (Pimpl) | Publiczne API: `render`, tekstury, światła, cienie, mgła, feature'y | `render/renderer.h` |
| `RendererImpl` | Wewnętrzna implementacja Vulkana | Stan zasobów i przebiegi klatki; podzielona na `renderer_*.cpp` | `render/renderer_impl.h` (prywatny) |
| `RenderScene` | Paczki jednej klatki | Listy opaque/transparent/shadowOnly + referencje do casterów cienia | `render/render_pass.h` |
| `RenderPacket` | Spłaszczona paczka rysowania | Bufory, macierz, materiał, indeksy tekstur, dystans, flagi przebiegu | `render/render_types.h` |
| `TerrainSurface` | Wspólne dane powierzchni terenu | Warstwy/maski/wysokość; jedna na teren, współdzielona przez paczki (`shared_ptr`) | `render/render_types.h` |
| `IRenderFeature` | Interfejs dodatku do potoku | `preRender/render`, etap (`BeforeOpaque`, `AfterTransparent`, `Overlay`), `needsSceneColor` | `render/render_feature.h` |
| `IRenderPass` | Interfejs przebiegu geometrii | `OpaquePass`, `TransparentPass`, `TransparentDepthPrepass` | `render/render_pass.h` |
| `GraphicsPipelineBuilder` | Helper do tworzenia `VkPipeline` | Podaje tylko różnice od domyślnych ustawień; reszta stanu jest uzupełniana | `render/pipeline_builder.h` |
| `ShaderModule` | RAII na moduł SPIR-V | Wczytuje `.spv`, zwalnia w destruktorze; `code()` dla `filterVertexAttributes` | `render/pipeline_builder.h` |
| `VulkanPipeline` | Para `VkPipeline` + `VkPipelineLayout` | To, co `MaterialInstance` zwraca do bindowania | `render/render_types.h` |
| `MaterialInstance` | Interfejs instancji materiału | `bind`, `getPipeline`, `isTransparent`, gettery tekstur, `depthPrepass` | `render/render_types.h` |
| `BaseMaterialInstance` | Konkretna instancja materiału | Trzyma `Material` i pyta `MaterialCompiler` o potok | `render/base_material_instance.h` |
| `MaterialSystem` | Cache instancji materiałów | `getOrCreateMaterialInstance`, unieważnianie potoków przy hot reload | `render/material_system.h` |
| `MaterialCompiler` | Kompilator i cache potoków | Klucz `PipelineKey` → `VulkanPipeline`; tworzy potoki przez builder | `render/material_compiler.h` |
| `PipelineKey` (wewnętrzna) | Klucz cache potoku | Shader + blend + depth test/write + cull + colorWrite | `render/material_compiler.h` |

### Czym jest `GraphicsPipelineBuilder`?

To **helper do tworzenia niezmiennego obiektu `VkPipeline`** w Vulkanie, a nie
element rysowany co klatkę. Potok graficzny to „zamrożony” komplet ustawień:
shadery, format wierzchołków, topologia, rasteryzacja, głębokość, blending
i formaty celów. W czystym Vulkanie wymaga to ~120 linii struktur i łatwo
pominąć pole.

`GraphicsPipelineBuilder`:

* zbiera tylko to, co różni się od domyślnych ustawień silnika,
* uzupełnia resztę sensownymi wartościami (trójkąty, cull BACK, depth LESS,
  viewport/scissor dynamiczny, brak blendingu),
* pozwala zbudować **wiele wariantów** tym samym kodem (np. cień słońca
  z `depthClamp` i cień światła lokalnego bez),
* jest używany **tylko przy tworzeniu zasobów**, nie w pętli klatki.

Czego **nie** robi:

* nie alokuje deskryptorów ani buforów,
* nie nagrywa komend,
* nie wiąże potoku — to robi `vkCmdBindPipeline` w przebiegu,
* nie zarządza pamięcią — `VkPipeline` trzeba zniszczyć w `cleanup`.

Przepływ tworzenia:

```text
ShaderModule (SPIR-V) ─┐
vertex input          ├─> GraphicsPipelineBuilder ─> VkPipeline
formaty/stany         ─┘                              │
                                                      └─> VulkanPipeline
                                                           (pipeline + layout)
```

`VulkanPipeline` to wynik dla `MaterialInstance`; `MaterialCompiler` trzyma go
w cache pod `PipelineKey`.

---

## 5. Backend Vulkan

Pojęcia **swapchain**, **bindless**, **deskryptor**, **fence**, **semafor**,
**bariera**, **layout** i **synchronization2** są wyjaśnione od podstaw
w [27_vulkan_od_podstaw.md](27_vulkan_od_podstaw.md). Poniższa tabela opisuje
klasy, które je realizują.

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `VulkanContext` | Właściciel instancji, GPU, urządzenia, kolejek, VMA | Wybiera kartę z Vulkanem 1.4 i wymaganymi cechami; tworzy surface/device | `render/backend/vulkan_context.h` |
| `VulkanSwapchain` | Obrazy koloru + głębi + prezentacja | `acquireNextImage`, `present`, przebudowa po resize | `render/backend/vulkan_swapchain.h` |
| `VulkanBuffer` | RAII na `VkBuffer` + alokację VMA | `upload` dla host-visible; `DEVICE_LOCAL` wypełniany stagingiem | `render/backend/vulkan_resources.h` |
| `ImageBarrier` | Opis bariery w stylu synchronization2 | Jedno miejsce na `oldLayout/newLayout/srcStage/srcAccess/dst...` | `render/vulkan_util.h` |
| `TextureUploadData` | Zdekodowane piksele + format + regiony kopiowania | Wynik `TextureAssetLoader`; wejście `uploadTexture` | `render/texture_asset_loader.h` |
| `TextureResources` (wewnętrzna) | Obraz, pamięć, view, sampler, mipmapy | Wpis w mapie tekstur renderera | `render/renderer_impl.h` |
| `SolidTexture` (wewnętrzna) | Tekstura 1×1 | Domyślne albedo/normalna/ORM/czerń, gdy materiał nie ma mapy | `render/renderer_impl.h` |
| `GlobalUBO` (wewnętrzna) | Dane klatki dla wszystkich shaderów | Macierze, kamera, światła, cienie, mgła, mgła wojny, wiatr | `render/renderer_impl.h` |
| `LightData` (wewnętrzna) | Światło w formacie GPU | Pozycja, kierunek, kolor, parametry stożka, warstwa cienia | `render/renderer_impl.h` |
| `ShadowCaster` (wewnętrzna) | Światło rzucające cień w klatce | Indeks światła, warstwa kaskady, slot atlasu | `render/renderer_impl.h` |

---

## 6. Push constants i formaty wierzchołków

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `MaterialPushConstants` | 240 B danych obiektu/materialu | Macierz modelu, kolory, czynniki PBR, indeksy tekstur, warstwy terenu | `render/push_constants.h` |
| `ShadowPushConstants` | Dane do mapy cieni | Macierz modelu, `lightViewProj`, alpha cutoff, tekstura | `render/push_constants.h` |
| `TerrainShadowPushConstants` | Cień terenu teselowanego | Macierz, światło, heightmapa, skala Y, teselacja | `render/push_constants.h` |
| `SkinVertex` | Dane kości wierzchołka | `joints[4]` + `weights[4]`; drugi bufor wierzchołków | `core/types.h` |

> Układ bajtów `*PushConstants` musi odpowiadać blokowi w HLSL
> (`shared/material_push.hlsl`, `shared/shadow_push.hlsl`). `static_assert`
> pilnuje rozmiaru.

---

## 7. Kamera i picking

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `CameraController` | Kontroler FPS/fly | Yaw/pitch, prędkości z tarciem, `update` wpisuje pozycję do kamery | `camera/camera_controller.h` |
| `RtsCameraController` | Kontroler kamery strategicznej | Orbita wokół `focus`, pan/zoom/rotate/tilt, wygładzanie | `camera/rts_camera_controller.h` |
| `Ray` | Półprosta `origin + t * direction` | Picking myszą, raycast terenu, trafienia w sfery/płaszczyzny | `camera/camera_picking.h` |
| `screenPointToRay`, `worldToScreen`, `rayPlaneY`, `raySphere` | Funkcje pickingu | Zamiana pikseli na promień i świata na ekran bez odwracania macierzy | `camera/camera_picking.h` |

---

## 8. GUI i tekst

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `GuiMgr` | Menedżer GUI + `IRenderFeature` | Okna, HUD, kursor, obrazy, `update` myszy, rysowanie w `Overlay` | `feature/gui/gui.h` |
| `GuiWindow` | Okno GUI | Pozycja, rozmiar, belka, przeciąganie, lista widgetów | `feature/gui/gui.h` |
| `GuiWidget` | Bazowy widget | `update/draw`, `isInside`, `isOpen/close` | `feature/gui/gui.h` |
| `GuiButton`, `GuiCheckBox`, `GuiComboBox`, `GuiListBox`, `GuiMemo`, `GuiScrollBar`, `GuiStaticText`, `GuiTrackBar`, `GuiInputText` | Konkretne widgety | Callbacki `setOnClick/setOnToggle/setOnSelect/setOnChange` | `feature/gui/gui_*.h` |
| `GuiMsg` | Komunikat z okna | `CLICK/STRING/BOOL/REAL` + tekst/wartość; alternatywa dla callbacków | `feature/gui/gui.h` |
| `GuiRenderer` | Renderer 2D GUI | Prostokąty, tekst, obrazki; jeden potok, bufor na klatkę | `feature/gui/gui_renderer.h` |
| `GuiVertex` | Wierzchołek GUI | Pozycja NDC, UV, kolor | `feature/gui/gui_renderer.h` |
| `GuiToolbar` | Pasek ikon | Grupy przycisków, skróty, karty po najechaniu, `enabled/active` | `feature/gui/gui_toolbar.h` |
| `GuiCard` | Karta informacji | Tytuł, opis, żetony kosztu, ostrzeżenie, stopka | `feature/gui/gui_toolbar.h` |
| `PixelIcon` | Ikona pixel-art z tekstu | Znaki → kolorowe piksele rysowane prostokątami HUD | `feature/gui/gui_pixel_icon.h` |
| `FontAtlasData` / `FontAtlasGlyph` | Atlas glifów i metryki | Tekstura R8 + UV/advance/bearing dla każdego znaku | `feature/font/font_atlas_loader.h` |
| `TextOverlayRenderer` | Renderer tekstu 2D | Nakładka `Overlay`; jeden bufor wierzchołków na klatkę | `feature/font/text_overlay_renderer.h` |
| `TextVertex` | Wierzchołek tekstu | Pozycja, UV, kolor | `feature/font/text_overlay_renderer.h` |

---

## 9. Audio

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `AudioSystem` | Mikser i zarządca dźwięku | Efekty, warianty, limity, dźwięk 3D, muzyka strumieniowa | `feature/audio/audio_system.h` |
| `SoundBuffer` (wewnętrzna) | Zdekodowane próbki stereo | Jeden wariant efektu; głosy trzymają wskaźnik | `feature/audio/audio_system.h` |
| `SoundEntry` (wewnętrzna) | Wpisy jednego dźwięku | Lista wariantów + limity + czas ostatniego odtworzenia | `feature/audio/audio_system.h` |
| `Voice` (wewnętrzna) | Grający efekt | Bufor, pozycja, pitch, gain L/P | `feature/audio/audio_system.h` |
| `Command` / `CommandQueue` (wewnętrzna) | Kolejka poleceń SPSC | Gra → wątek audio bez muteksów i alokacji | `feature/audio/audio_system.h` |
| `MusicTrack` (wewnętrzna) | Strumień utworu | Dekodowanie OGG, bufor pierścieniowy, przenikanie | `feature/audio/audio_system.h` |

---

## 10. Cząsteczki i sprite'y

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `ParticleSystem` | Grupa emiterów + `IRenderFeature` | Ładuje `.particle`, aktualizuje, wysyła instancje | `feature/particle/system.h` |
| `ParticleEmitter` | Emiter cząstek | Parametry, pula cząstek, zdarzenia, `update/updateParallel` | `feature/particle/emitter.h` |
| `Particle` | Stan cząstki | Pozycja, prędkość, kolor, rozmiar, masa, wiek, życie | `feature/particle/particle.h` |
| `ParticleEvent` | Zmiana w czasie życia | `at`/`fade` z pliku `.particle` (kolor, rozmiar, masa...) | `feature/particle/particle_event.h` |
| `RecyclingArray<T>` | Tablica z wolnymi slotami | Brak `new`/`delete` co cząstkę | `feature/particle/recycling_array.h` |
| `ParticleInstanceData` | Dane instancji cząstki | Pozycja, rozmiar, kolor dla bufora instancji | `core/types.h` |
| `SpriteSystem` | System sprite'ów + `IRenderFeature` | Animacje z atlasu/sekwencji, grupowanie po teksturze | `feature/sprite/sprite.h` |
| `SpriteSystem::Sprite` | Pojedynczy sprite | Pozycja, rozmiar, kolor, klatki, UV, czas | `feature/sprite/sprite.h` |

---

## 11. AI i mgła

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `NavGrid` | Siatka nawigacji 2D (XZ) | A*, przeszkody statyczne/dynamiczne, regiony dostępności | `feature/ai/nav_grid.h` |
| `VolumetricFogRenderer` | Mgła wolumetryczna (froxele) | Compute gęstości + composite na kolor sceny | `feature/fog/volumetric_fog_renderer.h` |
| `LocalFogVolume` | Lokalny obszar mgły | Doliny, kotły; `center`, `halfExtents`, `density`, `edgeFade` | `feature/fog/volumetric_fog_renderer.h` |

---

## 12. Teren

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `Terrain` | Heightfield + quadtree LOD | Ładowanie/generowanie, budowa drzewa, LOD, paczki, `getHeight`, `raycast` | `object/terrain/terrain.h` |
| `Terrain::Node` | Węzeł quadtree | Obszar, wysokości min/max, dzieci, sąsiedzi, LOD, widoczność, `meshId` | `object/terrain/terrain.h` |
| `TerrainLayer` | Warstwa tekstur | Tekstura podłoża + maska wagi (splatting) | `object/terrain/terrain.h` |
| `TerrainSurface` | Wspólne dane powierzchni dla paczek | Warstwy/maski/wysokość; indeksy bindless uzupełnia renderer | `render/render_types.h` |
| `TerrainIndexGenerator` | Generator indeksów LOD/stitching | 5 poziomów LOD × 16 konfiguracji zszywania | `object/terrain/terrain_index_generator.h` |
| `TerrainHeightmapGenerator` | Generatory proceduralne | Plasma, Fault, Deposition | `object/terrain/terrain_heightmap_generator.h` |
| `TerrainSystem` | System ECS terenu | Co klatkę aktualizuje LOD/widoczność | `object/terrain/terrain_system.h` |
| `TerrainPlacementSystem` | System ECS stawiania na terenie | Ustawia Y encji z `TerrainPlacementComponent` | `object/terrain/terrain_placement_system.h` |

---

## 13. Modele

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `Model3ds` | Loader 3DS | Chunki, obiekty, materiały, `MeshData` per obiekt | `object/model_3ds.h` |
| `Model3ds::t3DObject` | Obiekt 3DS | Wierzchołki, twarze, UV, hierarchia, `MeshData` | `object/model_3ds.h` |
| `Model3ds::tMaterialInfo` | Materiał 3DS | Kolory, tekstura, przezroczystość, `twoSided` | `object/model_3ds.h` |
| `Md2ModelData` | Model MD2 | Klatki, trójkąty, UV, `triangles` | `object/model_md2.h` |
| `Md2AnimationState` | Stan animacji MD2 | Zakres klatek, prędkość, interpolacja | `object/model_md2.h` |
| `GltfModel` | Model glTF | Prymitywy, animacje, skiny, węzły | `object/gltf/model_gltf.h` |
| `GltfPrimitive` | Część modelu glTF | `MeshData`, materiał, tekstury, jointy/wagi | `object/gltf/model_gltf.h` |
| `GltfAnimation` / `GltfAnimChannel` | Animacja glTF | Kanały node/path z czasami i wartościami | `object/gltf/model_gltf.h` |
| `GltfSkin` | Szkielet glTF | Lista jointów + macierze odwrotne bind | `object/gltf/model_gltf.h` |
| `GltfSkinnedModel` | Model szkieletowy | Ładuje glTF, tworzy `GltfPrimitiveInstanceInfo` | `object/gltf/model_gltf.h` |
| `GltfSkinnedInstance` | Instancja animowana | Encje, bufor kości (4 sloty), `update` animacji | `object/gltf/model_gltf.h` |
| `GltfPrimitiveInstanceInfo` | Instancja prymitywu | Nazwa mesh, materiał, bufor `SkinVertex` | `object/gltf/model_gltf.h` |

---

## 14. Otoczenie

| Nazwa | Co to jest | Rola w silniku | Plik |
|---|---|---|---|
| `SkydomeRenderer` | Niebo proceduralne | Chmury, słońce, pora dnia; `BeforeOpaque`; opcjonalnie daje światło słońca | `object/skydome_renderer.h` |
| `SkyboxRenderer` | Panorama z tekstur DDS | Sześcian wokół kamery, bez zapisu głębi | `object/skybox_renderer.h` |
| `OceanRenderer` | Woda | Fale, odbicia/załamania z kopii sceny; `AfterTransparent` | `object/ocean_renderer.h` |
| `GridRenderer` | Siatka pomocnicza | Linie XZ, test głębokości; debug/edytor | `object/grid_renderer.h` |
| `VegetationSystem` | Roślinność | Generuje instancje, wiatr, gatunki | `object/vegetation/vegetation_system.h` |
| `VegetationSpecies` | Gatunek rośliny | Siatka, materiał, gęstość, skala, nachylenie, wysokość | `object/vegetation/vegetation_system.h` |

---

## 15. Jak czytać słownik

* Szukasz **jak użyć** danej rzeczy — przejdź do dokumentu modułu (np.
  `08_render_backend.md`, `09_render_potoki.md`).
* Szukasz **czym to jest** — zostań tutaj.
* Szukasz **dlaczego tak** — dokument modułu ma sekcje „jak to działa”
  i „pułapki”.
* Jeśli struktura jest oznaczona jako **wewnętrzna**, nie używaj jej w kodzie
  gry — może się zmienić bez ostrzeżenia.

## 16. Pomocnicze typy i rejestry

| Typ | Co oznacza i gdzie go szukać |
|---|---|
| `ShaderProgram` | ścieżki vertex/fragment SPIR-V i nazwy punktów wejścia; `core/types.h` |
| `ShaderManifestEntry`, `ShaderManifest` | nazwa programu i lista programów do rejestracji |
| `ShaderManager` | rejestr `nazwa → ShaderProgram`; `get` zwraca wskaźnik lub null, `require` rzuca przy braku; sam nie kompiluje HLSL ani nie tworzy `VkPipeline` |
| `MaterialTag`, `TextureTag`, `MeshTag` | znaczniki typów dla `AssetHandle`; zapobiegają pomyleniu kategorii, nie są zasobami GPU |
| `MeshAsset`, `TextureAsset`, `FontAsset` | opisy zasobów CPU: nazwa i odpowiednio siatka, ścieżka tekstury albo parametry czcionki; `core/types.h` |
| `RotationSpeed` | pomocnicze prędkości obrotu X/Y/Z w `core/types.h`; nie jest systemem aktualizacji rotacji |
| `InputState` | pomocnicza struktura klawiszy i myszy z `core/types.h`; aktualne wejście aplikacji opisuje `InputSnapshot` w rozdziale 06 |
| `LogBackend` | interfejs miejsca zapisu komunikatu; `core/logger.h`, przestrzeń `engine::core` |
| `ConsoleBackend`, `FileBackend`, `MultiBackend` | wyjście na konsolę, do pliku albo do kilku backendów; `Logger` steruje poziomem i formatowaniem |
| `Md2FrameData`, `Md2Triangle` | pozycje/normalne jednej klatki i osobne indeksy wierzchołków/UV trójkąta; `object/model_md2.h` |
| `SkydomePushConstants` | wewnętrzny pakiet parametrów nieba: kamera, czas, chmury, słońce i kolory; `object/skydome_renderer.h` |
| `particles::RealNumber`, `IntNumber`, `Vec3Number`, `Vec4Number` | zakresy min/max parametrów cząstek, a nie generatory liczb losowych; `feature/particle/types.h` |

## 17. Algorytmy przykładu Osadników

| Pojęcie | Znaczenie |
|---|---|
| `Transport::NetDistances` | dystanse i drogi poprzedzające flagi, obliczone Dijkstrą od jednego źródła |
| pamięć Dijkstry (`netCache_`) | cache `źródło → NetDistances`, unieważniany wersją grafu dróg |
| `Ref<T>` | odwołanie do slotu z kontrolą generacji; nadal wymaga sprawdzania `alive` |
| `SlotFreeList` | kolejka martwych slotów, które po kwarantannie można wykorzystać ponownie |
| `FactionFog` | odkrycie, bieżąca widoczność, liczniki obserwatorów i obszar zmian jednej frakcji |
| kubełki przestrzenne | grupowanie jednostek po komórkach XZ, by ograniczyć liczbę kandydatów przy szukaniu wroga |
| planista potrzeb (`AiPlanner`) | reguły wyboru rozbudowy na podstawie stanu gospodarki, wojska i odkrytej mapy |

Wyprowadzenie, przykłady i ograniczenia: [28_settlers_algorytmy.md](28_settlers_algorytmy.md).
