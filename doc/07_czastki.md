# System Cząsteczek

System cząsteczek w `engine3d` pozwala na tworzenie efektów takich jak ogień, dym, deszcz czy eksplozje. Jest to system zoptymalizowany pod kątem masowego przetwarzania małych obiektów graficznych.

## Architektura Systemu

### ParticleEmitter
Emiter jest jednostką odpowiedzialną za generowanie i zarządzanie życiem cząsteczek.
- **Parametry**: Definiuje tempo emisji, kierunek, prędkość, czas życia, kolor oraz grawitację.
- **Zasoby**: Każdy emiter korzysta z `TextureHandle` dla tekstury cząsteczki.
- **Recycling**: System używa wewnętrznej tablicy z recyklingiem obiektów, co eliminuje częste alokacje pamięci (Memory Pooling).

### ParticleSystem
Manager zarządzający wszystkimi emiterami.
- **Wielowątkowość**: Aktualizacja cząsteczek (`update`) jest zrównoleglona przy użyciu `std::async`. Każdy emiter może być przetwarzany w osobnym wątku, co znacząco poprawia wydajność przy dużej liczbie efektów.
- **Integracja z ECS**: `ParticleSystem` działa jako system sceny, zbierając dane o pozycji emiterów z encji i przekazując dane renderujące do renderera.

---

## Renderowanie Cząsteczek

Cząsteczki są renderowane jako **Billboardy** (zawsze zwrócone w stronę kamery).
- **Instancing**: Zamiast rysować każdą cząsteczkę osobno, system wysyła dane o wszystkich cząsteczkach emitera w jednym pakiecie (Draw Call).
- **Blending**: Wsparcie dla addytywnego mieszania kolorów (Additive Blending), idealnego dla ognia i efektów świetlnych.
- **Depth**: Cząsteczki są renderowane w fazie `TransparentPass` z wyłączonym zapisem do bufora głębi, co pozwala na ich poprawne przenikanie się.

---

## Definicja Efektów (.particle)

System wspiera ładowanie konfiguracji emiterów z plików tekstowych:

```text
ParticleSystem 1.0 explosion
particles "smoke"
{
    count = 100
    rate = 50
    life = 1.0 2.0
    velocity = -1 -1 -1 1 1 1
    color = 0.5 0.5 0.5 1.0 0.2 0.2 0.2 0.0
    size = 0.1 0.4
}
```

## Wydajność i Limity

- **Max Particles**: Domyślnie system jest zoptymalizowany pod kątem tysięcy aktywnych cząsteczek.
- **SIMD/Optimization**: Matematyka aktualizacji pozycji wykorzystuje zoptymalizowane struktury danych, aby zminimalizować błędy pamięci podręcznej (Cache Misses).
