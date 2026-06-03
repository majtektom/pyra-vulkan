# Kamera i Sterowanie

System wizji i interakcji w `engine3d` został zaprojektowany tak, aby oddzielić dane kamery od logiki jej poruszania oraz wejścia sprzętowego.

## System Kamer

Kamera w silniku jest komponentem (`CameraComponent`), który może być dołączony do dowolnej encji (`Entity`). Pozwala to na łatwe tworzenie kamer podążających za graczem, kamer statycznych czy kamer umieszczonych "w oczach" postaci.

### Kluczowe Parametry
- **FOV (Field of View)**: Kąt widzenia kamery.
- **Aspect Ratio**: Stosunek szerokości do wysokości okna (obliczany automatycznie).
- **Near/Far Plane**: Zakres widoczności (odległość rysowania).

### Macierze Widoku i Projekcji
Macierz widoku (`View Matrix`) nie jest przechowywana bezpośrednio. Jest ona generowana w każdej klatce na podstawie aktualnej pozycji i rotacji encji, do której dołączona jest kamera. Macierz projekcji (`Projection Matrix`) jest generowana na podstawie parametrów perspektywy.

### CameraExtractionSystem
To system odpowiedzialny za:
1. Znalezienie aktywnej kamery na scenie.
2. Pobranie jej parametrów oraz pozycji/rotacji z `TransformComponent`.
3. Wyliczenie macierzy i przygotowanie struktury `RenderView` dla renderera.

---

## System Sterowania (Input)

Silnik wykorzystuje abstrakcyjną warstwę wejścia opartą na **akcjach** (`InputActionMap`). Zamiast sprawdzać konkretne klawisze w kodzie gry, sprawdzamy nazwane akcje.

### Mapowanie Akcji
Akcje mapują fizyczne przyciski (klawiatura, mysz) na logiczne nazwy:
- `move.forward` -> klawisz `W`
- `move.left` -> klawisz `A`
- `ui.click` -> lewy przycisk myszy

### Przetwarzanie Wejścia
W każdej klatce `Engine` pobiera migawkę stanu wejścia (`InputSnapshot`). Dzięki temu:
- Logika gry korzysta ze spójnego stanu przez całą klatkę.
- Można łatwo zaimplementować pauzę lub blokowanie sterowania (np. gdy otwarte jest okno GUI).
- Obsługiwane są stany: `isDown` (przycisk wciśnięty), `wasPressed` (kliknięcie), `wasReleased` (puszczenie).

---

## Kontroler FPS (Fly-around)

Typowy kontroler używany w przykładach łączy obie powyższe funkcje:
1. **Ruch**: Akcje `move.*` modyfikują pozycję encji kamery wzdłuż jej wektorów kierunkowych.
2. **Rozglądanie**: Ruch myszy (delta) modyfikuje rotację encji (Yaw/Pitch).
3. **Gładkość**: Wszystkie ruchy są mnożone przez `DeltaTime`, co zapewnia stałą prędkość niezależnie od liczby FPS.

```cpp
auto& input = engine.input();
if (input.actionDown("move.forward")) {
    position += direction * speed * dt;
}
```
