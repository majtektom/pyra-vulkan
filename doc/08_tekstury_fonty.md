# Tekstury i Fonty

Zarządzanie zasobami 2D w `engine3d` opiera się na wydajnym ładowaniu formatów binarnych oraz integracji z nowoczesnym systemem **Bindless** w Vulkanie.

## System Tekstur

### Obsługiwane Formaty
- **DDS (DirectDraw Surface)**: Rekomendowany format. Wspiera kompresję blokową (DXT1, DXT3, DXT5), którą GPU dekompresuje sprzętowo. Pozwala na ładowanie gotowych mipmap bezpośrednio do pamięci VRAM.
- **BMP / TGA**: Formaty nieskompresowane, używane głównie do prostych assetów lub podczas tworzenia prototypów. Są one konwertowane do formatu RGBA8 podczas ładowania.

### Ładowanie i Rejestracja
Za ładowanie odpowiada `TextureAssetLoader`. Proces wygląda następująco:
1. Odczyt pliku z dysku.
2. Utworzenie obrazu Vulkan przy użyciu VMA.
3. Rejestracja w `AssetRegistry` i przypisanie unikalnego `TextureHandle`.
4. Przesłanie danych do GPU.

### Rendering Bindless
Dzięki wykorzystaniu `descriptorIndexing`, silnik nie musi przełączać deskryptorów dla każdej tekstury. Wszystkie załadowane tekstury trafiają do jednej dużej tablicy na GPU, a shadery odwołują się do nich poprzez indeks przekazywany w **Push Constants**.

---

## System Czcionek i Tekstu

Silnik wspiera renderowanie tekstu 2D oraz tekstowych nakładek (Overlays) przy użyciu czcionek TrueType (.ttf).

### FontAtlas
Silnik nie renderuje glifów bezpośrednio z pliku TTF w każdej klatce. Zamiast tego:
1. Używa biblioteki **SDL3_ttf** do wygenerowania bitmap dla potrzebnych znaków.
2. Składa te bitmapy w jeden duży **Font Atlas** (tekstura z glifami).
3. Generuje dane UV dla każdego znaku.

### TextOverlay
Moduł odpowiedzialny za rysowanie tekstu na ekranie.
- **Wydajność**: Cały tekst w danej klatce jest zbierany w jeden bufor wierzchołków i rysowany jednym wywołaniem (Draw Call).
- **UTF-8**: Pełne wsparcie dla kodowania UTF-8, co pozwala na wyświetlanie polskich znaków i symboli specjalnych.
- **Dynamiczność**: Tekst może być zmieniany co klatkę (np. licznik FPS, debug info).

---

## Zarządzanie Zasobami (AssetRegistry)

Zarówno tekstury, jak i fonty są częścią systemu `AssetRegistry`.
- **Cache**: Ponowne żądanie tego samego pliku zwraca ten sam `Handle`.
- **Domyślne Zasoby**: Silnik zapewnia tekstury fallback (np. różowy checkerboard dla brakujących tekstur), aby uniknąć błędów renderowania i crashy.
