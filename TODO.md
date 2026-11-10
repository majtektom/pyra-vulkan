# TODO — engine3d

Legenda: `[x]` zrobione, `[ ]` do zrobienia. Odnośniki do plików względem katalogu głównego repo.

Wszystkie przykłady (01–09) przechodzą walidację Vulkana bez błędów i ostrzeżeń (także wydajnościowych),
również z walidacją synchronizacji. Uruchamianie z pełnym logiem:

```sh
VK_LAYER_VALIDATE_SYNC=1 VK_LAYER_DUPLICATE_MESSAGE_LIMIT=1000 ./bin/09_vegetation
```

---

## 1. Błędy działania

### Swapchain i synchronizacja

- [x] `VK_SUBOPTIMAL_KHR` przy acquire traktowany jak błąd (semafor zostawał zasygnalizowany)
- [x] Bariera obrazu swapchaina z `TOP_OF_PIPE` zamiast `COLOR_ATTACHMENT_OUTPUT`
- [x] Semafory prezentacji per klatka zamiast per obraz swapchaina
- [x] Minimalizacja okna (swapchain 0×0) — renderowanie wstrzymane
- [x] Fallback formatu swapchaina
- [x] Początkowe przejścia layoutu mapy cieni i buforów głębi
- [x] Brakujące bariery przed przebiegiem 2 (ocean/przezroczyste) i przed overlayem
- [x] Głębia read-only w przebiegu 2 ze `storeOp = STORE` → `STORE_OP_NONE`
- [x] Przełączanie trybu synchronizacji (Standard ↔ Timeline) w trakcie działania

### Zapisy CPU do pamięci czytanej przez GPU (klatki w locie)

- [x] `MeshManager`: wierzchołki MD2, instancje cząsteczek i sprite'ów — bufory zastępowane, stare zwalniane po `kMaxFrames + 1` klatkach
- [x] Kości GLTF — pierścień slotów w buforze kości instancji
- [x] Przeładowanie tekstury / zmiana filtrowania — `vkDeviceWaitIdle` przed zniszczeniem
- [x] Nowe tekstury w trakcie gry — `UPDATE_UNUSED_WHILE_PENDING` na bindless
- [x] Przebudowa atlasu fontu przy nowych znakach
- [x] Bufor lokalnych wolumenów mgły — zapis tylko po zmianie
- [x] Przykład `03_flyaround` przepisany na `GltfSkinnedModel`/`GltfSkinnedInstance` (usunięta ręczna kopia skinningu)

### Niepoprawne użycie API

- [x] Wireframe bez `fillModeNonSolid`; anizotropia bez sprawdzenia wsparcia
- [x] Czyszczenie i zapis depth w layoucie read-only (overlay ma teraz własny przebieg)
- [x] Zapis głębi przez materiały przezroczyste w przebiegu read-only
- [x] Set 0 bindowany niezgodnym layoutem w `OpaquePass`/`TransparentPass`
- [x] Brak formatu depth w pipeline'ach deferred i kompozycji mgły
- [x] Format obrazu mgły niezgodny z shaderem (RGBA16F vs Rgba32f)
- [x] `vkFreeDescriptorSets` na pulach bez `FREE_DESCRIPTOR_SET_BIT`
- [x] Wycieki przy zamykaniu (GUI, layouty pipeline'ów przy przełączaniu wireframe)
- [x] Atrybuty wierzchołka nieczytane przez shader (ostrzeżenia `PERFORMANCE`) — `vk::filterVertexAttributes` odczytuje wejścia z SPIR-V
- [x] Shader siatki (grid) czytał kolor z lokacji normalnej — siatka i osie były zawsze zielone
- [ ] Generowanie mipmap przez blit na teksturach DXT/BC — `vkCmdBlitImage` nie działa dla formatów skompresowanych; brak sprawdzenia `VK_FORMAT_FEATURE_BLIT_DST` / `SAMPLED_IMAGE_FILTER_LINEAR` (`engine/render/renderer_core.cpp`, `loadTexture`)
- [ ] Indeks bindless bez limitu — `nextTextureIndex_` nie jest porównywany z `kMaxBindlessTextures` (1024), indeksy nie są zwalniane

### Mgła wolumetryczna

- [x] Kompozycja z głębi sceny zamiast G-buffera (obejmuje teren rysowany forwardem)
- [x] Odbicie mgły w pionie (podwójne odwracanie Y)
- [x] Całkowanie od kamery w głąb (front-to-back)
- [x] Mgła wysokościowa sumowana z lokalnymi wolumenami
- [x] Kolor mgły z mapy, kierunek słońca z pierwszego światła kierunkowego
- [ ] Podwójna mgła — `"fog": {"enabled": true}` włącza naraz liniową (deferred, teren) i wolumetryczną
- [ ] Ocean nie zapisuje głębi — nad wodą mgła liczy się do dna / nieba (trochę za gęsta)

### Logika

- [ ] Delta time w milisekundach — szarpanie przy wysokim FPS, dt = 0 powyżej 1000 FPS (`engine/core/engine.cpp`, `calculateDeltaTimeMs`)
- [ ] `loadMap`: jeden `currentBrainModel_` dla wszystkich `gltf_skinned_models` (drugi nadpisuje pierwszy); ponowne `loadMap` nie czyści starych encji
- [ ] `JobSystem`: przepełnienie, gdy `hardware_concurrency()` zwraca 0 (`engine/core/job_system.h`)
- [ ] Niedeterministyczna kolejność paczek po scaleniu buforów wątków (G-buffer, cienie)
- [ ] Skydome to wycinek kuli — w oddali widać jej ciemną krawędź na horyzoncie

---

## 2. Wydajność

- [ ] Roślinność: jedna encja = jedno źdźbło = osobny draw call (+4 kaskady cieni). Instancing: jeden draw na gatunek i chunk świata (`MeshManager` ma już `instanceBuffer`; shadery `default`/`gbuffer` muszą czytać macierz z `VK_VERTEX_INPUT_RATE_INSTANCE`)
- [ ] Frustum culling dla siatek (jest tylko dla terenu) i culling per kaskada cienia
- [ ] Sortowanie paczek po pipeline/materiale, mniej rebindów
- [ ] Statyczne siatki w `DEVICE_LOCAL` przez staging (teraz wszystkie host-visible)
- [ ] Tekstury i render targety przez VMA zamiast surowego `vkAllocateMemory`
- [ ] Upload tekstur: jeden command buffer zamiast 3–4 submitów z `vkQueueWaitIdle` na teksturę
- [ ] `TerrainPlacementSystem` co klatkę liczy wysokość dla wszystkich statycznych obiektów
- [ ] Kopiowanie sceny ze swapchaina do `sceneColor` w każdej klatce, także bez oceanu
- [ ] Nagrywanie komend z wielu wątków (secondary command buffers, osobna pula komend na wątek)

---

## 3. Architektura

- [ ] HDR i tonemapping — render do `R16G16B16A16_SFLOAT`, tonemapping (ACES/AgX), na końcu kopia do swapchaina (usuwa też kopiowanie ze swapchaina)
- [ ] Spójność forward/deferred — teren i materiały custom idą forwardem z własnym oświetleniem; albo wszystko przez G-buffer, albo wspólna funkcja oświetlenia
- [ ] `engine/render/renderer_core.cpp` (~3300 linii) — podział; helper `transition(image, from, to)` albo prosty render graph (`RenderOrchestrator`/`renderPlan_` istnieje, ale nieużywany)
- [ ] Cienie punktowe — teraz jedna perspektywa 120° w dół; potrzebne cube mapy lub dual-paraboloid
- [ ] Push constants 224 B (gwarantowane minimum 128 B) — dane materiału do SSBO z indeksem
- [ ] UBO/SSBO per klatka dla danych obiektów zamiast push constants, gdy scena urośnie
- [ ] Ścieżki `bin/assets/...` zależą od katalogu roboczego — ścieżka bazowa przez `SDL_GetBasePath`
- [ ] `-Wall -Wextra` w `cmake/ProjectSettings.cmake`, w Debug także `-fsanitize=address,undefined`
- [ ] Shadery HLSL kompiluje `glslangValidator` (brak `dxc` w PATH, choć README go wymaga) — nie obsługuje części atrybutów `[[vk::...]]`
- [ ] Tekstury osadzone w GLTF/GLB są zapisywane do `/tmp/gltf_embed_*.bmp` i wczytywane z dysku — wczytywać prosto z pamięci (`engine/object/gltf/model_gltf.cpp`)

---

## 4. Teren 3D (porównanie ze starym systemem)

- [x] LOD stitching (`TerrainIndexGenerator`, `computeStitchConfig`)
- [x] Multi-texturing z maskami (do 12 warstw)
- [x] Dynamiczny LOD wg odległości od kamery
- [x] Frustum culling węzłów quadtree
- [x] Znajdowanie sąsiadów do stitchingu
- [x] Teselacja zagęszczająca siatkę
- [x] Wygładzanie krawędzi do poziomu wody, generatory, heightmapa, XML/JSON, zapytania wysokości
- [ ] Przypinanie obiektów do węzłów quadtree (teraz `TerrainPlacementComponent` tylko dociąga wysokość)
- [ ] Debug rendering — wireframe węzłów quadtree

---

## 5. Pomysły na przyszłość

- **Mesh shaders** (task + mesh) — culling i generowanie geometrii na GPU z danych w SSBO.
- **ECS w stylu data-oriented** — encja jako ID, komponenty jako czyste struktury w ciągłych tablicach, logika w systemach (ewentualnie biblioteka EnTT). Hierarchia przez `HierarchyComponent` + `TransformSystem` (częściowo już jest).
- **Zrównoleglenie aktualizacji ECS** — `Scene::updateEntities` w paczkach na `JobSystem`.

## 6. settlers

Rozgrywka (największy efekt):


Zapis i wczytanie gry. Plik ustawień w JSON już jest, a świat da się zapisać jako ziarno mapy plus listy budynków, jednostek i surowców.
Mgła wojny i zwiad: teren wroga jest niewidoczny, dopóki nie podejdą tam twoje jednostki. Można ją zrobić jako teksturę widoczności nakładaną w shaderze terenu i na minimapie.
Silnik (przyda się też innym przykładom):

Instancing: drzewa, słupki granicy i jednostki to setki osobnych obiektów. Rysowanie ich jedną komendą na typ siatki mocno podniesie FPS przy dużych mapach.
Animacje jednostek: szkieletowe albo proste chodzenie i machanie narzędziem, zamiast przesuwania brył.
Cząsteczki: dym z kominów, pożar przy paleniu budynków, kurz przy budowie.
Szybkie usprawnienia (godzina–dwie każde):

statystyki na koniec partii (wykres surowców, armii i strat w czasie);
skróty i kolejki w koszarach, punkt zbiórki dla nowych żołnierzy;
podgląd zasięgu terytorium nowej wieży przy jej stawianiu.