# do zrobienia

- dodać wiele pipeline'ów/materialów per obiekt, zamiast jednego activeShaderProgram na całą scenę
- wprowadzić UBO/SSBO per frame dla większej liczby obiektów zamiast push constants, gdy scena urośnie
- dołożyć prosty loader modeli i tekstur, żeby odciąć przykłady od ręcznie wpisanych wierzchołków
 Uwaga techniczna: Nowe pasy renderowania (OpaquePass, TransparentPass) są przygotowane do rysowania, ale ze względu na stopień skomplikowania pełnej implementacji Vulkana (setki linii na potok), używają one obecnie uproszczonych struktur (stubs).
  Silnik jest jednak w pełni gotowy na wpięcie kompletnej logiki shaderów w te dedykowane moduły.

textury z modelu gltf w postaci binrnej są wyciągane do tmp i wczytywane, popraw na wczytywanie odrau do pamięci

teren3d
  Główne braki w porównaniu ze starym (2400 linii):
Feature
LOD stitching — 16 wzorców łączenia poziomów
Multi-texturing — 4 warstwy z maskami
Dynamiczny LOD — wybór poziomu wg odległości od kamery
Frustum culling — test sfera/frustum na noda
Zarządzanie obiektami — przypinanie obiektów do nodów quadtree
Debug rendering — wireframe quadtree
Znajdowanie sąsiadów — do stitchingu LOD


gotowe
Wygładzanie krawędzi — brzegi do poziomu wody
Generatory terenu
Wczytywanie heightmapy
Wczytywanie XML
QuadTree (struktura)
Zapytania wysokości
Budowa meshy
Kluczowy brak: QuadTree jest zbudowany, ale nie używany do renderowania. Stary system dynamicznie wybierał LOD per node na podstawie odległości od kamery i łączył sąsiednie poziomy szczegółowości specjalnymi index bufferami — to te 800 linii stitchingu. Bez tego teren to po prostu statyczna mesh.


Wraz z nadejściem Vulkana 1.3, API graficzne przeszło ewolucję z modelu "programowania sprzętu przez bardzo drobne instrukcje" w stronę modelu "zarządzania potężnymi, zautomatyzowanymi blokami operacji".Poniżej zestawiam kluczowe techniki renderingu, które definiują nowoczesny Vulkan (1.3+) w porównaniu do wczesnych wersji (1.0/1.1).

1. Mesh Shading (Rewolucja w geometrii)Vulkan 1.3 wspiera Mesh Shaders jako standard (często przez rozszerzenia, ale w pełni zintegrowane z ekosystemem).Stare podejście: Vertex Shader + Geometry Shader + Tessellation. CPU musiało wysyłać wszystkie wierzchołki, a GPU tylko je "przepuszczało".Vulkan 1.3: Mesh Shaders (Task + Mesh) pozwalają GPU na samodzielne decydowanie o tym, co jest widoczne (culling) i generowanie geometrii z danych w pamięci (SSBO).Zaleta: Przejście z "przesyłania danych" na "przesyłanie instrukcji", co eliminuje wąskie gardło procesora.
2. Bindless RenderingTechnika, która w nowym Vulkanie stała się standardem dzięki rozszerzeniu VK_EXT_descriptor_indexing.Stare podejście: Bardzo częste używanie vkCmdBindDescriptorSets dla każdego obiektu (kosztowne przełączanie kontekstu).Vulkan 1.3: Wszystkie zasoby (tekstury, bufory) są dostępne w jednym, ogromnym "tablicowym" deskryptorze. W shaderze po prostu pobierasz indeks materiału.Zaleta: Rysowanie tysięcy obiektów różnego typu w jednym Draw Callu (tzw. Draw Call reduction).
3. Asynchronous Compute (Wzmocnione w 1.3)Choć obecne w 1.0, to w 1.3 zyskały na znaczeniu dzięki lepszemu wsparciu dla Timeline Semaphores.Stare podejście: Synchronizacja GPU przez VkFence i zwykłe semafory była trudna i często prowadziła do "zastojów" (GPU czekało na CPU).Vulkan 1.3: Timeline Semaphores pozwalają na precyzyjne odliczanie punktów w czasie pracy GPU (np. "poczekaj, aż operacja X osiągnie wartość 5").Zaleta: Płynne mieszanie pracy graficznej z fizyką, cząsteczkami czy odszumianiem (denoising) w tym samym czasie.
   
Zestawienie technik: Vulkan 1.3 vs Stare wersjeTechnikaVulkan 1.0 - 1.2Vulkan 1.3+KonfiguracjaRenderPass + FramebufferDynamic RenderingGeometriaVertex Input (statyczne)Mesh Shaders (dynamiczne)ZasobyWiązanie deskryptorów co obiektBindless (tablice deskryptorów)SynchronizacjaSemafory (proste)Timeline Semaphores (liczniki)PamięćRęczna alokacjaVMA (standard w ekosystemie)Dlaczego to jest ważne dla Twoich cząsteczek?Jeśli planujesz upgrade swojego silnika do Vulkana 1.3:Porzuć VkRenderPass: Zastosuj vkCmdBeginRendering (Dynamic Rendering). Kod stanie się o połowę krótszy.Zastosuj Bindless dla tekstur: Pozwoli Ci to zredukować narzut na sterownik przy rysowaniu 300 000 cząsteczek.Użyj Timeline Semaphores: Pomogą Ci lepiej zsynchronizować Compute Shader (który liczy cząsteczki) z Graphics Queue (która je rysuje), bez ryzyka wyścigu danych.



teren3d
Tak — teren jest renderowany wielokrotnie. Każda z 12 warstw z JSON-a tworzy osobny Entity z tym samym meshem, każda z innym materiałem:
Warstwa
0 (tl_e0)
1-11 (tl_e1..11)
Każda warstwa renderuje CAŁĄ siatkę terenu, a maska (binding 2, linkowana przez linkNormalMap) kontroluje alpha output shadera — gdzie warstwa ma być widoczna, a gdzie przezroczysta.
Warstwy 1+ idą przez TransparentPass, który sortuje pakiety wg odległości. Ponieważ wszystkie warstwy mają tę samą pozycję (ten sam mesh), sortowanie jest niestabilne — kolejność warstw może być przypadkowa. Jeśli warstwa 11 wylosuje się jako ostatnia, przykryje wszystko.
To działało wcześniej, bo TransparentPass był przed oceanem — ale to nie powinno wpływać na sortowanie warstw między sobą. Sprawdź proszę, czy ocean już widać z GREATER + depthWrite=false, i czy warstwy terenu dalej mają jedną teksturę?