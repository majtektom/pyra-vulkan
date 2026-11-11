# Dźwięk: efekty, 3D i muzyka

Pliki: `engine/feature/audio/audio_system.*`, `stb_vorbis*`.

Wyjaśnienie kolejki SPSC, publikacji danych między wątkami i bufora
pierścieniowego: [29_algorytmy_silnika.md](29_algorytmy_silnika.md).

`AudioSystem` to mikser na SDL3 (urządzenie float32 stereo) z własnym wątkiem
audio. Obsługuje:

* efekty WAV (SDL3) i OGG (stb_vorbis),
* wiele wariantów tego samego dźwięku (losowy wybór),
* limity instancji i minimalny odstęp,
* dźwięk przestrzenny (głośność + panorama),
* strumieniowaną muzykę OGG z przenikaniem.

Gdy nie ma urządzenia audio, wszystkie wywołania są **no-op** — gra działa dalej.

---

## 0. Najważniejsze struktury audio

| Struktura | Co to jest | Rola |
|---|---|---|
| `AudioSystem` | Mikser i zarządca dźwięku | Efekty, warianty, limity, 3D, muzyka |
| `SoundBuffer` (wewnętrzna) | Zdekodowane próbki stereo | Jeden wariant efektu |
| `SoundEntry` (wewnętrzna) | Wpis dźwięku | Warianty, limity, czas ostatniego odtwarzania |
| `Voice` (wewnętrzna) | Grający efekt | Bufor, pozycja, pitch, gain L/P |
| `Command` / `CommandQueue` (wewnętrzna) | Kolejka SPSC | Gra → wątek audio bez blokad |
| `MusicTrack` (wewnętrzna) | Strumień utworu | Dekodowanie OGG, bufor pierścieniowy |

Pełny słownik: [00_slownik_pojec.md](00_slownik_pojec.md).

---

## 1. Wątki i zasada „audio nie czeka”

```plantuml
@startuml
title Trzy światy AudioSystem
participant "Wątek gry" as Game
participant "Kolejka poleceń\n(lock-free)" as Q
participant "Wątek audio\n(SDL callback)" as Audio
participant "Wątek strumienia" as Stream
participant "Bufor pierścieniowy\nmuzyki" as Ring

Game -> Q : Command{Voice / StartMusic / FadeMusic}
Q -> Audio : pop (bez blokad)
Stream -> Ring : dekoduje OGG z wyprzedzeniem ~1 s
Audio -> Ring : czyta gotowe próbki
Audio -> Audio : miksuje efekty + muzykę
Audio -> [głośniki] : bufor wyjściowy
@enduml
```

Zasady:

* **Wątek audio nie bierze muteksu, nie alokuje i nie dekoduje plików.**
  Musi oddać próbki na czas (deadline sterownika).
* Wątek gry podejmuje decyzje (limity, głośność) i wysyła gotowe polecenia.
* Muzyka jest dekodowana w osobnym wątku do `SampleRing`; wątek audio tylko czyta.

---

## 2. Ładowanie efektów

```cpp
AudioSystem* audio = engine.getAudio();

audio->loadSound("krok", "assets/sound/settlers/sfx/krok1.wav");
audio->loadSound("krok", "assets/sound/settlers/sfx/krok2.wav");  // wariant
audio->loadSound("krok", "assets/sound/settlers/sfx/krok3.wav");

audio->setSoundLimits("krok", /*maxInstances*/ 3, /*minInterval*/ 0.05f);
```

Kolejne `loadSound` z tą samą nazwą dodają **warianty**. `play` losuje jeden
z nich — dzięki temu 30 drwali nie brzmi jak jeden robot.

`setSoundLimits` chroni przed kakofonią:

* `maxInstances` — ile naraz może grać,
* `minInterval` — minimalny odstęp między odtworzeniami.

---

## 3. Odtwarzanie

```cpp
// 2D (interfejs)
audio->play("klik", /*volume*/ 0.8f, /*pan*/ 0.0f, /*pitch*/ 1.0f);

// 3D (świat)
audio->setListener(cameraPos, cameraRight);
audio->setDistanceModel(/*ref*/ 60.0f, /*max*/ 600.0f);
audio->play3D("mlot", pozycjaBudynku, 1.0f, /*pitchVariation*/ 0.08f);
```

Dźwięk 3D:

* głośność maleje od `refDistance` do `maxDistance` (potem cisza),
* panorama L/P z rzutu wektora na `listenerRight`,
* `pitchVariation` dodaje losowe odchylenie (naturalność).

Słuchacz aktualizuj co klatkę po ruchu kamery:

```cpp
const Vec3 pos = camera->getOwner()->transform().position;
audio->setListener(pos, cameraRight);
```

---

## 4. Muzyka

```cpp
audio->playMusic("assets/sound/settlers/music/theme.ogg", /*loop*/ true, /*fade*/ 2.0f);
audio->stopMusic(1.5f);
bool gra = audio->isMusicPlaying();
```

* OGG jest dekodowany strumieniowo (mała pamięć),
* przenikanie utworów (`fadeSeconds`),
* `playMusic` nie dekoduje na wątku gry — pierwszą porcję robi wątek strumienia.

Głośności:

```cpp
audio->setMasterVolume(1.0f);
audio->setMusicVolume(0.55f);
audio->setSfxVolume(0.9f);
```

`GameSettings::applyAudio(audio)` ustawia je z pliku ustawień i wycisza muzykę,
gdy `musicOn == false`.

---

## 5. Diagnostyka

```cpp
float peak = audio->peakLevel();    // szczyt od ostatniego odczytu (0..1)
int voices = audio->activeVoices(); // liczba grających efektów
```

Przydatne, gdy dźwięk „trzaska” (peak > 1) albo podejrzewasz wyciek głosów.

---

## 6. `10_settlers` — praktyka

* efekty: klik, budowa, młot, drwal, walka, śmierć,
* warianty kroków i uderzeń,
* limit instancji dla częstych dźwięków,
* muzyka w tle z przenikaniem przy zmianie stanu gry,
* 3D dla pracy budynków i walki.

Dźwięk jest w `app_audio.cpp`; `World` nie zna `AudioSystem` — aplikacja mapuje
zdarzenia symulacji na dźwięki.

---

## 7. Pułapki

* **Dekodowanie OGG na wątku gry** — `playMusic` już tego nie robi; nie wołaj
  ręcznie dekodera w `onUpdate`.
* **Brak `setListener`** — dźwięk 3D gra w środku głowy (panorama 0).
* **Zbyt wiele instancji** — ustaw `setSoundLimits`; inaczej miksowanie i słuch
  cierpią.
* **Zmiana głośności z wątku audio** — nie rób tego; używaj setterów z wątku gry
  (są atomowe).
* **Urządzenie audio niedostępne** — `isReady() == false`, ale API nadal działa
  (no-op). Nie zakładaj, że dźwięk zawsze jest.
* **Zamykanie SDL przed audio** — `AudioSystem::shutdown` musi być przed
  `SDL_Quit`; `Engine::cleanup` pilnuje kolejności.
