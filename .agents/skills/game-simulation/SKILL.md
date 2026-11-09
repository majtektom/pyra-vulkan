---
name: game-simulation
description: Wzorce symulacji gier strategicznych/ekonomicznych (RTS, gry w stylu Osadników) - automaty stanów jednostek, przydział prac, wykrywanie zawieszeń, logistyka towarów (drogi, tragarze, magazyny, rezerwacje), terytorium, AI z listą budowy, balans i diagnostyka. Używaj przy projektowaniu lub naprawianiu logiki gry (jednostki stoją, towary nie dochodzą, AI się blokuje, ekonomia się korkuje).
---

# Symulacja gry strategicznej - sprawdzone wzorce

Źródło doświadczeń: `examples/10_settlers` (pliki `world_*.cpp`). Każdy punkt to problem, który realnie wystąpił.

## 1. Dane i identyfikatory

- Wektory obiektów z `alive`; indeks = ID; martwych nie usuwamy. Zawsze sprawdzaj `alive` przy odwołaniu.
- Wolne sloty można używać ponownie (np. zlecenia), ale wtedy przy porzucaniu sprawdzaj, czy slot dalej
  należy do tego samego właściciela (`orders[i].carrier == ten tragarz`).
- `dt` ograniczaj (np. do 0,1 s), przyspieszenie czasu = kilka kroków symulacji na klatkę, nie większe `dt`.

## 2. Automat stanów jednostki

- Jedna funkcja na stan (`settlerGoBuild`, `settlerGather`...), rozdzielnik `switch`. Na początku każdej:
  "czy mój budynek/cel nadal istnieje?" - jeśli nie, `becomeIdle()` (zwolnij WSZYSTKIE rezerwacje).
- Jedna funkcja porzucania zadania (`becomeIdleSettler`) zwalnia: rezerwację surowca, miejsce pracownika,
  rezerwację towaru, miejsce w magazynie, niesiony ładunek (odłożony na najbliższej fladze, nie zgubiony).
- "Dotarł?" nie może wymagać trafienia w punkt: budynki blokują całe komórki nawigacji, więc koniec ścieżki
  wypada dalej od ściany. Akceptuj "ścieżka skończona i odległość < 2 komórki".
- Model postaci zależny od stanu/ładunku ustawiaj w jednym miejscu po automacie.

## 3. Ruch i blokady

- Ścieżka: linia prosta, gdy wolna (`lineWalkable`), inaczej A*. Przed wysłaniem sprawdź `sameRegion`.
- Rozpychanie: stojący ustępują idącym (w bok względem kierunku marszu), pracujący stoją w miejscu.
- Tłok: brak postępu przez ~1,5 s → chwilowe "przeciskanie" (ignorowanie kolizji z innymi).
- Wykrywanie zawieszeń: jednostka w stanie "w drodze" bez ruchu przez N s → krok 1: nowa ścieżka,
  krok 2: zakończ zadanie (blisko celu - zalicz, daleko - zgłoś "brak dojścia" i oznacz budynek).
  Stany, w których czekanie jest normalne (np. tragarz czekający na miejsce), wyłącz z wykrywania.
- Jednostki uwięzione w zablokowanej komórce wypychaj tylko, gdy NIE idą (inaczej walczy to z ruchem).

## 4. Przydział prac (dispatcher)

- Co ~0,5 s, kolejność ważności jawnie w komentarzu: minimum tragarzy → budowy → warsztaty → reszta tragarzy → naprawy.
- Pule osadników (tragarze) nie mogą zagłodzić innych prac: gdy brak wolnego osadnika, zwolnij czekającego
  członka puli (zostaw minimum).
- Nie wysyłaj ludzi do zadań, które nie mogą ruszyć (budowniczy dopiero, gdy dotarły materiały), ale warunek
  "nieosiągalne" sprawdzaj od razu - inaczej plac bez dojścia nigdy nie zostanie oznaczony.
- Przyrost ludności (nowi osadnicy) zależny od liczby czekających zadań + darmowy, gdy osadników prawie nie ma
  (ochrona przed "spiralą śmierci").

## 5. Logistyka towarów (model, który zadziałał)

- Warsztat odkłada towar na fladze przed wejściem (limit, np. 8) - pełna flaga wstrzymuje produkcję
  (pracownik czeka z towarem w rękach i ponawia odłożenie).
- Drogi = graf flag (Dijkstra) + bonus prędkości dla wszystkich na komórkach drogi.
- Wspólna pula tragarzy (liczba ustawiana przez gracza albo Auto: warsztaty + kolejka zleceń + czekające towary),
  wolny tragarz bierze najbliższe zlecenie i niesie CAŁĄ trasę:
  - odbiór: flaga warsztatu → najbliższy po drogach magazyn z miejscem,
  - dostawa: magazyn → plac budowy / koszary.
- Rezerwacje przy przydziale: towar na fladze (per rodzaj), miejsce w magazynie (`incoming`), zlecenie (`carrier`).
  Każda rezerwacja musi być zwalniana w JEDNEJ funkcji porzucania zadania.
- Wydatek zdejmuje towar z magazynu od razu (zlecenie "opłacone"), postęp budowy ograniczony przez dowiezione
  materiały. Odbiorca zniknął przed odbiorem → towar wraca do magazynu; zniknął w trakcie → towar ląduje na fladze.
- ODRZUCONY model: tragarz na każdym odcinku drogi przekazujący towar z flagi na flagę - na krótkich odcinkach
  stoją bezczynni, na ruchliwych się nie wyrabiają, a routing "najkrótszą drogą" potrafi kierować towar na odcinek
  bez tragarza (wszystko staje).
- Surowce "na koncie" = suma zapasów magazynów (licz zawsze z magazynów, nie trzymaj osobnego licznika).

## 6. Terytorium

- Własność punktu: spośród budynków wojskowych, w których zasięgu leży, wygrywa ten, w którym leży "najgłębiej"
  (promień − odległość). Granica wypada w połowie nakładania się zasięgów - brak ziemi niczyjej i wspólnej.
- Przejęty teren: budynki cywilne/place budowy i drogi/flagi przeciwnika na nim płoną.
- Granicę rysuj: zewnętrzną po okręgach, a linię z wrogiem - bisekcją na siatce właścicieli.

## 7. AI

- Lista budowy + cykl późnej gry; pilne potrzeby nadpisują listę (Dom przy limicie ludności, Magazyn przy
  zapełnieniu/korkach). Po kilku nieudanych próbach postawienia pomiń pozycję.
- AI używa tych samych rozkazów i zasad co gracz (koszty, drogi, tragarze). Każdy postawiony budynek od razu
  łączy drogą (`autoConnectFlag`) i okresowo naprawia brakujące połączenia (limit prób na krok).
- Fale ataku: wymagany rozmiar armii rośnie z numerem fali, kilku obrońców zostaje.
- Drobny dochód bierny dla AI kompensuje prostotę decyzji - zależny od poziomu trudności.

## 8. Diagnostyka (zrób ją od razu, nie po fakcie)

- Log autoplay co 30 s: zasoby, ludność, budynki, wojsko + osobna linia transportu (tragarze/zajęci,
  towary na flagach, zlecenia, magazyny, wstrzymane warsztaty). Korki i zagłodzenie widać od razu w liczbach.
- F3: ścieżki i stany jednostek, licznik jednostek per stan (tablica rozmiaru = liczba stanów! - dodanie stanu
  do enum bez powiększenia tablicy psuje pamięć), F9: pełny opis zaznaczonego obiektu i powiązanych jednostek.
- Testy symulacji odtwarzają zgłoszony przez użytkownika przypadek (np. "dwie drogi, jedna bez tragarza")
  i sprawdzają WYNIK (towar dotarł, budynek ukończony), a nie szczegół implementacji.
- Testy dzielące świat: jeden test zostawia warsztaty/drogi, które wpływają na kolejny (np. zapełniają magazyn) -
  na początku testu ustaw znany stan (zapasy, ludność, limit).
- Przy błędzie nieodtwarzalnym dopisz szczegółowy log w miejscu porażki testu zamiast zgadywać.

## 9. Interfejs gracza dla ekonomii

- Znaczniki problemów nad budynkami ("! brak drogi", "! pełna flaga", "! brak dojścia"), panel zaznaczenia z
  przyczyną i podpowiedzią co zrobić, okno gospodarki (gdzie są towary: magazyny / flagi / w drodze).
- Komunikat o problemie raz na kilkadziesiąt sekund na obiekt (inaczej zalewa ekran).
- Brak surowców pod przyciskiem: podpowiedź "brakuje: 20 drewna".
