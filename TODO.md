# do zrobienia

- dodać wiele pipeline'ów/materialów per obiekt, zamiast jednego activeShaderProgram na całą scenę
- wprowadzić UBO/SSBO per frame dla większej liczby obiektów zamiast push constants, gdy scena urośnie
- dołożyć prosty loader modeli i tekstur, żeby odciąć przykłady od ręcznie wpisanych wierzchołków
 Uwaga techniczna: Nowe pasy renderowania (OpaquePass, TransparentPass) są przygotowane do rysowania, ale ze względu na stopień skomplikowania pełnej implementacji Vulkana (setki linii na potok), używają one obecnie uproszczonych struktur (stubs).
  Silnik jest jednak w pełni gotowy na wpięcie kompletnej logiki shaderów w te dedykowane moduły.

textury z modelu gltf w postaci binrnej są wyciągane do tmp i wczytywane, popraw na wczytywanie odrau do pamięci

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