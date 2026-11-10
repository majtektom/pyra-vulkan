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

Instancing: drzewa, słupki granicy i jednostki to setki osobnych obiektów. Rysowanie ich jedną komendą na typ siatki mocno podniesie FPS przy dużych mapach.
Animacje jednostek: szkieletowe albo proste chodzenie i machanie narzędziem, zamiast przesuwania brył.
Cząsteczki: dym z kominów, pożar przy paleniu budynków, kurz przy budowie.
Szybkie usprawnienia (godzina–dwie każde):

statystyki na koniec partii (wykres surowców, armii i strat w czasie);
skróty i kolejki w koszarach, punkt zbiórki dla nowych żołnierzy;
podgląd zasięgu terytorium nowej wieży przy jej stawianiu.



 od największego zysku:
1. Optymalizacje o najlepszym stosunku zysku do pracy
Minimapa jako tekstura, nie 4096 prostokątów — app_hud.cpp:232-243 tworzy 64×64 = 4096 addOverlayRect co klatkę (plus ikony). To stały koszt niezależny od sceny. Trzeba dodać do GUI widget obrazka/tekstury i API Renderer::updateTexture (dziś jest tylko loadTextureFromMemory, renderer.h:94), a minimapę odświeżać tylko gdy zmieni się mgła/obiekty.
Rozdzielić symulację od prezentacji — app_core.cpp:495 woła całe World::update gameSpeed_ razy (do x24), a w środku jest syncVisuals (world_update.cpp:166), mgła i przebudowa terytorium/regionów. Przy x24 to 24× zapisy transformów i upload tekstury mgły na klatkę. Wprowadziłbym stały krok z akumulatorem (limit podkroków, np. 8), a sync modeli, upload mgły i rebuildTerritory robił raz na klatkę. To naprawia też rozjazd czasu przy zacięciu (dt obcinane do 0,1 s, world_update.cpp:109).
Transport: jedna Dijkstra na źródło, nie na zlecenie — world_roads.cpp:964-1080: dla każdego tragarza liczone są networkDistances(u.atFlag), a potem wewnątrz pętli po zleceniach kolejne networkDistances(src) (1006-1008) i jeszcze raz dla wybranej flagi (1037). networkDistances alokuje dwa wektory przy każdym wywołaniu (523-546). Do tego fundOrder robi Dijkstrę dla każdego nieopłaconego zlecenia co 0,5 s (633-703). Rozwiązanie: cache odległości per flaga ważny do routingDirty_, przeliczany raz na tick; zlecenia sortowane po źródle.
Siatki przestrzenne — dziś: territoryOwnerAt skanuje wszystkie budynki (world_queries.cpp:72-86), isInTerritory robi do 17 takich wywołań (90-106), canPlace dodatkowo skanuje budynki/węzły/wszystkie punkty dróg (117-190) i jest wołane co klatkę w podglądzie stawiania (app_input.cpp:382), a findPlacementNear próbuje 120× (408-419). findEnemyUnitNear/findEnemyBuildingNear (373-405) to O(żołnierze × jednostki). Wzorzec już jest w separateUnits (world_movement.cpp:202-232) — warto go uogólnić na jednostki, budynki, węzły i terytorium.
Drogi: batchowanie i wyciek siatek — rebuildRoadCells (pełne 256×256 + wszystkie punkty dróg) leci przy każdym createRoad/removeRoad (world_roads.cpp:258,272), a buildRoad tworzy wiele odcinków. Nazwy siatek "road_" + ++meshCounter (327-335) nigdy nie są zwalniane, bo MeshManager nie ma removeMesh (mesh_manager.cpp:64). Każda budowa/rozbiórka drogi i każde wczytanie zapisu zostawia VRAM. Fix: stabilna nazwa + updateMesh albo removeMesh i dirty-flag na przebudowę komórek raz na tick.
Mgła wojny — world_fog.cpp:77-125: co 0,25 s gry pełne fill 65 536 komórek, revealCircle dla każdej jednostki (przy zasięgu 80 j. ~5000 komórek), rozmycie 3×3 (590 tys. operacji), a przy x24 to 96 razy na sekundę realnie. Dodatkowo mgła ustawia territoryDirty_ (121-124), sprzęgając ją z bardzo drogim rebuildTerritory. Rozwiązanie: rozmycie rozdzielne, odświeżanie tylko komórek wokół jednostek, która faktycznie się ruszyły; terytorium debounce raz na klatkę.
2. Silnik/renderer
G-Buffer: brak sortowania i zbędne bindowanie — renderer_core.cpp:493-558: dla każdej paczki ponownie vkCmdBindPipeline, vkCmdBindDescriptorSets (ten sam set 0 dla wszystkich!), vertex/index buffer i push constants. Sortowanie front-to-back + po (pipeline, mesh) i cache „ostatnio bindowane” to zwykle największy zysk CPU przy tysiącach obiektów.
Cienie liczone nawet gdy wyłączone — render_pass.h:17-40 kopiuje każdą paczkę do shadowPackets, renderer_core.cpp:211 zawsze woła updateVisibleDepthRange, a assignShadowSlots alokuje i sortuje. Przy shadowsEnabled_ == false cały ten tor powinien być pominięty; cull 8 narożników policzyć raz na światło, nie dla każdej paczki i kaskady.
Bufory siatek w pamięci hosta — mesh_manager.cpp:47-73 alokuje wierzchołki/indeksy jako HOST_VISIBLE|HOST_COHERENT. Statyczna geometria powinna iść do DEVICE_LOCAL przez staging. updateMesh (88-116) dodatkowo re-uploaduje niezmienione indeksy MD2 co klatkę.
Cząsteczki i audio — emitter.cpp:181,342 tworzy std::async co klatkę per emitter (jest przecież JobSystem); audio_system.cpp:292-335 alokuje std::vector i dekoduje OGG w wątku audio pod mutexem trzymanym przez cały mix(). To proszenie się o glitche i blokowanie klatki. Real-time audio wymaga preallokowanych buforów pierścieniowych i kolejki komend z głównego wątku.
Architektura — renderer_core.cpp ma 3894 linie i miesza alokację, tekstury, mgłę, cienie i UI; RenderOrchestrator/renderPlan_ są martwe (3622-3631); layout push-constants istnieje w trzech kopiach (packet_draw.cpp:51, renderer_core.cpp:325,519). Wydzieliłbym ShadowPass, GBufferPass, DeferredPass, SceneColorCopyPass, OverlayPass i jeden wspólny nagłówek push-constants. RenderPacket z 24 polami terenu w każdej paczce (render_types.h:41-89) to też powód dużych kopii.
3. Co bym dodała (funkcje)
Widget obrazka w GUI + dynamiczna tekstura — od razu rozwiązuje minimapę i pozwala robić podglądy/portrety.
Overlay profilera w grze — Profiler::lastReport() już istnieje, brak tylko wyświetlania; plus benchmark regresyjny (250 jednostek/100 budynków < X ms).
Stabilne uchwyty z generacją zamiast indeksów w wektorach z martwymi wpisami (settlers_world.h:10-14); listy żywych obiektów, bo wektory nigdy nie są kompaktowane i długie partie skanują coraz więcej trupów.
Wersjonowanie zapisu + migracje; obecny saveLoadTest (app_tests.cpp:731-768) nie wykryje pola pominiętego w fields(...), bo porównuje JSON z JSON-em. Potrzebny test „ustaw wszystkie pola na niezerowe → round-trip”.
Testy izolowane per seed — dziś wszystkie dzielą jeden World (app_tests.cpp:1098-1113) i losową mapę, więc awaria jednego zanieczyszcza następne.
AI: cache ekonomii i deterministyczne szukanie miejsca zamiast 120 losowych prób; opcjonalnie mgła wojny dla AI jako poziom trudności.
Sterowanie grupami/patrol/attack-move, flow-field lub lokalne unikanie zamiast samego rozpychania; UI kolejki produkcji.
4. Kolejność, którą proponuję
Zmierzyć: ENGINE_PROFILE=1 SETTLERS_AUTOPLAY=900 SETTLERS_SPEED=24 i SETTLERS_STRESS=200 — bez tego reszta to zgadywanie.
Minimapa-tekstura + rozdzielenie symulacji/prezentacji (mały zakres, duży zysk).
Cache transportu i siatki przestrzenne (największe O(n²)).
Batchowanie renderera + pomijanie cieni, gdy wyłączone.
DEVICE_LOCAL dla siatek, naprawa wycieku siatek dróg, wątki cząsteczek/audio.
Testy izolowane + benchmark regresyjny, potem refaktoryzacja World/Renderer na moduły.