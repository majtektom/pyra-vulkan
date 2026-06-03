# Modele 3D i Loadery

Silnik `engine3d` wspiera kilka popularnych formatów modeli 3D, z których każdy jest dedykowany do innych celów: od klasycznych modeli statycznych po nowoczesne pliki z animacją szkieletową.

## Obsługiwane Formaty

### 1. glTF 2.0 (.gltf / .glb)
Główny i najbardziej zaawansowany format wspierany przez silnik.
- **Biblioteka**: `tinygltf`.
- **Funkcje**: Ładowanie geometrii, materiałów PBR, osadzonych tekstur oraz **animacji szkieletowej (Skinning)**.
- **Skinning**: Dane o wagach kostek i indeksach są przesyłane do Vertex Shadera, gdzie macierze kości są aplikowane w czasie rzeczywistym.

### 2. Autodesk 3DS (.3DS)
Klasyczny format dla modeli statycznych.
- **Loader**: Autorski `Model3ds`.
- **Specyfika**: Obsługuje hierarchię obiektów zdefiniowaną w pliku, mapowanie tekstur i materiały.
- **Konwersja**: Loader automatycznie konwertuje współrzędne z systemu 3DS (Z-up) na system silnika (Y-up).

### 3. Quake II MD2 (.md2)
Format historyczny, używany głównie do prostych animacji klatkowych (vertex animation).
- **Zastosowanie**: Szybkie renderowanie prostych postaci z predefiniowanymi klatkami animacji.

---

## Integracja z Systemem ECS

Modele po załadowaniu są dzielone na sub-meshe. Każdy sub-mesh jest reprezentowany przez:
- **`MeshData`**: Bufor wierzchołków (Pozycja, Normalna, UV, TBN, ewentualnie Jointy/Weighty) i indeksów.
- **`MeshComponent`**: Komponent przypisany do `Entity`, przechowujący `MeshHandle` oraz `MaterialHandle`.

Proces dodawania modelu do sceny:
1. Ładowanie pliku (np. `loadGltfModel`).
2. Rejestracja mesha w `MeshManager` -> otrzymanie `MeshHandle`.
3. Utworzenie encji w `Scene`.
4. Dodanie `MeshComponent` z otrzymanym uchwytem.

---

## Transformacje i Koordynaty

Wszystkie loadery dbają o spójność z matematyką silnika:
- **Skala**: Możliwość przeskalowania modelu podczas ładowania lub przez `TransformComponent`.
- **TBN (Tangent, Bitangent, Normal)**: Automatyczne przeliczanie wektorów stycznych niezbędnych do poprawnego działania **Normal Mappingu**.
- **Wyrównanie**: Automatyczne centrowanie modeli lub ustawianie punktu Pivot na podstawie danych z pliku źródłowego.

---

## Optymalizacje

- **VBO/IBO Sharing**: Wiele encji może korzystać z tego samego `MeshHandle`, co oznacza, że dane wierzchołków są przechowywane w pamięci GPU tylko raz (Instancing).
- **Index Buffers**: Silnik promuje użycie buforów indeksów (Triangle Lists), co redukuje rozmiar danych przesyłanych do GPU i poprawia wydajność cache'u wierzchołków.
