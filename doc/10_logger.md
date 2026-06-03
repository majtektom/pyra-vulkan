# System Logowania (Logger)

Silnik `engine3d` posiada wbudowany, wysokowydajny system logowania diagnostycznego, który zastępuje standardowe wyjścia `std::cout` i `printf`. Jest on kluczowy dla debugowania procesów asynchronicznych i monitorowania stanu backendu Vulkan.

## Kluczowe Funkcje

- **Poziomy Logowania**: `TRACE`, `INFO`, `WARN`, `ERROR`, `CRITICAL`.
- **Thread-Safety**: Loger jest bezpieczny wątkowo, co pozwala na logowanie z systemów wielowątkowych (np. `ParticleSystem`).
- **Wiele Backendów**: Możliwość logowania do konsoli, do pliku tekstowego lub obu jednocześnie.
- **Formatowanie C++20**: Wykorzystanie `std::format` do czytelnego i bezpiecznego typowania komunikatów.
- **Znaczniki Czasowe**: Każdy wpis automatycznie otrzymuje precyzyjny znacznik czasu.

---

## Użycie w Kodzie

Silnik udostępnia zestaw makr, które są najprostszym sposobem na dodanie logów:

```cpp
LOG_INFO("Inicjalizacja systemu: {}", systemName);
LOG_WARN("Nie znaleziono pliku konfiguracji, używam domyślnej.");
LOG_ERROR("Błąd krytyczny Vulkan: {}", vkResultToString(res));
```

Dostępne makra:
- `LOG_TRACE(...)`
- `LOG_INFO(...)`
- `LOG_WARN(...)`
- `LOG_ERROR(...)`
- `LOG_CRITICAL(...)`

---

## Konfiguracja Loggera

Loger działa jako Singleton, do którego dostęp uzyskujemy przez `Logger::getInstance()`.

### Ustawianie Poziomu Logowania
Możemy filtrować mniej istotne wiadomości:
```cpp
// Loguj tylko błędy i ostrzeżenia
Logger::getInstance().setLevel(LogLevel::Warn);
```

### Zmiana Celu Logowania (Backends)
Domyślnie loger wypisuje dane na konsolę. Można to zmienić:
```cpp
// Loguj tylko do pliku
Logger::getInstance().useFileBackend("engine.log");

// Loguj do konsoli i do pliku jednocześnie
Logger::getInstance().useBothBackends("engine.log");
```

---

## Integracja z Vulkan

Loger jest zintegrowany z funkcją `debugCallback` Vulkana. Wszystkie komunikaty z warstw walidacyjnych (Validation Layers) są przechwytywane i przekierowywane do logera z odpowiednim poziomem (ERROR dla błędów walidacji, WARN dla ostrzeżeń wydajnościowych).

## Wydajność

System logowania został zaprojektowany tak, aby minimalizować narzut w wersjach Release:
- Logi poziomu `TRACE` mogą zostać całkowicie wycięte podczas kompilacji.
- Blokady (`mutex`) są używane tylko w momencie faktycznego zapisu do strumienia wyjściowego.
