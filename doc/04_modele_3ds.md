# Ładowanie modeli 3DS

## Format 3DS

3DS to binarny format plików 3D Studio Max (stary, ale prosty). Plik składa się z
**chunków** — każdy chunk ma 2-bajtowy identyfikator i 4-bajtową długość.

### Struktura chunka
```
[2 bajty: ID] [4 bajty: długość] [dane...]
```

Główne chunki w loaderze:

| ID      | Nazwa            | Zawartość |
|---------|------------------|-----------|
| 0x4D4D  | M3DMAGIC         | Główny chunk |
| 0x3D3D  | M3D_VERSION      | Dane sceny |
| 0xAFFF  | MATERIAL         | Definicja materiału |
| 0xA000  | MAT_NAME         | Nazwa materiału |
| 0xA081  | MAT_TWO_SIDE     | Flaga dwustronności (bez danych, sama obecność = true) |
| 0xA200  | MAT_TEXMAP       | Mapa tekstury |
| 0xA300  | MAT_MAPFILE      | Nazwa pliku tekstury |
| 0x4000  | NAMED_OBJECT     | Obiekt (mesh) |
| 0x4100  | TRIANGULAR_MESH  | Siatka trójkątów |
| 0x4110  | VERTEX_LIST      | Lista wierzchołków (x,y,z) |
| 0x4120  | FACE_DESCRIPTION | Lista ścian (indeksy wierzchołków) |
| 0x4140  | TEX_COORDS       | Współrzędne UV |
| 0x4130  | FACE_MATERIAL    | Który materiał dla której ściany |
| 0xB000  | KEYFRAME         | Dane animacji (nieobsługiwane) |

---

## Proces ładowania

```
Model3ds::load("sciezka.3DS")
│
├─ Otwórz plik, sprawdź nagłówek M3DMAGIC
│
├─ processNextChunk() — pętla po chunkach
│   ├─ 0xAFFF (MATERIAL) → processNextMaterialChunk()
│   │   ├─ 0xA000 → nazwa materiału
│   │   ├─ 0xA081 → twoSided = true
│   │   ├─ 0xA200 + 0xA300 → ścieżka tekstury
│   │   └─ ... kolory, przezroczystość
│   │
│   └─ 0x4000 (NAMED_OBJECT) → processNextObjectChunk()
│       ├─ 0x4100 (TRIANGULAR_MESH)
│       │   ├─ 0x4110 → readVertices() → tablica Vec3
│       │   ├─ 0x4120 → readVertexIndices() → tablica tFace
│       │   ├─ 0x4140 → readUVCoordinates() → tablica UV
│       │   └─ 0x4130 → readObjectMaterial() → indeks materiału
│       └─ ...
│
├─ computeNormals() — oblicz normalne per-wierzchołek
│   (uśrednianie normalnych ścian współdzielących wierzchołek)
│
├─ buildMeshData() — konwersja do MeshData
│   (Vec3 → Vertex {position, normal, color, texCoord})
│
└─ resolveTexturePaths() — znajdź pliki tekstur na dysku
```

---

## Konwersja współrzędnych

3DS używa układu: **Y=góra, Z=głębia**. Silnik używa: **Y=góra, Z=przód**.
Konwersja w `readVertices()`:

```cpp
// 3DS → engine
object->pVerts[i].x = rawVerts[i*3+0];  // X bez zmian
object->pVerts[i].y = rawVerts[i*3+2];  // Z_3DS → Y_engine
object->pVerts[i].z = -rawVerts[i*3+1]; // Y_3DS → -Z_engine
```

Negacja Z wynika ze zmiany układu prawoskrętnego na lewoskrętny (lub odwrotnie).

---

## Normalne

Jeśli model 3DS nie zawiera normalnych, są one obliczane automatycznie:

```cpp
void computeNormals(t3DObject* object) {
    // Dla każdej ściany: oblicz normalną (cross product krawędzi)
    // Dla każdego wierzchołka: uśrednij normalne wszystkich współdzielonych ścian
    // Normalizuj wynik
}
```

Daje to gładkie cieniowanie (smooth shading) zamiast płaskiego (flat shading).

---

## Dwustronne renderowanie (two-sided)

Format 3DS przechowuje flagę `MAT_TWO_SIDE` (chunk 0xA081). Jeśli jest obecna,
materiał renderuje trójkąty z obu stron (backface culling wyłączony).

W silniku ta flaga jest konwertowana na `CullMode`:
```cpp
mat.cullMode = mat3ds.twoSided ? CullMode::Disabled : CullMode::Back;
```

Dodatkowo, materiały z teksturami z kanałem alfa (liście, trawa) automatycznie
dostają `CullMode::Disabled` — nawet jeśli model nie ma flagi `MAT_TWO_SIDE`.

---

## Konwersja materiału 3DS → Material engine'a

```cpp
Material Convert3dsMaterialToEngineMaterial(const Model3ds::tMaterialInfo& mat3ds) {
    Material mat;
    mat.shaderProgram = "default";
    mat.cullMode = mat3ds.twoSided ? CullMode::Disabled : CullMode::Back;
    // ... domyślne wartości dla reszty pól
    return mat;
}
```

W przykładzie `flyaround`, konwersja jest rozszerzona o:
- Wykrywanie przezroczystości (DXT3/DXT5 → alpha blending)
- Automatyczny double-sided dla tekstur z alfą
- Fallback tekstur, gdy plik nie istnieje

---

## Struktury danych

```cpp
// Metadane materiału z pliku 3DS
class tMaterialInfo {
    std::string name;              // nazwa materiału
    std::string textureFile;       // nazwa pliku tekstury
    std::array<float,4> ambientColor;
    std::array<float,4> diffuseColor;
    float transparency;
    bool twoSided;                 // czy dwustronny
};

// Jeden obiekt/mesh w pliku 3DS
class t3DObject {
    std::string strName;           // nazwa obiektu
    Vec3* pVerts;                  // wierzchołki
    Vec3* pNormals;                // normalne
    tFace* pFaces;                 // ściany (indeksy)
    std::array<float,2>* pTexVerts;// UV
    MeshData mesh;                 // wynikowa siatka
    int materialIndex;             // indeks w liście materiałów
};
```

---

## Wskazówki do własnego loadera

1. Użyj struktury chunków — łatwo rozszerzyć o nowe typy chunków
2. Zawsze sprawdzaj `bytesRead < length` przy czytaniu chunków
3. Konwertuj współrzędne na swój układ przy ładowaniu, nie przy renderowaniu
4. Generuj normalne jeśli model ich nie ma — `computeNormals()` jest prosty
5. Rozdziel logikę parsowania od budowania MeshData — ułatwia testowanie
