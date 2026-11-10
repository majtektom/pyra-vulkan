# changelog

## 0.7.0

Przykład `10_settlers` — gra strategiczna w stylu "Osadników" (budowa, ekonomia, walka z AI).

Nowe w silniku:
 - Cienie terenu teselowanego (fix "plam cieni znikąd" w 08_game): teren rysuje się do mapy cieni tym samym
   shaderem co na ekranie (`terrain_shadow`: teselacja + wysokość z mapy, wspólna funkcja
   `shared/terrain_height.hlsl`), więc cień powstaje z widocznej geometrii - wcześniej rzucała go siatka CPU 17x17
   (wierzchołek co 2 teksele), która w dolinach leżała nad terenem; mapa wysokości na GPU 16-bit z zaokrągleniem
   (było 8-bit z obcięciem), próbkowanie w środku teksela i zawsze mip 0; odrzucanie obiektów spoza obszaru kaskady
 - zwykła siatka 17x17 ma wierzchołek
   co 2 teksele i w dolinach leżała nad widocznym terenem; mapa wysokości na GPU 16-bit z zaokrągleniem (było 8-bit
   z obcięciem), próbkowanie w środku teksela i zawsze mip 0; odrzucanie obiektów spoza obszaru kaskady
 - Cienie słońca: słońce pod horyzontem nie rzuca cieni (mapa cieni nie jest renderowana), przy horyzoncie cienie
   zanikają łagodnie; w trybie bez oświetlenia siła cienia zależy od siły słońca (w nocy brak cieni);
   `depthClamp` tylko w potokach kaskad słońca (osobne potoki `shadowSunPipeline_`) - obiekty przed bliską płaszczyzną
   kaskady nie znikają z mapy; światła lokalne (rzut perspektywiczny) bez depthClamp, bo wyłącza on obcinanie
   geometrii za "okiem" światła (cienie światła punktowego w 07 znikały, gdy światło zjeżdżało poniżej słupa)
 - 08_game: `GAME08_NOLIGHT=1`, `GAME08_NOFOG=1` - bez oświetlenia / mgły (podgląd samych cieni)
 - Teren rzuca cienie także spoza kadru: węzły pominięte przy rysowaniu (za kamerą), ale bliżej niż
   `Terrain::setShadowCasterDistance` (domyślnie 2000), trafiają do mapy cieni jako `RenderPacket::shadowOnly` -
   wcześniej wzgórze za kamerą nie rzucało cienia, a cień pojawiał się/znikał przy obrocie kamery
 - Automatyczne dopasowanie kaskad cieni słońca do widocznej geometrii: renderer wyznacza zakres głębokości,
   w którym w kadrze są obiekty (granice siatek `GpuMesh`/`RenderPacket::boundsMin/Max`, węzły terenu), kaskady
   zaczynają się przy najbliższej widocznej geometrii, a zasięg (`configureDirectionalShadow(halfSize)`) liczony jest
   od niej - przy oddaleniu kamery cienie nie znikają i są ostrzejsze (gry ustawiające `startDistance` > 0 bez zmian)
 - 08_game: `GAME08_CAMY=<wysokość>` - kamera wyżej nad terenem (test cieni)
 - Cienie słońca: płynny ruch razem ze słońcem (bez skokowej kwantyzacji kierunku), wygładzony w czasie zakres
   kaskad (automatyczne dopasowanie i zasięg w 10_settlers), łagodne wygaszanie w ostatnich 15% zasięgu
 - Skydome: siła słońca zależna od jego wysokości (pełna od ~18°, słabnie do horyzontu), barwa z pory dnia
   znormalizowana - wcześniej siła spadała 7x między 16:00 a 17:00 i cienie gasły długo przed zachodem
 - 08_game: `GAME08_MAP=<mapa>` i `GAME08_HOUR=<godzina>` - start od razu w grze (testy, zrzuty ekranu)
 - `Material::depthPrepass` — półprzezroczyste obiekty bez widocznego wnętrza (podglądy budowy, hologramy):
   `TransparentDepthPrepass` zapisuje najpierw samą głębokość obiektu (część 1 klatki, gdy depth jest zapisywalny),
   potem TransparentPass rysuje tylko najbliższą powierzchnię; wspólne rysowanie paczek `render/packet_draw.h`
   (OpaquePass/TransparentPass/prepass); obejście awarii sterownika NVIDIA przy `colorWriteMask = 0` (mieszanie ZERO/ONE)
 - `feature/gui/gui_toolbar.h` — `GuiToolbar` (pasek z grupami ikon, skrótami, stanem enabled/active) i `GuiCard`
   (karta informacji: tytuł, opis, żetony kosztu, ostrzeżenie); `feature/gui/gui_pixel_icon.h` — ikony pixel-art z tekstu
   z domyślną paletą; demo w `05_gui_demo`
 - `GuiMgr::addOverlayLine/addOverlayBar/addOverlayText/addOverlayTextCentered`, `setTextMeasureFn`/`measureText`
 - `core/game_settings.h` — `GameSettings`: ustawienia gracza (dźwięk, okno, VSync, synchronizacja, FPS, cienie),
   JSON load/save, stosowanie w silniku i audio, lista rozdzielczości (przeniesione z 10_settlers)
 - `core/noise.h` — `ValueNoise` (value noise + fbm), `core/math.h` — `distanceXZ`, `smoothstep`, `rotateY`, `angleLerp`
 - `camera/camera_picking.h` — `screenPointToRay`, `worldToScreen`, `rayPlaneY`, `raySphere`
 - `camera/rts_camera_controller.h` — kamera strategiczna (pan/zoom/obrót/nachylenie z wygładzaniem, granice, wysokość terenu)
 - `CameraComponent::aspectRatio` / `autoAspect` — proporcje rzutowania zgodne z oknem (wcześniej na sztywno 4:3)
 - `Terrain::raycast`, `getNormal`, `containsXZ`, `setHeightmap` (heightmapa z pamięci)
 - `feature/ai/nav_grid.h` — siatka nawigacyjna z A* (8 kierunków, przeszkody zliczane referencyjnie, wygładzanie ścieżki)
 - `core/mesh_builder.h` — budowanie siatek low-poly z prymitywów z kolorami w wierzchołkach
 - `GuiMgr::addOverlayRect/addOverlayFrame` (HUD), `isPointerOverGui`, `GuiWindow::setPosition/setSize`
 - `feature/audio/audio_system.h` — dźwięk: mikser SDL3, OGG przez stb_vorbis, dźwięk 3D, muzyka strumieniowana z przenikaniem
 - `InputSnapshot::mouseWheelF` — precyzyjne kółko myszy/touchpad (wcześniej ułamki ginęły przy rzutowaniu na int)
 - Osadnicy: łańcuchy produkcji - 12 towarów (`CarryType`: kłody, kamień, deski, zboże, mąka, chleb, ryby, mięso,
   węgiel, ruda, żelazo, broń) i `Resources` jako tablica z pseudo-towarem "dowolna żywność" w kosztach; nowe budynki:
   Tartak, Młyn, Piekarnia, Rybak (nad wodą), Myśliwy, Kopalnia węgla, Kopalnia rudy, Huta, Kuźnia; tabela
   `productionDef` (zachowanie pracownika, wyjście, wejścia) zamiast warunków na typ budynku; warsztaty zamawiają
   wejścia z magazynów (dostawy tragarzy), kopalnie zjadają żywność, żołnierze wymagają broni; budowa i naprawy
   w deskach; pasek budowy w 6 grupach (`GuiToolbar::Style::areaLeft/areaRight`), okno gospodarki z 12 towarami,
   znacznik "brak dostaw"; AI buduje pełne łańcuchy; test `productionChainTest`
 - Osadnicy: dolny pasek budowy zamiast okna (`app_buildbar.cpp`) — ikony pixel-art w grupach Mieszkańcy/Surowce/
   Logistyka/Wojsko, karta informacji z opisem i kosztem (brakujące surowce na czerwono), klawisze skrótu na ikonach;
   panel zaznaczenia nad paskiem, okna akcji przy lewej krawędzi
 - Osadnicy: drogi, flagi i tragarze (`world_roads.cpp`) — towary z warsztatów czekają na fladze (max 8, pełna flaga
   wstrzymuje produkcję); wspólna pula tragarzy (liczba ustawiana w oknie Gospodarka (I): −/+/Auto) — wolny tragarz
   niesie towar całą trasą po drogach do najbliższego magazynu z miejscem, a materiały z magazynu na place budowy
   i koszt szkolenia do koszar (budowa/szkolenie postępują w miarę dostaw); po drogach chodzi się 1,5x szybciej;
   surowce są dostępne dopiero w magazynie (zamek/Magazyn po 200); nowy budynek Magazyn (9); budowa dróg klawiszem T
   z podglądem trasy, flagi na skrzyżowaniach, budynek postawiony przy drodze łączy się z nią; przycisk
   "Połącz drogą"; AI buduje drogi i magazyny; testy transportu, dostaw i budowy drogi myszą w SETTLERS_UITEST
 - Osadnicy: przejmowanie terenu — gdy zasięgi zamków/wież się nakładają, punkt należy do budynku, w którego zasięgu
   leży najgłębiej (`World::territoryOwnerAt`); wspólna granica z dwukolorowymi słupkami, budynki na przejętym terenie płoną
 - Osadnicy: teren jako wyspa (brzegi schodzą do morza), masywy górskie z łagodnymi zboczami (dostępne do chodzenia
   i budowy) i ośnieżonymi, niedostępnymi szczytami; nowe budynki: Kopalnia (kamień z wnętrza góry, tylko w górach)
   i Leśniczy (sadzi drzewa, które po 2 min drwal może ściąć); panel budowy 1-8; tryb SETTLERS_SHOWCASE=1
 - Mapy cieni: 5 warstw zamiast 64 (ok. 80 MB zamiast ok. 1 GB VRAM) - kaskady tylko dla słońca (pierwsze światło
   kierunkowe), światła lokalne w atlasie 2x2 (1024^2 każde, do 4 najbliższych kamerze świateł, których zasięg
   sięga obszaru cieni); przy cieniu światła lokalnego rysowane są tylko obiekty w jego zasięgu
 - Cienie: bufor pozycji świata w G-Bufferze w 32-bit float (half float dawał krok 0,25 j. przy współrzędnych ~300
   i regularne paski "shadow acne"); normal offset i bias skalowane rozmiarem teksela kaskady; podział kaskad
   "practical split" + `configureDirectionalShadow(halfSize, startDistance, splitLambda)`; zmiana wspólnych plików
   shaderów (`shared/*.hlsl`, `*_common.hlsl`) przebudowuje teraz zależne shadery
 - `Engine::mouseDown/mousePressed/mouseReleased`, obsługa środkowego przycisku myszy, `Engine::measureText`

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
