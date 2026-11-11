# Kierunek Rozwoju Silnika

Ten dokument opisuje ewolucję silnika `engine3d` oraz wyznacza cele na przyszłość. Silnik przeszedł transformację z prostego projektu edukacyjnego w modularną platformę opartą o nowoczesne standardy C++ i Vulkan.

---

## 1. Kamienie Milowe (Zakończone)

### Architektura i ECS
- [x] **Wprowadzenie Sceny**: Przeniesienie zarządzania encjami z `Engine` do dedykowanej klasy `Scene`.
- [x] **Systemy ECS**: Implementacja systemów masowo przetwarzających dane (`TransformSystem`, `LightSystem`, `RenderExtractionSystem`).
- [x] **Hierarchia Obiektów**: Pełne wsparcie dla relacji parent-child z poprawną propagacją transformacji.
- [x] **EntityId**: Stabilne identyfikatory dla encji, niezależne od ich adresu w pamięci.

### Rendering
- [x] **Vulkan 1.4**: Migracja na nowoczesne API (Dynamic Rendering, Timeline Semaphores).
- [x] **Deferred Shading**: Implementacja potoku odroczonego z G-Bufferem.
- [x] **Bindless Textures**: Przejście na `descriptorIndexing` — obsługa do 1024 tekstur bez przełączania setów.
- [x] **Shadow Mapping**: Obsługa cieni dla wielu świateł (do 16) przy użyciu tablic tekstur.
- [x] **Skinning**: Animacja szkieletowa glTF wykonywana w vertex shaderze.

### Zasoby i Dane
- [x] **Asset Registry**: System typowanych uchwytów (`Handle`) zamiast stringów w runtime.
- [x] **Modern Loaders**: Wsparcie dla glTF (przez tinygltf), 3DS i MD2.
- [x] **Logger**: Centralny, bezpieczny wątkowo system logowania diagnostycznego.

---

## 2. Cele Krótkoterminowe

### Asynchroniczne Ładowanie Zasobów
- [ ] Implementacja `AsyncAssetLoader`, który ładuje tekstury i modele w tle, nie blokując głównej pętli gry.
- [ ] System placeholderów (wyświetlanie domyślnego modelu/tekstury do czasu zakończenia ładowania).

### Rozbudowa Hot Reloadingu
- [ ] Rozszerzenie systemu o automatyczne odświeżanie materiałów i definicji scen po zmianie plików JSON.

### Fizyka i Kolizje
- [ ] Integracja prostej biblioteki fizycznej lub autorskie rozwiązanie dla kolizji brył sztywnych (AABB, Sphere).

---

## 3. Wizja Długoterminowa

### Render Graph
Przejście na pełny graf renderowania, co pozwoli na automatyczną optymalizację barier Vulkan i łatwiejsze dodawanie efektów Post-process (Bloom, SSAO, SSR).

### Editor Mode
Stworzenie wbudowanego edytora opartego o system GUI silnika:
- Hierarchia sceny (Tree View).
- Inspektor właściwości komponentów.
- Manipulatory transformacji (Gizmos).
- Przeglądarka zasobów.

### Skryptowanie
Integracja języka skryptowego (np. Lua lub autorski system "Visual Scripting") dla logiki wysokiego poziomu bez konieczności rekompilacji całego silnika.
