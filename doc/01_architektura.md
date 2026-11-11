# Architektura silnika

Ten dokument jest mapą całości: co jest czym, kto kogo zna, w jakiej kolejności
dzieje się klatka. Szczegóły modułów są w dokumentach 02–21.

---

## 1. Filozofia

Silnik jest **edukacyjny**, więc stawia na jawne, czytelne mechanizmy zamiast
magii frameworka. Cztery zasady przewodnie:

1. **Separacja odpowiedzialności.** Gra (`World`, `SettlersApp`) nie dotyka Vulkana.
   Renderer nie zna pojęcia „budynek” ani „gracz” — dostaje tylko paczki geometrii
   i widok kamery.
2. **Dane oddzielone od logiki (ECS-lite).** `Entity` to identyfikator i worek
   komponentów. Systemy (`ISceneSystem`) przetwarzają całe grupy encji.
3. **Jedno źródło prawdy.** Oświetlenie i powierzchnia są opisane raz, w
   `assets/shaders/shared/` — nie kopiuje się ich do shaderów. Układ push
   constants jest jeden (`render/push_constants.h` + odpowiedni `.hlsl`).
4. **Klatka jako przepływ danych.** Wejście → aktualizacja → ekstrakcja →
   renderowanie → prezentacja. Każdy etap ma własne struktury danych.

---

## 2. Warstwy

```plantuml
@startuml
skinparam packageStyle rectangle
top to bottom direction

package "Aplikacja (examples)" {
  [gra / przykład]
}

package "Scena i logika" {
  [Scene / SceneManager]
  [Entity + komponenty]
  [systemy ECS]
  [World / AI / gry]
}

package "Renderowanie" {
  [Renderer]
  [przebiegi: cienie, G-Buffer, deferred...]
  [materiały / tekstury]
  [IRenderFeature: GUI, ocean, mgła...]
}

package "Fundament" {
  [Engine]
  [Platform (SDL3)]
  [Core: math, job, logger, zasoby]
  [VulkanContext / swapchain / VMA]
}

[gra / przykład] --> [Engine]
[gra / przykład] --> [Scene / SceneManager]
[Scene / SceneManager] --> [Entity + komponenty]
[Scene / SceneManager] --> [systemy ECS]
[systemy ECS] --> [Renderer]
[Engine] --> [Renderer]
[Engine] --> [Platform]
[Renderer] --> [przebiegi: cienie, G-Buffer, deferred...]
[przebiegi: cienie, G-Buffer, deferred...] --> [VulkanContext / swapchain / VMA]
[Renderer] --> [IRenderFeature: GUI, ocean, mgła...]
@enduml
```

* **Fundament** nie wie nic o grze. Można go użyć w dowolnej aplikacji.
* **Scena** zna komponenty i systemy, ale nie wydaje poleceń Vulkan.
* **Renderowanie** operuje na spłaszczonych danych (`RenderPacket`, `RenderView`).
* **Aplikacja** decyduje, co jest w świecie i jak reaguje na wejście.

---

## 3. Własność obiektów

| Obiekt | Właściciel | Cykl życia |
|---|---|---|
| `VulkanContext`, `VulkanSwapchain` | `Engine` (przez `Renderer`) | `initialize` → `cleanup` |
| `Renderer` | `Engine` (`unique_ptr`) | `initialize` → `cleanup` |
| `Scene` | `SceneManager` (`unique_ptr`) | `createScene` → `removeScene`/koniec |
| `Entity` | `Scene` (`vector<unique_ptr>`) | `createEntity` → `removeEntity` |
| `Component` | `Entity` (`vector<unique_ptr>`) | `addComponent` → zniszczenie encji |
| `MeshData` w RAM | `MeshManager` (nazwa → `GpuMesh`) | `loadMesh` → `removeMesh` |
| tekstura GPU | `Renderer` (mapa nazwa → `TextureResources`) | `loadTexture` → `retireTexture` |
| `Material` (dane) | `MaterialManager` | `add` → koniec |
| `MaterialInstance` (potok) | `MaterialSystem` (cache) | na żądanie, do końca |

`Entity*` i `Component*` są **nietrwałe** — wektor encji może się przebudować.
W kodzie gry trzymaj `EntityId` albo nazwę, a wskaźnik pobieraj na bieżąco.
W `10_settlers` służą do tego `Ref<T>` (indeks + generacja).

---

## 4. Pętla gry

`Engine::run()` to klasyczna pętla „input → update → render → limiter”.

```plantuml
@startuml
title Engine::run()
start
:processEvents();
:pollInput() -> currentInput_;
:previousInput_ = currentInput_;
repeat
  :processEvents();
  :previousInput_ = currentInput_;
  :currentInput_ = pollInput();
  if (okno zamknięte?) then (tak)
    break
  endif
  :dt = calculateDeltaTimeMs();
  :update(dt);
  :renderFrame();
  if (limit FPS > 0?) then (tak)
    :sleep(do limitu);
  endif
repeat while (running && !shouldClose)
:renderer->waitIdle();
stop
@enduml
```

### `update(dt)` — kolejność

1. `MeshManager::beginFrame()` — odlicza wiek buforów wycofanych z użycia.
2. Aktualizacja `aspectRatio` kamer (`autoAspect`).
3. `onUpdate(dt, engine)` — logika aplikacji.
4. `ShaderHotReload::check()` (jeśli włączony).
5. Równolegle: animacje glTF (kości) i MD2 (morfing) przez `JobSystem::parallelFor`.
6. `Scene::updateEntities(dt)` — `Component::update` każdej encji.
7. `SceneManager::update(dt)` — systemy ECS w kolejności dodania.

### `renderFrame()` — kolejność

1. `renderScene.clear()`; ustawienie zbierania cieni.
2. `renderer->setLights(scene->frameLights())`.
3. Skopiowanie `scene->mutableRenderPackets()` do `RenderScene` (przenoszenie, bez kopii).
4. `MeshManager::flushUploads()` — nowe siatki trafiają na GPU.
5. `renderer->render(renderScene, renderView)`.

`RenderView` jest liczone przez `CameraExtractionSystem` w fazie systemów,
więc w `renderFrame` jest już gotowe.

---

## 5. Przepływ danych klatki

```plantuml
@startuml
title Od komponentów do pikseli

actor "Gracz" as Player
participant "Platform\n(SDL3)" as Platform
participant "Engine" as Engine
participant "Scene\n+ systemy ECS" as Scene
participant "Renderer" as Renderer
participant "GPU\n(Vulkan)" as GPU

Player -> Platform : klawisze, mysz
Platform -> Engine : InputSnapshot
Engine -> Scene : onUpdate / updateEntities
Scene -> Scene : TransformSystem\n(macierze świata)
Scene -> Scene : LightSystem\n(SceneLight[])
Scene -> Scene : CameraExtractionSystem\n(RenderView)
Scene -> Scene : RenderExtractionSystem\n(RenderPacket[])
Scene -> Scene : RenderFramePrepSystem\n(distanceToCamera)
Engine -> Renderer : setLights + RenderScene + RenderView
Renderer -> GPU : cienie (mapy głębokości)
Renderer -> GPU : G-Buffer (albedo, normal, AO/emissive, worldPos)
Renderer -> GPU : deferred (PBR + cienie + mgła)
Renderer -> GPU : kopia koloru sceny (dla oceanu)
Renderer -> GPU : przezroczyste + ocean + mgła wolumetryczna
Renderer -> GPU : nakładka (GUI, tekst, kursor)
GPU -> Player : obraz (present)
@enduml
```

Dlaczego ekstrakcja jest osobnym krokiem?

* renderer nie odpyta encji w trakcie rysowania (brak zależności zwrotnej),
* paczki można policzyć **równolegle** (`JobSystem`),
* można narysować scenę z innego wątku / opóźnieniem jednej klatki,
* łatwo dodać drugi renderer (np. minimapę) bez duplikowania logiki.

---

## 6. Przebiegi renderera

`Renderer::render` nagrywa polecenia w jednym buforze komend, w tej kolejności:

```plantuml
@startuml
title Przebiegi jednej klatki (jeden command buffer)
start
:preRender feature'ów;
if (cienie włączone?) then (tak)
  :recordShadowPass\n4 kaskady słońca + atlas 2x2 świateł lokalnych;
endif
:recordGBufferPass\ngeometria nieprzezroczysta -> 4 tekstury;
:recordLightingPass\ndeferred: pełnoekranowy trójkąt + forward opaque;
if (jakikolwiek feature potrzebuje koloru sceny?) then (tak)
  :copySceneColor\nkopia koloru + głębi do tekstur;
endif
:recordTransparentPass\nprzezroczyste, ocean, sprite'y, mgła wolumetryczna;
:recordOverlayPass\nGUI, tekst, kursor;
:submit;
:present;
stop
@enduml
```

Kolejność wynika z zależności:

* cienie muszą powstać **przed** G-Bufferem i deferred (są próbkowane),
* G-Buffer musi być gotowy **przed** deferred (deferred czyta 4 tekstury),
* kopia koloru musi powstać **po** deferred, a **przed** oceanem (ocean czyta
  odbicia i załamania),
* nakładka GUI ma własny wyczyszczony bufor głębokości i jest ostatnia.

Szczegóły: [09_render_potoki.md](09_render_potoki.md).

---

## 7. Wątki

| Wątek | Co robi |
|---|---|
| główny | pętla, wejście, `onUpdate`, ECS, nagrywanie komend, submit |
| `JobSystem` (N wątków) | `parallelFor`: ekstrakcja paczek, dystanse do kamery, animacje glTF/MD2, cząsteczki |
| audio (SDL callback) | miksowanie efektów i muzyki, **bez muteksów i alokacji** |
| strumień muzyki | dekodowanie OGG z wyprzedzeniem ~1 s |
| (opcjonalnie) wątek zasobów | obecnie brak asynchronicznego ładowania |

Zasady:

* `JobSystem::parallelFor` wykonuje też wątek wołający. Bufory „na wątek”
  indeksuj `jobs.currentSlot()` (rozmiar `threadCount() + 1`).
* Wątki robocze nie mogą wołać `VulkanContext`/`MeshManager::updateMesh`.
  Wyniki zbieraj i wysyłaj z wątku głównego.
* Wątek audio nie bierze muteksu gry — komunikacja idzie przez kolejkę poleceń.

---

## 8. Jak czytać kod

Kolejność lektury dla nowej osoby:

1. `engine/core/engine.h` + `engine.cpp` — pętla i orkiestracja.
2. `engine/scene/scene.h` + `engine/core/entity.h` — model świata.
3. `engine/scene/*_system.cpp` — co dzieje się z danymi przed renderem.
4. `engine/render/renderer.h` + `renderer_core.cpp` — wejście renderera.
5. `engine/render/renderer_impl.h` — komplet stanu i podział przebiegów.
6. `engine/core/mesh_manager.*`, `engine/render/material_*` — zasoby GPU.
7. `assets/shaders/shared/*.hlsl` — wspólna matematyka oświetlenia.

Dopiero potem przykłady (`examples/`), zaczynając od `01`, `03`, `07`, `10`.
