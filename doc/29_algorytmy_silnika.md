# Jak działają najważniejsze algorytmy silnika

Ten rozdział łączy opisy API z ideami potrzebnymi do samodzielnego czytania
implementacji. Synchronizacja i zasoby Vulkan mają własne wyjaśnienie
w [27](27_vulkan_od_podstaw.md), a algorytmy gry w [28](28_settlers_algorytmy.md).

## 1. Ekstrakcja sceny: po co RenderPacket

**Problem:** kod gry operuje encjami i komponentami, a GPU potrzebuje buforów,
macierzy i materiałów. Renderer przeszukujący komponenty przy każdym przebiegu
powtarzałby pracę i znał szczegóły sceny.

**Rozwiązanie:** systemy sceny obliczają transformacje i zbierają dane do
`RenderPacket`. `RenderScene` grupuje te paczki, a `RenderView` niesie kamerę.
Przebiegi czytają przygotowaną reprezentację zamiast świata gry. To oddzielenie
modelu sceny od danych potrzebnych do rysowania, nie trwała kopia całego świata:
paczki nadal odwołują się do zasobów o określonym czasie życia.

Transformacja lokalna opisuje obiekt względem rodzica. Transformacja świata
powstaje jako `parentWorld × local`. Dlatego transformacje trzeba obliczyć
przed ekstrakcją, a hierarchia musi być bez cykli. Przestawienie kolejności
systemów może dać obraz z poprzednimi macierzami mimo poprawnej logiki gry.

Kod i API: [03_ecs_scena.md](03_ecs_scena.md),
[`engine/scene/`](../engine/scene/).

## 2. Równoległe zakresy i bufory na wątek

**Problem:** tysiące niezależnych obiektów można przetwarzać równolegle, ale
tworzenie zadania dla każdego obiektu oraz wspólny `push_back` są kosztowne
lub wymagają blokad.

[`JobSystem::parallelFor`](../engine/core/job_system.h) dzieli zakres na
kawałki. Atomowy licznik `next` przydziela każdy kawałek jednemu wykonawcy.
Do kolejki trafia ograniczona liczba pomocników, a wątek wywołujący również
przetwarza zakresy. Czekając na zakończenie, może wykonywać zadania z kolejki.
To ogranicza narzut i ryzyko zakleszczenia przy zagnieżdżonym `parallelFor`.

Przy ekstrakcji każdy wykonawca może dopisywać do własnej listy paczek.
Po zakończeniu listy są scalane. Rozmiar tablicy list musi wynosić
`threadCount() + 1`, bo `currentSlot()` rezerwuje ostatni slot dla wątku
wywołującego. Sam `const` obiektu ani równoległe odczyty nie czynią bezpiecznym
dopisywania do wspólnego wektora.

Zysk zależy od rozmiaru pracy; zbyt małe kawałki zwiększają narzut. Nie jest
to obietnica równoległego wykonania wszystkich systemów ECS ani nagrywania
wszystkich przebiegów Vulkan na wielu wątkach. API: [05](05_narzedzia_core.md).

## 3. Deferred, forward i koszt świateł

**Co to jest deferred?** Najpierw geometria zapisuje cechy powierzchni
(G-Buffer), a następny przebieg oblicza światło na podstawie tych cech.
**Po co?** Nie trzeba dla każdego nakładającego się nieprzezroczystego
trójkąta ponownie wykonywać pełnego oświetlenia — po ustaleniu widocznej
powierzchni oświetlamy piksel.

Ten silnik zapisuje albedo/metaliczność, normalną/chropowatość, AO/emisję
i pozycję świata. Deferred odtwarza `SurfaceData` i wywołuje wspólne
`shadeSurface`. Pozycja zajmuje RGBA32F, bo mała precyzja RGBA16F przy dużych
współrzędnych powodowała błędy cieni. To świadomy koszt pamięci i przepustowości.

**Koszt nadal rośnie z liczbą świateł**: shader wykonuje pętlę po maksymalnie
16 źródłach na piksel. „Jeden przebieg” nie znaczy „stały koszt niezależny od
świateł”. To nie jest clustered/tiled deferred z listą świateł na obszar ekranu.

Przezroczystość wymaga mieszania kilku powierzchni, których pojedynczy wpis
G-Buffera nie opisuje. Dlatego część przezroczysta używa forward i sortowania
od tyłu do przodu. Sortowanie obiektów nie rozwiązuje idealnie przecinających
się trójkątów. `depthPrepass` dla podglądu modelu zapisuje najbliższą głębię,
ale zmienia efekt: ukrywa dalsze warstwy zamiast mieszać je wszystkie.

Kod: [`renderer_deferred_pass.cpp`](../engine/render/renderer_deferred_pass.cpp),
[`surface_lighting.hlsl`](../assets/shaders/shared/surface_lighting.hlsl).
Formaty i kolejność: [09](09_render_potoki.md).

## 4. PBR i normal mapping

PBR to model materiału i oświetlenia oparty na parametrach o interpretacji
fizycznej. W tej implementacji model Cooka–Torrance'a przybliża odbicie od
mikroskopijnych nierówności powierzchni:

* albedo określa barwę bazową,
* metallic rozdziela zachowanie dielektryka i metalu,
* roughness określa rozkład kierunków mikropowierzchni — większa daje szersze
  odbłyski,
* Fresnel opisuje zależność odbicia od kąta patrzenia,
* człon geometrii uwzględnia wzajemne przesłanianie mikropowierzchni.

Sprawdź [`pbr.hlsl`](../assets/shaders/shared/pbr.hlsl) oraz wywołania w
`surface_lighting.hlsl`. AO przyciemnia tu składową otoczenia, emisja jest
dodawana do wyniku. To przybliżenie lokalnego oświetlenia, nie śledzenie
wielokrotnych odbić. Aktualne `shadeSurface` ogranicza wynik przez `saturate`;
nie jest pełnym torem HDR z tonemappingiem.

Normal mapping zmienia normalną używaną w oświetleniu bez dodawania geometrii.
Próbka RGB mapy normalnych jest zamieniana z [0,1] do [-1,1], a następnie
przenoszona do przestrzeni świata. `perturbNormal` w
[`surface.hlsl`](../assets/shaders/shared/surface.hlsl) wyznacza bazę TBN
(styczna, bitangens, normalna) z pochodnych pozycji świata i UV `ddx/ddy`.
Dlatego nie wymaga tangentów w podstawowym formacie `Vertex`. Nie zmienia
sylwetki obiektu ani jego cienia geometrycznego. Materiały: [10](10_render_materialy.md).

## 5. Mapy cieni, kaskady, PCF i bias

**Mapa cieni** zapisuje najbliższą głębokość widzianą z pozycji światła.
W oświetleniu przeliczamy punkt świata do tego samego układu i porównujemy
jego głębokość z zapisaną. Jeśli przed nim jest bliższa powierzchnia, punkt
jest w cieniu. To dodatkowy rendering geometrii z innej kamery.

**Dlaczego kaskady?** Jedna tekstura słońca rozciągnięta na całą dużą mapę
miałaby za mało szczegółów blisko gracza. Renderer dzieli zakres widoku na
cztery przedziały i dla każdego tworzy osobną projekcję ortograficzną.
Bliski przedział dostaje gęstsze próbkowanie świata. Mieszanka podziału
liniowego i logarytmicznego steruje rozkładem granic. Zakres jest dopasowywany
do widocznej geometrii, żeby nie marnować rozdzielczości na pusty obszar.

**PCF** uśrednia wyniki porównań głębokości z okolicy punktu. Kod używa
próbek 3×3 i samplera porównującego, co zmiękcza schodki. Nie jest to
symulacja fizycznego półcienia zależnego od rozmiaru źródła.

**Bias i normal offset** kompensują błędy dyskretyzacji, przez które
powierzchnia zacieniałaby samą siebie (shadow acne). Kod skaluje przesunięcie
normalnej i bias rozmiarem teksela kaskady oraz kątem padania. Za duży bias
odkleja cień od obiektu. `depthClamp` ma sens w wariancie kaskad słońca;
w perspektywicznych mapach lokalnych użycie jest wyłączone.

Kod: [`renderer_shadow_pass.cpp`](../engine/render/renderer_shadow_pass.cpp),
[`shadow_pcf.hlsl`](../assets/shaders/shared/shadow_pcf.hlsl).
Ograniczenie: światło punktowe ma tu jedną projekcję, a nie pełne sześć ścian
cube mapy. Tabela zasobów: [09](09_render_potoki.md).

## 6. Teren: quadtree, LOD i zszywanie

**Heightfield** zapisuje wysokość Y jako funkcję XZ. Jest prostszy i tańszy
od dowolnej bryły, ale sam nie reprezentuje jaskiń i przewieszeń.
**Quadtree** dzieli prostokąt na cztery części, rekurencyjnie porządkując
obszary terenu. **LOD** zmniejsza liczbę trójkątów w odległych obszarach,
w których drobne szczegóły i tak zajmują mało pikseli.

W [`Terrain::update`](../engine/object/terrain/terrain.cpp) liście dostają LOD,
a kolejne przejścia korygują sąsiadów do różnicy najwyżej jednego poziomu.
Większy numer oznacza rzadszą siatkę. Przy kroku `s = 2^lod` używany jest co
s-ty wierzchołek siatki węzła.

**Dlaczego powstają szczeliny?** Gęsta krawędź ma pośredni wierzchołek,
którego rzadszy sąsiad nie ma. Po uwzględnieniu wysokości taki punkt nie musi
leżeć na prostym odcinku sąsiada: powstaje T-junction.
[`TerrainIndexGenerator`](../engine/object/terrain/terrain_index_generator.cpp)
na zszywanej krawędzi zastępuje dwie komórki wachlarzem trzech trójkątów,
używającym na brzegu co drugiego punktu. Cztery krawędzie dają 16 kombinacji.
Numer konfiguracji jest indeksem tabeli `kStitchTable`, **nie bezpośrednią
maską bitową**. `computeStitchConfig` wyszukuje odpowiednią kombinację.

**Teselacja** dodatkowo dzieli trójkąty na GPU. Nie zastępuje wyboru LOD na CPU
ani zasad spójności krawędzi. Shader cienia i shader widocznego terenu muszą
stosować tę samą funkcję wysokości, inaczej cień pochodziłby z innej powierzchni.
**Splatting** miesza warstwy tekstur wagami masek, np. trawę i skałę;
zmienia wygląd, nie kształt siatki.

API i raycast: [19](19_teren.md). Obszerne wyprowadzenie:
[detalis/teren3d.md](detalis/teren3d.md).

## 7. A*, heurystyka i znaczniki odwiedzin

**Po co A*?** Dijkstra bada według kosztu od startu; przy pojedynczym celu
można skierować wyszukiwanie w jego stronę. A* wybiera najmniejsze
`f = g + h`: g jest dotychczasowym kosztem, h dolnym oszacowaniem kosztu do celu.
W `NavGrid` ruch prosty kosztuje 1, skośny około √2, a heurystyka oktylna to
`dx + dz + (√2 − 2) × min(dx,dz)`. Odpowiada kosztowi marszu po pustej siatce
8-kierunkowej. Niedoszacowanie pozwala zachować własność najkrótszej ścieżki
w grafie przy pełnym wyszukiwaniu do celu.

Przeszkody mogą drogę wydłużyć, a ograniczenie `maxExpanded` może przerwać
poszukiwanie przed celem. Ścieżka częściowa, przesunięcie zablokowanego celu
do wolnej komórki i późniejsze wygładzanie wymagają interpretacji wyniku przez
grę — `true` nie zawsze oznacza dojście do pierwotnego punktu rozkazu.

Zamiast zerować tablice robocze całej siatki przy każdym zapytaniu, kod
zwiększa `stamp_`. Wartość pola jest aktualna, jeśli `visitStamp_[i]` ma bieżący
numer. `closedStamp_` analogicznie oznacza przetworzone komórki. Po zawinięciu
licznika znaczniki są czyszczone. Bufory są ponownie używane, lecz wynikowa
ścieżka i lokalne struktury wyszukiwania nadal mogą alokować pamięć.
To nie jest cache ścieżek ani gwarancja bezpieczeństwa równoległych zapytań
na jednym `NavGrid` — `mutable` bufory są wspólne.

API, BFS regionów i przykłady: [16](16_ai_nawigacja.md),
[`nav_grid.cpp`](../engine/feature/ai/nav_grid.cpp).

## 8. Animacja: klatki MD2 i skinning glTF

MD2 przechowuje kolejne pozy wierzchołków. Interpolacja między dwiema klatkami
pozwala uzyskać płynny ruch, ale wymaga zmieniania geometrii. Animacja
szkieletowa opisuje zamiast tego transformacje kości i wagi ich wpływu.

W glTF CPU oblicza animowane transformacje węzłów i macierze kości:
`jointWorld × inverseBind`. Macierz inverse bind cofa powiązanie z pozą
spoczynkową. Vertex shader sumuje pozycje przekształcone przez maksymalnie
cztery kości, z odpowiednimi wagami. Wagi powinny dawać łącznie 1; sama liczba
indeksów kości bez wag nie określa deformacji.

Dlatego określenie „animacja na GPU” dotyczy tu przede wszystkim deformacji
wierzchołków — nie całej oceny animacji. Dane kości muszą mieć bezpieczny
czas życia dla klatek w locie, a przebieg cieni musi wykonać ten sam skinning.
Kod: [`model_gltf.cpp`](../engine/object/gltf/model_gltf.cpp),
[`gbuffer_skinned.hlsl`](../assets/shaders/gbuffer/gbuffer_skinned.hlsl).
API i obsługiwane formaty: [20](20_modele.md).

## 9. Audio: kolejka SPSC i bufor pierścieniowy

**Problem:** callback audio musi dostarczyć próbki w terminie. Czekanie na
dysk, alokację albo mutex gry może spowodować trzaski lub przerwę.

Kod gry wysyła krótkie polecenia przez kolejkę **SPSC** (jeden producent,
jeden konsument). Producent wpisuje element, a potem publikuje nową pozycję
`head` z semantyką release. Konsument odczytuje ją z acquire, aby zobaczyć
również wcześniej zapisane dane. Po zużyciu przesuwa `tail`. Gdy kolejka jest
pełna, `push` zwraca `false` — to ograniczona kolejka, nie nieskończony magazyn
poleceń i nie kolejka dla wielu równoczesnych producentów.

Muzykę dekoduje osobny wątek do bufora pierścieniowego próbek. Odczyt i zapis
zawijają się po końcu tablicy, więc nie trzeba przesuwać całego dźwięku po
każdym odczycie. Ten bufor rozdziela nierówny czas dekodowania od regularnego
tempa urządzenia. Nadal trzeba obsługiwać niedobór danych i koniec utworu.

Kod: [`audio_system.cpp`](../engine/feature/audio/audio_system.cpp).
Użycie, dźwięk 3D i muzyka: [14](14_audio.md).

## 10. Froxele: dlaczego mgła ma teksturę 3D

Mgła odległościowa miesza kolor według dystansu do powierzchni. Nie opisuje
rozkładu oświetlonej mgły **pomiędzy** kamerą a powierzchnią.
Mgła wolumetryczna dzieli przestrzeń widoku na froxele, czyli komórki 3D
związane z kamerą. Compute wyznacza gęstość i rozproszenie, a następnie całkuje
kolumnę od kamery do każdego plasterka. Dla odcinka długości `ds` i gęstości
`d` transmitancja (część światła, która przechodzi) wynosi `exp(-d*ds)`.
Kolejny odcinek dodaje światło osłabione transmitancją wcześniejszych odcinków.
Tekstura 3D zawiera już skumulowane RGB i transmitancję w A.

Composite odtwarza dystans do powierzchni z głębi i pobiera **jedną próbkę
gotowej całki** dla tego dystansu. Wynik to `kolor sceny × transmitancja +
światło mgły`; nie wykonuje drugiego marszu po wszystkich froxelach na piksel.

Zaletą jest wielokrotne użycie zgrubnie obliczonej objętości przez piksele
ekranu. Koszt i dokładność zależą od rozdzielczości siatki oraz próbkowania;
małe objętości mogą tracić szczegóły. Nie jest to mgła wojny, która przechowuje
wiedzę frakcji o mapie. Kod:
[`fog_volume.hlsl`](../assets/shaders/fog/fog_volume.hlsl),
[`fog_composite.hlsl`](../assets/shaders/fog/fog_composite.hlsl).
Konfiguracja: [17](17_mgly.md).
