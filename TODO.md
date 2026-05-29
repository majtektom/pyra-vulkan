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
