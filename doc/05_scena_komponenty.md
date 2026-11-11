# Zarządzanie Światem: Scena, Encje i Systemy

Silnik `engine3d` wykorzystuje architekturę **ECS-lite** (Entity-Component-System), która separuje dane od logiki i pozwala na efektywne zarządzanie tysiącami obiektów.

## Kluczowe Pojęcia

### 1. Scena (`Scene`)
Scena jest najwyższym poziomem kontenera świata. Przechowuje ona wszystkie aktywne encje, systemy oraz zarządza cyklem życia obiektów.
- **Wieloscenowość**: `SceneManager` pozwala na trzymanie wielu scen w pamięci, ale tylko jedna jest aktualnie przetwarzana i renderowana.
- **Ekstrakcja**: Scena dostarcza dane do renderera w formie zoptymalizowanych list świateł i paczek renderujących.

### 2. Encja (`Entity`)
Encja to lekki obiekt logiczny posiadający unikalny `EntityId`. Sama w sobie nie zawiera logiki — jest jedynie "haczykiem" na komponenty.
- **Lifecycle**: `scene.createEntity("Name")` / `scene.destroyEntity(id)`.

### 3. Komponenty (`Component`)
Dane przypisane do encji. Główne typy:
- **`TransformComponent`**: Pozycja, rotacja i skala. Zawiera flagę `dirty`, informującą system o konieczności przeliczenia macierzy światowej.
- **`MeshComponent`**: Przechowuje uchwyty (`Handle`) do siatki geometrycznej i materiału.
- **`LightComponent`**: Definiuje parametry światła (typ, kolor, zasięg).
- **`CameraComponent`**: Parametry projekcji (FOV, Near/Far).
- **`HierarchyComponent`**: Zarządza relacją rodzic-dziecko.

### 4. Systemy (`Systems`)
Logika działająca na zbiorach komponentów. Przykłady:
- **`TransformSystem`**: Masowo przelicza macierze świata dla wszystkich encji w hierarchii.
- **`LightSystem`**: Zbiera dane o aktywnych światłach do przesłania do GPU.
- **`RenderExtractionSystem`**: Selekcjonuje obiekty widoczne dla kamery i przygotowuje dane dla renderera.

---

## Hierarchia i Transformacje

Relacje rodzic-dziecko są obsługiwane przez `HierarchyComponent`.
- **Propagacja**: Zmiana pozycji rodzica automatycznie (przez system) wpływa na pozycję wszystkich dzieci w następnej klatce.
- **Usuwanie**: Wsparcie dla polityk usuwania (np. usuwanie całego drzewa lub przepinanie dzieci do dziadka).

---

## Zalety Architektury ECS-lite

1. **SOLID**: Każdy system ma jedną odpowiedzialność (Single Responsibility).
2. **Łatwość Rozbudowy**: Dodanie nowej funkcjonalności (np. fizyki) polega na dodaniu nowego komponentu i systemu, bez modyfikacji istniejącego kodu encji.
3. **Data-Oriented**: Przetwarzanie komponentów w systemach pozwala na lepsze wykorzystanie pamięci podręcznej CPU (Cache Friendliness).
