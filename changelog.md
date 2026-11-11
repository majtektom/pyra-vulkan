# changelog

## 0.7.0

Przykład `10_settlers` — gra strategiczna w stylu "Osadników" (budowa, ekonomia, walka z AI).

Czytelność kodu:
 - Komunikaty logów i wyjątków silnika oraz komentarze po polsku (ok. 120 komunikatów, 45 komentarzy); usunięta
   niedokończona klasa `VulkanImage`; poprawiony komunikat cząsteczek z pojedynczym `{` (błąd formatu w `std::vformat`)
 - Przykłady 03-07 i 09: zamiast jednego długiego `main` (do 460 linii) kroki-funkcje (niebo, teren, modele,
   kamera, HUD); wspólne `FlyCamera` (engine/camera/fly_camera.h - WASD + mysz) oraz `Engine::keyPressed` /
   `requestQuit` zamiast powtarzanej obsługi Esc przez `SDL_EVENT_QUIT`. 06 i 09: kamera startuje nad terenem
   (wcześniej w środku góry - szary ekran)
 - `Engine::loadMap` (495 linii) podzielone na sekcje pliku mapy w `engine/core/engine_map.cpp`
 - `loadGltfModel`, `ParticleSystem::load` (opis formatu .particle), `recordShadowPass`, `recordGBufferPass`
   podzielone na kroki (glTF sprawdzony: wynik identyczny dla 11 modeli)
 - Osadnicy: planista AI (`aiBuildNext`) jako reguły `aiChoose*` z `AiChoice`; panel zaznaczenia na flagę /
   budynek / jednostkę / grupę; tragarz (`updateCarrier`) według stanów; nazwane stałe leczenia i napraw
   wspólne dla symulacji i HUD (`kRecoveryDelay`, `kRepairBelowHp`)
 - `GuiRenderer` i skybox: czytelne nazwy zmiennych zamiast skrótów (`vb_`, `odl`, `przes`...)

Usunięte duplikaty (ok. 1800 linii kodu mniej; zachowanie bez zmian, sprawdzone walidacją i testami):
 - `vk::GraphicsPipelineBuilder` + `vk::createPipelineLayout` + `vk::ShaderModule` (RAII) - 17 potoków w 11 plikach
   (G-Buffer, cienie, deferred, materiały, niebo, ocean, siatka, GUI, tekst, mgła) bez ręcznych struktur Vulkana;
   wspólne ustawienia potoków cieni (`buildShadowPipelinePair`) i G-Buffera (`gbufferPipelineBase`)
 - shadery: jeden blok push constants materiału `shared/material_push.hlsl` (był skopiowany w 6 shaderach)
   i cieni `shared/shadow_push.hlsl` (2 shadery) - układ zgodny z `push_constants.h` w jednym miejscu
 - `SkinVertex` (core/types.h): opis danych kości był powtórzony w G-Buffer, cieniach, materiałach i glTF
 - generator indeksów terenu: 15 ręcznie rozpisanych przypadków zszywania krawędzi (745 linii) zastąpione jedną
   regułą "wachlarza" na krawędź (115 linii); wynik identyczny dla wszystkich rozmiarów siatki, LOD i konfiguracji
 - dekoder DXT1/3/5: wspólne `decodeBc1ColorBlock` / `decodeBc4AlphaBlock` zamiast trzech kopii
 - tekstury: 4 funkcje domyślnych tekstur 1x1 -> `createSolidTexture` (kopiowanie z CPU, bez bufora pośredniego);
   `loadTexture` i `loadTextureFromMemory` dzielą `uploadTexture`; atlas czcionki wgrywany jedną funkcją
   (był skopiowany przy przebudowie atlasu) przez hostImageCopy
 - jedno `vk::beginOneTimeCommands/endOneTimeCommands` zamiast 4 kopii; samplery G-Buffera i mgły przez
   `vk::createClampSampler(device, filtr)`
 - cząsteczki: wspólny krok symulacji dla wersji jednowątkowej i równoległej; Osadnicy: `dist2D` -> `distanceXZ`

Vulkan 1.4 jako minimum (bez ścieżek dla 1.3):
 - Wymagany Vulkan 1.4 z push descriptors i hostImageCopy (wybór karty sprawdza też formaty R8/RGBA8); usunięte
   `apiVersion()`, `ENGINE_VULKAN_API`, rozszerzenie KHR dla push descriptors
 - Synchronizacja 2: wszystkie 31 `vkCmdPipelineBarrier` zastąpione `vk::imageBarrier(s)/memoryBarrier`
   (pola z nazwami, etapy przy każdym obrazie); jedno `vk::generateMipmaps` zamiast dwóch kopii generowania mipmap
 - Push descriptors: oświetlenie deferred (G-Buffer + mapa cieni) i GUI (biała tekstura, obrazki) bez pul i setów
   osobnych na każdą klatkę
 - Kopiowanie obrazów z CPU (hostImageCopy): mgła wojny, obrazki GUI (minimapa) i biała tekstura GUI bez buforów
   pośrednich, poleceń GPU i `vkQueueWaitIdle` - aktualizacja minimapy i mgły to jedno `vkCopyMemoryToImage`

Vulkan 1.4 (wcześniejszy etap - zastąpiony powyżej):
 - Silnik naprawdę korzysta z Vulkan 1.4, gdy jest dostępny (minimum 1.3): wersja API = min(loader, sterownik karty,
   1.4) - wcześniej instancja prosiła o 1.4, ale urządzenie dostawało tylko cechy 1.2/1.3, a VMA miało wpisane 1.4 na
   sztywno (na sterowniku 1.3 wołałoby nieistniejące funkcje). Przy 1.4 włączane są obsługiwane cechy rdzenia 1.4
   (maintenance5/6, pushDescriptor, dynamicRenderingLocalRead, indexTypeUint8). Wybór GPU sprawdza wersję, wymagane
   cechy i swapchain, karta dedykowana ma pierwszeństwo (nie "pierwsza z listy"); log podaje używaną wersję i powód
   pominięcia innych kart. `ENGINE_VULKAN_API=1.3` - wymuszenie 1.3

Usunięty martwy kod (ok. 1100 linii; wszystko jest w historii git):
 - nieużywane zmienne i funkcje z -Wall -Wextra (m.in. model_3ds, model_gltf, sprite, skydome, przykłady 03/06/07/08)
 - całe podsystemy bez użytkowników: `LayerManager` (silnik aktualizował pustą listę warstw), zapis/wczytanie scen
   (`SceneSerializer`, `PrefabSerializer` z jego testem i metody `SceneManager::serialize*/deserialize*/…Prefab`)
 - ok. 80 nigdy niewołanych metod (gettery kamery, terenu, mgły wolumetrycznej, GUI, sprite'ów, cząsteczek,
   `Entity::getComponents/hasComponent/removeComponent`, akcje `actionReleased/bindActionKey/wasReleased`,
   `MeshBuilder::setTransform` z całym przekształceniem, kolejka i pula poleceń obliczeniowych Vulkan itd.)
 - zostawione świadomie: konfiguracja loggera (`setLevel`, `useFileBackend`…) - opisana w doc/10_logger.md

Przegląd błędów (narzędzia):
 - Sprawdzone: build z -Wall -Wextra (36 ostrzeżeń - martwy kod, porównania int/uint w pętlach; bez błędów logiki),
   ASan + UBSan (clang) na teście UI, autoplay z walką i przykładach 01-09 (0 zgłoszeń, 0 wycieków w naszym kodzie),
   build z -D_GLIBCXX_ASSERTIONS (sprawdzanie indeksów kontenerów) na teście UI i autoplay, walidacja Vulkan w Debug.
   ThreadSanitizer nie działa ze sterownikiem NVIDIA (wewnętrzny błąd TSan przy starcie).
 - Naprawione: po wprowadzeniu Ref<T> nieaktualne odwołanie daje -1 - tragarz idący z towarem do magazynu, który
   zburzono, a jego slot zajął nowy budynek, czytał buildings[-1] (wywalenie gry w długiej partii). `buildingOrDead`
   w kodzie tragarzy i zleceń, zabezpieczenia żołnierzy (wieża / cel budynek), kwarantanna slotów budynków, flag
   i dróg 30 s. Test `staleRefTest` (w buildzie z asercjami bez poprawki przerywa program, z poprawką przechodzi)

Raport awarii:
 - Naprawione wywalanie gry w widoku debug (F3): `snprintf("%d", b.worker)` po zmianie pól na `Ref<T>` dostawał
   obiekt zamiast inta (to samo w `describeUnit`). Build ma teraz `-Werror=format` - taka pomyłka nie skompiluje się
 - `installCrashHandler` (engine/core/crash_handler.*, woła go Engine::initialize): przy SIGSEGV/SIGBUS/SIGFPE/
   SIGILL/SIGABRT i nieobsłużonym wyjątku C++ zapis `crash/<program>_<data>.log` - przyczyna i adres, miejsce awarii
   (plik:linia z łańcuchem funkcji inline), stos wywołań z nazwami funkcji i liniami (addr2line z binutils), ostatnie
   64 wpisy logu; potem systemowy coredump jak wcześniej. Release budowany z `-g1` (tablice linii; binarki ~4x
   większe), symbole binarek eksportowane. Test: `ENGINE_CRASH_TEST=segv|abort|throw`

Poprawki z drugiego przeglądu:
 - `Ref<T>` pamięta wektor swojego świata (odczyt zawsze sprawdza właściwy świat, także przy dwóch światach naraz;
   `World::activate()` ustawia świat dla nowych przypisań), indeks spoza wektora = nieważny (uszkodzony zapis nie
   wyjdzie poza tablicę), wczytanie przez `Ref::setLoadedIndex` (generacja 0, bez czytania wektora)
 - `saveLoadTest` porównuje wszystkie klucze zapisu (nowe pola, np. wiedza AI o mapie, sprawdzane automatycznie)
 - AI: po "żadna wieża nie doda lądu" minuta bez wież poszerzających (bez prób i logów co 3 s)
 - AI mniej pasywne: etap "wojsko" (koszary + łańcuch broni) przed bieżącymi brakami materiałów, gdy podstawa
   gospodarki stoi; mniejsza pierwsza fala (3/4/5 wg poziomu, kolejne do +4), 1 obrońca przy małej armii;
   producenta brakującego materiału AI dobudowuje tylko, gdy ma mniej niż dwóch (wcześniej 6 kopalń kamienia "na
   wieżę" zjadało jedzenie i osada stawała); wieża obronna tylko, gdy stać na nią od razu. Autoplay (4 mapy,
   x24): każda partia kończy się zwycięstwem w 20.-30. minucie, fale od 8.-20. minuty; log `[ai]` fal i
   `[autoplay] wojsko` (wolni żołnierze, ludność, fale)

Obrona AI:
 - Wieże obronne: przy zagrożeniu (niedawny atak - frakcja pamięta, skąd przyszedł, `lastAttackPos`; albo znany
   budynek wroga bliżej niż 650) AI stawia gęsto wieże na granicy od strony zagrożenia (`aiDefenseFront`), bez wymogu
   nowego lądu. Limit wież frontu (w stożku +-35° w stronę zagrożenia): 1 + budynki/8 (max 5), +1 po ataku, nie więcej
   niż żołnierzy na załogi; tylko przy działającej gospodarce (tartak, min. 8 budynków)

Poprawki AI (wieże):
 - AI nie stawia już "stada" wież przy morzu ani pierścienia wież wokół zamku: wieża musi dodać min. 35% nowego,
   osiągalnego lądu w swoim zasięgu (`aiTowerGain`) i stać z dala od innych wież/zamku; cel rozbudowy (las, skały)
   musi być osiągalny pieszo (`landReachable` - las za wodą dawał rząd wież wzdłuż brzegu). Gdy żadna wieża nic
   nie doda, AI rezygnuje z wieży i buduje co innego; log `[ai]` podaje zysk lądu każdej wieży

Poprawki z przeglądu:
 - Osadnicy: `canPlace` sprawdza wszystkie drogi w pobliżu (wcześniej po 16 drogach pętla je pomijała - przy gęstej
   sieci budynek dawał się postawić na drodze); `removeFlag` unieważnia pamięć odległości po drogach;
   `findEnemyUnitNear` bez alokacji (kubełki przeglądane wprost); wygląd drzew i skał (skala, obrót) liczony
   w `present` raz na klatkę zamiast w każdym kroku symulacji
 - Dźwięk: `playMusic` nie dekoduje na wątku gry (pierwszą porcję robi wątek strumienia), a wątek strumienia
   dekoduje bez blokady - `playMusic`/`stopMusic` nie czekają na dekoder
 - Renderer: kopia koloru sceny tylko, gdy aktywny feature jej potrzebuje (`IRenderFeature::needsSceneColor`,
   ocean) - bez oceanu ~8 MB kopii na klatkę mniej; zmiana filtra i przeładowanie tekstury bez `vkDeviceWaitIdle`
   (deskryptor podmieniany w secie każdej klatki po jej zakończeniu, stare samplery/obrazy niszczone po klatkach
   w locie)
 - `JobSystem::parallelFor` bez alokacji na kawałek (licznik atomowy, pomocnicy z jednym wskaźnikiem, wywołujący
   też liczy i pomaga, czekając - zagnieżdżenie bez zakleszczenia; `currentSlot()` = bufor na wątek);
   animacje glTF + MD2 jednym `parallelFor`, wyniki MD2 i bufory ekstrakcji paczek używane ponownie -
   ekstrakcja paczek 0,49 -> 0,36 ms

Osadnicy:
 - Stabilne uchwyty z generacją (`slot_ref.h`): odwołania między obiektami to `Ref<T>` (indeks + generacja slotu),
   martwe sloty jednostek, budynków, flag, dróg, zleceń i sadzonek są po kwarantannie używane ponownie - wektory
   nie rosną przez całą partię, a stare odwołanie do obiektu z zajętego slotu daje -1 (wcześniej zlecenia
   transportu zajmowały martwe sloty od razu i bez generacji - tragarz mógł "przejąć" cudze zlecenie)
 - Mgła wojny dla AI: każda frakcja ma własną mapę odkrytego terenu; AI zna tylko odkryte surowce, góry i budynki
   gracza, wysyła zwiadowcę, a wież "na ślepo" stawia najwyżej kilka (dalej czeka na zwiad); zapis zachowuje
   wiedzę AI (`fogExploredAi`). `SETTLERS_AIFOG=0` - AI bez mgły (porównania)
 - AI: pamięć ekonomii (budynki frakcji według typu liczone raz na krok myślenia zamiast przeglądania wszystkich
   budynków w każdym zapytaniu), deterministyczne szukanie miejsca (pierścienie od środka zamiast 120 losowych
   prób - zwartsza osada, krótsze drogi, ten sam wynik za każdym razem)
 - `SETTLERS_SEED=<n>` - stała mapa (ziarno wypisywane w logu); testy `slotReuseTest`, `aiFogTest`; log autoplay:
   zwiad (odkryta część mapy, znane budynki wroga) i sloty (rozmiar wektora / żywe obiekty)

Nowe w silniku:
 - Renderer podzielony na pliki przebiegów: `render()` to lista kroków (cienie, G-Buffer, oświetlenie deferred,
   kopia sceny, przezroczyste, nakładka), klasa `RendererImpl` w prywatnym `renderer_impl.h`, metody w
   `renderer_shadow_pass/gbuffer_pass/deferred_pass/scene_copy/overlay_pass/textures/fog_of_war.cpp`
   (renderer_core.cpp: 3970 -> 830 linii); usunięty nieużywany `RenderOrchestrator`
 - `render/push_constants.h`: jeden układ push constants materiału (`MaterialPushConstants`, `makeMaterialPush`)
   i cieni zamiast trzech kopii; `static_assert` pilnuje rozmiaru zgodnego z shaderami
 - `RenderPacket`: dane terenu (warstwy tekstur, mapa wysokości) we wspólnym `TerrainSurface` zamiast ~300 B
   w każdej paczce; indeksy tekstur terenu liczone raz na teren, a nie dla każdej paczki
 - Siatki statyczne w pamięci karty: `loadMesh` tworzy bufory DEVICE_LOCAL, dane idą przez bufor pośredni,
   a `MeshManager::flushUploads` (Engine woła przed renderem) wysyła wszystkie kopie klatki jednym zgłoszeniem;
   `updateMesh` (siatki zmieniane co klatkę) zostaje w pamięci hosta
 - Cienie: przy wyłączonych cieniach renderer nie zbiera listy cieni ani nie liczy kaskad; lista cieni to
   odwołania zamiast kopii paczek; tańszy test "paczka w kaskadzie" (środek + rozmiar zamiast 8 narożników)
   - nagrywanie cieni 0,28 -> 0,18 ms
 - Cząsteczki liczone na puli wątków silnika (`Engine::getJobSystem`, parametr `ParticleSystem::initialize`)
   zamiast tworzenia wątków `std::async` co klatkę
 - Dźwięk w czasie rzeczywistym: wątek audio nie czeka na muteks i nie alokuje - polecenia z gry przez kolejkę
   bez blokad, muzykę OGG dekoduje osobny wątek do bufora pierścieniowego (brak trzasków przy zacięciach gry)
 - Mniej zmian stanu przy rysowaniu: `DrawBindCache` (packet_draw.h) pamięta zbindowany potok, descriptor sety
   i bufory i pomija powtórzenia; paczki G-Buffera sortowane po potoku, pierścieniu odległości i siatce
   (front-to-back, teren na końcu), paczki cieni - raz na klatkę po potoku i siatce. Przy ~1000 paczkach
   ~3500 bindowań mniej na klatkę, nagrywanie cieni 0,41 -> 0,28 ms
 - Wydajność i wycieki: `MeshManager::removeMesh/removeMeshesWithPrefix/meshCount` (bufory zwalniane bezpiecznie
   przez retire), `Terrain::releaseMeshes`; obrazki w GUI (`GuiMgr::createImage/updateImage/addOverlayImage`,
   kopia na klatkę w locie, przesył w preRender) - np. minimapa jako jedna tekstura zamiast 4096 prostokątów
 - Porządek w shaderach: jedno miejsce na każdą rzecz - `shared/globals.hlsl` (UBO, tekstury bindless, mapa cieni;
   wcześniej 12 kopii cbuffer), `shared/surface.hlsl` (`SurfaceData`, `perturbNormal` - wcześniej 4 kopie mapowania
   normalnych), `shared/gbuffer_output.hlsl` (`packGBuffer`), `shared/surface_lighting.hlsl` (`shadeSurface`,
   `applyAtmosphere` - wcześniej 4 kopie oświetlenia); teren zapisuje G-Buffer (`MaterialCompiler::writesGBuffer`,
   `RenderPacket::gbufferMaterialPass`) - światło, cienie, mgłę i mgłę wojny liczy dla niego deferred; materiał
   domyślny przezroczysty i roślinność dostały cienie (wspólna funkcja); deferred czyta mgłę wojny z tablicy
   bindless (bez osobnego wiązania); formaty G-Buffera w `render_types.h` (`gbufferAttachmentFormat`)
 - Mgła wojny (fog of war): `Renderer::setFogOfWar(enabled, origin, size, explored)` + `updateFogOfWar(dane)` -
   tekstura widoczności 256x256 (0 nieodkryte, 128 odkryte, 255 widoczne), osobna kopia na każdą klatkę w locie
   (przesył na początku klatki, bez czekania na GPU); przyciemnianie w deferred (budynki, roślinność), w shaderze
   terenu i oceanu (przez tablicę bindless, indeks w UBO) - wspólna funkcja `shared/fog_of_war.hlsl`;
   poza obszarem mapy widoczność = 0 (fix: odkryte miejsca powtarzały się na oceanie za krawędzią mapy)
 - Wydajność: warstwy walidacji Vulkana tylko w buildzie Debug albo z `ENGINE_VALIDATION=1` - w grze sprawdzały
   każde polecenie na CPU i zajmowały ~90% klatki (duża osada Osadników: 20 ms -> 1,9 ms na klatkę, GPU wcześniej
   czekało na CPU); profiler klatki `ENGINE_PROFILE=1` (`core/profiler.h`: `PROFILE_SCOPE`, `Profiler::lap/count`)
   z punktami pomiaru w pętli silnika i rendererze
 - Paczka gry do przekazania (Linux): `tools/build_release.sh` buduje w kontenerze Ubuntu 25.04 (glibc 2.38+),
   opcja CMake `ENGINE3D_PORTABLE` (statyczne libstdc++/libgcc), `ENGINE3D_OUTPUT_DIR` (katalog wyjścia),
   RUNPATH `$ORIGIN` dla wszystkich binarek (libengine3d.so obok binarki); SDL3_ttf i SDL3_image jako wymagane
   zależności - obrazy (mapy wysokości terenu, tekstury glTF przez `TinyGLTF::SetImageLoader`) wczytuje SDL3_image
   zamiast systemowego `stb_image.h`; usunięte nieużywane SDL3_sound;
   usunięte nieużywane bezwzględne ścieżki shaderów z definicji kompilacji; puste `add_dependencies` w 01/02
   (błąd w CMake z Ubuntu)
 - Cienie terenu teselowanego (fix "plam cieni znikąd" w 08_game): teren rysuje się do mapy cieni tym samym
   shaderem co na ekranie (`terrain_shadow`: teselacja + wysokość z mapy, wspólna funkcja
   `shared/terrain_height.hlsl`), więc cień powstaje z widocznej geometrii - wcześniej rzucała go siatka CPU 17x17
   (wierzchołek co 2 teksele), która w dolinach leżała nad terenem; mapa wysokości na GPU 16-bit z zaokrągleniem
   (było 8-bit z obcięciem), próbkowanie w środku teksela i zawsze mip 0; odrzucanie obiektów spoza obszaru kaskady
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
 - Osadnicy: optymalizacje z przeglądu - symulacja i prezentacja rozdzielone (`World::simulate` w krokach
   <= 0,05 s, najwyżej 12 na klatkę + `World::present` raz na klatkę; koniec gubienia czasu przy zacięciach),
   pamięć odległości po drogach (`networkFrom`), mapa właścicieli terenu i kubełki jednostek, przyrostowa mgła
   wojny, minimapa jako tekstura; naprawiony wyciek siatek (drogi, teren i granice starego świata zostawały
   w VRAM przy każdej budowie/rozbiórce drogi i każdym wczytaniu); testy `spatialGridTest` i liczby siatek;
   duża osada x24: 2,3 -> 1,7 ms na klatkę, 480 jednostek x24: 3,8 -> 1,5 ms
 - Osadnicy: mgła wojny i zwiad (`world_fog.cpp`) - siatka widoczności gracza 256x256 (4 x 4 j.), zasięg wzroku
   jednostek (łucznik najdalej) i budynków (zamek, wieże - dalej niż terytorium); jednostki wroga widać tylko
   w zasięgu wzroku, budynki/flagi/drogi/granice wroga - na odkrytym terenie; bez zaznaczania niewidocznego wroga,
   bez pasków zdrowia we mgle; minimapa przyciemniona; odkryty teren w zapisie gry (RLE); wyłączona w pokazie AI
   i autoplay, `SETTLERS_NOFOG=1`; AI mgły nie używa; test `fogOfWarTest`
 - Osadnicy: wolna gra po zwycięstwie - przycisk "Graj dalej" w oknie końca gry (po porażce: "Oglądaj mapę"),
   symulacja i budowa działają dalej, "Wolna gra" w górnym pasku, stan zapisywany w pliku zapisu; test `freePlayTest`
 - Osadnicy: zapis i wczytanie gry (F5 / F8, przyciski w menu głównym i pauzie, `save/settlers_save.json`) -
   `world_save.cpp`: ziarno mapy + pełne listy budynków, jednostek, flag, dróg i zleceń (z martwymi pozycjami -
   indeksy odwołań się zgadzają), stan frakcji, zmiany w surowcach (ścięte drzewa, sadzonki), stan generatora
   losowego; jedna lista pól na strukturę dla zapisu i odczytu (`fields(Archive&, T&)`); `World::create` podzielone
   na `createLand` / `createStartingBases` / `finishCreate`; zapis przez plik tymczasowy; test `saveLoadTest`
   (zapis -> wczytanie -> zapis daje identyczny stan); `SETTLERS_SAVEAT`, `SETTLERS_LOAD` + `SETTLERS_OPEN=load`
 - Osadnicy: wydajność symulacji - rozpychanie jednostek przez siatkę przestrzenną zamiast porównywania każdej
   z każdą i najwyżej 20 razy na sekundę czasu gry (500 jednostek przy x24: 11,8 ms -> 3,8 ms na klatkę);
   sekcje świata w profilerze; `SETTLERS_NOVSYNC`, `SETTLERS_SLOWAT`; FPS w logu autoplay
 - Osadnicy: limit ludności 250 na stronę (był ukryty limit 120 - domy ponad niego nic nie dawały, gra o tym nie
   mówiła); stała `kMaxPopulation`, "(maks.)" w górnym pasku, komunikat przy stawianiu Domu przy maksimum, AI nie
   stawia wtedy domów; `SETTLERS_STRESS=<n>` (n osadników więcej na stronę) i FPS w logu autoplay - pomiar wydajności
 - Osadnicy: nowe AI - planista potrzeb zamiast sztywnej listy budowy: czyta statystyki i ostrzeżenia (dostawy
   czekające na towar, zapasy, przestoje, zaległości na flagach, brak ludzi) i buduje to, czego brakuje; proporcje
   warsztatów z tabeli `kAiRatios` (1 tartak na 4 drwali, 1 leśniczy na 3 drwali, 1 młyn na 3 farmy, piekarnia /
   młyn, huta / kopalnia rudy, kuźnia / huta); burzy wyczerpanych kamieniarzy i drwali bez drzew, place bez drogi;
   drwal przy lesie, kamieniarz przy skałach, leśniczy przy drwalu; wieże w stronę brakującego lasu / skał / gór,
   gdy na terytorium brakuje miejsca; rezerwa materiałów na tartak / źródło kamienia / magazyn; Dom ponad limit
   budów przy limicie ludności; AI ustawia sobie limity zapasu desek i broni; log decyzji `[ai]` (autoplay,
   `SETTLERS_AILOG=1`), log autoplay: place budowy i wydajność warsztatów (towary/min); test `aiPlannerTest`
 - Osadnicy: balans pod proporcje AI (zmierzony): drwal wolniejszy (rąbanie 20 s), tartak 2 kłody → 4 deski
   w 2 s (`ProductionDef::outputCount`), farma wolniejsza (30 s); limity zapasu żywności (chleb/ryby/mięso po 40);
   dostawy opłacane tylko z magazynów/flag osiągalnych drogami (wcześniej plac bez drogi blokował się na zawsze);
   limit tragarzy w trybie Auto 80; prędkość gry x24
 - Osadnicy: las się nie kończy — AI stawia Leśniczego wcześnie (zaraz po drugim drwalu) i zawsze przy drwalu
   (sadzi w promieniu 60, drwal wycina w 170), a gdy drwalowi zostaje ≤10 drzew bez leśniczego obok - pilnie;
   gracz dostaje ostrzeżenie już przy 10 drzewach z podpowiedzią "postaw obok Leśniczego"; log autoplay
   `drzewa przy drwalach`
 - Osadnicy: rekrutacja automatyczna w koszarach — przełącznik "Auto-rekrutacja" i udział mieczników (−/+ co 10%,
   reszta łucznicy); kolejka (do 2) uzupełnia się sama wg proporcji, przy braku kosztu jednego typu szkoli drugi;
   naprawione okno koszar zasłaniane przez okno akcji (okna przy lewej krawędzi układane co klatkę jedno nad drugim,
   nad paskiem budowy - `layoutSideWindows`); `SETTLERS_SELECT=barracks`; test `autoRecruitTest`
 - Osadnicy: uczciwe AI — bez dochodu biernego (wcześniej dostawało deski, kamień i broń "z powietrza", więc miało
   wojsko bez kuźni); ta sama ekonomia co gracz, poziom trudności = zapasy startowe (wróg bez broni na start, bogaty
   start na Trudnym), gotowe budynki wroga na starcie i tempo fal; AI buduje łańcuch broni wcześniej (kopalnie węgla
   i rudy, huta, kuźnia), dobudowuje producenta brakującego materiału (kamieniarz/kopalnia kamienia, tartak/drwal) i
   żywność, gdy kopalnie głodują; log autoplay `budynki` (budynki każdej strony i zapasy)
 - Osadnicy: koniec zapychania magazynów — dostawy (budowy, koszary, wejścia warsztatów) opłacane także towarem
   leżącym na flagach (`TransportOrder::sourceFlag`, rezerwacja w `Flag::reserved`), więc deski z tartaku trafiają na
   budowę przy pełnych magazynach; limity zapasu per towar (`FactionState::stockLimit`, przyciski −/+ w oknie
   Gospodarka, domyślnie dla surowców i półproduktów) - powyżej limitu tragarze nie wnoszą towaru do magazynu;
   "stać nas" liczone z magazynów i wolnych towarów na flagach (`World::spendable`, także górny pasek); znacznik nad
   warsztatem podaje przyczynę (`flagBlockReason`: magazyny pełne / limit zapasu / za mało tragarzy), tryb Auto
   tragarzy liczy tylko towary, które da się zanieść; AI stawia koszary, gdy je pominęło (brak miejsca) i ich nie
   ma - wcześniej strona AI potrafiła całą grę nie mieć wojska; test `storageJamTest`
 - Osadnicy: załogi i zdobywanie wież — wieża strzela tylko z załogą (do 3 żołnierzy, w środku leczą się);
   wolni żołnierze sami obsadzają puste wieże, PPM na własnej wieży = obsadzenie, przycisk "Wyprowadź załogę";
   PPM na wrogiej wieży = szturm: obrońcy wychodzą walczyć, a żołnierz, który wejdzie do pustej wieży, przejmuje ją
   razem z terenem (drogi i flaga wieży zmieniają właściciela, cywilne budynki wroga na przejętym terenie płoną);
   AI obsadza swoje wieże, a fala ataku, której celem jest wieża, zdobywa ją zamiast burzyć;
   test `towerAssaultTest`, log autoplay `wojsko` (broń, koszary, kolejka, wieże i załogi)
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
