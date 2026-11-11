# Algorytmy i struktury danych w 10_settlers

[Rozdział 25](25_settlers.md) opisuje grę od strony użytkownika. Tutaj
przechodzimy od problemu do rozwiązania i konkretnego kodu. To logika
**przykładu**, nie uniwersalne API silnika. `NavGrid` należy do silnika,
natomiast `Transport`, `AiPlanner`, ekonomia i terytorium należą do gry.

## 1. Dwie mapy do dwóch różnych pytań

| Reprezentacja | Węzeł i połączenie | Pytanie | Algorytm |
|---|---|---|---|
| siatka `NavGrid` | komórka terenu i przejście do sąsiada | którędy człowiek ominie przeszkodę? | linia prosta albo A* |
| graf dróg | flaga i odcinek drogi o określonej długości | do którego magazynu dotrze towar i jaką trasą? | Dijkstra |
| regiony nawigacji | etykiety spójnych obszarów siatki | czy punkty w ogóle są połączone? | BFS |
| łączność flag | flagi osiągalne z zamku lub magazynów | czy budynek ma dostęp do sieci gospodarczej? | przejście grafu ze stosem |

Odległość w linii prostej nie uwzględnia objazdu zatoki, a połączenie
w `NavGrid` nie oznacza istnienia drogi gospodarczej. Nie należy zamieniać
tych zapytań jednym „najbliższym punktem”.

## 2. Dijkstra: najkrótsze drogi od jednej flagi

**Problem:** dla tragarza trzeba porównać wiele miejsc odbioru, a dla towaru
wiele magazynów. Same współrzędne flag nie wystarczą — liczy się długość
połączonych odcinków dróg.

[`Transport::networkDistances`](../examples/10_settlers/transport.cpp) oblicza
odległości od **jednego źródła do wszystkich flag**, nie tylko do jednego celu.
Wagą krawędzi jest `Road::length` (nieujemna długość), więc można użyć
algorytmu Dijkstry.

1. `dist` dostaje `1e9f` (umowna nieskończoność), a `prevRoad` wartość `-1`.
2. Źródło ma odległość 0. Para `(0, źródło)` trafia do kolejki priorytetowej
   zwracającej najmniejszą odległość.
3. Zdejmujemy `(d, cur)`. Jeśli `d > dist[cur]`, wpis jest przestarzały i go
   pomijamy. W kolejce mogą być starsze, gorsze propozycje dla tej samej flagi.
4. Dla każdej żywej drogi przy fladze liczymy `nd = d + road.length`.
5. Gdy `nd < dist[next]`, zapisujemy lepszą odległość i numer drogi,
   którą tam doszliśmy, a nową propozycję dodajemy do kolejki.
6. Kończymy po opróżnieniu kolejki. Nieosiągalne flagi zachowują nieskończoność.

To poprawianie oszacowania nazywa się **relaksacją krawędzi**.

```text
A --4-- B --3-- C
 \------------/
       12

start A: dist[A]=0, dist[B]=4, dist[C]=12
po przetworzeniu B: dist[C]=7, prevRoad[C]=droga B–C
wpis (12,C) zostaje potem pominięty jako przestarzały
```

`networkPath(A,C)` idzie po `prev` od C do A, odwraca kolejność odcinków
i skleja `Road::points`. Jeśli odcinek jest pokonywany od `flagB` do `flagA`,
odwraca również kolejność jego punktów. Zapamiętanie samego dystansu nie
wystarczyłoby do odtworzenia marszu.

Koszt wyszukiwania z kolejką kopcową wynosi typowo O((V+E) log V) dla tego
grafu; V to flagi, E to drogi. Tablice wyniku zajmują O(V). Implementacja
pozwala na duplikaty w kolejce zamiast operacji zmniejszenia klucza.

## 3. „Pamięć Dijkstry”: cache wyników i wersja grafu

**Co to jest?** To pamięć podręczna wyników powyższego algorytmu, a nie inny
algorytm trasowania. Wcześniej przydział pracy mógł liczyć podobne dystanse
wielokrotnie dla tragarzy i zleceń. Dopóki sieć dróg się nie zmieniła, wyniki
od tego samego źródła pozostają użyteczne.

[`Transport::NetDistances`](../examples/10_settlers/transport.h) trzyma:

| Pole | Znaczenie |
|---|---|
| `dist[flaga]` | koszt najkrótszej trasy od zapamiętanego źródła |
| `prev[flaga]` | numer drogi poprzedzającej tę flagę na trasie |
| `version` | wersja sieci, dla której obliczono wynik |

`netCache_` jest mapą `flaga źródłowa → NetDistances`. Metoda `networkFrom`
działa następująco (pseudokod odpowiadający implementacji):

```text
wpis = cache[źródło]
jeżeli wpis.version != roadGraphVersion albo rozmiar dist != liczba slotów flag:
    policz Dijkstrę do wpis.dist i wpis.prev
    wpis.version = roadGraphVersion
zwróć wpis
```

Przykład: 20 tragarzy stoi przy fladze A. Pierwsze `networkFrom(A)` liczy
odległości do wszystkich flag; kolejne odczytują gotową tablicę. Sam dostęp
do `dist[B]` jest O(1), a odtworzenie trasy nadal wymaga przejścia jej odcinków.

### Unieważnianie

`roadGraphChanged()` zwiększa `roadGraphVersion_`. Nie przelicza natychmiast
wszystkich źródeł. Dopiero następne zapytanie przeliczy potrzebny wpis:
to **leniwe unieważnianie**. Sprawdź wywołania w
[`world_roads.cpp`](../examples/10_settlers/world_roads.cpp), m.in. dodawanie
i usuwanie dróg oraz usuwanie flag.

Usunięcie B–C z poprzedniego przykładu zmienia dystans A→C z 7 na 12.
Bez zwiększenia wersji cache nadal zwróciłby 7 i wskazał usuniętą drogę.
Zmiana wag też wymaga unieważnienia. Samo sprawdzenie rozmiaru tablicy nie
wykryje usunięcia drogi ani ponownego użycia slotu flagi.

### Cena optymalizacji

Dla S zapamiętanych źródeł pamięć wynosi O(S·V); gdy pytamy od każdej flagi,
może zbliżyć się do O(V²). Każda zmiana grafu unieważnia logicznie wszystkie
wyniki, nawet jeśli dotyczy odległej części sieci. Cache nie ma limitu LRU
(usuwania najdawniej używanych wpisów) ani przyrostowej naprawy tras.
Przy często przebudowywanej sieci oszczędność będzie mniejsza.

Nie jest to też gwarancja bezpieczeństwa wątkowego: metody `const` modyfikują
`mutable netCache_`. Obecny model używa go w sekwencyjnej symulacji.

Ćwiczenie: policz wynik dla grafu powyżej, dodaj drogę A–C długości 2 i wyjaśnij,
dlaczego zmiana numeru wersji wystarcza bez czyszczenia całej mapy cache.

## 4. Łączność sieci a odległości

[`World::updateRouting`](../examples/10_settlers/world_roads.cpp) przechodzi
graf dwukrotnie. Pierwsze przejście zaczyna od flag zamków i ustawia
`castleLinked`. Drugie zaczyna od flag wszystkich ukończonych magazynów
i ustawia `connected`. Używa wektora jako stosu i tablicy odwiedzonych flag.

To wyszukiwanie osiągalności O(V+E), bez wag. Jest tańsze od liczenia wszystkich
odległości, gdy potrzebujemy tylko odpowiedzi tak/nie.
`routingDirty_` sygnalizuje konieczność odświeżenia tej informacji.
`roadGraphVersion_` unieważnia natomiast **odległości** w `Transport`.
Te dwa mechanizmy mają różne wyniki; jeden nie zastępuje drugiego.

## 5. Przydział transportu jest heurystyką

`assignTransportJobs` wybiera zadanie osobno dla kolejnych wolnych tragarzy:

* odbiór z flagi: wynik oceny `odległość po drogach − 8 × dostępne sztuki`,
* dostawa opłaconego zlecenia: `odległość do źródła − 20`,
* po wyborze odbioru: najbliższy po drogach magazyn z niezarezerwowanym miejscem.

Mniejsza ocena wygrywa. Towar zalegający na fladze zyskuje pilność, a dostawy
budowlane dostają premię. To lokalna, zachłanna decyzja; nie jest globalnym
optymalnym dopasowaniem wszystkich ludzi do wszystkich zadań. Kolejność
tragarzy może wpływać na wynik.

Ważny szczegół: `fundOrder` sprawdza osiągalność po grafie, ale źródła
porównuje **odległością XZ** do odbiorcy. Flaga ma mnożnik 0,8, bo odbiór z niej
odblokowuje produkcję. Nie każde wystąpienie „najbliższy” w logistyce oznacza
minimalny dystans Dijkstry.

## 6. Rezerwacje: dlaczego dwóch ludzi nie bierze tej samej deski

Towar może istnieć fizycznie, ale być już obiecany innemu zadaniu. Dlatego
trzeba rozróżniać stan, dostępność i miejsce docelowe:

| Stan | Znaczenie |
|---|---|
| `Flag::goods` | fizyczne towary na fladze |
| `Flag::reserved[typ]` | część towarów przydzielona do odbioru |
| `Building::stock` | zapas magazynu; opłacenie zlecenia zdejmuje go od razu |
| `Building::incoming` | zarezerwowane miejsce dla odbiorów do magazynu |
| `TransportOrder::funded` / `picked` | towar przypisany do zlecenia / już podniesiony |
| `TransportOrder::carrier` | tragarz obsługujący zlecenie |
| `Building::delivered` | dostarczone wejścia produkcji, budowy lub szkolenia |

Dostępne deski na fladze = liczba desek w `goods` minus rezerwacje. Wolne
miejsce magazynu = pojemność minus `stock.total()` minus `incoming`.

Przy anulowaniu **przed odbiorem** można zwolnić rezerwację na fladze albo
zwrócić opłacony zapas do źródłowego magazynu. **Po odbiorze** towar jest
ładunkiem człowieka; kod porzucania zadania próbuje odłożyć go na odpowiedniej
fladze. Te ścieżki są skupione w `unfundOrder`, `releaseCarrierJob` i
`becomeIdleSettler`. Przeczytaj je razem z funkcjami podnoszenia i kończenia
transportu — zwolnienie tylko `carrier` nie wystarcza.

### Zakleszczenie pełnego magazynu

Magazyn jest pełen kamienia, deski stoją na fladze tartaku, a budowa wymaga
desek. Reguła „budowa kupuje wyłącznie z magazynu” blokowałaby cały cykl.
Dlatego `fundOrder` może rezerwować towar bezpośrednio na fladze. Limity zapasu
ograniczają dodatkowo wnoszenie jednego towaru do magazynów. `storeBound`
uwzględnia zapasy oraz towary już niesione do magazynów, żeby wielu tragarzy
nie przekroczyło limitu jednocześnie.

## 7. Automaty stanów i produkcja opisana danymi

Jednostka nie wykonuje całego zadania jednym wywołaniem. Pamięta stan,
cel, ścieżkę i timer; każdy krok przesuwa ją dalej.
[`updateSettler`](../examples/10_settlers/world_settlers.cpp) rozdziela stany
na osobne funkcje. Tragarz ma własny automat w `Transport`:

```text
CarrierWait → GoToRoad (idzie po towar)
            → CarrierWalk (niesie) → CarrierWait
```

Pracownik warsztatu przechodzi m.in. przez `GoToWork`, `WorkWait`,
`GoGather`, `Gathering`, `ReturnHome`. Konkretna praca zależy od
`productionDef` i `WorkKind` w
[`settlers_defs.cpp`](../examples/10_settlers/settlers_defs.cpp).
Tabela określa wejścia, wyjście i czas cyklu; automat realizuje wspólne kroki.
To pozwala dodać przetwórnię bez kopiowania całego zachowania pracownika.

`requestInputs` zamawia brakujący bufor wejść, licząc **również istniejące
zlecenia**. Bez tego każde odświeżenie tworzyłoby kolejne zamówienie na ten
sam brak. `AnyFood` oznacza dowolny dopuszczony rodzaj żywności; `fundOrder`
wybiera konkretny towar. Czekanie na wejścia albo wolną flagę jest prawidłowym
stanem, a nie dowodem, że jednostka się zawiesiła.

`dispatchJobs` w `world_update.cpp` przydziela prace okresowo, z pierwszeństwem
minimalnej obsady transportu, budów i warsztatów. Przy diagnozie bezczynności
sprawdź zarówno warunki przydziału, jak i automat już przydzielonej jednostki.

## 8. Ruch, rozpychanie i wykrywanie braku postępu

[`setPath`](../examples/10_settlers/world_movement.cpp) najpierw próbuje
`lineWalkable`; tylko przy przeszkodzie liczy A* z limitem 30000 rozwinięć
i dopuszczeniem ścieżki częściowej. `followPath` przesuwa po punktach,
a `separateUnits` rozsuwa nakładających się ludzi. Trasa omijająca budynki nie
rozwiązuje sama konfliktu dwóch ludzi idących naprzeciw siebie.

`updateUnit` mierzy brak postępu w stanach podróży. Po `kStallSeconds = 6`
`recoverStall` próbuje nowej ścieżki i chwilowego przeciskania. Po około
12 sekundach, zależnie od stanu, zalicza dotarcie przy budynku albo porzuca
nieudane zadanie i zgłasza problem. Niedostępny węzeł surowca jest czasowo
pomijany (`markNodeUnreachable`).

Dlaczego nie ponawiać bez końca? Jeden pracownik mógłby stale rezerwować
niedostępne drzewo lub budynek. Dlaczego nie porzucać od razu? Chwilowy tłok
nie powinien przerywać poprawnej dostawy.

## 9. Krok symulacji i prezentacja

[`SettlersApp::advanceSimulation`](../examples/10_settlers/app_core.cpp) liczy
`gameDt = realDt × gameSpeed`, dzieli go na kroki o długości **najwyżej 0,05 s**
i wykonuje maksymalnie **12 kroków na klatkę**. Dopiero potem raz woła
`World::present(realDt)`.

Przykład: 1/60 s przy x24 daje 0,4 s czasu gry, czyli 8 kroków po 0,05 s.
Modele, mgła i granice nie muszą być aktualizowane osiem razy dla jednego
obrazu. Krótkie kroki zmniejszają przeskakiwanie przeszkód i niestabilność ruchu.

To podział na ograniczone podkroki, **nie pełny akumulator stałego kroku**.
Przy przekroczeniu 0,6 s gry na klatkę nadmiar nie jest odkładany do późniejszego
odrobienia: gra zwalnia, aby uniknąć niekończącego się nadrabiania.
`World::simulate` dodatkowo ogranicza bezpośrednio przekazane `dt` do 0,1 s.
`World::update` jest skrótem `simulate` + `present`, używanym m.in. przez testy.

Podział nie oznacza całkowicie niezależnej symulacji bez silnika: `World`
nadal posiada referencję do `Engine`, tworzy encje i siatki, a część skutków
symulacji dotyka ich czasu życia. UITEST i autoplay uruchamiają aplikację
z oknem/Vulkanem.

## 10. Ponowne użycie slotów i uchwyty z generacją

**Problem 1:** po śmierci obiekt zostaje jako martwy slot w wektorze.
Bez ponownego użycia pamięć i koszt skanowania rosłyby z historią partii.
**Problem 2:** po użyciu slotu 17 dla nowego człowieka stare „cel = 17” mogłoby
wskazać zupełnie inną jednostkę.

[`Ref<T>` i `SlotFreeList`](../examples/10_settlers/slot_ref.h) rozwiązują to
parą **indeks + generacja** oraz powiązaniem odwołania z wektorem świata.
`SlotFreeList::allocate` resetuje ponownie użyty obiekt i zwiększa jego `gen`.
Odwołanie pamiętające starą generację zwraca wtedy `-1`.

```text
cel = Ref(slot=17, gen=4)
jednostka umiera → alive=false
slot po kwarantannie dostaje nową jednostkę, gen=5
stary cel: 4 != 5 → brak odwołania
```

`Ref::valid()` sprawdza zakres i generację, **nie sprawdza `alive`**.
Przed ponownym użyciem slotu odwołanie do martwego obiektu może nadal mieć
zgodną generację; dlatego kod nadal sprawdza `alive`.
Kwarantanna ma różne długości: jednostki i węzły 5 s, budynki/flagi/drogi
30 s, zlecenia 1 s czasu gry. Daje czas na posprzątanie starszych zadań,
ale nie zastępuje kontroli odwołań.

Nowe trwałe powiązania między obiektami deklaruj jako `Ref<odpowiedni typ>`.
Surowy `int` w lokalnej liście nie zyskuje ochrony generacją automatycznie.
Przy przypisywaniu indeksów rejestr slotów musi wskazywać właściwy świat
(`World::activate`).

## 11. Kubełki przestrzenne jednostek

**Problem:** jeśli każda z N jednostek szuka przeciwnika przez skan N ludzi,
koszt zbliża się do O(N²).
[`rebuildUnitBuckets`](../examples/10_settlers/world_queries.cpp) raz na krok
wpisuje żywe jednostki do komórek siatki przestrzennej według pozycji XZ.

`findEnemyUnitNear` skanuje tylko kubełki przecinające obszar poszukiwania
(z zapasem 8 jednostek na ruch od przebudowy), a następnie sprawdza dokładny
dystans i reguły wyboru. Żołnierz ma pierwszeństwo przed osadnikiem przez
karę 40 w ocenie osadnika; remis rozstrzyga mniejszy indeks.

Koszt przebudowy to O(N + liczba kubełków), a zapytania zależy od liczby
lokalnych kandydatów. Przy skupieniu wszystkich ludzi w jednym miejscu korzyść
maleje. To indeks do wyboru celów, nie pełny system fizyki; nie należy z niego
wnioskować, że każda pętla po parach w grze została usunięta.

## 12. Terytorium: pole wpływu i cache właścicieli

[`territoryOwnerExact`](../examples/10_settlers/world_queries.cpp) dla każdego
źródła wpływu liczy `promień − odległość XZ`. Wśród dodatnich wyników wygrywa
największy. Dla jednakowych promieni granica dwóch źródeł leży na symetralnej
łączącego je odcinka; dla różnych promieni jest przesunięta. To nie jest
Dijkstra ani rozchodzenie się po przechodnim terenie: woda i przeszkody nie
zmieniają tej odległości.

Wiele zapytań przy budowie i AI korzysta z `territoryOwnerAt`. Po zmianie
źródeł `rebuildTerritoryGrid` buduje siatkę właścicieli. Gdy komórka i jej
czterej sąsiedzi mają tego samego właściciela, kod zwraca wynik z siatki.
Przy wykrytej granicy i przy brzegu mapy wraca do dokładnego przeglądu źródeł.
To przyspieszenie oparte na próbkowaniu; jednolitość pięciu próbek nie jest
matematycznym dowodem braku bardzo małej granicy wewnątrz komórki.

`markTerritoryChanged` unieważnia dane. `applyTerritoryChanges` wprowadza
skutki dla rozgrywki (utrata budynków i dróg), a `rebuildTerritoryMesh` tworzy
słupki osobno, w prezentacji. Granicę z wrogiem przybliża próbkowaniem co
4 jednostki i 6 krokami bisekcji odcinka, na którym zmienia się właściciel.
Jedna siatka słupków na frakcję ogranicza liczbę encji i draw calli.

## 13. Przyrostowa mgła wojny

[`FactionFog`](../examples/10_settlers/settlers_world.h) przechowuje dla każdej
frakcji siatkę: `explored` (kiedykolwiek odkryte), `visible` (widoczne teraz),
`count` (liczba obserwatorów widzących komórkę) oraz poprzednie położenia
obserwatorów. Implementacja:
[`world_fog.cpp`](../examples/10_settlers/world_fog.cpp).

Dlaczego licznik zamiast boola? Jeśli dwóch łuczników widzi tę samą komórkę,
odejście jednego nie może jej zasłonić. `stampSight` dodaje albo odejmuje
wkład jednego koła widzenia; `visible = count > 0`.

`moveObserver` porównuje poprzednią komórkę i promień z bieżącymi. Gdy nic się
nie zmieniło, nie przelicza koła. W przeciwnym razie odejmuje stare koło,
dodaje nowe i oznacza prostokąt zmian. Śmierć, zmiana frakcji lub ukrycie
jednostki w załodze także muszą zdjąć jej wkład.

`updateFogOfWar` jest wywoływane z `present` i odświeża mapy co około 0,15 s
**czasu rzeczywistego**. Nadal przegląda listy obserwatorów, ale kosztowne
stemplowanie wykonuje tylko przy zmianach. Z prostokąta zmian gracza,
powiększonego o jeden piksel, powstaje tekstura: 0/128/255 rozmyte filtrem
3×3. `fowVersion_` rośnie przy zmianie bajtów; renderer osobno aktualizuje
bezpieczne kopie GPU. Ograniczenie przeliczania na CPU do prostokąta nie
oznacza automatycznie częściowego uploadu tekstury na GPU.

Mgła jest kołem widzenia na siatce, bez testów zasłaniania przez góry i domy.
AI pyta o swoją wiedzę przez `AiPlanner::knows`. Wysokie przyspieszenie gry
oznacza więcej czasu gry pomiędzy odświeżeniami wiedzy, bo ich zegar jest
rzeczywisty. Budynki wroga na odkrytym terenie są pokazywane według bieżących
obiektów świata — nie ma osobnych historycznych kopii budynków „ostatnio
widzianych”.

## 14. Planista potrzeb AI

[`AiPlanner`](../examples/10_settlers/ai_planner.cpp) nie szuka globalnie
optymalnej strategii i nie używa uczenia maszynowego. To planista regułowy:
buduje podsumowanie gospodarki (`Economy`) i okresowo wybiera najpilniejszą
decyzję. Ułatwia to zrozumienie powodów rozbudowy i debugowanie przestojów.

`buildNext` najpierw sprząta zbędne obiekty i sprawdza limit budów. Następnie
przechodzi przez potrzeby pilne, proporcje warsztatów, wojsko, niedobory
towarów oraz minimum gospodarki i wzrost. `applyMaterialReserve` i
`redirectToMaterialProducer` chronią materiały potrzebne do utrzymania
podstawowej produkcji. Nie jest to sztywna lista kolejnych budynków.

`planFor` schodzi po zależnościach produktu: brak broni może wynikać z braku
żelaza, rudy, węgla lub żywności dla kopalń. Sama budowa drugiej kuźni nie
usunie problemu kopalni bez jedzenia. Z kolei zalegający produkt na fladze
wskazuje kłopot z odbiorem, a nie automatycznie brak producenta.

Proporcje są tabelą `kAiRatios`, więc ich poprawność zależy od czasów
produkcji i logistyki. `knows`, zwiad oraz pamięć nieosiągalnych celów
ograniczają decyzje do wiedzy frakcji. Log `[ai]` podaje wybrany budynek
i powód; porównania rób na tym samym `SETTLERS_SEED`.

## 15. Zapis: stan trwały i dane odtwarzalne

[`world_save.cpp`](../examples/10_settlers/world_save.cpp) używa wspólnych
funkcji `fields` dla odczytu i zapisu. Przechowuje kolejność slotów wraz
z martwymi pozycjami, ponieważ inne obiekty odwołują się indeksami. `Ref`
zapisuje ważny indeks albo `-1`; przy odczycie generacje startują od zera,
a `setLoadedIndex` nie czyta generacji z jeszcze nieuzupełnionego wektora.

Odkryta mgła jest zapisana jako **RLE**: zamiast wielu jednakowych wartości
przechowujemy wartość i liczbę powtórzeń, np. `0:5,1:3,`. To oszczędza miejsce
przy dużych jednolitych obszarach. Liczniki bieżących obserwatorów można
odtworzyć z ludzi i budynków.

Nie jest to zapis każdego szczegółu wykonania. Modele i dane pochodne są
odtwarzane; pociski i efekty nie są zachowywane. Zachowanie stanu RNG pomaga
w kontynuacji, ale nie gwarantuje identycznego przebiegu walki sprzed zapisu.
Test zapis → odczyt → zapis sprawdza stabilność serializowanych danych,
nie wykryje automatycznie nowego pola, którego w ogóle nie dopisano do `fields`.

## 16. Jak sprawdzać algorytmy na istniejących testach

Scenariusze w [`app_tests.cpp`](../examples/10_settlers/app_tests.cpp):

| Scenariusz | Co pomaga zweryfikować |
|---|---|
| `economyRoadTest`, `storageJamTest`, `productionChainTest` | drogi, dostawy, pełne magazyny, wejścia produkcji |
| `crowdAndAccessTest` | tłok i dojście do budynków |
| `spatialGridTest` | wybór celu przez kubełki względem pełnego skanu |
| `slotReuseTest`, `staleRefTest` | ponowne użycie slotów i stare odwołania |
| `territoryCaptureTest`, `towerAssaultTest` | granice, utrata terenu, zdobywanie wieży |
| `fogOfWarTest`, `aiFogTest` | widoczność i wiedza AI |
| `aiPlannerTest` | reakcje planisty potrzeb |
| `saveLoadTest` | stabilność zapisu i odtworzenie stanu |

Uruchomienie całego zestawu: `SETTLERS_UITEST=1 ./bin/10_settlers_example`.
To test aplikacji wymagający środowiska graficznego. Nie ma osobnego testu
każdego warunku cache Dijkstry: przy rozbudowie tego mechanizmu warto dodać
scenariusz wielokrotnego zapytania, zmiany drogi i ponownego zapytania.
Poprawny dystans przed i po zmianie sprawdza **wynik**, a licznik przeliczeń
byłby potrzebny do sprawdzenia skuteczności samego cache.
