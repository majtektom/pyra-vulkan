# System cząstek

## Architektura

System cząstek składa się z trzech warstw:

```
CParticleSystem          – kontener wielu emiterów, wczytuje plik .particle
├─ CListParticles        – pojedynczy emiter (parametry + cząstki)
│  ├─ RecyclingArray<CParticle>  – tablica cząstek z recyklingiem
│  └─ CParticleEvent[]   – zdarzenia modyfikujące cząstki w czasie
```

---

## Pojedyncza cząstka (CParticle)

```cpp
class CParticle {
    Vec3 m_pos;              // pozycja
    Vec3 m_velocity;         // prędkość
    std::array<float,4> m_color;  // kolor (RGBA)
    float m_size;            // rozmiar
    float m_mass;            // masa (wpływa na grawitację)
    float m_lifeTime;        // całkowity czas życia
    float m_age;             // aktualny wiek
    ListEventIter m_iter_event;  // iterator zdarzeń
};
```

Każda cząstka ma też pola `_step` i `_time` dla płynnych zmian
(kolor, rozmiar, pozycja, prędkość, masa) — używane przez zdarzenia `fade`.

---

## RecyclingArray — zarządzanie pamięcią

`RecyclingArray` to szablon tablicy, która pozwala na ponowne użycie
martwych elementów (bez realokacji):

```cpp
template<class T>
class RecyclingArray {
    T* m_aElements;      // tablica elementów
    bool* m_bAlive;      // czy element jest żywy
    int m_iLastNew;      // indeks ostatnio utworzonego (do szybkiego szukania)

    T* New();            // znajdź martwy element i zwróć go (lub nullptr)
    bool Delete(int i);  // oznacz jako martwy
    bool IsAlive(int i); // sprawdź czy żywy
};
```

Zaleta: `New()` nie alokuje pamięci — szuka wolnego slotu w istniejącej tablicy.
Wada: tablica ma stały rozmiar (max particles).

---

## Zdarzenia (CParticleEvent)

Zdarzenia modyfikują właściwości cząstek w określonym czasie:

```cpp
class CParticleEvent {
    float time;              // kiedy zdarzenie się aktywuje
    EVENT_TYPES type;        // typ: SIZE, MASS, LIFETIME, COLOR, POSITION, VELOCITY
    float m_min_duration;    // czas trwania (dla fade)
    float m_min_1..4;        // wartości docelowe
};
```

Zdarzenia są sortowane po czasie i wywoływane w `update()`:

```cpp
for (auto& event : events) {
    if (particle->m_ageEvent > event->GetTime()) {
        event->RunEvent(particle);  // ustaw wartości _step i _time
    }
}
```

### Typy zdarzeń

| Typ | Efekt |
|-----|-------|
| SIZE | Zmienia rozmiar cząstki |
| MASS | Zmienia masę |
| LIFETIME | Zmienia czas życia |
| COLOR | Zmienia kolor (płynnie, przez `_step`) |
| POSITION | Przesuwa cząstkę |
| VELOCITY | Zmienia prędkość |

---

## Aktualizacja cząstek (fizyka)

### Sekwencyjna (oryginalna)
```cpp
void CListParticles::update(float time) {
    // Faza 1: aktualizacja istniejących cząstek
    for (int i = 0; i < totalElements; i++) {
        par->m_age += time;
        par->m_pos += par->m_velocity * time + gravitation * par->m_mass * time;
        // ... eventy: kolor, rozmiar, masa ...
    }
    // Faza 2: emisja nowych
    for (int j = 0; j < newparticles; j++) {
        par = particles->New();
        // ... losowe wartości startowe ...
    }
}
```

### Równoległa (zoptymalizowana)
```cpp
void CListParticles::updateParallel(float time, int threadCount) {
    // Faza 1 (równolegle): aktualizacja w N wątkach
    // Każdy wątek dostaje swój kawałek tablicy [cstart, cend)
    std::async([&]() {
        for (int i = cstart; i < cend; i++) { /* fizyka */ }
    });
    // Faza 2 (sekwencyjnie): usuwanie martwych + emisja
}
```

Używa `std::async` z per-thread `std::mt19937` RNG (zamiast `std::rand()`).

---

## Renderowanie cząstek (billboard quady)

Cząstki są renderowane jako **billboardy** — quady zawsze zwrócone przodem do kamery:

```cpp
void buildMeshDataParallel(MeshData& mesh, ...) {
    // Równolegle buduje siatkę 4 wierzchołków + 6 indeksów na cząstkę
    for each particle:
        float s = par->m_size;
        // Billboard: quad w płaszczyźnie prostopadłej do kamery
        v0 = pos - right*s - up*s;    // lewy-dolny
        v1 = pos + right*s - up*s;    // prawy-dolny
        v2 = pos + right*s + up*s;    // prawy-górny
        v3 = pos - right*s + up*s;    // lewy-górny
}
```

`right` i `up` to wektory kamery (aktualizowane co klatkę).

---

## Optymalizacje

1. **Równoległa aktualizacja** — `updateParallel()` dzieli tablicę na wątki
2. **Równoległe budowanie mesh** — `buildMeshDataParallel()` używa `std::async`
3. **Pre-komputacja indeksów** — indeksy `{vi+0,vi+1,vi+2, vi+2,vi+3,vi+0}` są
   generowane tylko gdy zmieni się liczba cząstek (zapisane w `lastActiveCount`)
4. **Host-visible bufory** — `updateMesh()` używa `memcpy` zamiast stagingu
5. **Per-thread RNG** — każdy wątek ma własny `std::mt19937`, unikając
   współdzielenia stanu `std::rand()`

---

## Format pliku .particle

Plik `.particle` to format tekstowy definiujący emitery:

```
emitter ogien_dzialo {
    numparticles 70000
    emitrate 14000
    position (-2, 0, -2) (2, 0, 2)
    velocity (1, 5, 1) (2, 10, 2)
    lifetime 0.5 2.0
    size 0.1 0.3
    mass 0.5 1.0
    color (1, 0.4, 0.1, 1) (1, 0.8, 0.3, 1)
    texture "og.dds"
    fade color (1, 0.2, 0, 0.5) at 0.5 duration 0 1
    fade size 0 0.1 at 1.0 duration 0 0.5
}
```

`fade` tworzy zdarzenia `CParticleEvent`, które płynnie zmieniają
właściwości cząstek w czasie.
