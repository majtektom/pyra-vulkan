# TODO — engine3d

Wewnętrzny backlog zadań technicznych. `[ ]` = do zrobienia. Historia
wykonanych prac jest w [changelog.md](changelog.md), a dokumentacja użytkownika
w [doc/README.md](doc/README.md) (zbiorczo ograniczenia i plan:
[doc/26_ograniczenia_i_plan.md](doc/26_ograniczenia_i_plan.md)).

---

## 1. Błędy i ograniczenia

### Renderowanie i zasoby

- [ ] Mipmapy tekstur DXT/BC przez blit — `vkCmdBlitImage` nie obsługuje
  formatów skompresowanych; sprawdzać `VK_FORMAT_FEATURE_BLIT_DST` /
  `SAMPLED_IMAGE_FILTER_LINEAR` (`engine/render/renderer_textures.cpp`).
- [ ] Limit indeksów bindless — `nextTextureIndex_` nie jest porównywany
  z `kMaxBindlessTextures` (1024), indeksy nie są zwalniane
  (`renderer_textures.cpp`, `renderer_impl.h`).
- [ ] Dwie mgły naraz — `"fog": {"enabled": true}` włącza liniową (deferred)
  i wolumetryczną; potrzebna jedna wspólna konfiguracja.
- [ ] Ocean nie zapisuje głębi — nad wodą mgła liczy się do dna/nieba.
- [ ] Skydome to wycinek kuli — w oddali widać ciemną krawędź na horyzoncie.
- [ ] Push constants materiału mają 240 B, a Vulkan gwarantuje minimum 128 B —
  przy takiej karcie potrzebne SSBO zamiast push constants.
- [ ] Ścieżki `bin/assets/...` zależą od katalogu roboczego — dodać ścieżkę
  bazową z `SDL_GetBasePath`.
- [ ] Tekstury osadzone w glTF/GLB są zapisywane do `/tmp/gltf_embed_*.bmp`
  i wczytywane z dysku — wczytywać prosto z pamięci
  (`engine/object/gltf/model_gltf.cpp`).
- [ ] Upload tekstur — jeden command buffer zamiast kilku submitów
  z `vkQueueWaitIdle` na teksturę.
- [ ] Ostrzeżenia kompilatora — dodać `-Wall -Wextra` w
  `cmake/ProjectSettings.cmake`; w Debug opcjonalnie ASan/UBSan.
- [ ] Shadery HLSL kompiluje `glslangValidator` (gdy brak `dxc`); nie obsługuje
  części atrybutów `[[vk::...]]`. `dxc` jest opcjonalny — doprecyzować
  wymagania w komentarzach/CMake.

### Logika i synchronizacja

- [ ] Delta time w milisekundach — szarpanie przy wysokim FPS, `dt = 0` powyżej
  ~1000 FPS (`engine/core/engine.cpp`, `calculateDeltaTimeMs`).
- [ ] `loadMap`: jeden `currentBrainModel_` dla wszystkich wpisów
  `gltf_skinned_models` (drugi nadpisuje pierwszy); ponowne `loadMap` nie
  czyści starych encji.
- [ ] `JobSystem`: przepełnienie, gdy `hardware_concurrency()` zwraca 0
  (`engine/core/job_system.h`).
- [ ] Niedeterministyczna kolejność paczek po scaleniu buforów wątków
  (G-Buffer, cienie) — utrudnia porównywanie zrzutów.

### Cienie

- [ ] Cienie świateł punktowych — teraz jedna perspektywa 120° w dół; potrzebne
  cube mapy lub dual-paraboloid.
- [ ] Culling per kaskada cienia (teraz liczone są wszystkie paczki cieni).

---

## 2. Wydajność

- [ ] Roślinność: jedna encja = jedno źdźbło = osobny draw call (+4 kaskady
  cieni). Instancing: jeden draw na gatunek i chunk świata; shadery
  `default`/`gbuffer` muszą czytać macierz z `VK_VERTEX_INPUT_RATE_INSTANCE`.
- [ ] Frustum culling dla siatek (jest tylko dla terenu).
- [ ] Dalsze sortowanie paczek po pipeline/materiale — mniej rebindów.
- [ ] Tekstury i render targety przez VMA zamiast surowego `vkAllocateMemory`.
- [ ] `TerrainPlacementSystem` co klatkę liczy wysokość dla wszystkich
  statycznych obiektów — przenieść do fazy tworzenia.
- [ ] Nagrywanie komend z wielu wątków (secondary command buffers, osobna pula
  komend na wątek).

---

## 3. Architektura

- [ ] HDR i tonemapping — render do `R16G16B16A16_SFLOAT`, ACES/AgX, na końcu
  kopia do swapchaina.
- [ ] Render graph — automatyczne bariery i kolejność przebiegów.
- [ ] Push constants → SSBO z indeksem, gdy scena urośnie (materiały, dane
  obiektów).
- [ ] Mesh shaders (task + mesh) — culling i generowanie geometrii na GPU.
- [ ] Data-oriented ECS — komponenty w ciągłych tablicach, logika w systemach
  (ewentualnie EnTT).
- [ ] Równoległa aktualizacja ECS — `Scene::updateEntities` w paczkach na
  `JobSystem`.
- [ ] Asynchroniczne ładowanie zasobów — tekstury i modele w tle, placeholdery.
- [ ] Debug rendering — wireframe węzłów quadtree terenu.

---

## 4. Teren

- [ ] Przypinanie obiektów do węzłów quadtree (teraz
  `TerrainPlacementComponent` tylko dociąga wysokość).
- [ ] Lepsze dopasowanie zasięgu kaskad cieni do widocznego terenu (dalsza
  optymalizacja `updateVisibleDepthRange`).

---

## 5. Gra `10_settlers`

- [ ] Instancing drzew, słupków granicy i jednostek — jeden draw na typ siatki.
- [ ] Animacje jednostek — szkieletowe albo proste chodzenie/machanie
  narzędziem zamiast przesuwania brył.
- [ ] Cząsteczki — dym z kominów, pożar przy paleniu budynków, kurz przy
  budowie.
- [ ] Statystyki na koniec partii (surowce, armia, straty w czasie).
- [ ] Kolejki w koszarach i punkt zbiórki dla nowych żołnierzy.
- [ ] Podgląd zasięgu terytorium nowej wieży przy jej stawianiu.

---

## 6. Pomysły

- Mesh shaders, render graph, HDR, data-oriented ECS.
- Lepsze cienie lokalne (cube mapy), global illumination w przyszłości.
- Edytor sceny oparty o GUI silnika (hierarchia, inspektor, gizmos).
- Skryptowanie logiki gry bez rekompilacji.
