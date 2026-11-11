# 10_settlers — Osadnicy (gra przykładowa)

`examples/10_settlers` to pełna gra strategiczna w stylu klasycznych „Osadników”,
zbudowana na silniku. Jest zarazem największym testem integracyjnym: pokazuje
ekonomię, transport, walkę, AI, mgłę wojny, zapis gry i debugowanie.

Opis techniczny algorytmów znajduje się w
[28_settlers_algorytmy.md](28_settlers_algorytmy.md): Dijkstra i cache z wersją
grafu, rezerwacje transportu, automaty jednostek, kroki symulacji, uchwyty
z generacją, kubełki przestrzenne, terytorium, przyrostowa mgła i planista AI.

Uruchamianie:

```bash
./bin/10_settlers_example
```

Uruchamiaj z katalogu głównego repo — gra czyta `assets/` względem katalogu
bieżącego. Szczegóły budowania: [22_budowanie_i_wdrazanie.md](22_budowanie_i_wdrazanie.md).

---

## 1. Ekonomia i produkcja

Gospodarka opiera się na **łańcuchach produkcji** (12 towarów):

| Łańcuch | Produkcja |
|---|---|
| drewno | drwal → tartak (kłody → deski, materiał budowlany) |
| kamień | kamieniarz, kopalnia kamienia |
| żywność | farma → młyn → piekarnia (zboże → mąka → chleb), rybak, myśliwy |
| metal | kopalnie węgla i rudy (w górach, zjadają żywność) → huta (żelazo) → kuźnia (broń) |
| las | leśniczy sadzi las |

Domy podnoszą limit ludności. Koszary szkolą mieczników i łuczników (ręcznie
albo **Auto-rekrutacją** z ustawioną proporcją). Wieże poszerzają terytorium
i strzelają do wrogów.

### Flagi, drogi i tragarze

* Warsztat odkłada towary na **fladze** przed wejściem (najwyżej 8). Gdy nikt
  ich nie odbiera, produkcja staje.
* Drogi łączą budynki z magazynami i przyspieszają marsz.
* Wolni tragarze (wspólna pula) niosą towar całą trasą z warsztatu do
  najbliższego magazynu z miejscem, a w drugą stronę — materiały z magazynu na
  place budowy i koszt szkolenia do koszar.
* Budowa i szkolenie postępują w miarę dostaw.
* Dostawa może wziąć towar prosto z flagi warsztatu (np. deski z tartaku na
  budowę) — działa nawet przy pełnych magazynach.

Liczbę tragarzy ustawia się w oknie **Gospodarka** (I) przyciskami −/+ albo
trybem Auto.

### Limity zapasu

W oknie Gospodarka każdy towar ma **limit zapasu** (−/+). Domyślnie limitowane
są surowce i półprodukty: drewno 40, kamień 60, zboże/mąka/węgiel/ruda/żelazo
po 20. Powyżej limitu tragarze nie wnoszą towaru do magazynu, więc magazyny nie
zapychają się jednym towarem, a warsztat z pełną flagą czeka, aż ktoś zużyje
jego wyroby.

Do finansowania dostaw służą zapasy magazynów oraz dostępne towary na
połączonych flagach (`spendable`, `fundOrder`). Zamek i Magazyn mieszczą
po 200 towarów, więc Magazyny warto stawiać blisko produkcji. Okno Gospodarka
pokazuje towary w magazynach, na flagach i w drodze, zapełnienie magazynów,
tragarzy i wstrzymane warsztaty.

---

## 2. Walka i terytorium

* Komputerowy przeciwnik rozbudowuje osadę i wysyła fale ataku.
* Wygrywa ten, kto zniszczy wrogi zamek.
* Wieża przy granicy odbiera wrogowi pas ziemi (granica wypada w połowie
  nakładania się zasięgów). Budynki na przejętym terenie płoną.
* Wieża strzela tylko wtedy, gdy ma **załogę** (do 3 żołnierzy). Wolni
  żołnierze obsadzają puste wieże sami; PPM na własnej wieży wysyła zaznaczonych.
* Wrogiej wieży się nie burzy — **szturmuje się ją** (PPM): obrońcy wychodzą do
  walki, a żołnierz, który wejdzie do pustej wieży, przejmuje ją razem
  z otaczającym terenem.
* Po zwycięstwie przycisk **Graj dalej** przełącza w wolną grę — osadę można
  rozbudowywać bez końca (zapis też działa).

---

## 3. Mgła wojny

* Teren poza zasięgiem wzroku twoich ludzi jest czarny, dopóki go nie odkryjesz.
* Odkryty, ale bez zwiadu — przyciemniony: widać teren i zapamiętane budynki
  wroga, nie widać jego wojska.
* Najdalej widzą łucznicy, wieże i zamek.
* Teren, roślinność, budynki, morze i minimapa są przyciemniane tą samą teksturą
  widoczności.
* Komputer też gra w mgle: zna tylko teren odkryty przez swoich ludzi, wysyła
  zwiadowcę (łucznika), rozbudowuje się w stronę znanych lasów i skał, a falę
  ataku kieruje dopiero na budynek, który już zobaczył.

Implementacja: [17_mgly.md](17_mgly.md).

---

## 4. AI

AI **nie ma listy budowy**. Co kilka sekund czyta te same statystyki co gracz
(braki towarów, przestoje, zapasy, ludność) i stawia to, czego brakuje, pilnując
proporcji warsztatów:

* 1 tartak obsługuje 4 drwali,
* 1 leśniczy wystarcza na 3 drwali,
* 1 młyn na 3 farmy.

Wyczerpanych kamieniarzy burzy, a gdy brakuje miejsca lub surowców, stawia
wieżę w ich stronę. Gra na tych samych zasadach co gracz — bez darmowych
surowców. Decyzje AI można śledzić w logu `[ai]` (`SETTLERS_AILOG=1`).

---

## 5. Sterowanie

| Wejście | Działanie |
|---|---|
| WSAD / krawędź ekranu | kamera |
| Q/E / środkowy przycisk | obrót |
| kółko myszy | zoom |
| LPM | zaznaczanie (ramka) |
| PPM | ruch / atak |
| Ctrl+PPM | marsz bojowy |
| 1–9 | budowa |
| T | droga |
| R | obrót budynku |
| Del | zburzenie / usunięcie flagi |
| Tab | całe wojsko |
| I | gospodarka |
| M | muzyka |
| F1 | pomoc |
| Esc | pauza |

Budowanie: dolny pasek z ikonami budynków pogrupowanymi tematycznie. Po
najechaniu pojawia się karta z opisem i kosztem.

Menu główne i pauza mają okna **Ustawienia** (muzyka/efekty, głośności,
rozdzielczość, pełny ekran, VSync, synchronizacja GPU-CPU, limit FPS, cienie —
zapis w `save/settlers_settings.json`) oraz **Sterowanie** z pełną listą
klawiszy.

---

## 6. Zapis i wczytywanie

* **Zapis**: F5 albo przycisk w menu pauzy.
* **Wczytanie**: F8 albo przycisk w menu głównym / pauzy.
* Plik: `save/settlers_save.json`.

Zapis zawiera:

* ziarno mapy (z niego odtwarza się teren, lasy i skały),
* listy budynków, jednostek, flag, dróg i zleceń transportu,
* stan obu stron i zmiany w surowcach mapy,
* stan generatora losowego.

Zapis pozwala kontynuować stan gospodarki i jednostek. Nie zachowuje pocisków
ani efektów, a dane pochodne są odtwarzane, więc nie gwarantuje identycznego
przebiegu klatka po klatce. Szczegóły serializacji i jej testu: [28](28_settlers_algorytmy.md).

---

## 7. Czas, tryby i debugowanie

* Prędkość czasu: **[** i **]** (x1, x2, x4, x8, x16, x24) albo przycisk
  w menu pauzy.
* W menu głównym **Pokaz: AI kontra AI** — komputer gra za obie strony,
  a gracz ogląda (domyślnie x4).
* **F3** przełącza tryby debug: 1 — ścieżki, cele, stany jednostek, wejścia
  budynków, panel statystyk; 2 — dodatkowo siatka kolizji wokół kamery.
* **F9** zrzuca do konsoli pełny stan zaznaczonych i zawieszonych jednostek.
* `SETTLERS_DEBUG=1|2` włącza tryb debug od startu.
* Symulacja sama wykrywa zawieszone jednostki (log `[zawieszenie]`) i próbuje je
  odblokować.

Tryby testowe:

```bash
SETTLERS_AUTOPLAY=<sekundy> SETTLERS_SPEED=<mnożnik> ./bin/10_settlers_example
SETTLERS_UITEST=1 ./bin/10_settlers_example
```

`SETTLERS_AUTOPLAY` — AI gra za obie strony, stan w logu. `SETTLERS_UITEST=1`
wstrzykuje zdarzenia klawiatury/myszy i sprawdza picking, budowanie,
zaznaczanie i rozkazy; kończy się linią `koniec, błędów: N`.

Pełna lista zmiennych: [22_budowanie_i_wdrazanie.md](22_budowanie_i_wdrazanie.md).

---

## 8. Kod i architektura

Kod jest podzielony na:

* `World` (`settlers_world.h`, pliki `world_*.cpp`) — **symulacja** bez wejścia
  i GUI: jednostki, budynki, drogi, transport, ekonomia, walka, AI, mgła,
* `Transport` (`transport.h/.cpp`) — trasy, pamięć wyników Dijkstry, zlecenia
  i automat tragarza; operuje na danych `World`,
* `AiPlanner` (`ai_planner.h/.cpp`) — gospodarcze i wojskowe decyzje komputera,
* `SettlersApp` (`settlers_app.h`, pliki `app_*.cpp`) — **prezentacja**: silnik,
  kamera, HUD, menu, dźwięk, zapis, debug, testy.

Zasada podziału: decyzje gospodarcze należą do świata, obsługa ekranu do
aplikacji. `simulate` i `present` rozdzielają częstotliwość aktualizacji,
ale `World` nadal korzysta z `Engine`, encji i siatek. Obecne autoplay i UITEST
uruchamiają aplikację z oknem i Vulkanem; nie są osobnym trybem headless.

Mapa plików i kolejność czytania: komentarz na początku
`examples/10_settlers/main.cpp`; opisy klas w `settlers_world.h` i
`settlers_app.h`.

Powiązane moduły silnika:

* [16_ai_nawigacja.md](16_ai_nawigacja.md) — `NavGrid`, A*, regiony,
* [19_teren.md](19_teren.md) — teren, raycast, splatting,
* [12_gui.md](12_gui.md) — HUD, minimapa, `GuiToolbar`,
* [14_audio.md](14_audio.md) — efekty 3D i muzyka,
* [03_ecs_scena.md](03_ecs_scena.md) — encje i systemy.

---

## 9. Zasoby i licencje

Muzyka (RandomMind) i efekty (Kenney) są na licencji **CC0** — szczegóły:
`assets/sound/settlers/CREDITS.md`.
