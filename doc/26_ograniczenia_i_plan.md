# Znane ograniczenia i plan rozwoju

Ten dokument zbiera to, co użytkownik silnika powinien wiedzieć: **obecne
ograniczenia** (i obejścia) oraz **kierunki rozwoju**. Nie jest to pełna lista
zadań — techniczny backlog znajduje się w [../TODO.md](../TODO.md), a historia
wykonanych prac w [../changelog.md](../changelog.md).

---

## 1. Ograniczenia renderowania

### Mipmapy skompresowanych tekstur

`vkCmdBlitImage` nie obsługuje formatów blokowych (DXT/BC). DDS **z gotowymi
mipmapami** działa dobrze; DDS bez mipmap jest dekodowany do RGBA8 i mipmapy
generuje GPU. Tekstura skompresowana bez mipmap może migotać w oddali.

**Obejście:** używaj DDS z mipmapami albo nieskompresowanych tekstur, dla
których `vk::generateMipmaps` działa.

### Limit tekstur bindless

Tablica bindless ma `kMaxBindlessTextures = 1024` slotów. `nextTextureIndex_`
nie jest obecnie porównywany z limitem, a indeksy nie są zwalniane.

**Obejście:** trzymaj liczbę unikalnych tekstur poniżej ~1000; łącz klatki
animacji w atlasy.

### Mgła: dwie warstwy naraz

Włączenie w mapie `"fog": {"enabled": true}` uruchamia **liniową mgłę
deferred** i **mgłę wolumetryczną** (jeśli jest dodana). Efekt może być zbyt
gęsty.

**Obejście:** wybierz jedną mgłę albo zmniejsz `density`/`fogEnd`.

### Ocean a głębia

Ocean nie zapisuje głębi, więc mgła nad wodą liczy się do dna/nieba i bywa
zbyt gęsta. To ograniczenie kolejności przebiegów (ocean jest przezroczysty).

**Obejście:** mniejsza gęstość mgły nad wodą lub lokalne wolumeny.

### Cienie świateł punktowych

Światła lokalne dostają jedną perspektywę 120° w dół (atlas 2×2). To nie jest
pełny cień we wszystkich kierunkach.

**Obejście:** ważne cienie rób światłem kierunkowym/reflektorem; punktowe
traktuj jako efekt świetlny.

### Push constants

`MaterialPushConstants` ma 240 B, a layouty materiałów rezerwują 256 B.
Vulkan **1.4**, którego wymaga ten silnik, gwarantuje minimum 256 B
`maxPushConstantsSize`. Dawne minimum 128 B dotyczy wersji 1.0–1.3;
nie jest obecnym ograniczeniem zgodnego urządzenia 1.4.
[Wymagania wersji Vulkan według Khronos](https://docs.vulkan.org/guide/latest/versions.html).

Ograniczeniem pozostaje stały rozmiar: dodawanie kolejnych parametrów może
przekroczyć 256 B. Wtedy potrzebny będzie bufor danych obiektów/materiałów
(np. SSBO), zamiast dalszego powiększania push constants.

### Skydome na horyzoncie

Skydome jest wycinkiem kuli — w oddali może być widać ciemną krawędź.

**Obejście:** zwiększ `domeScale`, użyj skyboxa albo chmur przy horyzoncie.

### Nagrywanie komend na jednym wątku

Wszystkie przebiegi są nagrywane w wątku głównym. Przy bardzo dużej liczbie
obiektów CPU może być wąskim gardłem.

**Obejście:** mniej draw calli (instancing, łączenie siatek), `ENGINE_PROFILE=1`.

### Brak frustum cullingu siatek

Frustum culling jest tylko dla terenu. Wszystkie `RenderPacket` trafiają do
przebiegów (poza `shadowOnly`).

**Obejście:** dziel scenę na grupy i usuwaj niewidoczne encje samodzielnie.

### Roślinność bez instancingu

Jedna roślina = jedna encja = osobny draw call (plus kaskady cieni). Przy dużych
lasach to kosztowne.

**Obejście:** mniejsza `density`, mniejsze mapy, łączenie siatek.

---

## 2. Ograniczenia danych i logiki

### Delta time w milisekundach

`Engine::calculateDeltaTimeMs` liczy `dt` w milisekundach. Przy bardzo wysokim
FPS może wyjść 0 (powyżej ~1000 FPS) albo szarpanie.

**Obejście:** włącz VSync (`PresentMode::Fifo`) lub `setFrameRateLimit(240)`.

### `loadMap` i modele szkieletowe

`loadMap` trzyma jeden `currentBrainModel_` dla wszystkich wpisów
`gltf_skinned_models` — drugi wpis nadpisuje pierwszy. Ponowne `loadMap` nie
czyści starych encji.

**Obejście:** jeden model szkieletowy na mapę albo osobna scena na mapę.

### `JobSystem` przy `hardware_concurrency() == 0`

Konstruktor zakłada, że system poda co najmniej 1 rdzeń. Na egzotycznych
platformach może utworzyć zbyt wiele wątków.

**Obejście:** podaj `threadCount` jawnie.

### Kolejność paczek

Kolejność paczek po scaleniu buforów wątków nie jest deterministyczna. Nie ma
to wpływu na wynik obrazu (poza kolejnością rysowania przezroczystych, którą
i tak sortujemy), ale utrudnia porównywanie zrzutów.

### Ścieżki zasobów

Ścieżki `assets/...` są względne wobec katalogu roboczego. Uruchamianie
z innego katalogu kończy się brakiem plików.

**Obejście:** uruchamiaj z katalogu głównego repo albo z katalogu paczki
(`uruchom.sh` w `10_settlers`).

### Tekstury osadzone w glTF

Osadzone tekstury GLB są zapisywane do `/tmp/gltf_embed_*.bmp` i wczytywane
z dysku. W systemach z read-only `/tmp` lub przy braku miejsca może to zawieść.

### Ładowanie tekstur

Każda tekstura to osobny submit z `vkQueueWaitIdle`. Ładowanie wielu tekstur
jest więc wolniejsze niż mogłoby być.

**Obejście:** ładuj raz, używaj DDS z mipmapami, unikaj setek małych plików.

### `TerrainPlacementSystem` co klatkę

System liczy wysokość terenu dla wszystkich encji z
`TerrainPlacementComponent`, także statycznych.

**Obejście:** dla statycznych obiektów ustaw Y raz przy tworzeniu i usuń
komponent.

### Ostrzeżenia kompilatora i sanitizery

`-Wall -Wextra` nie jest domyślnie włączone, a CMake nie dodaje ASan/UBSan.
Warto je włączyć ręcznie przy większych zmianach (patrz
[22_budowanie_i_wdrazanie.md](22_budowanie_i_wdrazanie.md)).

### Kompilacja shaderów

Domyślnie używany jest `glslangValidator` (o ile nie ma `dxc`). Część atrybutów
`[[vk::...]]` może nie być obsługiwana. `dxc` jest opcjonalny.

**Obejście:** zainstaluj `dxc` przy nietypowych bindingach; nie polegaj na
`[[vk::...]]` w shaderach przenośnych.

---

## 3. Plan rozwoju

### Rendering

* **HDR + tonemapping** — render do `R16G16B16A16_SFLOAT`, ACES/AgX, kopia do
  swapchaina. Upraszcza też kolor sceny dla oceanu.
* **Render graph** — automatyczne bariery i kolejność przebiegów; obecnie
  kolejność jest ręczna w `render()`.
* **Instancing roślinności i jednostek** — jeden draw na gatunek/chunk świata.
* **Frustum culling siatek** i culling per kaskada cienia.
* **Cienie punktowe** — cube mapy lub dual-paraboloid.
* **Mesh shaders** — culling i generowanie geometrii na GPU.
* **SSBO na dane materiałów** — zamiast push constants przy dużych scenach.

### Architektura

* **Data-oriented ECS** — komponenty w ciągłych tablicach, systemy masowe
  (ewentualnie EnTT).
* **Równoległa aktualizacja ECS** — `Scene::updateEntities` w paczkach na
  `JobSystem`.
* **Wielowątkowe nagrywanie komend** — secondary command buffers.
* **Asynchroniczne ładowanie zasobów** — tekstury i modele w tle, placeholdery.

### Teren

* Przypinanie obiektów do węzłów quadtree (teraz
  `TerrainPlacementComponent` tylko dociąga wysokość).
* Debug rendering — wireframe węzłów quadtree.

### Gra `10_settlers`

* Instancing drzew i jednostek. Słupki granic są już łączone w jedną siatkę
  na frakcję (`rebuildTerritoryMesh`).
* Animacje szkieletowe jednostek gry. Proste chodzenie i machanie przez
  transformacje są już realizowane w `syncVisuals`.
* Cząsteczki: dym z kominów, pożar, kurz przy budowie.
* Rozbudowa statystyk na koniec partii i podglądu zasięgu nowej wieży.
  Kolejki szkolenia i auto-rekrutacja koszar są już zaimplementowane.

Pełne, techniczne zadania z odnośnikami do plików: [../TODO.md](../TODO.md).
