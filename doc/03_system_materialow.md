# System materiałów i shaderów

## Struktura Material

`Material` to struktura definiująca wygląd obiektu. Znajduje się w `engine/core/types.h`:

```cpp
struct Material {
    std::string name;                    // nazwa materiału
    std::string textureName;             // nazwa tekstury (lub "" dla braku)
    std::string shaderProgram;           // nazwa katalogu shaderów ("default", "rotating_cube")
    BlendMode blendMode;                 // Disabled / Alpha
    bool depthTest;                      // czy test głębi
    bool depthWrite;                     // czy zapis do bufora głębi
    CullMode cullMode;                   // Back (domyślnie) / Disabled / Front
    std::string vertexEntryPoint;        // "vertexMain" (domyślnie)
    std::string fragmentEntryPoint;      // "fragmentMain" (domyślnie)
    std::array<float, 4> color;          // kolor bazowy (1,1,1,1)
    float shininess;                     // połysk (nieużywane obecnie)
    // ... więcej pól na przyszłość
};
```

### Jak działa wybór shadera

```cpp
// W material_system.cpp:
std::string shaderName = materialData.shaderProgram.empty()
    ? "default" : materialData.shaderProgram;
std::string shaderDir = "bin/assets/shaders/" + shaderName + "/";

// Ładuje SPIR-V z plików:
auto vertCode = vk::readFile(shaderDir + shaderName + ".vert.spv");
auto fragCode = vk::readFile(shaderDir + shaderName + ".frag.spv");
```

Czyli dla `shaderProgram = "default"` ładuje `bin/assets/shaders/default/default.vert.spv`.
Dla `shaderProgram = "rotating_cube"` ładuje `bin/assets/shaders/rotating_cube/rotating_cube.vert.spv`.

### Pipeline cache

Pipeline'y Vulkan są drogie w tworzeniu, więc są cache'owane:

```cpp
struct PipelineKey {
    std::string shaderName;   // który shader
    BlendMode blendMode;      // tryb mieszania
    bool depthTest;           // test głębi
    bool depthWrite;          // zapis głębi
    CullMode cullMode;        // odrzucanie ścianek
};
// Hash: xor wszystkich pól
```

Dwa materiały o tym samym kluczu współdzielą ten sam `VkPipeline`.

---

## Shadery HLSL

Wszystkie shadery są pisane w **HLSL** i kompilowane do SPIR-V przez **DXC** (DirectX Shader Compiler).
Każdy shader to pojedynczy plik `.hlsl` z dwiema funkcjami wejściowymi:

```hlsl
// default.hlsl — domyślny shader dla modeli 3DS i ogólnego renderowania

// === BUFORY GLOBALNE ===
cbuffer GlobalUBO : register(b0) {
    column_major float4x4 view;   // macierz widoku
    column_major float4x4 proj;   // macierz projekcji
};

// === PUSH CONSTANTS (per-obiekt) ===
[[vk::push_constant]]
cbuffer PushConstants {
    column_major float4x4 model;       // macierz modelu
    float4 materialColor;              // kolor materiału
};

// === TEKSTURA (binding 1, set 0) ===
Texture2D texSampler : register(t1, space0);
SamplerState texSamplerState : register(s1, space0);

// === VERTEX SHADER ===
VertexOutput vertexMain(VertexInput input) {
    // mnoży pozycję przez model, view, proj
    output.position = mul(proj, mul(view, mul(model, float4(input.position, 1.0))));
    // przekazuje kolor, UV i kolor materiału do fragment shadera
    return output;
}

// === FRAGMENT SHADER ===
float4 fragmentMain(FragmentInput input) : SV_Target0 {
    // próbkuje teksturę, mnoży przez kolor wierzchołka i kolor materiału
    return texSampler.Sample(texSamplerState, input.texCoord)
           * float4(input.color, 1.0) * input.matColor;
}
```

### Dlaczego HLSL a nie GLSL?

- HLSL ma lepsze wsparcie dla Vulkan przez `[[vk::...]]` atrybuty
- Jeden plik `.hlsl` zawiera vertex + fragment shader (czystsza organizacja)
- DXC produkuje zoptymalizowany SPIR-V
- Łatwiejsze mapowanie bindingów (`register(b0)`, `register(t1)`, `register(s1)`)

---

## Kompilacja shaderów (CMake)

Kompilacją shaderów zarządza `cmake/ShaderBundles.cmake`:

```cmake
add_shader_bundle(
    default
    default.vert.hlsl=default.hlsl@vertexMain    # vertex: funkcja "vertexMain"
    default.frag.hlsl=default.hlsl@fragmentMain  # fragment: funkcja "fragmentMain"
)
```

Za kulisami:
1. Dla każdej specyfikacji shadera, CMake wywołuje DXC:
   ```
   dxc -spirv -T vs_6_0 -E vertexMain -Fo default.vert.spv default.hlsl
   dxc -spirv -T ps_6_0 -E fragmentMain -Fo default.frag.spv default.hlsl
   ```

2. Wynikowe pliki `.spv` są umieszczane w `bin/assets/shaders/<nazwa>/`

3. Docelowy `add_dependencies` zapewnia, że shadery są kompilowane przed linkowaniem.

---

## Descriptor Set Layout (układ deskryptorów)

Pipeline używa jednego descriptor set layout z dwoma bindingami:

```
Binding 0: VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER       (VERTEX stage)
Binding 1: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER (FRAGMENT stage)
```

To oznacza, że każdy draw call potrzebuje:
1. Bufora uniform z macierzami view + proj
2. Samplera tekstury (lub białej tekstury 1x1 jako fallback)

Descriptor sety są alokowane per-tekstura, per-klatka (double buffering).

---

## Przepływ tworzenia MaterialInstance

```
MaterialManager::add(material)     // zarejestruj materiał
        │
        ▼
MeshComponent::materialName        // przypisz nazwę do komponentu
        │
        ▼ (w renderFrame)
MaterialSystem::getOrCreateMaterialInstance(material)
        │
        ├─ PipelineKey(material) już w cache?
        │   ├─ TAK → zwróć istniejący pipeline
        │   └─ NIE  → stwórz nowy VkPipeline:
        │       ├─ załaduj SPIR-V z dysku
        │       ├─ stwórz VkShaderModule
        │       ├─ skonfiguruj wszystkie stany (rasteryzacja, głębia, blend)
        │       ├─ vkCreateGraphicsPipelines()
        │       └─ zapisz w pipelineCache_
        │
        └─ BaseMaterialInstance(material, pipeline, VK_NULL_HANDLE)
```

Descriptor set (VK_NULL_HANDLE) jest ustawiany później przez `OpaquePass` —
pobiera go z `textureDescriptorSets_` na podstawie nazwy tekstury.
