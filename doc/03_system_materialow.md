# System Materiałów i Shaderów

System materiałów w `engine3d` odpowiada za definicję wyglądu obiektów oraz zarządzanie potokami graficznymi (`VkPipeline`). Architektura opiera się na modelu **PBR (Physically Based Rendering)** oraz nowoczesnym podejściu **Bindless**.

## Materiał PBR

Klasa `Material` przechowuje właściwości fizyczne powierzchni:

| Właściwość | Opis |
|------------|------|
| `baseColor` | Kolor podstawowy (Albedo). |
| `metallic` | Stopień metaliczności [0-1]. |
| `roughness` | Stopień chropowatości [0-1]. |
| `emissive` | Kolor emisji światła. |
| `occlusion` | Współczynnik cieniowania wnęk (Ambient Occlusion). |
| `bumpIntensity` | Siła wpływu mapy normalnych. |

### Tekstury i Uchwyty
Materiały nie przechowują wskaźników ani nazw tekstur bezpośrednio w runtime. Korzystają z **`TextureHandle`**, które są rozwiązywane przez renderer na indeksy w tablicy Bindless.
- `baseColorMap`: Albedo + Alpha.
- `normalMap`: Mapa normalnych w przestrzeni stycznej (Tangent Space) lub świata.
- `metallicRoughnessMap`: Zintegrowana mapa parametrów fizycznych (kanały R/G).

---

## Shadery HLSL i SPIR-V

Silnik używa języka **HLSL** do pisania shaderów. Są one kompilowane przez `dxc` do binarnego formatu **SPIR-V**, który jest natywnie rozumiany przez Vulkan.

### Struktura Shadera (Vertex/Fragment)
Głównym shaderem dla potoku odroczonego jest `gbuffer.hlsl`. 
- **Vertex Shader**: Odpowiada za transformację pozycji, skinning (jeśli dotyczy) oraz przekazywanie wektorów TBN (Tangent, Bitangent, Normal).
- **Fragment Shader**: Zapisuje dane PBR do wielu załączników (Multi-Render Targets) G-Buffera.

### Hot Reloading
Silnik posiada system `ShaderHotReload`, który monitoruje katalog `assets/shaders`. Po wykryciu zmiany w pliku `.hlsl`, shader jest automatycznie rekompilowany, a potoki w rendererze są odświeżane bez konieczności restartu aplikacji.

---

## Zarządzanie Potokami (MaterialSystem)

Vulkan wymaga, aby wszystkie stany (blend, depth test, shadery) były zamrożone w obiekcie `VkPipeline`.

### MaterialCompiler i PipelineCache
- **`MaterialCompiler`**: Analizuje parametry materiału (np. czy jest przezroczysty, czy ma culling) i generuje odpowiedni opis potoku.
- **`PipelineCache`**: Przechowuje już skompilowane potoki. Jeśli dwa materiały korzystają z tych samych ustawień i shaderów, współdzielą ten sam obiekt `VkPipeline`, co oszczędza pamięć i czas GPU.

---

## Integracja z AssetRegistry

Wszystkie materiały są rejestrowane w `AssetRegistry`. 
1. Użytkownik prosi o materiał po nazwie: `registry.materialHandle("gold")`.
2. Otrzymuje `MaterialHandle`.
3. Komponent `MeshComponent` przechowuje ten uchwyt.
4. `RenderExtractionSystem` pobiera pełne dane `Material` z `MaterialManager` na podstawie uchwytu podczas przygotowywania klatki.

---

## Przykład definicji materiału (JSON/Code)

Materiały mogą być wczytywane z plików `.mat.xml` (legacy) lub definiowane bezpośrednio w kodzie:

```cpp
Material gold;
gold.name = "gold";
gold.baseColor = {1.0f, 0.84f, 0.0f, 1.0f};
gold.metallic = 1.0f;
gold.roughness = 0.1f;
gold.shaderProgram = "gbuffer";
materialManager.add(gold);
```
